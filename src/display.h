// LCD driver glue: wraps LiquidCrystal (parallel) or LiquidCrystal_I2C behind
// irr::CharDisplay and pushes frames through the dirty-cell mirror.
#pragma once

#include <irrigation_core.h>

namespace display {

void begin();
/// Sends only the cells that differ from what the panel shows.
void show(const irr::Frame& frame);

}  // namespace display
