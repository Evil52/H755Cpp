#pragma once
#include "h755/usb/cdc.hpp"
#include <async/just.hpp>
#include <async/let_error.hpp>
#include <async/let_value.hpp>
#include <async/repeat.hpp>

namespace h755::usb
{
    enum class EchoErrorAction
    {
        retry,
        fault
    };

    constexpr EchoErrorAction classify_echo_error(ErrorCode code) noexcept
    {
        switch (code)
        {
        case ErrorCode::reset:
        case ErrorCode::not_ready:
        case ErrorCode::timeout:
            return EchoErrorAction::retry;
        case ErrorCode::busy:
        case ErrorCode::invalid_timeout:
        case ErrorCode::transport:
            return EchoErrorAction::fault;
        }
        return EchoErrorAction::fault;
    }

    template <class Device, class ErrorHandler>
    auto echo(Device& device, std::span<std::byte> buffer, ErrorHandler& on_error)
    {
        return device.wait_ready(1000) |
               async::let_value([&device, buffer](std::size_t)
                                { return device.read_some(buffer, 1000); }) |
               async::let_value(
                   [&device, buffer](std::size_t count) {
                       return device.write_all(std::span<const std::byte>{buffer.first(count)},
                                               1000);
                   }) |
               async::let_error([&on_error](TransferError error) {
                   on_error(error);
                   return async::just(std::size_t{0});
               }) |
               async::repeat();
    }
}
