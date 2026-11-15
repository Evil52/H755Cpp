#pragma once

#include "h755/ethernet/lwip_clock.h"
#include "h755/ethernet/mac_engine.hpp"
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace h755::ethernet
{
    template <class Mac> class LwipNetwork final
    {
      public:
        LwipNetwork(Mac& mac, std::array<std::uint8_t, 6U> address) noexcept
            : mac_{mac}, address_{address}
        {
        }
        LwipNetwork(const LwipNetwork&) = delete;
        LwipNetwork& operator=(const LwipNetwork&) = delete;

        [[nodiscard]] bool start(std::uint32_t now) noexcept
        {
            if (started_)
            {
                return false;
            }
            h755_lwip_set_now(now);
            lwip_init();
            ip4_addr_t zero{};
            if (netif_add(&interface_, &zero, &zero, &zero, this, initialize_interface,
                          ethernet_input) == nullptr)
            {
                return false;
            }
            netif_set_default(&interface_);
            netif_set_up(&interface_);
            if (dhcp_start(&interface_) != ERR_OK)
            {
                netif_set_down(&interface_);
                netif_remove(&interface_);
                return false;
            }
            started_ = true;
            return true;
        }

        void poll(std::uint32_t now, LinkState link) noexcept
        {
            poll(now, link, [](std::span<const std::byte>) noexcept {});
        }

        template <class Observer>
        void poll(std::uint32_t now, LinkState link, Observer&& observer) noexcept
        {
            h755_lwip_set_now(now);
            mac_.poll(now, link,
                      [this, &observer](std::span<const std::byte> frame) noexcept
                      {
                          observer(frame);
                          receive(frame);
                      });
            if (started_)
            {
                const bool running = mac_.state() == MacState::running;
                if (running != link_up_)
                {
                    if (running)
                    {
                        netif_set_link_up(&interface_);
                    }
                    else
                    {
                        netif_set_link_down(&interface_);
                    }
                    link_up_ = running;
                }
                sys_check_timeouts();
            }
        }

        [[nodiscard]] bool address_ready() const noexcept
        {
            return started_ && dhcp_supplied_address(&interface_) != 0;
        }
        [[nodiscard]] std::uint32_t ipv4_address() const noexcept
        {
            return ip4_addr_get_u32(netif_ip4_addr(&interface_));
        }
        [[nodiscard]] std::uint32_t received_frames() const noexcept
        {
            return received_frames_;
        }
        [[nodiscard]] std::uint32_t rejected_frames() const noexcept
        {
            return rejected_frames_;
        }
        [[nodiscard]] std::uint32_t transmitted_frames() const noexcept
        {
            return transmitted_frames_;
        }
        [[nodiscard]] netif& interface() noexcept
        {
            return interface_;
        }

      private:
        static err_t initialize_interface(netif* iface) noexcept
        {
            if (iface == nullptr || iface->state == nullptr)
            {
                return ERR_ARG;
            }
            auto& self = *static_cast<LwipNetwork*>(iface->state);
            iface->name[0] = 'e';
            iface->name[1] = 'n';
            iface->mtu = 1500U;
            iface->hwaddr_len = 6U;
            for (std::size_t index = 0U; index < self.address_.size(); ++index)
            {
                iface->hwaddr[index] = self.address_[index];
            }
            iface->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
            iface->output = etharp_output;
            iface->linkoutput = transmit;
            return ERR_OK;
        }

        static err_t transmit(netif* iface, pbuf* packet) noexcept
        {
            if (iface == nullptr || iface->state == nullptr || packet == nullptr)
            {
                return ERR_ARG;
            }
            auto& self = *static_cast<LwipNetwork*>(iface->state);
            const auto length = static_cast<std::size_t>(packet->tot_len);
            if (length > self.tx_frame_.size())
            {
                return ERR_BUF;
            }
            const auto padded = std::max<std::size_t>(length, 60U);
            std::fill(self.tx_frame_.begin() + static_cast<std::ptrdiff_t>(length),
                      self.tx_frame_.begin() + static_cast<std::ptrdiff_t>(padded), std::byte{});
            if (pbuf_copy_partial(packet, self.tx_frame_.data(), packet->tot_len, 0U) !=
                packet->tot_len)
            {
                return ERR_BUF;
            }
            const auto result =
                self.mac_.send(std::span<const std::byte>{self.tx_frame_.data(), padded});
            if (result == MacError::none)
            {
                ++self.transmitted_frames_;
                return ERR_OK;
            }
            return result == MacError::queue_full ? ERR_MEM : ERR_IF;
        }

        void receive(std::span<const std::byte> frame) noexcept
        {
            if (!started_ || frame.size() < 14U || frame.size() > mac_buffer_size)
            {
                ++rejected_frames_;
                return;
            }
            if (frame[12U] != std::byte{0x08U} ||
                (frame[13U] != std::byte{0x00U} && frame[13U] != std::byte{0x06U}))
            {
                return;
            }
            const auto minimum = frame[13U] == std::byte{0x06U} ? 42U : 34U;
            if (frame.size() < minimum)
            {
                ++rejected_frames_;
                return;
            }
            auto* packet = pbuf_alloc(PBUF_RAW, static_cast<u16_t>(frame.size()), PBUF_POOL);
            if (packet == nullptr)
            {
                ++rejected_frames_;
                return;
            }
            if (pbuf_take(packet, frame.data(), static_cast<u16_t>(frame.size())) != ERR_OK ||
                interface_.input(packet, &interface_) != ERR_OK)
            {
                pbuf_free(packet);
                ++rejected_frames_;
                return;
            }
            ++received_frames_;
        }

        Mac& mac_;
        std::array<std::uint8_t, 6U> address_;
        netif interface_{};
        std::array<std::byte, mac_buffer_size> tx_frame_{};
        std::uint32_t received_frames_{};
        std::uint32_t rejected_frames_{};
        std::uint32_t transmitted_frames_{};
        bool started_{};
        bool link_up_{};
    };
}
