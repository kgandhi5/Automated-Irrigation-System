// Allocation-free helpers for the serial console: tokenising a line in place
// and strict, range-checked number parsing.
#pragma once

#include <stdint.h>

namespace irr {

/// Splits `line` on spaces/tabs in place, lower-casing ASCII so commands are
/// case-insensitive. Returns the token count (at most `maxArgs`).
uint8_t tokenize(char* line, char** argv, uint8_t maxArgs);

/// Parses a plain decimal integer in [lo, hi]. Rejects signs, junk and overflow.
bool parseUint(const char* s, uint32_t lo, uint32_t hi, uint32_t* out);

/// "1".."4" -> 0..3, "*" or "all" -> kAllZones (see controller.h).
bool parseZone(const char* s, uint8_t* zone);

bool streq(const char* a, const char* b);

}  // namespace irr
