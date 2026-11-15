include(${CMAKE_CURRENT_LIST_DIR}/verify_lwip_vendor.cmake)

set(h755_lwip_sources
  src/ethernet/lwip_clock.c
  vendor/lwip/src/core/init.c
  vendor/lwip/src/core/def.c
  vendor/lwip/src/core/inet_chksum.c
  vendor/lwip/src/core/ip.c
  vendor/lwip/src/core/mem.c
  vendor/lwip/src/core/memp.c
  vendor/lwip/src/core/netif.c
  vendor/lwip/src/core/pbuf.c
  vendor/lwip/src/core/sys.c
  vendor/lwip/src/core/tcp.c
  vendor/lwip/src/core/tcp_in.c
  vendor/lwip/src/core/tcp_out.c
  vendor/lwip/src/core/timeouts.c
  vendor/lwip/src/core/udp.c
  vendor/lwip/src/core/ipv4/acd.c
  vendor/lwip/src/core/ipv4/dhcp.c
  vendor/lwip/src/core/ipv4/etharp.c
  vendor/lwip/src/core/ipv4/icmp.c
  vendor/lwip/src/core/ipv4/ip4.c
  vendor/lwip/src/core/ipv4/ip4_addr.c
  vendor/lwip/src/netif/ethernet.c)

add_library(h755_lwip STATIC ${h755_lwip_sources})
target_include_directories(h755_lwip SYSTEM PUBLIC
  ${PROJECT_SOURCE_DIR}/vendor/lwip/src/include)
target_include_directories(h755_lwip PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(h755_lwip PRIVATE c_std_11)
if(CMAKE_CROSSCOMPILING)
  target_compile_options(h755_lwip PRIVATE
    -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
    -ffreestanding -ffunction-sections -fdata-sections -fstack-usage)
endif()
