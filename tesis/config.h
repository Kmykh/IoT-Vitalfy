// config.h — Configuración del wearable Vitalify (ESP32-S3 Zero) y de la red por donde viajan
// los datos hasta el backend:
//
//   sensores ─> ESP32-S3 ──Wi-Fi──> broker Mosquitto ──MQTT──> backend Vitalify
//               device/<CODIGO_DISPOSITIVO>/telemetria  (QoS 1, JSON del contrato)
//
// Las claves (redes Wi-Fi y MQTT) van aparte, en secretos.h, que no se versiona.
#pragma once

#define FIRMWARE_VERSION "2.0.0"

// ============================ Dispositivo ============================
// Código con el que el administrador registró el sensor en Vitalify (POST /api/v1/dispositivos,
// patrón ^[A-Z0-9-]{3,32}$). Es también el usuario MQTT y el client id.
#define CODIGO_DISPOSITIVO "ESP32-001"

// ============================ Red: broker MQTT ============================
// Nombre mDNS de la laptop con Mosquitto: no cambia cuando el DHCP le da otra IP.
// Si una red bloquea mDNS, pon la IP de la laptop en esa red dentro de secretos.h.
#define MQTT_BROKER_POR_DEFECTO "MacBook-Air-de-Mayck.local"
#define MQTT_PUERTO 1883
#define MQTT_KEEPALIVE_S 15  // el broker publica "offline" si no sabe del wearable en ~22 s

// Tópicos (Vitalify/Backend/docs/contrato-telemetria.md)
#define TOPICO_TELEMETRIA "device/" CODIGO_DISPOSITIVO "/telemetria"  // lecturas, QoS 1
#define TOPICO_ESTADO "device/" CODIGO_DISPOSITIVO "/estado"          // "online"/"offline" retenido

// ============================ Telemetría ============================
#define INTERVALO_TELEMETRIA_MS 10000  // una lectura cada 10 s (frecuencia del contrato); una caída sale al instante
#define CAPACIDAD_BUFER_OFFLINE 360    // lecturas guardadas sin red: 1 h (el backend acepta hasta 24 h de atraso)

// ============================ Red: Wi-Fi y hora ============================
#define WIFI_POTENCIA_TX WIFI_POWER_11dBm  // menos pico de corriente en el riel 3V3 que comparten los sensores
#define NTP_SERVIDOR_1 "pool.ntp.org"      // la hora de cada lectura ("ts") sale del NTP
#define NTP_SERVIDOR_2 "time.google.com"
#define NTP_SERVIDOR_3 "time.cloudflare.com"
#define ZONA_HORARIA "<-05>5"              // Perú (UTC-5), solo para el reloj de la OLED: el JSON va en UTC

// ============================ Pines ============================
#if defined(CONFIG_IDF_TARGET_ESP32)
// ESP32 DevKit (ESP32-D0WD, USB por CH340). Los GPIO 6 a 11 son de la flash y los 34 a 39 solo entrada.
#define PIN_I2C_SDA 26      // bus 0: MAX30102 (0x57), ADXL345 (0x53), OLED principal 128x64 (0x3C)
#define PIN_I2C_SCL 33
#define PIN_I2C2_SDA 13     // bus 1: OLED del ECG 128x32 (también 0x3C: por eso va en otro bus)
#define PIN_I2C2_SCL 14     // no D12 (en alto al reiniciar, el ESP32 no arranca) ni D2 (su LED la deja en bajo)
#define PIN_DS18B20 25      // datos (amarillo) + resistencia de 4,7 kΩ a 3V3
#define PIN_ECG_SALIDA 34   // AD8232 OUTPUT (ADC1_CH6: funciona con el Wi-Fi encendido)
#define PIN_ECG_LO_MENOS 35 // AD8232 LO-
#define PIN_ECG_LO_MAS 32   // AD8232 LO+. SDN al aire o a 3V3: SDN a GND APAGA el AD8232
#define PIN_LED_OK 27       // LED verde (ánodo, con resistencia de 220–330 Ω; cátodo a GND): todo funciona
#else
// ESP32-S3 Zero
#define PIN_I2C_SDA 5       // bus 0: MAX30102 (0x57), ADXL345 (0x53), OLED principal 128x64 (0x3C)
#define PIN_I2C_SCL 6
#define PIN_I2C2_SDA 7      // bus 1: OLED del ECG 128x32 (0x3C)
#define PIN_I2C2_SCL 8
#define PIN_DS18B20 4       // datos (amarillo) + resistencia de 4,7 kΩ a 3V3
#define PIN_ECG_SALIDA 1    // AD8232 OUTPUT (ADC1: funciona con el Wi-Fi encendido)
#define PIN_ECG_LO_MENOS 2  // AD8232 LO-
#define PIN_ECG_LO_MAS 3    // AD8232 LO+. SDN al aire o a 3V3: SDN a GND APAGA el AD8232
#define PIN_LED_OK -1       // sin LED verde
#endif
#define PIN_BOTON 0         // botón BOOT de la placa: en el arranque, sigue aunque falte un sensor

// ============================ Sensores ============================
#define PPG_CORRIENTE_LED 60  // MAX30102: 0x3C ≈ 12 mA
#define PPG_RANGO_ADC 16384   // nA de fondo de escala. Con 4096 el dedo saturaba el ADC (IR = 262143)
// Detección de dedo (conteos IR). Con rango 16384 los niveles son ~1/4 de los medidos antes con 4096
// (cuarto oscuro ~1k -> ~300, teclado ~17k -> ~4k); un dedo queda muy por encima de DEDO_MINIMO.
#define DEDO_SALTO 3000       // subida mínima del IR sobre el ambiente
#define DEDO_MINIMO 8000      // IR mínimo con dedo
#define DEDO_SALIDA 5000      // bajo esto el dedo se da por retirado
// Calibración SpO2 = A·R² + B·R + C (curva de referencia de Maxim). Ajustar contra un oxímetro comercial.
#define SPO2_A -45.060f
#define SPO2_B 30.354f
#define SPO2_C 94.845f

// Temperatura (DS18B20, ver temperatura.h): solo se informa con la sonda sobre la piel y estable.
// Colóquela en la axila: en la mano o al aire marca 24–33 °C, que no es la temperatura corporal.
#define TEMP_MIN_CONTACTO 32.0f  // °C: por debajo, la sonda está al aire y no se envía
#define TEMP_CORRECCION_C 0.0f   // °C a sumar: diferencia medida contra un termómetro clínico en el mismo sitio

#define ECG_FRECUENCIA_RED_HZ 60  // interferencia de la red eléctrica que filtra el notch (Perú: 60 Hz)
