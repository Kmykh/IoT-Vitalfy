#!/bin/sh
# Muestra en vivo todo lo que publican los wearables (telemetría y estado), con el usuario del
# backend. Es exactamente lo que recibirá el adaptador MQTT del backend.
#   ./escuchar.sh              todos los dispositivos
#   ./escuchar.sh ESP32-001    uno solo
set -eu
cd "$(dirname "$0")"
. ./.env
dispositivo="${1:-+}"
exec docker exec -it vitalify-mosquitto mosquitto_sub -u "$MQTT_BACKEND_USUARIO" -P "$MQTT_BACKEND_CLAVE" \
  -t "device/$dispositivo/telemetria" -t "device/$dispositivo/estado" -v
