#!/bin/sh
# Corre dentro del contenedor: crea el archivo de claves desde las variables de .env y lanza
# Mosquitto. Las claves nunca se escriben en archivos versionados.
set -eu
: "${MQTT_BACKEND_USUARIO:?falta en broker/.env}"
: "${MQTT_BACKEND_CLAVE:?falta en broker/.env}"
: "${MQTT_DISPOSITIVOS:?falta en broker/.env}"

claves=/mosquitto/config/passwd
mosquitto_passwd -c -b "$claves" "$MQTT_BACKEND_USUARIO" "$MQTT_BACKEND_CLAVE"
for par in $MQTT_DISPOSITIVOS; do  # CODIGO:clave separados por espacios
  mosquitto_passwd -b "$claves" "${par%%:*}" "${par#*:}"
done
# Mosquitto exige que los archivos de seguridad no sean legibles por otros usuarios
cp /vitalify/acl /mosquitto/config/acl
chown mosquitto:mosquitto "$claves" /mosquitto/config/acl
chmod 0600 "$claves" /mosquitto/config/acl
echo "Usuarios MQTT: $MQTT_BACKEND_USUARIO y $(echo "$MQTT_DISPOSITIVOS" | wc -w | tr -d ' ') wearable(s)"

exec /usr/sbin/mosquitto -c /mosquitto/config/mosquitto.conf
