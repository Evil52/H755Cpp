#include "h755/ethernet/diagnostic_echo.hpp"
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>

using namespace h755::ethernet;

namespace
{
    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "MAC engine: %s\n", message);
            std::abort();
        }
    }

    struct FakePort
    {
        DmaStorage& storage;
        bool cache{}, valid_storage{true}, stopped{};
        std::uint32_t status_value{}, tx_base{}, rx_base{}, initial_tx_tail{}, initial_rx_tail{};
        std::uint32_t last_tx_tail{}, last_rx_tail{};
        std::array<std::uint8_t, 6U> mac{};
        LinkState mode{LinkState::down};
        unsigned configurations{}, starts{}, stops{}, barriers{}, complete_barriers{};

        bool cache_enabled() const
        {
            return cache;
        }
        bool storage_ready(const DmaStorage& candidate) const
        {
            return valid_storage && &candidate == &storage;
        }
        std::uint32_t address(const void* pointer) const
        {
            const auto base = std::bit_cast<std::uintptr_t>(&storage);
            const auto value = std::bit_cast<std::uintptr_t>(pointer);
            check(value >= base && value < base + sizeof(storage),
                  "DMA address belongs to storage");
            return 0x30000000U + static_cast<std::uint32_t>(value - base);
        }
        void configure(std::array<std::uint8_t, 6U> address_value, std::uint32_t tx,
                       std::uint32_t rx, std::uint32_t tx_tail, std::uint32_t rx_tail)
        {
            mac = address_value;
            tx_base = tx;
            rx_base = rx;
            initial_tx_tail = tx_tail;
            initial_rx_tail = rx_tail;
            ++configurations;
        }
        void start_link(LinkState link)
        {
            mode = link;
            stopped = false;
            ++starts;
        }
        void stop_link()
        {
            ++stops;
        }
        bool channels_stopped() const
        {
            return stopped;
        }
        std::uint32_t status() const
        {
            return status_value;
        }
        void clear_status(std::uint32_t bits)
        {
            status_value &= ~bits;
        }
        void tx_tail(std::uint32_t address_value)
        {
            bool published = false;
            for (std::size_t index = 0U; index < mac_slots; ++index)
            {
                if (address_value == address(&storage.tx[(index + 1U) % mac_slots]))
                {
                    published = (storage.tx[index].word3 & descriptor_own) != 0U;
                }
            }
            check(published, "TX OWN precedes tail publication");
            last_tx_tail = address_value;
        }
        void rx_tail(std::uint32_t address_value)
        {
            bool published = false;
            for (std::size_t index = 0U; index < mac_slots; ++index)
            {
                if (address_value == address(&storage.rx[index]))
                {
                    published = (storage.rx[index].word3 & descriptor_own) != 0U;
                }
            }
            check(published, "RX OWN precedes tail publication");
            last_rx_tail = address_value;
        }
        void barrier()
        {
            ++barriers;
        }
        void complete_barrier()
        {
            ++complete_barriers;
        }
    };

    struct Fixture
    {
        DmaStorage storage{};
        FakePort port{storage};
        std::array<std::uint8_t, 6U> address{2U, 1U, 2U, 3U, 4U, 5U};
        MacEngine<FakePort> mac{port, storage, address};
        void running()
        {
            check(mac.start(), "initial start");
            mac.poll(1U, LinkState::mb100_full);
            check(mac.state() == MacState::running, "100/full running");
        }
    };

    void startup_and_ring()
    {
        Fixture f;
        check(f.mac.state() == MacState::cold, "initial state");
        check(f.mac.start(), "start succeeds");
        check(!f.mac.start(), "duplicate start rejected");
        check(f.port.configurations == 1U && f.port.mac == f.address, "configured once");
        check(f.port.tx_base == f.port.address(f.storage.tx.data()), "TX descriptor base");
        check(f.port.rx_base == f.port.address(f.storage.rx.data()), "RX descriptor base");
        check(f.port.initial_tx_tail == f.port.tx_base, "initial TX tail");
        check(f.port.initial_rx_tail == f.port.address(&f.storage.rx[3U]), "initial RX tail");
        f.mac.poll(0U, LinkState::down);
        check(f.mac.state() == MacState::ready && f.port.starts == 0U, "down link waits");
        f.mac.poll(1U, LinkState::mb100_full);
        check(f.port.mode == LinkState::mb100_full && f.port.starts == 1U, "mode passed to port");
        for (const auto& descriptor : f.storage.rx)
        {
            check(descriptor.word3 == (descriptor_own | rx_buffer1_valid),
                  "RX ownership published");
        }
        check(f.port.barriers >= 4U && f.port.complete_barriers == 1U, "DMA barriers run");

        std::array<std::byte, 60U> frame{};
        check(f.mac.send({frame.data(), 0U}) == MacError::invalid_frame, "empty TX rejected");
        for (std::size_t index = 0U; index < mac_slots; ++index)
        {
            frame[0U] = static_cast<std::byte>(index);
            check(f.mac.send(frame) == MacError::none, "TX enqueue");
            check(f.storage.tx[index].word3 ==
                      (descriptor_own | descriptor_first | descriptor_last | 60U),
                  "TX OWN and length");
            check(f.storage.tx_bytes[index][0U] == frame[0U], "TX payload copied");
            check(f.port.last_tx_tail == f.port.address(&f.storage.tx[(index + 1U) % mac_slots]),
                  "TX tail advances in descriptor order");
        }
        check(f.mac.send(frame) == MacError::queue_full, "TX backpressure");
        f.storage.tx[0U].word3 &= ~descriptor_own;
        f.storage.tx[1U].word3 = descriptor_error;
        f.mac.poll(2U, LinkState::mb100_full);
        check(f.mac.transmitted() == 1U && f.mac.transmit_errors() == 1U,
              "TX success and error separated");
        check(f.mac.send(frame) == MacError::none, "TX slot reused after completion");

        f.storage.rx[0U].word3 = descriptor_first | descriptor_last | 64U;
        f.storage.rx[1U].word3 = descriptor_first | descriptor_last | descriptor_error | 64U;
        f.mac.poll(3U, LinkState::mb100_full);
        check(f.mac.received() == 1U && f.mac.dropped() == 1U, "RX valid and error counted");
        check(f.storage.rx[0U].word3 == (descriptor_own | rx_buffer1_valid), "RX rearmed");
        check(f.port.last_rx_tail == f.port.address(&f.storage.rx[1U]),
              "RX tail follows recycling");
        f.storage.rx[2U].word3 = descriptor_first | descriptor_last | 64U;
        f.storage.rx[3U].word3 = descriptor_first | descriptor_last | 64U;
        f.port.status_value = dma_rx_unavailable;
        f.mac.poll(4U, LinkState::mb100_full);
        check(f.mac.received() == 3U && f.port.status_value == 0U,
              "RX wrap clears buffer-unavailable status");
        check(f.port.last_rx_tail == f.port.address(&f.storage.rx[3U]),
              "RX tail wraps after fourth descriptor");
        f.storage.rx[0U].word3 = descriptor_first | descriptor_last | 64U;
        f.mac.poll(5U, LinkState::mb100_full);
        check(f.mac.received() == 4U && f.port.last_rx_tail == f.port.address(&f.storage.rx[0U]),
              "RX accepts a frame after wrap");
    }

    void transitions_and_faults()
    {
        Fixture f;
        f.running();
        f.mac.poll(10U, LinkState::mb10_half);
        check(f.mac.state() == MacState::stopping && f.port.stops == 1U,
              "mode change stops both channels");
        f.mac.poll(59U, LinkState::mb10_half);
        check(f.mac.state() == MacState::stopping, "stop has 50 ms deadline");
        f.port.stopped = true;
        f.mac.poll(60U, LinkState::mb10_half);
        check(f.mac.state() == MacState::ready, "DMA process state permits reinitialization");
        f.mac.poll(61U, LinkState::mb10_half);
        check(f.mac.state() == MacState::running && f.port.mode == LinkState::mb10_half &&
                  f.port.starts == 2U,
              "new mode starts");
        std::array<std::byte, 60U> frame{};
        check(f.mac.send(frame) == MacError::none, "TX before cable loss");
        f.mac.poll(62U, LinkState::down);
        check(f.mac.state() == MacState::stopping, "cable loss stops DMA");
        check(f.mac.send(frame) == MacError::link_down, "TX rejected while stopping");
        f.port.stopped = true;
        f.mac.poll(63U, LinkState::down);
        check(f.mac.state() == MacState::ready, "link down remains ready");
        check(f.mac.transmit_errors() == 1U, "inflight TX recorded as aborted");

        Fixture timeout;
        timeout.running();
        timeout.port.status_value = dma_tx_stopped | dma_rx_stopped;
        timeout.mac.poll(0xFFFF'FFF0U, LinkState::down);
        timeout.mac.poll(0x0000'0021U, LinkState::down);
        check(timeout.mac.state() == MacState::stopping,
              "event flags do not substitute for stopped process state");
        timeout.mac.poll(0x0000'0022U, LinkState::down);
        check(timeout.mac.state() == MacState::fault && timeout.mac.error() == MacError::dma_fault,
              "stuck stop faults at deadline");

        Fixture fatal;
        fatal.running();
        fatal.port.status_value = dma_fatal_bus;
        fatal.mac.poll(2U, LinkState::mb100_full);
        check(fatal.mac.state() == MacState::fault && fatal.port.stops == 1U,
              "fatal bus error stops MAC");

        Fixture cache;
        cache.port.cache = true;
        check(!cache.mac.start() && cache.mac.error() == MacError::cache_enabled,
              "D-cache requires a coherency policy");
        Fixture storage;
        storage.port.valid_storage = false;
        check(!storage.mac.start() && storage.mac.error() == MacError::invalid_storage,
              "DMA storage must be accessible");
    }

    void received_frame_borrow()
    {
        Fixture f;
        f.running();
        for (std::size_t index = 0U; index < 60U; ++index)
        {
            f.storage.rx_bytes[0U][index] = static_cast<std::byte>(index);
        }
        f.storage.rx[0U].word3 = descriptor_first | descriptor_last | 60U;
        unsigned delivered{};
        f.mac.poll(2U, LinkState::mb100_full,
                   [&](std::span<const std::byte> frame) noexcept
                   {
                       ++delivered;
                       check(frame.size() == 60U, "RX length reaches consumer");
                       check(frame[0U] == std::byte{0U} && frame[59U] == std::byte{59U},
                             "RX bytes reach consumer intact");
                       check((f.storage.rx[0U].word3 & descriptor_own) == 0U,
                             "RX buffer remains CPU-owned during consumer call");
                   });
        check(delivered == 1U && f.mac.received() == 1U, "valid frame delivered once");
        check((f.storage.rx[0U].word3 & descriptor_own) != 0U,
              "RX buffer returned to DMA after consumer call");
        f.storage.rx[1U].word3 = descriptor_first | descriptor_last | descriptor_error | 60U;
        f.mac.poll(3U, LinkState::mb100_full,
                   [&](std::span<const std::byte>) noexcept { ++delivered; });
        check(delivered == 1U && f.mac.dropped() == 1U,
              "invalid frame is not delivered to consumer");
    }

    void diagnostic_echo()
    {
        constexpr std::array<std::uint8_t, 6U> own{2U, 1U, 2U, 3U, 4U, 5U};
        DiagnosticEcho echo{own};
        std::array<std::byte, 60U> request{};
        for (std::size_t index = 0U; index < 6U; ++index)
        {
            request[index] = std::byte{0xFFU};
            request[6U + index] = static_cast<std::byte>(0x10U + index * 2U);
        }
        request[12U] = std::byte{0x88U};
        request[13U] = std::byte{0xB5U};
        constexpr char magic[] = "H755PING";
        for (std::size_t index = 0U; index < 8U; ++index)
        {
            request[14U + index] = static_cast<std::byte>(magic[index]);
        }
        request[22U] = std::byte{0x12U};
        request[23U] = std::byte{0x34U};
        request[24U] = std::byte{0x56U};
        request[25U] = std::byte{0x78U};
        std::array<std::byte, 60U> response{};
        unsigned sends{};
        auto sender = [&](std::span<const std::byte> bytes) noexcept
        {
            ++sends;
            check(bytes.size() == response.size(), "fixed diagnostic reply length");
            for (std::size_t index = 0U; index < response.size(); ++index)
            {
                response[index] = bytes[index];
            }
            return MacError::none;
        };
        check(echo.on_frame({request.data(), 10U}, sender) == DiagnosticResult::ignored,
              "short diagnostic frame ignored");
        request[13U] = std::byte{0x06U};
        check(echo.on_frame(request, sender) == DiagnosticResult::ignored,
              "unrelated EtherType ignored");
        request[13U] = std::byte{0xB5U};
        request[14U] = std::byte{0U};
        check(echo.on_frame(request, sender) == DiagnosticResult::ignored, "wrong magic ignored");
        request[14U] = std::byte{'H'};
        request[0U] = std::byte{0x02U};
        check(echo.on_frame(request, sender) == DiagnosticResult::ignored,
              "other destination ignored");
        request[0U] = std::byte{0xFFU};
        request[6U] = std::byte{0x11U};
        check(echo.on_frame(request, sender) == DiagnosticResult::ignored,
              "multicast source ignored");
        request[6U] = std::byte{0x10U};
        check(echo.on_frame(request, sender) == DiagnosticResult::sent,
              "one valid diagnostic request answered");
        check(echo.attempted() && echo.result() == DiagnosticResult::sent && sends == 1U,
              "response is bounded to one attempt");
        for (std::size_t index = 0U; index < own.size(); ++index)
        {
            check(response[index] == request[6U + index], "reply destination is requester");
            check(response[6U + index] == static_cast<std::byte>(own[index]),
                  "reply source is board");
        }
        constexpr char pong[] = "H755PONG";
        for (std::size_t index = 0U; index < 8U; ++index)
        {
            check(response[14U + index] == static_cast<std::byte>(pong[index]),
                  "reply marker is PONG");
        }
        for (std::size_t index = 0U; index < 4U; ++index)
        {
            check(response[22U + index] == request[22U + index], "sequence is echoed");
        }
        check(echo.on_frame(request, sender) == DiagnosticResult::ignored && sends == 1U,
              "repeat request cannot amplify traffic");
        DiagnosticEcho failed{own};
        check(failed.on_frame(request, [](std::span<const std::byte>) noexcept
                              { return MacError::queue_full; }) == DiagnosticResult::send_failed &&
                  failed.attempted(),
              "failed send is recorded without retrying");
    }

    void diagnostic_rx_to_tx()
    {
        Fixture f;
        f.running();
        DiagnosticEcho echo{f.address};
        auto& frame = f.storage.rx_bytes[0U];
        for (std::size_t index = 0U; index < 6U; ++index)
        {
            frame[index] = std::byte{0xFFU};
            frame[6U + index] = static_cast<std::byte>(0x20U + index * 2U);
        }
        frame[12U] = std::byte{0x88U};
        frame[13U] = std::byte{0xB5U};
        constexpr char magic[] = "H755PING";
        for (std::size_t index = 0U; index < 8U; ++index)
        {
            frame[14U + index] = static_cast<std::byte>(magic[index]);
        }
        frame[22U] = std::byte{0xABU};
        f.storage.rx[0U].word3 = descriptor_first | descriptor_last | 60U;
        f.mac.poll(2U, LinkState::mb100_full,
                   [&](std::span<const std::byte> received) noexcept
                   {
                       check(echo.on_frame(received, [&](std::span<const std::byte> reply) noexcept
                                           { return f.mac.send(reply); }) == DiagnosticResult::sent,
                             "diagnostic RX request enqueued for TX");
                   });
        check(f.mac.received() == 1U &&
                  f.storage.tx[0U].word3 ==
                      (descriptor_own | descriptor_first | descriptor_last | 60U),
              "received request copied into owned TX descriptor");
        check(f.storage.tx_bytes[0U][22U] == std::byte{0xABU} &&
                  f.storage.tx_bytes[0U][14U] == std::byte{'H'} &&
                  f.storage.tx_bytes[0U][18U] == std::byte{'P'},
              "TX buffer retains reply after RX descriptor recycled");
        f.storage.tx[0U].word3 &= ~descriptor_own;
        f.mac.poll(3U, LinkState::mb100_full);
        check(f.mac.transmitted() == 1U, "TX completion counted after diagnostic reply");
    }
}

int main()
{
    startup_and_ring();
    transitions_and_faults();
    received_frame_borrow();
    diagnostic_echo();
    diagnostic_rx_to_tx();
}
