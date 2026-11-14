#include "h755/usb/tinyusb_port.hpp"
#include "tusb.h"
#include "device/dcd.h"
#include <atomic>
namespace
{
    std::atomic<std::uint32_t> generation{};
    bool line_open{};
    static_assert(decltype(generation)::is_always_lock_free);
}
namespace h755::usb
{
    bool TinyUsbPort::start()
    {
        if (tud_inited())
        {
            return false;
        }
        const tusb_rhport_init_t config{.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
        return tusb_init(0, &config);
    }
    void TinyUsbPort::service()
    {
        tud_task_ext(0, false);
    }
    Status TinyUsbPort::status() const
    {
        const auto epoch = generation.load(std::memory_order_relaxed);
        const auto mounted = tud_mounted();
        const auto suspended = tud_suspended();
        const auto open = tud_cdc_connected();
        const auto pending = tud_task_event_ready();
        return {mounted, suspended, open, epoch,
                pending || epoch != generation.load(std::memory_order_relaxed)};
    }
    std::size_t TinyUsbPort::available() const
    {
        return tud_cdc_available();
    }
    std::size_t TinyUsbPort::write_available() const
    {
        return tud_cdc_write_available();
    }
    std::size_t TinyUsbPort::read(std::span<std::byte> buffer)
    {
        if (buffer.size() > 64U)
        {
            return 0;
        }
        return tud_cdc_read(buffer.data(), static_cast<std::uint32_t>(buffer.size()));
    }
    std::size_t TinyUsbPort::write(std::span<const std::byte> buffer)
    {
        if (buffer.size() > 64U)
        {
            return 0;
        }
        const auto count = tud_cdc_write(buffer.data(), static_cast<std::uint32_t>(buffer.size()));
        tud_cdc_write_flush();
        return count;
    }
}
extern "C" void tud_event_hook_cb(std::uint8_t, std::uint32_t event, bool)
{
    if (event == DCD_EVENT_BUS_RESET || event == DCD_EVENT_UNPLUGGED)
    {
        generation.fetch_add(1, std::memory_order_relaxed);
    }
}

extern "C" void tud_cdc_line_state_cb(std::uint8_t interface, bool dtr, bool)
{
    // CDC control callbacks run in the main-loop USB task, unlike reset/unplug event hooks.
    if (interface == 0U && dtr != line_open)
    {
        line_open = dtr;
        generation.fetch_add(1, std::memory_order_relaxed);
    }
}
