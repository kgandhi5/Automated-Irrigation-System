// Settings persistence in the ATmega328P's internal EEPROM.
#pragma once

#include <irrigation_core.h>

namespace storage {

/// Returns false (and leaves `s` untouched) if the stored image is missing,
/// from another firmware version, or fails its CRC.
bool load(irr::Settings& s);

/// EEPROM.put() only rewrites bytes that changed, so saving an unchanged
/// config costs no write cycles (each cell is rated for ~100k).
void save(const irr::Settings& s);

}  // namespace storage
