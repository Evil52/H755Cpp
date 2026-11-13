.syntax unified
.cpu cortex-m4
.thumb
.section .isr_vector,"a",%progbits
.word 0x38010000
.word Park_Reset_Handler
.rept 14
.word Park_Fault_Handler
.endr
.section .text.park,"ax",%progbits
.global Park_Reset_Handler
.type Park_Reset_Handler,%function
.thumb_func
Park_Reset_Handler:
  cpsid i
  ldr r0, =0x38000000
  ldr r1, =0x5041524b
  str r1, [r0]
  dsb
.global Park_Idle
.type Park_Idle,%function
.thumb_func
Park_Idle:
  wfe
  b Park_Idle
.size Park_Reset_Handler, .-Park_Reset_Handler
.type Park_Fault_Handler,%function
.thumb_func
Park_Fault_Handler:
  cpsid i
  b Park_Fault_Handler
.size Park_Fault_Handler, .-Park_Fault_Handler
