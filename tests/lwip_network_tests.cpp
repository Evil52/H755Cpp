#include "h755/ethernet/lwip_network.hpp"
#include "h755/ethernet/lwip_echo.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>

extern "C" void h755_lwip_assert(const char*)
{
    std::abort();
}

extern "C" std::uint32_t h755_lwip_random()
{
    return 0x8C4F9215U;
}

namespace
{
    void check(bool value, const char* message)
    {
        if (!value)
        {
            std::cerr << message << '\n';
            std::abort();
        }
    }

    struct FakeMac
    {
        h755::ethernet::MacState current{h755::ethernet::MacState::ready};
        std::vector<std::byte> last_tx;
        std::vector<std::byte> pending_rx;
        h755::ethernet::MacError next_send{h755::ethernet::MacError::none};
        [[nodiscard]] h755::ethernet::MacState state() const noexcept
        {
            return current;
        }
        [[nodiscard]] h755::ethernet::MacError send(std::span<const std::byte> frame)
        {
            if (next_send != h755::ethernet::MacError::none)
            {
                const auto result = next_send;
                next_send = h755::ethernet::MacError::none;
                return result;
            }
            last_tx.assign(frame.begin(), frame.end());
            return h755::ethernet::MacError::none;
        }
        template <class Receive>
        void poll(std::uint32_t, h755::ethernet::LinkState link, Receive&& receive)
        {
            current = link == h755::ethernet::LinkState::down ? h755::ethernet::MacState::ready
                                                              : h755::ethernet::MacState::running;
            if (!pending_rx.empty())
            {
                receive(std::span<const std::byte>{pending_rx});
                pending_rx.clear();
            }
        }
    };

    std::uint16_t checksum(std::span<const std::uint8_t> bytes)
    {
        std::uint32_t sum{};
        for (std::size_t index = 0U; index < bytes.size(); index += 2U)
        {
            sum += static_cast<std::uint32_t>(bytes[index]) << 8U;
            if (index + 1U < bytes.size())
            {
                sum += bytes[index + 1U];
            }
        }
        while (sum > 0xFFFFU)
        {
            sum = (sum & 0xFFFFU) + (sum >> 16U);
        }
        return static_cast<std::uint16_t>(~sum);
    }

    void put16(std::vector<std::uint8_t>& frame, std::uint16_t value)
    {
        frame.push_back(static_cast<std::uint8_t>(value >> 8U));
        frame.push_back(static_cast<std::uint8_t>(value));
    }

    void put32(std::vector<std::uint8_t>& frame, std::uint32_t value)
    {
        put16(frame, static_cast<std::uint16_t>(value >> 16U));
        put16(frame, static_cast<std::uint16_t>(value));
    }

    std::vector<std::uint8_t> ethernet(const std::array<std::uint8_t, 6>& destination,
                                       const std::array<std::uint8_t, 6>& source,
                                       std::uint16_t type)
    {
        std::vector<std::uint8_t> result(destination.begin(), destination.end());
        result.insert(result.end(), source.begin(), source.end());
        put16(result, type);
        return result;
    }

    void inject(FakeMac& mac, const std::vector<std::uint8_t>& frame)
    {
        mac.pending_rx.clear();
        for (const auto value : frame)
        {
            mac.pending_rx.push_back(static_cast<std::byte>(value));
        }
    }

    std::uint8_t octet(const std::vector<std::byte>& frame, std::size_t index)
    {
        return std::to_integer<std::uint8_t>(frame[index]);
    }

    void add_ipv4(std::vector<std::uint8_t>& frame, std::uint8_t protocol,
                  std::span<const std::uint8_t> payload,
                  std::array<std::uint8_t, 4> destination = {192U, 168U, 7U, 2U})
    {
        const auto start = frame.size();
        frame.insert(frame.end(), {0x45U, 0U});
        put16(frame, static_cast<std::uint16_t>(20U + payload.size()));
        frame.insert(frame.end(), {0U, 1U, 0U, 0U, 64U, protocol, 0U, 0U, 192U, 168U, 7U, 1U});
        frame.insert(frame.end(), destination.begin(), destination.end());
        const auto value = checksum({frame.data() + start, 20U});
        frame[start + 10U] = static_cast<std::uint8_t>(value >> 8U);
        frame[start + 11U] = static_cast<std::uint8_t>(value);
        frame.insert(frame.end(), payload.begin(), payload.end());
        if (frame.size() < 60U)
        {
            frame.resize(60U, 0U);
        }
    }

