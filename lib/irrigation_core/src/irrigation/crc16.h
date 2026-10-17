#pragma once

#include <stddef.h>
#include <stdint.h>

namespace irr {

/// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). Bitwise rather than
/// table-driven: the 512-byte table would cost a quarter of the Uno's SRAM
/// (or flash reads), and the config block is only ~60 bytes.
uint16_t crc16(const uint8_t* data, size_t len, uint16_t crc = 0xFFFF);

}  // namespace irr
