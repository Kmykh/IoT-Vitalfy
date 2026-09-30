// telemetria.cpp — ver telemetria.h
#include "telemetria.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

size_t formatearIso8601(int64_t epochMs, char* buf, size_t tam) {
  if (epochMs <= 0) return 0;
  const time_t segundos = (time_t)(epochMs / 1000);
  struct tm t;
  if (gmtime_r(&segundos, &t) == nullptr) return 0;
  const int k = snprintf(buf, tam, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", t.tm_year + 1900, t.tm_mon + 1,
                         t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, (int)(epochMs % 1000));
  return (k > 0 && (size_t)k < tam) ? (size_t)k : 0;
}

size_t construirJsonTelemetria(const char* dispositivo, const Lectura& l, char* buf, size_t tam) {
  char ts[32];
  if (formatearIso8601(l.epochMs, ts, sizeof ts) == 0) return 0;

  size_t n = 0;
  const auto agregar = [&](int k) {
    if (k < 0 || n + (size_t)k >= tam) {
      n = tam;  // no cupo
    } else {
      n += (size_t)k;
    }
  };
  agregar(snprintf(buf, tam, "{\"dispositivo\":\"%s\",\"ts\":\"%s\",\"seq\":%lu", dispositivo, ts,
                   (unsigned long)l.seq));
  if (n < tam && l.fc >= 0) agregar(snprintf(buf + n, tam - n, ",\"fc\":%d", l.fc));
  if (n < tam && l.spo2 >= 0) agregar(snprintf(buf + n, tam - n, ",\"spo2\":%d", l.spo2));
  if (n < tam && l.tempDecimas != kSinTemperatura) {
    const int t = l.tempDecimas;
    agregar(snprintf(buf + n, tam - n, ",\"temp\":%s%d.%d", t < 0 ? "-" : "", abs(t) / 10, abs(t) % 10));
  }
  if (n < tam) agregar(snprintf(buf + n, tam - n, ",\"caida\":%s}", l.caida ? "true" : "false"));
  return n < tam ? n : 0;
}
