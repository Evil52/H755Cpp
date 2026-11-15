#pragma once

#include <stdint.h>

#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_PLATFORM_DIAG(x)                                                                      \
    do                                                                                             \
    {                                                                                              \
    } while (0)
#define LWIP_PLATFORM_ASSERT(x) h755_lwip_assert(x)
#define LWIP_RAND() h755_lwip_random()

#ifdef __cplusplus
extern "C"
{
#endif
    __attribute__((noreturn)) void h755_lwip_assert(const char* message);
    uint32_t h755_lwip_random(void);
#ifdef __cplusplus
}
#endif
