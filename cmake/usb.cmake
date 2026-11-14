set(H755_USB_VENDOR_ROOT "${PROJECT_SOURCE_DIR}/vendor")
include(${CMAKE_CURRENT_LIST_DIR}/verify_usb_vendor.cmake)

add_library(h755_async INTERFACE)
target_include_directories(h755_async SYSTEM INTERFACE
  ${PROJECT_SOURCE_DIR}/vendor/async/include
  ${PROJECT_SOURCE_DIR}/vendor/concurrency/include
  ${PROJECT_SOURCE_DIR}/vendor/stdx/include
  ${PROJECT_SOURCE_DIR}/vendor/mp11/include)
target_compile_definitions(h755_async INTERFACE SIMULATE_FREESTANDING=1)

option(H755_USB_CDC "Build isolated USB CDC candidate; does not flash" OFF)
if(NOT CMAKE_CROSSCOMPILING OR H755_USB_CDC)
  add_library(h755_tinyusb STATIC
    ${PROJECT_SOURCE_DIR}/vendor/tinyusb/src/tusb.c
    ${PROJECT_SOURCE_DIR}/vendor/tinyusb/src/common/tusb_fifo.c
    ${PROJECT_SOURCE_DIR}/vendor/tinyusb/src/device/usbd.c
    ${PROJECT_SOURCE_DIR}/vendor/tinyusb/src/class/cdc/cdc_device.c)
  target_include_directories(h755_tinyusb SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/vendor/tinyusb/src)
  target_include_directories(h755_tinyusb PUBLIC ${PROJECT_SOURCE_DIR}/src/usb)
  set_target_properties(h755_tinyusb PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
  add_library(h755_usb_core OBJECT src/usb/descriptors.cpp src/usb/tinyusb_port.cpp)
  h755_cpp26(h755_usb_core)
  target_include_directories(h755_usb_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
  target_link_libraries(h755_usb_core PUBLIC h755_tinyusb h755_async)
  if(CMAKE_CROSSCOMPILING)
    target_compile_definitions(h755_tinyusb PUBLIC CFG_TUSB_MCU=OPT_MCU_STM32H7 STM32H755xx CORE_CM7)
    target_include_directories(h755_tinyusb SYSTEM PRIVATE
      ${PROJECT_SOURCE_DIR}/vendor/cmsis-core/Include ${PROJECT_SOURCE_DIR}/vendor/cmsis-device-h7/Include)
    target_compile_options(h755_tinyusb PRIVATE -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffreestanding -ffunction-sections -fdata-sections)
    target_compile_options(h755_usb_core PRIVATE -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -ffreestanding -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -fstack-usage -fcontracts -fcontract-evaluation-semantic=enforce)
    target_compile_definitions(h755_usb_core PUBLIC _GLIBCXX_ASSERTIONS=1)
    target_sources(h755_tinyusb PRIVATE
      ${PROJECT_SOURCE_DIR}/vendor/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c
      ${PROJECT_SOURCE_DIR}/vendor/tinyusb/src/portable/synopsys/dwc2/dwc2_common.c)
  else()
    target_compile_definitions(h755_tinyusb PUBLIC CFG_TUSB_MCU=OPT_MCU_NONE TUP_DCD_ENDPOINT_MAX=9)
    target_compile_definitions(h755_usb_core PUBLIC _GLIBCXX_ASSERTIONS=1 _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE)
  endif()
endif()
