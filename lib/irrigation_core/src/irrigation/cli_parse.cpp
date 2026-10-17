#include "cli_parse.h"

#include "controller.h"
#include "types.h"

namespace irr {

uint8_t tokenize(char* line, char** argv, uint8_t maxArgs) {
  uint8_t argc = 0;
  char* p = line;
  while (*p != '\0') {
    while (*p == ' ' || *p == '\t') {
      *p++ = '\0';
    }
    if (*p == '\0') {
      break;
    }
    if (argc == maxArgs) {
      break;
    }
    argv[argc++] = p;
    while (*p != '\0' && *p != ' ' && *p != '\t') {
      if (*p >= 'A' && *p <= 'Z') {
        *p = static_cast<char>(*p - 'A' + 'a');
      }
      ++p;
    }
  }
  return argc;
}

bool parseUint(const char* s, uint32_t lo, uint32_t hi, uint32_t* out) {
  if (s == nullptr || *s == '\0') {
    return false;
  }
  uint32_t v = 0;
  for (; *s != '\0'; ++s) {
    if (*s < '0' || *s > '9') {
      return false;
    }
    const uint8_t digit = static_cast<uint8_t>(*s - '0');
    if (v > (0xFFFFFFFFUL - digit) / 10UL) {
      return false;  // would overflow
    }
    v = v * 10UL + digit;
  }
  if (v < lo || v > hi) {
    return false;
  }
  *out = v;
  return true;
}

bool parseZone(const char* s, uint8_t* zone) {
  if (streq(s, "*") || streq(s, "all")) {
    *zone = kAllZones;
    return true;
  }
  uint32_t v = 0;
  if (!parseUint(s, 1, kZones, &v)) {
    return false;
  }
  *zone = static_cast<uint8_t>(v - 1);
  return true;
}

bool streq(const char* a, const char* b) {
  while (*a != '\0' && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}

}  // namespace irr
