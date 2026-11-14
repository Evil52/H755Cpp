#pragma once
#include "h755/usb/tinyusb_port.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>
namespace usb_test
{
    struct Endpoint
    {
        std::uint8_t* data{};
        std::uint16_t size{};
        bool pending{};
        bool stalled{};
    };
    extern std::array<Endpoint, 32> endpoints;
    extern std::uint32_t millis;
    extern unsigned failures;
    extern unsigned opens;
    extern bool irq_enabled;
    void check(bool good, const char* message);
    Endpoint& endpoint(std::uint8_t address);
    void complete(std::uint8_t address, std::span<const std::byte> input = {});
    void drain(h755::usb::TinyUsbPort& port);
    std::vector<std::uint8_t> control(h755::usb::TinyUsbPort& port, std::uint8_t type,
                                      std::uint8_t request, std::uint16_t value,
                                      std::uint16_t index, std::uint16_t size,
                                      std::span<const std::byte> payload = {});
    void configure(h755::usb::TinyUsbPort& port);
}
