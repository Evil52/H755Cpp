#pragma once
#include "h755/usb/cdc.hpp"
namespace h755::usb
{
    class TinyUsbPort
    {
      public:
        TinyUsbPort() = default;
        TinyUsbPort(const TinyUsbPort&) = delete;
        TinyUsbPort& operator=(const TinyUsbPort&) = delete;
        bool start();
        void service();
        Status status() const;
        std::size_t available() const;
        std::size_t write_available() const;
        std::size_t read(std::span<std::byte> buffer);
        std::size_t write(std::span<const std::byte> buffer);
    };
}
