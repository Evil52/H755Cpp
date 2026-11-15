#pragma once

#include "lwip/tcp.h"
#include "lwip/udp.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace h755::ethernet
{
    class LwipEcho final
    {
      public:
        static constexpr u16_t port = 7777U;
        LwipEcho() = default;
        LwipEcho(const LwipEcho&) = delete;
        LwipEcho& operator=(const LwipEcho&) = delete;

        [[nodiscard]] bool start() noexcept
        {
            if (udp_ != nullptr || listener_ != nullptr)
            {
                return false;
            }
            udp_ = udp_new();
            if (udp_ == nullptr)
            {
                return false;
            }
            if (udp_bind(udp_, IP_ANY_TYPE, port) != ERR_OK)
            {
                udp_remove(udp_);
                udp_ = nullptr;
                return false;
            }
            udp_recv(udp_, receive_udp, this);
            auto* pending = tcp_new();
            if (pending == nullptr)
            {
                udp_remove(udp_);
                udp_ = nullptr;
                return false;
            }
            if (tcp_bind(pending, IP_ANY_TYPE, port) != ERR_OK)
            {
                tcp_abort(pending);
                udp_remove(udp_);
                udp_ = nullptr;
                return false;
            }
            listener_ = tcp_listen_with_backlog(pending, 1U);
            if (listener_ == nullptr)
            {
                tcp_abort(pending);
                udp_remove(udp_);
                udp_ = nullptr;
                return false;
            }
            tcp_arg(listener_, this);
            tcp_accept(listener_, accept_tcp);
            return true;
        }

        [[nodiscard]] std::uint32_t udp_echoes() const noexcept
        {
            return udp_echoes_;
        }
        [[nodiscard]] std::uint32_t tcp_echoes() const noexcept
        {
            return tcp_echoes_;
        }
        [[nodiscard]] std::uint32_t rejected() const noexcept
        {
            return rejected_;
        }

      private:
        static void receive_udp(void* argument, udp_pcb* pcb, pbuf* incoming,
                                const ip_addr_t* address, u16_t remote_port) noexcept
        {
            auto& self = *static_cast<LwipEcho*>(argument);
            if (incoming == nullptr)
            {
                return;
            }
            if (incoming->tot_len == 0U || incoming->tot_len > self.udp_limit)
            {
                ++self.rejected_;
                pbuf_free(incoming);
                return;
            }
            auto* reply = pbuf_alloc(PBUF_TRANSPORT, incoming->tot_len, PBUF_RAM);
            if (reply == nullptr || pbuf_copy(reply, incoming) != ERR_OK ||
                udp_sendto(pcb, reply, address, remote_port) != ERR_OK)
            {
                ++self.rejected_;
            }
            else
            {
                ++self.udp_echoes_;
            }
            if (reply != nullptr)
            {
                pbuf_free(reply);
            }
            pbuf_free(incoming);
        }

        static err_t accept_tcp(void* argument, tcp_pcb* connection, err_t error) noexcept
        {
            auto& self = *static_cast<LwipEcho*>(argument);
            if (error != ERR_OK || connection == nullptr)
            {
                return ERR_VAL;
            }
            if (self.connection_ != nullptr)
            {
                ++self.rejected_;
                tcp_abort(connection);
                return ERR_ABRT;
            }
            self.connection_ = connection;
            tcp_arg(connection, &self);
            tcp_recv(connection, receive_tcp);
            tcp_err(connection, error_tcp);
            return ERR_OK;
        }

        static err_t receive_tcp(void* argument, tcp_pcb* connection, pbuf* incoming,
                                 err_t error) noexcept
        {
            auto& self = *static_cast<LwipEcho*>(argument);
            if (error != ERR_OK)
            {
                return error;
            }
            if (incoming == nullptr)
            {
                self.connection_ = nullptr;
                if (tcp_close(connection) != ERR_OK)
                {
                    tcp_abort(connection);
                    return ERR_ABRT;
                }
                return ERR_OK;
            }
            if (incoming->tot_len == 0U || incoming->tot_len > self.tcp_frame_.size() ||
                pbuf_copy_partial(incoming, self.tcp_frame_.data(), incoming->tot_len, 0U) !=
                    incoming->tot_len)
            {
                ++self.rejected_;
                pbuf_free(incoming);
                self.connection_ = nullptr;
                tcp_abort(connection);
                return ERR_ABRT;
            }
            const auto written = tcp_write(connection, self.tcp_frame_.data(), incoming->tot_len,
                                           TCP_WRITE_FLAG_COPY);
            if (written != ERR_OK)
            {
                return written;
            }
            tcp_recved(connection, incoming->tot_len);
            pbuf_free(incoming);
            ++self.tcp_echoes_;
            (void)tcp_output(connection);
            return ERR_OK;
        }

        static void error_tcp(void* argument, err_t) noexcept
        {
            static_cast<LwipEcho*>(argument)->connection_ = nullptr;
        }

        static constexpr u16_t udp_limit = 512U;
        udp_pcb* udp_{};
        tcp_pcb* listener_{};
        tcp_pcb* connection_{};
        std::array<std::uint8_t, TCP_MSS> tcp_frame_{};
        std::uint32_t udp_echoes_{};
        std::uint32_t tcp_echoes_{};
        std::uint32_t rejected_{};
    };
}
