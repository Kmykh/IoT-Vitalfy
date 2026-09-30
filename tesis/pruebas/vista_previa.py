"""Arma pantallas_oled.png con todas las vistas generadas por vista_previa.cpp (salida/*.pbm)."""
import glob
import os

from PIL import Image, ImageDraw

ESCALA = 4
FONDO = (18, 18, 22)
PIXEL = (235, 245, 255)
MARGEN = 16

aqui = os.path.dirname(os.path.abspath(__file__))
archivos = sorted(glob.glob(os.path.join(aqui, "salida", "*.pbm")))
vistas = []
for ruta in archivos:
    img = Image.open(ruta).convert("1")
    w, h = img.size
    oled = Image.new("RGB", (w * ESCALA, h * ESCALA), (0, 0, 0))
    px = img.load()
    d = ImageDraw.Draw(oled)
    for y in range(h):
        for x in range(w):
            if px[x, y] == 0:  # en PBM 1 = negro; en la OLED ese bit es un píxel encendido
                d.rectangle([x * ESCALA, y * ESCALA, x * ESCALA + ESCALA - 2, y * ESCALA + ESCALA - 2], fill=PIXEL)
    vistas.append((os.path.splitext(os.path.basename(ruta))[0], oled))

columnas = 2
ancho = 128 * ESCALA
filas = (len(vistas) + columnas - 1) // columnas
alto_fila = 64 * ESCALA + 28
hoja = Image.new("RGB", (columnas * (ancho + MARGEN) + MARGEN, filas * alto_fila + MARGEN), FONDO)
d = ImageDraw.Draw(hoja)
for i, (nombre, oled) in enumerate(vistas):
    x = MARGEN + (i % columnas) * (ancho + MARGEN)
    y = MARGEN + (i // columnas) * alto_fila
    d.text((x, y), nombre, fill=(160, 170, 190))
    hoja.paste(oled, (x, y + 14))
salida = os.path.join(aqui, "salida", "pantallas_oled.png")
hoja.save(salida)
print(salida)
