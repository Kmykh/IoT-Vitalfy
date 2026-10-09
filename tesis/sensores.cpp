// sensores.cpp — ver sensores.h
#include "sensores.h"

#include <DallasTemperature.h>
#include <MAX30105.h>
#include <OneWire.h>
#include <driver/gpio.h>

#include <atomic>

#include "filtros.h"

namespace {

// En el core 3.x de ESP32, endTransmission(false) deja el bus tomado hasta el requestFrom:
// por eso siempre se hacen juntos, aunque falle.
bool leerBloque(TwoWire& bus, uint8_t dir, uint8_t reg, uint8_t* datos, uint8_t n) {
  bus.beginTransmission(dir);
  bus.write(reg);
  bus.endTransmission(false);
  if (bus.requestFrom(dir, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) datos[i] = (uint8_t)bus.read();
  return true;
}

uint8_t leerRegistro(TwoWire& bus, uint8_t dir, uint8_t reg) {
  uint8_t v = 0;
  return leerBloque(bus, dir, reg, &v, 1) ? v : 0;
}

bool escribirRegistro(TwoWire& bus, uint8_t dir, uint8_t reg, uint8_t valor) {
  bus.beginTransmission(dir);
  bus.write(reg);
  bus.write(valor);
  return bus.endTransmission() == 0;
}

// ---------- MAX30102 ----------
MAX30105 max30102;
TwoWire* busPpg = nullptr;
const uint8_t kDirPpg = 0x57;
const uint8_t kFifoEscritura = 0x04, kFifoDesborde = 0x05, kFifoLectura = 0x06, kFifoDatos = 0x07;

// ---------- ADXL345 ----------
TwoWire* busAcel = nullptr;
const uint8_t kDirAcel = 0x53;
const uint8_t kDevId = 0x00, kBwRate = 0x2C, kPowerCtl = 0x2D, kDataFormat = 0x31, kDatos = 0x32, kFifoCtl = 0x38,
              kFifoStatus = 0x39;
const float kGPorBit = 0.0039f;  // 3,9 mg por bit en resolución completa

// ---------- DS18B20 ----------
DallasTemperature* ds18b20 = nullptr;
DeviceAddress direccionTemp;
enum class FaseTemp : uint8_t { Buscar, Solicitar, Convertir };
FaseTemp faseTemp = FaseTemp::Buscar;
bool tempEsta = false, tempOk = false, tempPrimeraBusqueda = true;
float celsius = 0;
uint32_t tFaseTemp = 0, tUltimaTemp = 0;
uint8_t fallosTemp = 0;
uint32_t lecturasTemp = 0;
Historial<3> ultimasTemps;

// ---------- AD8232 ----------
const int kAnilloEcg = 1024;  // 4 s a 250 Hz
MuestraEcg anilloEcg[kAnilloEcg];
std::atomic<uint32_t> escritasEcg{0};
uint32_t leidasEcg = 0;
uint8_t pinEcgSalida, pinEcgLoMas, pinEcgLoMenos;
std::atomic<bool> moduloEcg{false};

// En el ESP32 clásico, los GPIO 34 a 39 son solo entrada y no tienen resistencias internas: pedírselas
// no hace nada y ESP-IDF imprime un error en cada intento.
bool tienePull(uint8_t pin) { return GPIO_IS_VALID_OUTPUT_GPIO(pin); }

// ¿Hay un AD8232 manejando LO+ y LO-? Un módulo conectado fija su nivel (alto sin electrodos, bajo
// con electrodos) y no le afectan las resistencias internas del ESP32. Un pin al aire las sigue:
// si al cambiar de pull-down a pull-up cambia la lectura, no hay módulo (o falta un cable).
// Solo se prueban los pines con resistencias (en el DevKit, LO+; LO- está en GPIO35).
bool moduloResponde() {
  const bool mas = tienePull(pinEcgLoMas), menos = tienePull(pinEcgLoMenos);
  if (mas) pinMode(pinEcgLoMas, INPUT_PULLDOWN);
  if (menos) pinMode(pinEcgLoMenos, INPUT_PULLDOWN);
  delayMicroseconds(30);
  const int bajoMas = digitalRead(pinEcgLoMas), bajoMenos = digitalRead(pinEcgLoMenos);
  if (mas) pinMode(pinEcgLoMas, INPUT_PULLUP);
  if (menos) pinMode(pinEcgLoMenos, INPUT_PULLUP);
  delayMicroseconds(30);
  return (!mas || bajoMas == digitalRead(pinEcgLoMas)) && (!menos || bajoMenos == digitalRead(pinEcgLoMenos));
}

void tareaEcg(void*) {
  TickType_t ultimo = xTaskGetTickCount();
  uint8_t cuentaSuelto = 0, cuentaPuesto = 0;
  bool conectados = false;
  uint16_t ciclos = 249;
  bool anterior = false;
  for (;;) {
    vTaskDelayUntil(&ultimo, pdMS_TO_TICKS(4));  // 250 Hz exactos
    if (++ciclos == 250) {  // una vez por segundo; dos resultados iguales seguidos para cambiar
      ciclos = 0;
      const bool presente = moduloResponde();
      if (presente == anterior) moduloEcg = presente;
      anterior = presente;
    }
    const bool suelto = digitalRead(pinEcgLoMas) == HIGH || digitalRead(pinEcgLoMenos) == HIGH;
    if (suelto) {
      cuentaPuesto = 0;
      if (conectados && ++cuentaSuelto >= 13) conectados = false;  // 50 ms seguidos
    } else {
      cuentaSuelto = 0;
      if (!conectados && ++cuentaPuesto >= 63) conectados = true;  // 250 ms: el AD8232 se asienta
    }
    const uint32_t i = escritasEcg.load(std::memory_order_relaxed);
    anilloEcg[i % kAnilloEcg] = {(uint16_t)analogRead(pinEcgSalida), conectados};
    escritasEcg.store(i + 1, std::memory_order_release);
  }
}

}  // namespace

bool i2cPresente(TwoWire& bus, uint8_t direccion) {
  bus.beginTransmission(direccion);
  return bus.endTransmission() == 0;
}

bool i2cLineaEnAlto(uint8_t pin) {
  pinMode(pin, INPUT_PULLUP);
  delayMicroseconds(50);
  return digitalRead(pin) == HIGH;
}

void i2cLiberarBus(uint8_t sda, uint8_t scl) {
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, OUTPUT_OPEN_DRAIN);
  digitalWrite(scl, HIGH);
  for (int i = 0; i < 9 && digitalRead(sda) == LOW; i++) {  // hasta 9 pulsos: el esclavo suelta SDA
    digitalWrite(scl, LOW);
    delayMicroseconds(5);
    digitalWrite(scl, HIGH);
    delayMicroseconds(5);
  }
  pinMode(sda, OUTPUT_OPEN_DRAIN);  // condición de STOP: SDA sube con SCL en alto
  digitalWrite(sda, LOW);
  delayMicroseconds(5);
  digitalWrite(sda, HIGH);
  delayMicroseconds(5);
}

