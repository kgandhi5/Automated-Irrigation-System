#include "storage.h"

#include <EEPROM.h>

namespace {
constexpr int kConfigAddr = 0;
}

namespace storage {

bool load(irr::Settings& s) {
  irr::ConfigImage img;
  EEPROM.get(kConfigAddr, img);
  return irr::openImage(img, s);
}

void save(const irr::Settings& s) {
  irr::ConfigImage img;
  irr::sealImage(img, s);
  EEPROM.put(kConfigAddr, img);
}

}  // namespace storage
