#pragma once
#include "h755/usb/cdc.hpp"
#include <async/just.hpp>
#include <async/let_error.hpp>
#include <async/let_value.hpp>
#include <async/repeat.hpp>

namespace h755::usb
{
    template <class Device> auto echo(Device& device, std::span<std::byte> buffer)
    {
        return device.wait_ready(1000) |
               async::let_value([&device, buffer](std::size_t)
                                { return device.read_some(buffer, 1000); }) |
               async::let_value(
                   [&device, buffer](std::size_t count) {
                       return device.write_all(std::span<const std::byte>{buffer.first(count)},
                                               1000);
                   }) |
               async::let_error([](TransferError) { return async::just(std::size_t{0}); }) |
               async::repeat();
    }
}
