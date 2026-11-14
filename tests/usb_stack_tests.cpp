#include "usb_host_simulator.hpp"
#include "h755/usb/echo.hpp"
#include "tusb.h"
#include "device/dcd.h"
#include <async/connect.hpp>
#include <async/start.hpp>
#include <array>
#include <cstdio>
#include <vector>
using namespace usb_test;
namespace
{
    struct Clock
    {
        std::uint32_t now() const
        {
            return millis;
        }
    };
    struct Receiver
    {
        using is_receiver = void;
        void set_value(auto&&...) &&
        {
        }
        void set_error(h755::usb::TransferError) &&
        {
            check(false, "echo handles expected USB failures");
        }
        void set_stopped() &&
        {
        }
    };
}
int main()
{
    h755::usb::TinyUsbPort port;
    check(port.start() && !port.start(), "one TinyUSB device owner");
    check(irq_enabled && !port.status().mounted, "initial stack awaits enumeration");
    dcd_event_bus_reset(0, TUSB_SPEED_FULL, false);
    drain(port);
    const auto device = control(port, 0x80, TUSB_REQ_GET_DESCRIPTOR, 0x0100, 0, 18);
    check(device.size() == 18 && device[8] == 0xFE && device[9] == 0xCA,
          "device descriptor over real EP0 stack");
    const auto configuration = control(port, 0x80, TUSB_REQ_GET_DESCRIPTOR, 0x0200, 0, 255);
    check(configuration.size() == TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN && configuration[4] == 2,
          "complete CDC/IAD configuration crosses EP0 packet boundary");
    const auto manufacturer = control(port, 0x80, TUSB_REQ_GET_DESCRIPTOR, 0x0301, 0x0409, 64);
    check(manufacturer.size() == 16 && manufacturer[2] == 'H' && manufacturer[3] == 0,
          "manufacturer UTF16 descriptor over EP0");
    const auto text = control(port, 0x80, TUSB_REQ_GET_DESCRIPTOR, 0x0302, 0x0409, 64);
    check(text.size() == 30 && text[1] == TUSB_DESC_STRING, "bounded UTF16 product string");
    const auto bad = control(port, 0x80, TUSB_REQ_GET_DESCRIPTOR, 0x03FF, 0x0409, 64);
    check(bad.empty() && endpoint(0).stalled && endpoint(0x80).stalled,
          "unknown string descriptor stalls safely");
    configure(port);
    const std::array<std::byte, 7> line{std::byte{0}, std::byte{0xC2}, std::byte{1}, std::byte{0},
                                        std::byte{0}, std::byte{0},    std::byte{8}};
    control(port, 0x21, CDC_REQUEST_SET_LINE_CODING, 0, 0, 7, line);
    Clock clock;
    h755::usb::Cdc session{port, clock};
    std::array<std::byte, 64> buffer{};
    auto operation = async::connect(h755::usb::echo(session, buffer), Receiver{});
    async::start(operation);
    for (auto count : {1U, 63U, 64U, 65U, 257U})
    {
        std::vector<std::byte> input(count);
        for (std::size_t i = 0; i < input.size(); ++i)
        {
            input[i] = static_cast<std::byte>(i & 0xFFU);
        }
        std::vector<std::byte> output;
        std::size_t sent{};
        for (unsigned step = 0; step < 2000 && output.size() < input.size(); ++step)
        {
            if (sent < input.size() && endpoint(2).pending)
            {
                auto size = input.size() - sent;
                if (size > 64)
                {
                    size = 64;
                }
                complete(2, std::span<const std::byte>{input}.subspan(sent, size));
                sent += size;
            }
            drain(port);
            session.poll();
            if (endpoint(0x82).pending)
            {
                const auto& ep = endpoint(0x82);
                for (std::size_t i = 0; i < ep.size; ++i)
                {
                    output.push_back(static_cast<std::byte>(ep.data[i]));
                }
                complete(0x82);
                drain(port);
            }
            ++millis;
        }
        check(output == input, "USB CDC binary echo through real EP0, CDC, FIFO and Intel async");
        if (endpoint(0x82).pending)
        {
            complete(0x82);
            drain(port);
        }
    }
    const auto prior_line = port.status().generation;
    control(port, 0x21, CDC_REQUEST_SET_CONTROL_LINE_STATE, 3, 0, 0);
    check(port.status().generation == prior_line, "duplicate DTR state preserves pending IO");
    control(port, 0x21, CDC_REQUEST_SET_CONTROL_LINE_STATE, 0, 0, 0);
    control(port, 0x21, CDC_REQUEST_SET_CONTROL_LINE_STATE, 3, 0, 0);
    check(port.status().generation != prior_line,
          "close/reopen invalidates earlier CDC operations even between polls");
    const auto generation = port.status().generation;
    dcd_event_bus_reset(0, TUSB_SPEED_FULL, false);
    session.poll();
    check(port.status().generation != generation && port.status().pending_events,
          "ISR reset invalidates operation epoch before queued stack work");
    drain(port);
    session.poll();
    opens = 0;
    configure(port);
    for (unsigned i = 0; i < 5; ++i)
    {
        port.service();
        session.poll();
    }
    check(port.status().open, "CDC reenumerates after reset");
    check(tud_descriptor_configuration_cb(1) == nullptr &&
              tud_descriptor_string_cb(3, 0x0409) == nullptr &&
              tud_descriptor_string_cb(1, 0x0419) == nullptr,
          "descriptor validation rejects unsupported indexes/language");
    check(tud_descriptor_string_cb(0, 0) != nullptr, "language descriptor present");
    std::array<std::byte, 65> oversized{};
    check(port.read(oversized) == 0 && port.write(oversized) == 0,
          "backend enforces bounded transfer size");
    return failures == 0 ? 0 : 1;
}
