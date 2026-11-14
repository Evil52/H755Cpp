#include "usb_host_simulator.hpp"
#include "h755/usb/tinyusb_port.hpp"
#include "h755/usb/echo.hpp"
#include "tusb.h"
#include "device/dcd.h"
#include <async/connect.hpp>
#include <async/start.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace usb_test
{
    std::array<Endpoint, 32> endpoints{};
    std::uint32_t millis{};
    unsigned failures{};
    unsigned opens{};
    bool irq_enabled{};
    void check(bool good, const char* message)
    {
        if (!good)
        {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", message);
        }
    }
    Endpoint& endpoint(std::uint8_t address)
    {
        return endpoints[(address & 0x0FU) + ((address & 0x80U) != 0U ? 16U : 0U)];
    }
    void complete(std::uint8_t address, std::span<const std::byte> input)
    {
        auto& ep = endpoint(address);
        check(ep.pending, "host completion targets an armed endpoint");
        if (!ep.pending)
        {
            return;
        }
        const auto size =
            (address & 0x80U) != 0U ? ep.size : static_cast<std::uint16_t>(input.size());
        check(size <= ep.size, "host payload fits pending receive");
        if (!input.empty())
        {
            std::memcpy(ep.data, input.data(), input.size());
        }
        ep.pending = false;
        dcd_event_xfer_complete(0, address, size, XFER_RESULT_SUCCESS, false);
    }
    void drain(h755::usb::TinyUsbPort& port)
    {
        for (unsigned i = 0; i < 8 && tud_task_event_ready(); ++i)
        {
            port.service();
        }
    }
    std::vector<std::uint8_t> control(h755::usb::TinyUsbPort& port, std::uint8_t type,
                                      std::uint8_t request, std::uint16_t value,
                                      std::uint16_t index, std::uint16_t size,
                                      std::span<const std::byte> payload)
    {
        endpoint(0).pending = false;
        endpoint(0x80).pending = false;
        endpoint(0).stalled = false;
        endpoint(0x80).stalled = false;
        const std::array<std::uint8_t, 8> setup{type,
                                                request,
                                                static_cast<std::uint8_t>(value & 0xFFU),
                                                static_cast<std::uint8_t>(value >> 8U),
                                                static_cast<std::uint8_t>(index & 0xFFU),
                                                static_cast<std::uint8_t>(index >> 8U),
                                                static_cast<std::uint8_t>(size & 0xFFU),
                                                static_cast<std::uint8_t>(size >> 8U)};
        dcd_event_setup_received(0, setup.data(), false);
        drain(port);
        std::vector<std::uint8_t> response;
        for (unsigned step = 0; step < 12; ++step)
        {
            if (endpoint(0x80).pending)
            {
                const auto& ep = endpoint(0x80);
                if (ep.size != 0)
                {
                    response.insert(response.end(), ep.data, ep.data + ep.size);
                }
                complete(0x80);
                drain(port);
            }
            else if (endpoint(0).pending)
            {
                complete(0, payload);
                payload = {};
                drain(port);
            }
            else
            {
                return response;
            }
        }
        check(false, "control transfer bounded to expected stages");
        return response;
    }
    void configure(h755::usb::TinyUsbPort& port)
    {
        control(port, 0x00, TUSB_REQ_SET_CONFIGURATION, 1, 0, 0);
        check(port.status().mounted && opens == 3, "real stack opens CDC notify/data endpoints");
        control(port, 0x21, CDC_REQUEST_SET_CONTROL_LINE_STATE, 3, 0, 0);
        check(port.status().open, "DTR opens actual CDC interface");
    }
}
using namespace usb_test;
extern "C"
{
#ifndef H755_TEST_REAL_RUNTIME
    std::uint32_t tusb_time_millis_api()
    {
        return millis;
    }
    void h755_usb_stack_fault()
    {
        std::fputs("TinyUSB invariant failed\n", stderr);
        std::abort();
    }
#endif
    bool dcd_init(std::uint8_t, const tusb_rhport_init_t*)
    {
        return true;
    }
    bool dcd_deinit(std::uint8_t)
    {
        return true;
    }
    void dcd_int_enable(std::uint8_t)
    {
        irq_enabled = true;
    }
    void dcd_int_disable(std::uint8_t)
    {
        irq_enabled = false;
    }
    void dcd_int_handler(std::uint8_t)
    {
    }
    void dcd_set_address(std::uint8_t, std::uint8_t)
    {
        endpoint(0x80) = {nullptr, 0, true, false};
    }
    void dcd_remote_wakeup(std::uint8_t)
    {
    }
    void dcd_connect(std::uint8_t)
    {
    }
    void dcd_disconnect(std::uint8_t)
    {
    }
    void dcd_sof_enable(std::uint8_t, bool)
    {
    }
    bool dcd_edpt_open(std::uint8_t, const tusb_desc_endpoint_t*)
    {
        ++opens;
        return true;
    }
    void dcd_edpt_close_all(std::uint8_t)
    {
        for (auto& ep : endpoints)
        {
            ep = {};
        }
        opens = 0;
    }
    bool dcd_edpt_xfer(std::uint8_t, std::uint8_t address, std::uint8_t* buffer, std::uint16_t size,
                       bool)
    {
        endpoint(address) = {buffer, size, true, false};
        return true;
    }
    void dcd_edpt_stall(std::uint8_t, std::uint8_t address)
    {
        endpoint(address).stalled = true;
        endpoint(address).pending = false;
    }
    bool dcd_edpt_iso_alloc(std::uint8_t, std::uint8_t, std::uint16_t)
    {
        return false;
    }
    bool dcd_edpt_iso_activate(std::uint8_t, const tusb_desc_endpoint_t*)
    {
        return false;
    }
    void dcd_edpt_clear_stall(std::uint8_t, std::uint8_t address)
    {
        endpoint(address).stalled = false;
    }
}
