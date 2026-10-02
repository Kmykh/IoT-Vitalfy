// sensores.h — Acceso al hardware: MAX30102, ADXL345, DS18B20 y AD8232.
// Los algoritmos están aparte (ppg, ecg, caidas); aquí solo se leen los datos crudos.
#pragma once

#include <Arduino.h>
#include <Wire.h>

// true si un dispositivo responde en esa dirección (Adafruit_SSD1306::begin devuelve true aunque
// la pantalla no esté conectada).
bool i2cPresente(TwoWire& bus, uint8_t direccion);
// Escribe en buf las direcciones que responden ("0x3C 0x53 0x57" o "ninguno"), para diagnóstico.
// Libera un bus I2C trabado: si el ESP32 se reinició a mitad de una transmisión, un esclavo (p. ej.
// la OLED) puede quedar sosteniendo SDA en bajo. Se llama antes de Wire.begin().
void i2cLiberarBus(uint8_t sda, uint8_t scl);
void i2cEscanear(TwoWire& bus, char* buf, size_t tam);

// ---------- MAX30102 (oxímetro, I2C 0x57) ----------
// Rojo + IR a 100 Hz (400 muestras/s promediadas de a 4), 18 bits.
bool ppgIniciar(TwoWire& bus, uint8_t corrienteLed, int rangoAdc);
// Lee el FIFO completo del sensor (hasta 32 muestras). El driver de SparkFun guarda solo 4 y
// perdía muestras cada vez que el loop se demoraba dibujando las pantallas.
// *perdidas = muestras que el sensor descartó por desborde desde la última lectura.
int ppgLeer(uint32_t* ir, uint32_t* rojo, int max, uint8_t* perdidas);

// ---------- ADXL345 (acelerómetro, I2C 0x53) ----------
// ±16 g en resolución completa (un golpe de caída supera los 2 g del modo anterior), 100 Hz,
// FIFO en modo stream: no se pierden muestras aunque el loop tarde.
bool acelIniciar(TwoWire& bus);
int acelLeer(float* ax, float* ay, float* az, int max);  // en g

// ---------- DS18B20 (temperatura, 1-Wire) ----------
// Conversión de 12 bits (0,0625 °C) sin bloquear: una lectura cada ~2 s, mediana de las 3 últimas.
void tempIniciar(uint8_t pin);
void tempActualizar(uint32_t ahoraMs);
bool tempPresente();
bool tempValida();
float tempCelsius();
uint32_t tempLecturas();  // cuántas lecturas válidas van (cambia con cada lectura nueva)

// ---------- AD8232 (ECG) ----------
// Una tarea de alta prioridad muestrea a 250 Hz exactos (el loop ya no deja huecos en la onda).
struct MuestraEcg {
  uint16_t adc;     // 0..4095
  bool conectados;  // electrodos puestos (LO+ y LO- en bajo, con antirrebote)
};
void ecgIniciar(uint8_t pinSalida, uint8_t pinLoMas, uint8_t pinLoMenos);
int ecgLeer(MuestraEcg* destino, int max);
// true si el módulo AD8232 está conectado al ESP32 (aunque no haya electrodos en el paciente)
bool ecgModuloPresente();
