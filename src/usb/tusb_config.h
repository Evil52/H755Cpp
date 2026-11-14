#pragma once
#include <stdint.h>
#define CFG_TUSB_OS OPT_OS_NONE
#define CFG_TUSB_DEBUG 0
#define CFG_TUD_ENABLED 1
#define CFG_TUD_VBUS_DETECT_HW 1
#define CFG_TUH_ENABLED 0
#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_CDC 1
#define CFG_TUD_CDC_NOTIFY 1
#define CFG_TUD_CDC_RX_BUFSIZE 128
#define CFG_TUD_CDC_TX_BUFSIZE 128
#define CFG_TUD_CDC_RX_EPSIZE 64
#define CFG_TUD_CDC_TX_EPSIZE 64
#define CFG_TUD_TASK_QUEUE_SZ 16
#define CFG_TUD_TASK_EVENTS_PER_RUN 4
#define CFG_TUD_DWC2_DMA_ENABLE 0
#define CFG_TUD_DWC2_SLAVE_ENABLE 1
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#define CFG_TUSB_DEBUG_BREAKPOINT h755_usb_stack_fault
#ifdef __cplusplus
extern "C" {
#endif
void h755_usb_stack_fault(void);
void h755_usb_wait(volatile uint32_t* reg, uint32_t mask, uint32_t expected);
#ifdef __cplusplus
}
#endif
