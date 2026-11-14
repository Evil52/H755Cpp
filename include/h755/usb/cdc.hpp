#pragma once
#include "h755/async/context.hpp"
#include <async/completion_tags.hpp>
#include <async/concepts.hpp>
#include <async/debug_context.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace h755::usb
{
    struct Status
    {
        bool mounted{};
        bool suspended{};
        bool open{};
        std::uint32_t generation{};
        bool pending_events{};
    };
    enum class State
    {
        offline,
        configured,
        ready,
        suspended
    };
    enum class ErrorCode
    {
        busy,
        invalid_timeout,
        not_ready,
        reset,
        timeout,
        transport
    };
    struct TransferError
    {
        ErrorCode code;
        std::size_t transferred{};
    };

    template <class Port, class Clock> class Cdc
    {
        enum class Kind : std::size_t
        {
            ready,
            read,
            write
        };
        enum class Phase
        {
            created,
            queued,
            completed
        };
        enum class Completion
        {
            value,
            error,
            stopped
        };
        struct Node
        {
            Kind kind;
            Phase phase{Phase::created};
            std::span<std::byte> input{};
            std::span<const std::byte> output{};
            std::uint32_t timeout;
            std::uint32_t started{};
            std::uint32_t generation{};
            std::size_t transferred{};
            bool cancelled{};
            std::optional<ErrorCode> error{};
            void (*complete)(Node&, Completion, TransferError);
        };

      public:
        template <class Receiver> class Operation : private Node
        {
          public:
            using h755_usb_operation = void;
            Operation(Cdc& device, Kind kind, std::span<std::byte> input,
                      std::span<const std::byte> output, std::uint32_t timeout, Receiver receiver)
                : Node{.kind = kind,
                       .input = input,
                       .output = output,
                       .timeout = timeout,
                       .complete = deliver},
                  device_{device}, receiver_{std::move(receiver)}
            {
            }
            Operation(const Operation&) = delete;
            Operation& operator=(const Operation&) = delete;
            ~Operation()
            {
                if (this->phase == Phase::queued)
                {
                    device_.detach(*this);
                }
            }
            void start() &
            {
                device_.attach(*this);
            }
            void cancel() noexcept
            {
                this->cancelled = true;
            }

          private:
            static void deliver(Node& node, Completion channel, TransferError error)
            {
                auto& operation = static_cast<Operation&>(node);
                if (channel == Completion::value)
                {
                    async::set_value(std::move(operation.receiver_), std::size_t{node.transferred});
                }
                else if (channel == Completion::error)
                {
                    async::set_error(std::move(operation.receiver_), error);
                }
                else
                {
                    async::set_stopped(std::move(operation.receiver_));
                }
            }
            Cdc& device_;
            Receiver receiver_;
        };

        template <Kind Direction> struct Sender
        {
            using is_sender = void;
            using completion_signatures =
                async::completion_signatures<async::set_value_t(std::size_t),
                                             async::set_error_t(TransferError),
                                             async::set_stopped_t()>;
            Cdc& device;
            std::span<std::byte> input;
            std::span<const std::byte> output;
            std::uint32_t timeout;
            template <async::receiver Receiver> auto connect(Receiver&& receiver) const
            {
                return Operation<std::remove_cvref_t<Receiver>>{
                    device, Direction, input, output, timeout, std::forward<Receiver>(receiver)};
            }
        };

        Cdc(Port& port, Clock& clock) noexcept : port_{port}, clock_{clock}
        {
        }
        Cdc(const Cdc&) = delete;
        Cdc& operator=(const Cdc&) = delete;
        ~Cdc()
        {
            for (const auto* node : pending_)
            {
                if (node != nullptr)
                {
                    __builtin_trap();
                }
            }
        }
        auto wait_ready(std::uint32_t timeout)
        {
            return Sender<Kind::ready>{*this, {}, {}, timeout};
        }
        auto read_some(std::span<std::byte> buffer, std::uint32_t timeout)
        {
            return Sender<Kind::read>{*this, buffer, {}, timeout};
        }
        auto write_all(std::span<const std::byte> buffer, std::uint32_t timeout)
        {
            return Sender<Kind::write>{*this, {}, buffer, timeout};
        }
        State state() const noexcept
        {
            return state_;
        }

        void poll()
        {
            const auto status = port_.status();
            state_ = status.suspended
                         ? State::suspended
                         : (!status.mounted ? State::offline
                                            : (status.open ? State::ready : State::configured));
            const auto now = clock_.now();
            for (std::size_t i = 0; i < pending_.size(); ++i)
            {
                const auto index = (cursor_ + i) % pending_.size();
                auto* node = pending_[index];
                if (node == nullptr)
                {
                    continue;
                }
                if (node->kind != Kind::ready && node->generation != status.generation)
                {
                    node->error = ErrorCode::reset;
                }
                if (node->cancelled)
                {
                    finish(*node, Completion::stopped, ErrorCode::not_ready);
                    return;
                }
                if (node->error)
                {
                    finish(*node, Completion::error, *node->error);
                    return;
                }
                if (static_cast<std::uint32_t>(now - node->started) >= node->timeout)
                {
                    finish(*node, Completion::error, ErrorCode::timeout);
                    return;
                }
                if (status.pending_events)
                {
                    continue;
                }
                if (node->kind == Kind::ready)
                {
                    if (state_ == State::ready)
                    {
                        finish(*node, Completion::value, ErrorCode::not_ready);
                        return;
                    }
                    continue;
                }
                if (state_ == State::suspended)
                {
                    continue;
                }
                if (state_ != State::ready)
                {
                    finish(*node, Completion::error, ErrorCode::not_ready);
                    return;
                }
                if (node->kind == Kind::read)
                {
                    if (node->input.empty())
                    {
                        finish(*node, Completion::value, ErrorCode::not_ready);
                        return;
                    }
                    if (port_.available() == 0U)
                    {
                        continue;
                    }
                    const auto capacity =
                        node->input.size() < packet_size ? node->input.size() : packet_size;
                    const auto count = port_.read(node->input.first(capacity));
                    if (count > capacity)
                    {
                        finish(*node, Completion::error, ErrorCode::transport);
                        return;
                    }
                    if (count == 0U)
                    {
                        continue;
                    }
                    node->transferred = count;
                    finish(*node, Completion::value, ErrorCode::not_ready);
                    return;
                }
                if (node->transferred == node->output.size())
                {
                    finish(*node, Completion::value, ErrorCode::not_ready);
                    return;
                }
                auto capacity = node->output.size() - node->transferred;
                if (capacity > packet_size)
                {
                    capacity = packet_size;
                }
                if (port_.write_available() == 0U)
                {
                    continue;
                }
                const auto count = port_.write(node->output.subspan(node->transferred, capacity));
                if (count > capacity)
                {
                    finish(*node, Completion::error, ErrorCode::transport);
                    return;
                }
                node->transferred += count;
                if (node->transferred == node->output.size())
                {
                    finish(*node, Completion::value, ErrorCode::not_ready);
                }
                return;
            }
        }

      private:
        static constexpr std::size_t packet_size = 64;
        void attach(Node& node)
        {
            if (node.phase != Phase::created)
            {
                __builtin_trap();
            }
            node.started = clock_.now();
            node.generation = port_.status().generation;
            const auto slot = static_cast<std::size_t>(node.kind);
            if (pending_[slot] != nullptr)
            {
                finish(node, Completion::error, ErrorCode::busy);
                return;
            }
            if (node.timeout == 0U || node.timeout > 0x7FFF'FFFFU)
            {
                node.error = ErrorCode::invalid_timeout;
            }
            node.phase = Phase::queued;
            pending_[slot] = &node;
        }
        void detach(Node& node) noexcept
        {
            auto& slot = pending_[static_cast<std::size_t>(node.kind)];
            if (slot == &node)
            {
                slot = nullptr;
            }
        }
        void finish(Node& node, Completion channel, ErrorCode code)
        {
            detach(node);
            cursor_ = (static_cast<std::size_t>(node.kind) + 1U) % pending_.size();
            node.phase = Phase::completed;
            // Completion may destroy this operation or device; never access either afterwards.
            node.complete(node, channel, {code, node.transferred});
        }
        Port& port_;
        Clock& clock_;
        std::array<Node*, 3> pending_{};
        std::size_t cursor_{};
        State state_{State::offline};
    };
}

namespace async::debug
{
    template <class T>
        requires requires { typename T::h755_usb_operation; }
    struct context_for<T>
    {
        struct tag
        {
        };
        static constexpr auto name = stdx::ct_string{"usb_cdc"};
        using type = T;
        using children = stdx::type_list<>;
    };
}
