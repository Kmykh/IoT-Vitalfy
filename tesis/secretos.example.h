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

// Clave MQTT del dispositivo (su usuario es CODIGO_DISPOSITIVO). Debe coincidir con la que
// tiene el broker en broker/.env (MQTT_DISPOSITIVOS). Vacía = broker sin autenticación.
static const char MQTT_CLAVE[] = "<clave del dispositivo>";
