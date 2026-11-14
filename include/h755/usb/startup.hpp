#pragma once
#include <cstdint>
namespace h755::usb
{
    enum class StartupState
    {
        idle,
        waiting_clock,
        running,
        failed
    };
    enum class StartupError
    {
        none,
        clock_timeout,
        stack_init
    };
    template <class Platform, class Clock, class Stack> class Startup
    {
      public:
        Startup(Platform& platform, Clock& clock, Stack& stack)
            : platform_{platform}, clock_{clock}, stack_{stack}
        {
        }
        bool start()
        {
            if (state_ != StartupState::idle)
            {
                return false;
            }
            started_ = clock_.now();
            platform_.enable_clock();
            state_ = StartupState::waiting_clock;
            return true;
        }
        void poll()
        {
            if (state_ != StartupState::waiting_clock)
            {
                return;
            }
            if (static_cast<std::uint32_t>(clock_.now() - started_) >= 20U)
            {
                fail(StartupError::clock_timeout);
                return;
            }
            if (!platform_.clock_ready())
            {
                return;
            }
            platform_.configure();
            if (!stack_.start())
            {
                fail(StartupError::stack_init);
                return;
            }
            state_ = StartupState::running;
        }
        StartupState state() const
        {
            return state_;
        }
        StartupError error() const
        {
            return error_;
        }

      private:
        void fail(StartupError error)
        {
            error_ = error;
            state_ = StartupState::failed;
        }
        Platform& platform_;
        Clock& clock_;
        Stack& stack_;
        std::uint32_t started_{};
        StartupState state_{StartupState::idle};
        StartupError error_{StartupError::none};
    };
    template <class Read>
    bool wait_register(Read read, std::uint32_t mask, std::uint32_t expected, std::uint32_t budget)
    {
        for (std::uint32_t attempt = 0; attempt < budget; ++attempt)
        {
            if ((read() & mask) == expected)
            {
                return true;
            }
        }
        return false;
    }
}
