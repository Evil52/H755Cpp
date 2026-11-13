set(cm7_arch -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard)
set(device_includes
  ${PROJECT_SOURCE_DIR}/vendor/cmsis-core/Include
  ${PROJECT_SOURCE_DIR}/vendor/cmsis-device-h7/Include)
add_library(cm7_startup OBJECT
  ${PROJECT_SOURCE_DIR}/vendor/cmsis-device-h7/Source/Templates/gcc/startup_stm32h755xx.s
  ${PROJECT_SOURCE_DIR}/vendor/cmsis-device-h7/Source/Templates/system_stm32h7xx_dualcore_boot_cm4_cm7.c)
target_include_directories(cm7_startup SYSTEM PRIVATE ${device_includes})
target_compile_definitions(cm7_startup PRIVATE STM32H755xx CORE_CM7 USE_PWR_DIRECT_SMPS_SUPPLY)
target_compile_options(cm7_startup PRIVATE ${cm7_arch} -ffreestanding -ffunction-sections -fdata-sections)
add_executable(cm7_bringup cm7/main.cpp $<TARGET_OBJECTS:cm7_startup>)
set_target_properties(cm7_bringup PROPERTIES SUFFIX .elf)
h755_cpp26(cm7_bringup)
target_include_directories(cm7_bringup PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(cm7_bringup SYSTEM PRIVATE ${device_includes})
target_compile_definitions(cm7_bringup PRIVATE STM32H755xx CORE_CM7 _GLIBCXX_ASSERTIONS=1)
target_compile_options(cm7_bringup PRIVATE ${cm7_arch} -ffreestanding -fno-exceptions -fno-rtti
  -ffunction-sections -fdata-sections -fstack-usage -fcontracts -fcontract-evaluation-semantic=enforce)
set(cm7_linker ${PROJECT_SOURCE_DIR}/cmake/linker/cm7.ld)
set_property(TARGET cm7_bringup APPEND PROPERTY LINK_DEPENDS ${cm7_linker})
target_link_options(cm7_bringup PRIVATE ${cm7_arch} -nostartfiles --specs=nosys.specs
  -T${cm7_linker} -Wl,--gc-sections -Wl,--print-memory-usage -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/cm7_bringup.map
  -Wl,--wrap=_ZSt9terminatev -Wl,--wrap=_ZSt21__glibcxx_assert_failPKciS0_S0_)
add_executable(cm4_park cm4/park.s)
set_target_properties(cm4_park PROPERTIES SUFFIX .elf LINKER_LANGUAGE C)
target_compile_options(cm4_park PRIVATE -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=soft)
set(cm4_linker ${PROJECT_SOURCE_DIR}/cmake/linker/cm4-park.ld)
set_property(TARGET cm4_park APPEND PROPERTY LINK_DEPENDS ${cm4_linker})
# This assembly-only image links no M7 runtime or standard library.
target_link_options(cm4_park PRIVATE -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=soft -nostdlib -T${cm4_linker}
  -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/cm4_park.map)
foreach(image cm7_bringup cm4_park)
  add_custom_command(TARGET ${image} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${image}> ${CMAKE_CURRENT_BINARY_DIR}/${image}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${image}> ${CMAKE_CURRENT_BINARY_DIR}/${image}.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${image}>
    VERBATIM)
endforeach()

option(H755_TARGET_COVERAGE "Instrument isolated target test firmware; never for release" OFF)
if(H755_TARGET_COVERAGE)
  target_sources(cm7_bringup PRIVATE cm7/coverage.cpp)
  target_compile_definitions(cm7_bringup PRIVATE H755_TARGET_COVERAGE=1)
  target_compile_options(cm7_bringup PRIVATE -fprofile-arcs -ftest-coverage -fprofile-info-section)
  target_link_libraries(cm7_bringup PRIVATE gcov)
endif()
