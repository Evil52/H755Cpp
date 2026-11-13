cmake_minimum_required(VERSION 3.30)
project(H755CppToolchainChecks LANGUAGES C CXX ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(h755_cpp26 target)
  set_target_properties(${target} PROPERTIES
    CXX_STANDARD 26
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO)
  target_compile_options(${target} PRIVATE
    -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow)
endfunction()

if(CMAKE_CROSSCOMPILING)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR
      NOT CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL "16.2.0")
    message(FATAL_ERROR "Target checks require the pinned Arm GCC 16.2.0")
  endif()
  add_library(cm7_cpp26_probe OBJECT tools/probes/arm_cpp26.cpp)
  h755_cpp26(cm7_cpp26_probe)
  target_compile_definitions(cm7_cpp26_probe PRIVATE _GLIBCXX_ASSERTIONS=1)
  target_compile_options(cm7_cpp26_probe PRIVATE
    -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
    -ffreestanding -fno-exceptions -fno-rtti
    -fcontracts -fcontract-evaluation-semantic=enforce)
  add_library(cm7_ethernet_mdio OBJECT src/ethernet/cmsis_mdio.cpp)
  h755_cpp26(cm7_ethernet_mdio)
  target_include_directories(cm7_ethernet_mdio PRIVATE include)
  target_include_directories(cm7_ethernet_mdio SYSTEM PRIVATE
    vendor/cmsis-core/Include vendor/cmsis-device-h7/Include)
  target_compile_definitions(cm7_ethernet_mdio PRIVATE
    STM32H755xx CORE_CM7 _GLIBCXX_ASSERTIONS=1)
  target_compile_options(cm7_ethernet_mdio PRIVATE
    -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
    -ffreestanding -fno-exceptions -fno-rtti -fstack-usage
    -fcontracts -fcontract-evaluation-semantic=enforce)
  message(STATUS "Toolchain checks only: no firmware image or hardware operation is configured")
else()
  include(CTest)
  option(H755_HOST_SANITIZERS "Run host checks with ASan and UBSan" ON)
  add_executable(host_cpp26_probe tools/probes/host_cpp26.cpp)
  h755_cpp26(host_cpp26_probe)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_definitions(host_cpp26_probe PRIVATE
      _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE)
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_definitions(host_cpp26_probe PRIVATE _GLIBCXX_ASSERTIONS=1)
  endif()
  if(H755_HOST_SANITIZERS)
    target_compile_options(host_cpp26_probe PRIVATE
      -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(host_cpp26_probe PRIVATE
      -fsanitize=address,undefined -fno-sanitize-recover=all)
  endif()
  add_test(NAME host_cpp26_library COMMAND host_cpp26_probe)
  add_executable(mdio_tests tests/mdio_tests.cpp)
  h755_cpp26(mdio_tests)
  target_include_directories(mdio_tests PRIVATE include)
  target_compile_definitions(mdio_tests PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE>
    $<$<CXX_COMPILER_ID:GNU>:_GLIBCXX_ASSERTIONS=1>)
  if(H755_HOST_SANITIZERS)
    target_compile_options(mdio_tests PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(mdio_tests PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all)
  endif()
  add_test(NAME mdio_unit COMMAND mdio_tests unit)
  set_tests_properties(mdio_unit PROPERTIES LABELS unit)
  add_test(NAME mdio_register_integration COMMAND mdio_tests integration)
  set_tests_properties(mdio_register_integration PROPERTIES LABELS integration)
endif()
