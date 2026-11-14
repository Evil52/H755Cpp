#include "runtime.hpp"
#include "h755/fault.hpp"
#include "tusb.h"
#include <async/start.hpp>
#include <stdx/functional.hpp>
namespace
{
    h755::Timebase* clock_owner{};
    h755::usb::Runtime* runtime_owner{};
}
extern "C"
{
    volatile std::uint32_t g_usb_startup{};
    volatile std::uint32_t g_usb_state{};
    volatile std::uint32_t g_usb_error{};
}
namespace h755::usb
{
    Runtime::~Runtime()
    {
        operation_.reset();
        if (runtime_owner == this)
        {
            if (tud_inited())
            {
                tud_deinit(0);
            }
            clock_owner = nullptr;
            runtime_owner = nullptr;
        }
    }
    bool Runtime::start()
    {
        if (runtime_owner != nullptr)
        {
            return false;
        }
        runtime_owner = this;
        clock_owner = &clock_;
        return startup_.start();
    }
    void Runtime::poll()
    {
        startup_.poll();
        g_usb_startup = static_cast<std::uint32_t>(startup_.state());
        g_usb_error = static_cast<std::uint32_t>(startup_.error());
        if (startup_.state() == StartupState::failed)
        {
            fault::stop(FaultCode::usb);
        }
        if (startup_.state() != StartupState::running)
        {
            return;
        }
        if (!operation_)
        {
            operation_.emplace(stdx::with_result_of{
                [this] { return async::connect(echo(device_, buffer_), Receiver{}); }});
            async::start(*operation_);
        }
        port_.service();
        device_.poll();
        g_usb_state = static_cast<std::uint32_t>(device_.state());
    }
    void Runtime::Receiver::set_error(TransferError) &&
    {
        fault::stop(FaultCode::usb);
    }
    void Runtime::Receiver::set_stopped() &&
    {
        fault::stop(FaultCode::usb);
    }
}
extern "C" std::uint32_t tusb_time_millis_api()
{
    if (clock_owner == nullptr)
    {
        h755::fault::stop(h755::FaultCode::usb);
    }
    return clock_owner->now();
}
extern "C" void OTG_FS_IRQHandler()
{
    tusb_int_handler(0, true);
}
extern "C" void h755_usb_stack_fault()
{
    h755::usb::Platform::disconnect();
    g_usb_error = 0x100U;
    h755::fault::stop(h755::FaultCode::usb);
}
extern "C" void h755_usb_wait(volatile std::uint32_t* reg, std::uint32_t mask,
                              std::uint32_t expected)
{
    if (!h755::usb::wait_register([reg] { return *reg; }, mask, expected, 65'536U))
    {
        h755_usb_stack_fault();
    }
}

extern "C" [[noreturn]] void __wrap_abort()
{
    h755::fault::stop(h755::FaultCode::assertion);
}
