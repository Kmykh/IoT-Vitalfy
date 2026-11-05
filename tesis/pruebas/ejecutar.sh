#!/bin/sh
# Compila y corre las pruebas de los algoritmos en el computador (no necesita el ESP32).
#   ./ejecutar.sh
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

CC="${CC:-$(dirname "$CXX")/clang}"
[ -x "$CC" ] || CC=cc
mkdir -p salida
"$CC" -std=c99 -O2 -Wall -c ../qrcodegen.c -o salida/qrcodegen.o  # generador de QR (C, de Nayuki)
"$CXX" -std=c++17 -O2 -Wall -Wextra -I.. \
  ../ppg.cpp ../ecg.cpp ../caidas.cpp ../respiracion.cpp ../telemetria.cpp ../temperatura.cpp ../sesion.cpp \
  ../vinculacion.cpp salida/qrcodegen.o pruebas.cpp -o salida/pruebas
./salida/pruebas
