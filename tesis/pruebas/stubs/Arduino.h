// Arduino.h mínimo para compilar Adafruit_GFX en el computador (solo vista previa de pantallas).
#pragma once

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#define PROGMEM
#define radians(grados) ((grados) * M_PI / 180.0)
typedef bool boolean;
typedef uint8_t byte;

class __FlashStringHelper;

class String {
 public:
  String(const char* s = "") : s_(s) {}
  const char* c_str() const { return s_.c_str(); }
  unsigned int length() const { return (unsigned int)s_.size(); }

 private:
  std::string s_;
};

#include "Print.h"
