#pragma once
#include "h755/usb/echo.hpp"
#include "h755/usb/startup.hpp"
#include "h755/usb/tinyusb_port.hpp"
#include "h755/timebase.hpp"
#include <async/connect.hpp>
#include <array>
#include <optional>
namespace h755::usb
{
    class Platform
    {
      public:
        void enable_clock();
        bool clock_ready() const;
        void configure();
        static void disconnect();
    };
    class Runtime
    {
        using Device = Cdc<TinyUsbPort, Timebase>;
        struct Receiver
        {
            using is_receiver = void;
            void set_value(auto&&...) &&
            {
            }
            void set_error(TransferError) &&;
            void set_stopped() &&;
        };
        using Flow = decltype(echo(std::declval<Device&>(), std::declval<std::span<std::byte>>()));
        using Operation = async::connect_result_t<Flow, Receiver>;

      public:
        explicit Runtime(Timebase& clock)
            : clock_{clock}, device_{port_, clock}, startup_{platform_, clock, port_}
        {
        }
        ~Runtime();
        Runtime(const Runtime&) = delete;
        Runtime& operator=(const Runtime&) = delete;
        bool start();
        void poll();

      private:
        Timebase& clock_;
        Platform platform_{};
        TinyUsbPort port_{};
        Device device_;
        std::array<std::byte, 64> buffer_{};
        std::optional<Operation> operation_{};
        Startup<Platform, Timebase, TinyUsbPort> startup_;
    };
}
