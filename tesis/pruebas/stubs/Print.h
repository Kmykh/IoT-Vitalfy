// Print.h mínimo (lo que usa Adafruit_GFX para escribir texto).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* b, size_t n) {
    size_t k = 0;
    while (n--) k += write(*b++);
    return k;
  }
  size_t write(const char* s) { return s ? write((const uint8_t*)s, strlen(s)) : 0; }
  size_t print(const char* s) { return write(s); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(int v) {
    char b[16];
    snprintf(b, sizeof b, "%d", v);
    return write(b);
  }
  size_t print(double v, int decimales = 2) {
    char b[32];
    snprintf(b, sizeof b, "%.*f", decimales, v);
    return write(b);
  }
};
