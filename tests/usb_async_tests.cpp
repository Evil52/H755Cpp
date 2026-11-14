#include "h755/usb/cdc.hpp"
#include "h755/usb/echo.hpp"
#include "h755/usb/startup.hpp"
#include <async/connect.hpp>
#include <async/start.hpp>
#include <async/then.hpp>
#include <array>
#include <cstdio>
#include <limits>
#include <optional>
#include <type_traits>
#include <vector>

using namespace h755::usb;
namespace
{
    int failures{};
    void check(bool value, const char* reason)
    {
        if (!value)
        {
            std::fprintf(stderr, "FAIL: %s\n", reason);
            ++failures;
        }
    }
    struct Clock
    {
        std::uint32_t ticks{};
        std::uint32_t now() const
        {
            return ticks;
        }
    };
    struct Port
    {
        Status link{true, false, true, 1};
        std::vector<std::byte> input{};
        std::vector<std::byte> output{};
        std::size_t tx_capacity{64};
        std::optional<std::size_t> forced{};
        unsigned reads{};
        unsigned writes{};
        Status status() const
        {
            return link;
        }
        std::size_t available() const
        {
            return input.size();
        }
        std::size_t write_available() const
        {
            return tx_capacity;
        }
        std::size_t read(std::span<std::byte> bytes)
        {
            ++reads;
            if (forced)
            {
                return *forced;
            }
            const auto count = bytes.size() < input.size() ? bytes.size() : input.size();
            for (std::size_t i = 0; i < count; ++i)
            {
                bytes[i] = input[i];
            }
            input.erase(input.begin(), input.begin() + static_cast<std::ptrdiff_t>(count));
            return count;
        }
        std::size_t write(std::span<const std::byte> bytes)
        {
            ++writes;
            if (forced)
            {
                return *forced;
            }
            const auto count = bytes.size() < tx_capacity ? bytes.size() : tx_capacity;
            output.insert(output.end(), bytes.begin(),
                          bytes.begin() + static_cast<std::ptrdiff_t>(count));
            return count;
        }
    };
    struct Result
    {
        unsigned calls{};
        std::size_t count{};
        std::optional<TransferError> error{};
        bool stopped{};
    };
    struct Receiver
    {
        using is_receiver = void;
        Result& result;
        void set_value(std::size_t count) &&
        {
            ++result.calls;
            result.count = count;
        }
        void set_error(TransferError error) &&
        {
            ++result.calls;
            result.error = error;
        }
        void set_stopped() &&
        {
            ++result.calls;
            result.stopped = true;
        }
    };
    struct EndReceiver
    {
        using is_receiver = void;
        void set_value(auto&&...) &&
        {
        }
        void set_error(TransferError) &&
        {
            check(false, "echo catches transfer errors");
        }
        void set_stopped() &&
        {
        }
    };
    void reads_and_composition()
    {
        Clock clock;
        Port port;
        Cdc device{port, clock};
        std::array<std::byte, 128> buffer{};
        Result result;
        auto operation = async::connect(device.read_some(buffer, 100) |
                                            async::then([](std::size_t n) { return n * 2U; }),
                                        Receiver{result});
        async::start(operation);
        check(result.calls == 0 && port.reads == 0, "USB start only registers pending work");
        device.poll();
        check(result.calls == 0, "empty RX remains pending");
        port.input.assign(90, std::byte{0x41});
        device.poll();
        check(result.calls == 1 && result.count == 128 && port.input.size() == 26,
              "read at most one64-byte packet and then composition");
        device.poll();
        check(result.calls == 1 && port.reads == 1, "completion is exactly once");
    }
    void writes_and_backpressure()
    {
        Clock clock;
        Port port;
        Cdc device{port, clock};
        std::array<std::byte, 130> buffer{};
        Result result;
        auto operation = async::connect(device.write_all(buffer, 100), Receiver{result});
        async::start(operation);
        port.tx_capacity = 0;
        device.poll();
        check(port.writes == 0 && result.calls == 0, "backpressure never spins or drops data");
        port.tx_capacity = 20;
        for (unsigned i = 0; i < 6; ++i)
        {
            device.poll();
        }
        check(result.calls == 0 && port.output.size() == 120,
              "partial writes retain remaining ownership");
        device.poll();
        check(result.calls == 1 && result.count == 130 && port.output.size() == 130,
              "write_all completes only after every byte is copied to transport");
    }
    void deadline_reset_and_cancel()
    {
        for (auto code : {ErrorCode::timeout, ErrorCode::reset, ErrorCode::not_ready})
        {
            Clock clock;
            Port port;
            Cdc device{port, clock};
            std::array<std::byte, 8> buffer{};
            Result result;
            auto operation = async::connect(device.read_some(buffer, 10), Receiver{result});
            async::start(operation);
            clock.ticks = 9;
            device.poll();
            check(result.calls == 0, "request alive before deadline");
            if (code == ErrorCode::timeout)
            {
                clock.ticks = 10;
            }
            if (code == ErrorCode::reset)
            {
                ++port.link.generation;
            }
            if (code == ErrorCode::not_ready)
            {
                port.link.open = false;
            }
            device.poll();
            check(result.calls == 1 && result.error && result.error->code == code,
                  "explicit deadline/reset/disconnect outcome");
        }
        Clock clock;
        Port port;
        Cdc device{port, clock};
        std::array<std::byte, 4> buffer{};
        Result result;
        auto operation = async::connect(device.read_some(buffer, 10), Receiver{result});
        async::start(operation);
        operation.cancel();
        check(result.calls == 0, "cancel callback is deferred to poll");
        device.poll();
        check(result.calls == 1 && result.stopped,
              "cancel releases registration and completes stopped");
    }
    void guards_and_lifetimes()
    {
        Clock clock;
        Port port;
        Cdc device{port, clock};
        std::array<std::byte, 8> buffer{};
        for (auto timeout : {0U, 0x8000'0000U})
        {
            Result result;
            auto op = async::connect(device.read_some(buffer, timeout), Receiver{result});
            async::start(op);
            device.poll();
            check(result.error && result.error->code == ErrorCode::invalid_timeout,
                  "invalid timing rejected");
        }
        Result first, second;
        {
            auto a = async::connect(device.read_some(buffer, 100), Receiver{first});
            auto b = async::connect(device.read_some(buffer, 100), Receiver{second});
            static_assert(!std::is_move_constructible_v<decltype(a)>);
            async::start(a);
            async::start(b);
            check(second.error && second.error->code == ErrorCode::busy && first.calls == 0,
                  "single owner per direction");
        }
        check(first.calls == 0, "destroying pending operation unregisters without callback");
        Result replacement;
        auto op = async::connect(device.read_some(buffer, 100), Receiver{replacement});
        async::start(op);
        port.input = {std::byte{7}};
        device.poll();
        check(replacement.calls == 1 && buffer[0] == std::byte{7},
              "destroyed registration cannot receive a later event");
    }
    void wrap_suspend_and_states()
    {
        Clock clock{0xFFFF'FFFAU};
        Port port;
        Cdc device{port, clock};
        std::array<std::byte, 8> buffer{};
        Result result;
        auto operation = async::connect(device.read_some(buffer, 10), Receiver{result});
        async::start(operation);
        clock.ticks = 3;
        port.link.suspended = true;
        device.poll();
        check(device.state() == State::suspended && result.calls == 0,
              "suspend preserves pending request within budget");
        clock.ticks = 4;
        device.poll();
        check(result.error && result.error->code == ErrorCode::timeout,
              "timeout across uint32 wrap while suspended");
        port.link.suspended = false;
        port.link.open = false;
        device.poll();
        check(device.state() == State::configured, "mounted without DTR is configured");
        port.link.mounted = false;
        device.poll();
        check(device.state() == State::offline, "unmounted is offline");
        Result ready;
        auto wait = async::connect(device.wait_ready(100), Receiver{ready});
        async::start(wait);
        device.poll();
        check(ready.calls == 0, "wait_ready survives missing cable");
        ++port.link.generation;
        port.link.mounted = true;
        port.link.open = true;
        device.poll();
        check(ready.calls == 1 && device.state() == State::ready,
              "late connection resolves wait_ready");
    }
    void bounded_dispatch_and_bad_port()
    {
        Clock clock;
        Port port;
        Cdc device{port, clock};
        std::array<std::byte, 2> buffer{};
        Result read, write, ready;
        auto a = async::connect(device.read_some(buffer, 10), Receiver{read});
        auto b = async::connect(device.write_all(buffer, 10), Receiver{write});
        auto c = async::connect(device.wait_ready(10), Receiver{ready});
        async::start(a);
        async::start(b);
        async::start(c);
        port.input = {std::byte{1}};
        device.poll();
        check(read.calls + write.calls + ready.calls == 1, "one completion per poll");
        device.poll();
        device.poll();
        check(read.calls + write.calls + ready.calls == 3, "independent lanes make progress");
        Result invalid;
        port.forced = 3;
        port.input = {std::byte{1}};
        auto bad = async::connect(device.read_some(buffer, 100), Receiver{invalid});
        async::start(bad);
        device.poll();
        check(invalid.error && invalid.error->code == ErrorCode::transport,
              "port count validated before publishing");
    }
    void partial_failure_and_empty()
    {
        Clock clock;
        Port port;
        Cdc device{port, clock};
        std::array<std::byte, 4> buffer{};
        Result result;
        auto operation = async::connect(device.write_all(buffer, 10), Receiver{result});
        async::start(operation);
        port.tx_capacity = 2;
        device.poll();
        clock.ticks = 10;
        device.poll();
        check(result.error && result.error->code == ErrorCode::timeout &&
                  result.error->transferred == 2,
              "timeout reports already queued prefix");
        Result empty;
        auto zero = async::connect(device.read_some({}, 10), Receiver{empty});
        async::start(zero);
        device.poll();
        check(empty.calls == 1 && empty.count == 0, "empty read completes without port access");
    }
    void startup_and_wait_bounds()
    {
        struct Platform
        {
            unsigned enabled{};
            unsigned configured{};
            bool ready{};
            void enable_clock()
            {
                ++enabled;
            }
            bool clock_ready()
            {
                return ready;
            }
            void configure()
            {
                ++configured;
            }
        };
        struct Stack
        {
            unsigned calls{};
            bool good{true};
            bool start()
            {
                ++calls;
                return good;
            }
        };
        for (auto outcome :
             {StartupError::none, StartupError::clock_timeout, StartupError::stack_init})
        {
            Platform platform;
            Stack stack;
            Clock clock;
            Startup startup{platform, clock, stack};
            startup.poll();
            check(platform.enabled == 0, "idle startup has no register side effects");
            check(startup.start() && !startup.start(), "USB startup is single-shot");
            startup.poll();
            check(platform.configured == 0 && stack.calls == 0, "await clock before peripheral IO");
            if (outcome == StartupError::clock_timeout)
            {
                clock.ticks = 20;
            }
            else
            {
                platform.ready = true;
            }
            stack.good = outcome != StartupError::stack_init;
            startup.poll();
            startup.poll();
            check(startup.error() == outcome, "startup publishes actual failure reason");
            check(startup.state() == (outcome == StartupError::none ? StartupState::running
                                                                    : StartupState::failed),
                  "startup terminal transition");
            check(stack.calls <= 1 && platform.configured <= 1,
                  "no repeated controller resets after startup");
        }
        unsigned reads = 0;
        check(!wait_register(
                  [&]
                  {
                      ++reads;
                      return 0U;
                  },
                  1U, 1U, 4U) &&
                  reads == 4,
              "stuck hardware wait has exact finite read budget");
        reads = 0;
        check(wait_register([&] { return ++reads; }, 3U, 3U, 4U) && reads == 3,
              "wait stops at first matching observation");
        check(!wait_register([] { return 1U; }, 1U, 1U, 0U), "zero wait budget performs no read");
    }
    void main_loop_policy()
    {
        unsigned calls{};
        const auto result = h755::async_context::MainLoopPolicy::call_in_critical_section<void>(
            [&]
            {
                ++calls;
                return 42;
            },
            [] { return true; });
        check(result == 42 && calls == 1,
              "injected main-loop policy preserves guarded action/result without spinning");
    }
    void remaining_edges()
    {
        Clock clock;
        Port port;
        Cdc device{port, clock};
        std::array<std::byte, 4> bytes{};
        Result result;
        auto operation = async::connect(device.read_some(bytes, 100), Receiver{result});
        async::start(operation);
        port.input = {std::byte{1}};
        port.link.pending_events = true;
        device.poll();
        check(result.calls == 0 && port.reads == 0,
              "queued controller events processed before application IO");
        port.link.pending_events = false;
        port.forced = 0;
        device.poll();
        check(result.calls == 0, "zero read from port remains pending");
        port.forced.reset();
        port.link.suspended = true;
        device.poll();
        port.link.suspended = false;
        device.poll();
        check(result.calls == 1 && result.count == 1, "resume completes pending read");
        Result empty;
        auto zero = async::connect(device.write_all({}, 10), Receiver{empty});
        async::start(zero);
        device.poll();
        check(empty.calls == 1 && empty.count == 0, "empty write has no transport side effect");
        Result invalid;
        auto bad = async::connect(device.write_all(bytes, 10), Receiver{invalid});
        async::start(bad);
        port.forced = 5;
        device.poll();
        check(invalid.error && invalid.error->code == ErrorCode::transport,
              "write count cannot exceed supplied buffer");
        Result cancelled;
        auto cancel = async::connect(device.wait_ready(10), Receiver{cancelled});
        cancel.cancel();
        async::start(cancel);
        device.poll();
        check(cancelled.stopped, "cancel before start is observed at dispatch");
    }
    void echo_flow()
    {
        Clock clock;
        Port port;
        Cdc device{port, clock};
        std::array<std::byte, 64> buffer{};
        auto operation = async::connect(echo(device, buffer), EndReceiver{});
        async::start(operation);
        port.input = {std::byte{1}, std::byte{0}, std::byte{0xFF}};
        for (unsigned i = 0; i < 8; ++i)
        {
            device.poll();
        }
        check(port.output == std::vector<std::byte>({std::byte{1}, std::byte{0}, std::byte{0xFF}}),
              "real async wait/read/write/repeat echoes binary data");
        port.link.mounted = false;
        port.link.open = false;
        ++port.link.generation;
        for (unsigned i = 0; i < 4; ++i)
        {
            device.poll();
        }
        port.link.mounted = true;
        port.link.open = true;
        ++port.link.generation;
        port.input = {std::byte{42}};
        for (unsigned i = 0; i < 8; ++i)
        {
            device.poll();
        }
        check(port.output.back() == std::byte{42},
              "echo recovers after disconnect without recursive busy-loop");
    }
}
int main()
{
    reads_and_composition();
    writes_and_backpressure();
    deadline_reset_and_cancel();
    guards_and_lifetimes();
    wrap_suspend_and_states();
    bounded_dispatch_and_bad_port();
    partial_failure_and_empty();
    startup_and_wait_bounds();
    main_loop_policy();
    remaining_edges();
    echo_flow();
    return failures == 0 ? 0 : 1;
}
