#include "tusb.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace
{
    // USB fields are serialized little-endian; these are development-only example identifiers.
    constexpr std::array<std::uint8_t, 18> device{18,
                                                  TUSB_DESC_DEVICE,
                                                  0x00,
                                                  0x02,
                                                  TUSB_CLASS_MISC,
                                                  MISC_SUBCLASS_COMMON,
                                                  MISC_PROTOCOL_IAD,
                                                  64,
                                                  0xFE,
                                                  0xCA,
                                                  0x01,
                                                  0x40,
                                                  0x00,
                                                  0x01,
                                                  1,
                                                  2,
                                                  0,
                                                  1};
    constexpr std::uint16_t length = TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN;
    const std::array<std::uint8_t, length> configuration{
        TUD_CONFIG_DESCRIPTOR(1, 2, 0, length, TUSB_DESC_CONFIG_ATT_SELF_POWERED, 2),
        TUD_CDC_DESCRIPTOR(0, 0, 0x81, 8, 0x02, 0x82, 64)};
    std::array<std::uint16_t, 32> string_buffer{};
}
extern "C" const std::uint8_t* tud_descriptor_device_cb()
{
    return device.data();
}
extern "C" const std::uint8_t* tud_descriptor_configuration_cb(std::uint8_t index)
{
    return index == 0 ? configuration.data() : nullptr;
}
extern "C" const std::uint16_t* tud_descriptor_string_cb(std::uint8_t index, std::uint16_t language)
{
    if (index == 0)
    {
        string_buffer[0] = 0x0304;
        string_buffer[1] = 0x0409;
        return string_buffer.data();
    }
    if (language != 0 && language != 0x0409)
    {
        return nullptr;
    }
    std::string_view text;
    if (index == 1)
    {
        text = "H755Cpp";
    }
    else if (index == 2)
    {
        text = "H755 Async CDC";
    }
    else
    {
        return nullptr;
    }
    string_buffer[0] = static_cast<std::uint16_t>(0x0300U + 2U + 2U * text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        string_buffer[i + 1U] = static_cast<std::uint8_t>(text[i]);
    }
    return string_buffer.data();
}
