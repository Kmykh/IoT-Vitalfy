#!/bin/sh
# Dibuja todas las pantallas de las OLED con el código real del firmware y arma un PNG
# (salida/pantallas_oled.png). Necesita python3 con Pillow.
#   ./vista_previa.sh
set -e
cd "$(dirname "$0")"

CXX="${CXX:-c++}"
if ! echo 'int main(){}' | "$CXX" -x c++ - -o /dev/null >/dev/null 2>&1; then
  # macOS con la licencia de Xcode sin aceptar: se usan las Command Line Tools directamente
  CLT=/Library/Developer/CommandLineTools
  if [ -x "$CLT/usr/bin/clang++" ]; then
    export DEVELOPER_DIR="$CLT" SDKROOT="$CLT/SDKs/MacOSX.sdk"
    CXX="$CLT/usr/bin/clang++"
  fi
fi

GFX=../../libraries/Adafruit_GFX_Library
mkdir -p salida
rm -f salida/*.pbm
"$CXX" -std=c++17 -O1 -DARDUINO=100 -Istubs -I"$GFX" -I.. \
  ../pantallas.cpp ../ecg.cpp ../ppg.cpp ../temperatura.cpp "$GFX/Adafruit_GFX.cpp" vista_previa.cpp -o salida/vista_previa
./salida/vista_previa
python3 vista_previa.py