void i2cEscanear(TwoWire& bus, char* buf, size_t tam) {
  size_t n = 0;
  buf[0] = 0;
  for (uint8_t dir = 0x08; dir < 0x78; dir++) {
    if (!i2cPresente(bus, dir)) continue;
    const int k = snprintf(buf + n, tam - n, "%s0x%02X", n ? " " : "", dir);
    if (k < 0 || (size_t)k >= tam - n) break;
    n += (size_t)k;
  }
  if (n == 0) snprintf(buf, tam, "ninguno");
}

// ============================ MAX30102 ============================
bool ppgIniciar(TwoWire& bus, uint8_t corrienteLed, int rangoAdc) {
  busPpg = &bus;
  if (!max30102.begin(bus, 400000)) return false;
  // brillo, promedio de 4, modo rojo+IR, 400 muestras/s, pulso de 411 µs (18 bits), rango del ADC
  max30102.setup(corrienteLed, 4, 2, 400, 411, rangoAdc);
  return true;
}

int ppgLeer(uint32_t* ir, uint32_t* rojo, int max, uint8_t* perdidas) {
  TwoWire& bus = *busPpg;
  const uint8_t escritura = leerRegistro(bus, kDirPpg, kFifoEscritura);
  const uint8_t desborde = leerRegistro(bus, kDirPpg, kFifoDesborde);
  const uint8_t lectura = leerRegistro(bus, kDirPpg, kFifoLectura);
  int n = (escritura - lectura) & 0x1F;
  if (n == 0 && desborde > 0) n = 32;  // FIFO lleno: los punteros coinciden
  *perdidas = desborde;
  if (n > max) n = max;

  int leidas = 0;
  while (leidas < n) {
    uint8_t b[120];  // 20 muestras x 6 bytes: cabe en el búfer de Wire (128)
    const int bloque = (n - leidas) > 20 ? 20 : (n - leidas);
    if (!leerBloque(bus, kDirPpg, kFifoDatos, b, (uint8_t)(bloque * 6))) break;
    for (int i = 0; i < bloque; i++) {
      const uint8_t* m = b + i * 6;  // LED1 (rojo) y luego LED2 (IR), 3 bytes cada uno
      rojo[leidas] = (((uint32_t)m[0] << 16) | ((uint32_t)m[1] << 8) | m[2]) & 0x3FFFF;
      ir[leidas] = (((uint32_t)m[3] << 16) | ((uint32_t)m[4] << 8) | m[5]) & 0x3FFFF;
      leidas++;
    }
  }
  return leidas;
}

// ============================ ADXL345 ============================
bool acelIniciar(TwoWire& bus) {
  busAcel = &bus;
  if (leerRegistro(bus, kDirAcel, kDevId) != 0xE5) return false;
  return escribirRegistro(bus, kDirAcel, kPowerCtl, 0x00)      // reposo para configurar
         && escribirRegistro(bus, kDirAcel, kDataFormat, 0x0B)  // resolución completa, ±16 g
         && escribirRegistro(bus, kDirAcel, kBwRate, 0x0A)      // 100 Hz
         && escribirRegistro(bus, kDirAcel, kFifoCtl, 0x80)     // FIFO en modo stream
         && escribirRegistro(bus, kDirAcel, kPowerCtl, 0x08);   // a medir
}

