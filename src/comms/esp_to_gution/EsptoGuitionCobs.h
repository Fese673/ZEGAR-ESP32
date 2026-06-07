#pragma once
#include <stddef.h>
#include <stdint.h>
namespace EsptoGuition {

uint16_t crc16Ccitt(const uint8_t *data, size_t length);
size_t cobsEncode(const uint8_t *input, size_t length, uint8_t *output);
size_t cobsDecode(const uint8_t *input, size_t length, uint8_t *output);

} // namespace EsptoGuition