    std::vector<std::uint8_t> tcp_segment(std::uint32_t sequence, std::uint32_t acknowledge,
                                          std::uint8_t flags,
                                          std::span<const std::uint8_t> payload = {})
    {
        std::vector<std::uint8_t> segment;
        put16(segment, 44'001U);
        put16(segment, h755::ethernet::LwipEcho::port);
        put32(segment, sequence);
        put32(segment, acknowledge);
        segment.insert(segment.end(), {0x50U, flags, 0x10U, 0U, 0U, 0U, 0U, 0U});
        segment.insert(segment.end(), payload.begin(), payload.end());
        std::vector<std::uint8_t> pseudo{192U, 168U, 7U, 1U, 192U, 168U, 7U, 2U, 0U, 6U};
        put16(pseudo, static_cast<std::uint16_t>(segment.size()));
        pseudo.insert(pseudo.end(), segment.begin(), segment.end());
        const auto value = checksum(pseudo);
        segment[16U] = static_cast<std::uint8_t>(value >> 8U);
        segment[17U] = static_cast<std::uint8_t>(value);
        return segment;
    }

    std::uint32_t get32(const std::vector<std::byte>& frame, std::size_t index)
    {
        return (static_cast<std::uint32_t>(octet(frame, index)) << 24U) |
               (static_cast<std::uint32_t>(octet(frame, index + 1U)) << 16U) |
               (static_cast<std::uint32_t>(octet(frame, index + 2U)) << 8U) |
               octet(frame, index + 3U);
    }

    std::vector<std::uint8_t> dhcp_reply(const std::array<std::uint8_t, 6>& host,
                                         const std::array<std::uint8_t, 6>& board,
                                         std::uint32_t xid, std::uint8_t message)
    {
        constexpr std::array<std::uint8_t, 6> broadcast{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        std::vector<std::uint8_t> bootp(240U, 0U);
        bootp[0U] = 2U;
        bootp[1U] = 1U;
        bootp[2U] = 6U;
        bootp[4U] = static_cast<std::uint8_t>(xid >> 24U);
        bootp[5U] = static_cast<std::uint8_t>(xid >> 16U);
        bootp[6U] = static_cast<std::uint8_t>(xid >> 8U);
        bootp[7U] = static_cast<std::uint8_t>(xid);
        bootp[16U] = 192U;
        bootp[17U] = 168U;
        bootp[18U] = 7U;
        bootp[19U] = 2U;
        bootp[20U] = 192U;
        bootp[21U] = 168U;
        bootp[22U] = 7U;
        bootp[23U] = 1U;
        std::copy(board.begin(), board.end(), bootp.begin() + 28U);
        bootp[236U] = 99U;
        bootp[237U] = 130U;
        bootp[238U] = 83U;
        bootp[239U] = 99U;
        bootp.insert(bootp.end(),
                     {53U, 1U, message, 54U,  4U,   192U, 168U, 7U,  1U, 1U, 4U, 255U, 255U, 255U,
                      0U,  3U, 4U,      192U, 168U, 7U,   1U,   51U, 4U, 0U, 0U, 0U,   120U, 255U});
        std::vector<std::uint8_t> udp;
        put16(udp, 67U);
        put16(udp, 68U);
        put16(udp, static_cast<std::uint16_t>(8U + bootp.size()));
        put16(udp, 0U);
        udp.insert(udp.end(), bootp.begin(), bootp.end());
        auto frame = ethernet(broadcast, host, 0x0800U);
        add_ipv4(frame, 17U, udp, {255U, 255U, 255U, 255U});
        return frame;
    }
}

int main()
{
    FakeMac mac;
    constexpr std::array<std::uint8_t, 6> address{0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    h755::ethernet::LwipNetwork<FakeMac> network{mac, address};
    check(network.start(0), "network initializes");
    h755::ethernet::LwipEcho echo;
    check(echo.start(), "bounded UDP/TCP echo binds");
    check(!network.start(0), "network refuses duplicate start");
    check(!network.address_ready(), "no DHCP lease before exchange");
    network.poll(1, h755::ethernet::LinkState::mb100_full);
    check(network.interface().mtu == 1500, "Ethernet MTU");
    check(network.interface().hwaddr_len == 6, "Ethernet MAC length");
    check(mac.last_tx.size() >= 60, "outbound DHCP frame padded");
    check(network.transmitted_frames() > 0, "DHCP request sent through MAC");
    check(mac.last_tx.size() > 250U, "DHCP discover contains BOOTP payload");
    const auto xid = get32(mac.last_tx, 46U);
    constexpr std::array<std::uint8_t, 6> host{0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    inject(mac, dhcp_reply(host, address, xid, 2U));
    network.poll(2U, h755::ethernet::LinkState::mb100_full);
    inject(mac, dhcp_reply(host, address, xid, 5U));
    network.poll(3U, h755::ethernet::LinkState::mb100_full);
    std::uint32_t now = 253U;
    for (; now <= 30'003U && !network.address_ready(); now += 250U)
    {
        network.poll(now, h755::ethernet::LinkState::mb100_full);
    }
    check(network.address_ready(), "DHCP offer and acknowledgement assign address");
    auto* short_packet = pbuf_alloc(PBUF_RAW, 42U, PBUF_RAM);
    check(short_packet != nullptr, "short packet allocated");
    const auto previous_tx = network.transmitted_frames();
    mac.next_send = h755::ethernet::MacError::queue_full;
    check(network.interface().linkoutput(&network.interface(), short_packet) == ERR_MEM,
          "MAC queue pressure propagates to lwIP");
    check(network.transmitted_frames() == previous_tx,
          "rejected packet is not counted as transmitted");
    pbuf_free(short_packet);
    inject(mac, std::vector<std::uint8_t>(13U, 0U));
    network.poll(++now, h755::ethernet::LinkState::mb100_full);
    check(network.rejected_frames() == 1U, "short Ethernet frame rejected");
    auto short_arp = ethernet(address, host, 0x0806U);
    short_arp.push_back(0U);
    inject(mac, short_arp);
    network.poll(++now, h755::ethernet::LinkState::mb100_full);
    check(network.rejected_frames() == 2U, "short ARP frame rejected before lwIP parser");
    auto short_ipv4 = ethernet(address, host, 0x0800U);
    short_ipv4.push_back(0x45U);
    inject(mac, short_ipv4);
    network.poll(++now, h755::ethernet::LinkState::mb100_full);
    check(network.rejected_frames() == 3U, "short IPv4 frame rejected before lwIP parser");

    dhcp_stop(&network.interface());
    ip4_addr_t ip{};
    ip4_addr_t mask{};
    ip4_addr_t gateway{};
    IP4_ADDR(&ip, 192, 168, 7, 2);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gateway, 192, 168, 7, 1);
    netif_set_addr(&network.interface(), &ip, &mask, &gateway);
    constexpr std::array<std::uint8_t, 6> broadcast{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    auto arp = ethernet(broadcast, host, 0x0806U);
    arp.insert(arp.end(), {0U, 1U, 8U, 0U, 6U, 4U, 0U, 1U});
    arp.insert(arp.end(), host.begin(), host.end());
    arp.insert(arp.end(), {192U, 168U, 7U, 1U});
    arp.insert(arp.end(), {0U, 0U, 0U, 0U, 0U, 0U, 192U, 168U, 7U, 2U});
    arp.resize(60U, 0U);
    inject(mac, arp);
    network.poll(++now, h755::ethernet::LinkState::mb100_full);
    check(mac.last_tx.size() >= 60U && octet(mac.last_tx, 12U) == 0x08U &&
              octet(mac.last_tx, 13U) == 0x06U && octet(mac.last_tx, 21U) == 2U,
          "ARP request receives ARP reply");

    auto icmp = ethernet(address, host, 0x0800U);
    std::array<std::uint8_t, 8> echo_request{8U, 0U, 0U, 0U, 0x12U, 0x34U, 0U, 1U};
    const auto icmp_sum = checksum(echo_request);
    echo_request[2U] = static_cast<std::uint8_t>(icmp_sum >> 8U);
    echo_request[3U] = static_cast<std::uint8_t>(icmp_sum);
    add_ipv4(icmp, 1U, echo_request);
    inject(mac, icmp);
    network.poll(++now, h755::ethernet::LinkState::mb100_full);
    check(octet(mac.last_tx, 12U) == 8U && octet(mac.last_tx, 13U) == 0U &&
              octet(mac.last_tx, 34U) == 0U,
          "ICMP echo request receives echo reply");

    auto udp = ethernet(address, host, 0x0800U);
    std::array<std::uint8_t, 10> datagram{0xABU, 0xCDU, 0x1EU, 0x61U, 0U, 10U, 0U, 0U, 'o', 'k'};
    add_ipv4(udp, 17U, datagram);
    inject(mac, udp);
    network.poll(++now, h755::ethernet::LinkState::mb100_full);
    check(echo.udp_echoes() == 1U && octet(mac.last_tx, 34U) == 0x1EU &&
              octet(mac.last_tx, 35U) == 0x61U && octet(mac.last_tx, 42U) == 'o' &&
              octet(mac.last_tx, 43U) == 'k',
          "UDP echo returns the exact payload");
    auto oversized_udp = ethernet(address, host, 0x0800U);
    std::vector<std::uint8_t> oversized_datagram(521U, 0U);
    oversized_datagram[0U] = 0xABU;
    oversized_datagram[1U] = 0xCDU;
    oversized_datagram[2U] = 0x1EU;
    oversized_datagram[3U] = 0x61U;
    oversized_datagram[4U] = 0x02U;
    oversized_datagram[5U] = 0x09U;
    add_ipv4(oversized_udp, 17U, oversized_datagram);
    inject(mac, oversized_udp);
    network.poll(++now, h755::ethernet::LinkState::mb100_full);
    check(echo.rejected() == 1U && echo.udp_echoes() == 1U,
          "oversized UDP datagram is bounded and not echoed");

    constexpr std::uint32_t first_sequence = 0x12345678U;
    auto syn = ethernet(address, host, 0x0800U);
    const auto syn_segment = tcp_segment(first_sequence, 0U, 0x02U);
    add_ipv4(syn, 6U, syn_segment);
    inject(mac, syn);
    network.poll(++now, h755::ethernet::LinkState::mb100_full);
    check(octet(mac.last_tx, 23U) == 6U && (octet(mac.last_tx, 47U) & 0x12U) == 0x12U,
          "TCP SYN receives SYN ACK");
    const auto server_sequence = get32(mac.last_tx, 38U);
    auto ack = ethernet(address, host, 0x0800U);
    const auto ack_segment = tcp_segment(first_sequence + 1U, server_sequence + 1U, 0x10U);
    add_ipv4(ack, 6U, ack_segment);
    inject(mac, ack);
    network.poll(++now, h755::ethernet::LinkState::mb100_full);
    constexpr std::array<std::uint8_t, 3> payload{'a', 'b', 'c'};
    auto data = ethernet(address, host, 0x0800U);
    const auto data_segment =
        tcp_segment(first_sequence + 1U, server_sequence + 1U, 0x18U, payload);
    add_ipv4(data, 6U, data_segment);
    inject(mac, data);
    network.poll(++now, h755::ethernet::LinkState::mb100_full);
    check(echo.tcp_echoes() == 1U && octet(mac.last_tx, 54U) == 'a' &&
              octet(mac.last_tx, 55U) == 'b' && octet(mac.last_tx, 56U) == 'c',
          "TCP echo returns the exact payload");

    network.poll(++now, h755::ethernet::LinkState::down);
    check(!netif_is_link_up(&network.interface()), "link down is forwarded to lwIP");
    network.poll(++now, h755::ethernet::LinkState::mb100_full);
    check(netif_is_link_up(&network.interface()), "link recovery reaches lwIP without restart");
}