int acelLeer(float* ax, float* ay, float* az, int max) {
  TwoWire& bus = *busAcel;
  int n = leerRegistro(bus, kDirAcel, kFifoStatus) & 0x3F;
  if (n > max) n = max;
  for (int i = 0; i < n; i++) {
    uint8_t b[6];  // X, Y, Z en una sola lectura: así el FIFO avanza una muestra completa
    if (!leerBloque(bus, kDirAcel, kDatos, b, 6)) return i;
    ax[i] = (int16_t)(b[1] << 8 | b[0]) * kGPorBit;
    ay[i] = (int16_t)(b[3] << 8 | b[2]) * kGPorBit;
    az[i] = (int16_t)(b[5] << 8 | b[4]) * kGPorBit;
  }
  return n;
}

// ============================ DS18B20 ============================
void tempIniciar(uint8_t pin) {
  static OneWire bus(pin);
  static DallasTemperature sensor(&bus);
  ds18b20 = &sensor;
  ds18b20->setWaitForConversion(false);
}

void tempActualizar(uint32_t ahora) {
  if (ds18b20 == nullptr) return;
  switch (faseTemp) {
    case FaseTemp::Buscar:  // sin sonda: se busca cada 3 s (se puede conectar en caliente)
      if (!tempPrimeraBusqueda && ahora - tFaseTemp < 3000) return;
      tempPrimeraBusqueda = false;
      tFaseTemp = ahora;
      ds18b20->begin();
      if (ds18b20->getAddress(direccionTemp, 0)) {
        ds18b20->setResolution(direccionTemp, 12);
        ds18b20->setWaitForConversion(false);
        tempEsta = true;
        fallosTemp = 0;
        faseTemp = FaseTemp::Solicitar;
      }
      break;

    case FaseTemp::Solicitar:
      if (ahora - tFaseTemp < 1000) return;
      ds18b20->requestTemperaturesByAddress(direccionTemp);
      tFaseTemp = ahora;
      faseTemp = FaseTemp::Convertir;
      break;

    case FaseTemp::Convertir: {
      if (ahora - tFaseTemp < 760) return;  // 12 bits: 750 ms de conversión
      const float t = ds18b20->getTempC(direccionTemp);
      tFaseTemp = ahora;
      faseTemp = FaseTemp::Solicitar;
      if (t == DEVICE_DISCONNECTED_C || t < -55 || t > 125) {
        if (++fallosTemp >= 3) {  // se desconectó
          tempEsta = tempOk = false;
          ultimasTemps.limpiar();
          faseTemp = FaseTemp::Buscar;
        }
        return;
      }
      if (t == 85.0f) return;  // valor de encendido del DS18B20: no hubo conversión
      fallosTemp = 0;
      ultimasTemps.agregar(t);
      celsius = ultimasTemps.medianaUltimos(3);
      lecturasTemp++;
      tempOk = true;
      tUltimaTemp = ahora;
      break;
    }
  }
  if (tempOk && ahora - tUltimaTemp > 10000) tempOk = false;
}

bool tempPresente() { return tempEsta; }
bool tempValida() { return tempOk; }
float tempCelsius() { return celsius; }
uint32_t tempLecturas() { return lecturasTemp; }

// ============================ AD8232 ============================
void ecgIniciar(uint8_t pinSalida, uint8_t pinLoMas, uint8_t pinLoMenos) {
  pinEcgSalida = pinSalida;
  pinEcgLoMas = pinLoMas;
  pinEcgLoMenos = pinLoMenos;
  // Pull-up: si se suelta el cable de LO+/LO-, se ve como "electrodos desconectados" y no como ruido.
  // Un pin sin resistencias internas (GPIO35 en el DevKit) necesita una de 10 kΩ externa a 3V3.
  pinMode(pinLoMas, tienePull(pinLoMas) ? INPUT_PULLUP : INPUT);
  pinMode(pinLoMenos, tienePull(pinLoMenos) ? INPUT_PULLUP : INPUT);
  pinMode(pinSalida, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(pinSalida, ADC_11db);  // 0–3,1 V: la salida del AD8232 reposa en ~1,65 V
  // Núcleo 1 con más prioridad que el loop: interrumpe unos µs cada 4 ms, aun mientras se dibuja
  xTaskCreatePinnedToCore(tareaEcg, "ecg", 3072, nullptr, 5, nullptr, 1);
}

bool ecgModuloPresente() { return moduloEcg; }

int ecgLeer(MuestraEcg* destino, int max) {
  const uint32_t escritas = escritasEcg.load(std::memory_order_acquire);
  if (escritas - leidasEcg > (uint32_t)(kAnilloEcg - 16)) leidasEcg = escritas - (kAnilloEcg - 16);  // atrasado
  int n = 0;
  while (leidasEcg != escritas && n < max) destino[n++] = anilloEcg[leidasEcg++ % kAnilloEcg];
  return n;
}
