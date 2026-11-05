// secretos.example.h — Plantilla. Cópiala como secretos.h (que no se versiona) y completa tus datos.
// La incluye red.cpp.
#pragma once

// Redes Wi-Fi conocidas (solo 2,4 GHz). El wearable escanea y se une a la de mejor señal;
// si se cae, busca de nuevo. broker: nullptr = MQTT_BROKER_POR_DEFECTO (config.h), o la IP del
// broker en esa red cuando la red no deja resolver nombres .local.
static const RedWifi REDES_WIFI[] = {
    {"<SSID de casa>", "<clave>", nullptr},
    {"<SSID del hotspot>", "<clave>", nullptr},
};

// Broker de producción (Vitalify/Backend/docs/despliegue.md). Con estas dos líneas el wearable se conecta
// al dominio del servidor por el 8883 con TLS (raíces de Let's Encrypt en raices_tls.h) y no a la laptop;
// el broker de cada red Wi-Fi deja de usarse. Coméntalas para volver al broker de desarrollo de config.h.
#define MQTT_BROKER_TLS "<dominio del servidor, el DOMINIO de su .env>"
#define MQTT_PUERTO_TLS 8883

// Clave MQTT del dispositivo (su usuario es CODIGO_DISPOSITIVO). Vacía = broker sin autenticación.
#ifdef MQTT_BROKER_TLS
// La que se cargó en el servidor con scripts/agregar-wearable-mqtt.sh (deploy/mosquitto/secretos/passwd).
static const char MQTT_CLAVE[] = "<clave del dispositivo en producción>";
#else
// La del broker de desarrollo, en broker/.env (MQTT_DISPOSITIVOS).
static const char MQTT_CLAVE[] = "<clave del dispositivo en desarrollo>";
#endif
