"""Genera modelos pre calculados de estrellas y planetas para el motor.

Cada modelo tiene dos partes:
  - un PNG con el sprite que se ve de lejos;
  - una fila en catalogo.tsv con la paleta que usa la esfera procedural de cerca.

Solo usa la biblioteca estandar, asi que funciona con cualquier Python 3.8 o posterior, sin
numpy, Pillow ni perlin_noise. Misma semilla, mismos modelos.

    python tools/generar_modelos.py
    python tools/generar_modelos.py --variantes 12 --tam 256 --semilla 7
"""

import argparse
import colorsys
import hashlib
import math
import multiprocessing
import os
import random
import struct
import sys
import zlib

# Proporcion del disco dentro de la imagen. Coincide con DemoGalaxy.h.
DISCO_ESTRELLA = 0.72
DISCO_PLANETA = 0.90

# Tipos de SurfaceKind en RenderTypes.h.
TERRESTRE, GAS, LAVA, ROCA, ESTRELLA = 0, 1, 2, 3, 4

# Temperatura tipica por clase. Las paletas de estrella salen del color de cuerpo negro.
ESTRELLAS = {
    "O": 38000, "B": 16000, "A": 8600, "F": 6600, "G": 5700, "K": 4500, "M": 3100,
    "RG": 4100, "RSG": 3700, "BSG": 20000, "WD": 14000, "NS": 300000,
}

# categoria: (tipo, color a, color b, color c, atmosfera con intensidad)
PLANETAS = {
    "lava": (LAVA, (0.09, 0.025, 0.015), (0.36, 0.10, 0.03), (1.0, 0.68, 0.08), (1.0, 0.32, 0.05, 0.35)),
    "rock": (ROCA, (0.22, 0.20, 0.19), (0.58, 0.54, 0.50), (0, 0, 0), (0, 0, 0, 0)),
    "desert": (ROCA, (0.42, 0.22, 0.11), (0.86, 0.62, 0.38), (0, 0, 0), (0.95, 0.65, 0.40, 0.30)),
    "terran": (TERRESTRE, (0.07, 0.24, 0.62), (0.13, 0.47, 0.17), (0.44, 0.42, 0.36), (0.35, 0.58, 1.0, 0.75)),
    "ice": (ROCA, (0.52, 0.62, 0.72), (0.92, 0.95, 0.98), (0, 0, 0), (0.70, 0.85, 1.0, 0.20)),
    "hotjupiter": (GAS, (0.20, 0.05, 0.03), (0.62, 0.22, 0.06), (0.95, 0.55, 0.18), (1.0, 0.45, 0.15, 0.70)),
    "gas": (GAS, (0.40, 0.25, 0.15), (0.80, 0.64, 0.46), (0.94, 0.88, 0.76), (0.90, 0.72, 0.52, 0.45)),
    "icegiant": (GAS, (0.20, 0.46, 0.64), (0.36, 0.66, 0.80), (0.62, 0.84, 0.90), (0.45, 0.78, 0.95, 0.60)),
    "moon": (ROCA, (0.20, 0.20, 0.22), (0.62, 0.61, 0.60), (0, 0, 0), (0, 0, 0, 0)),
    "comet": (ROCA, (0.10, 0.10, 0.11), (0.34, 0.33, 0.33), (0, 0, 0), (0, 0, 0, 0)),
}


def cuerpo_negro(temperatura):
    """Color RGB de 0 a 1. Mismo ajuste que BlackbodyColor en Astro.cpp."""
    t = min(max(temperatura, 1000.0), 40000.0) / 100.0
    r = 255.0 if t <= 66 else 329.698727446 * (t - 60) ** -0.1332047592
    g = 99.4708025861 * math.log(t) - 161.1195681661 if t <= 66 else 288.1221695283 * (t - 60) ** -0.0755148492
    if t >= 66:
        b = 255.0
    elif t <= 19:
        b = 0.0
    else:
        b = 138.5177312231 * math.log(t - 10) - 305.0447927307
    return tuple(min(max(c / 255.0, 0.0), 1.0) for c in (r, g, b))


class Ruido:
    """Ruido de valor 3D con tabla de permutacion propia."""

    def __init__(self, semilla):
        azar = random.Random(semilla)
        tabla = list(range(256))
        azar.shuffle(tabla)
        self.p = tabla * 2
        self.v = [azar.uniform(-1.0, 1.0) for _ in range(256)]

    def valor(self, x, y, z):
        xi, yi, zi = math.floor(x), math.floor(y), math.floor(z)
        fx, fy, fz = x - xi, y - yi, z - zi
        sx, sy, sz = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy), fz * fz * (3 - 2 * fz)
        xi &= 255
        yi &= 255
        zi &= 255
        p, v = self.p, self.v
        a, b = p[xi] + yi, p[xi + 1] + yi
        aa, ab, ba, bb = p[a] + zi, p[a + 1] + zi, p[b] + zi, p[b + 1] + zi
        x00 = v[p[aa]] + (v[p[ba]] - v[p[aa]]) * sx
        x10 = v[p[ab]] + (v[p[bb]] - v[p[ab]]) * sx
        x01 = v[p[aa + 1]] + (v[p[ba + 1]] - v[p[aa + 1]]) * sx
        x11 = v[p[ab + 1]] + (v[p[bb + 1]] - v[p[ab + 1]]) * sx
        y0 = x00 + (x10 - x00) * sy
        y1 = x01 + (x11 - x01) * sy
        return y0 + (y1 - y0) * sz

    def fbm(self, x, y, z, capas, ganancia=0.54):
        suma, amplitud = 0.0, 0.55
        for _ in range(capas):
            suma += amplitud * self.valor(x, y, z)
            # Giro entre capas: evita que las ondas queden alineadas con los ejes.
            x, y, z = 1.62 * y + 1.22 * z + 11.7, -1.62 * x + 0.73 * y - 0.97 * z + 7.3, -1.22 * x - 0.97 * y + 1.30 * z + 3.1
            amplitud *= ganancia
        return suma


def mezcla(a, b, t):
    return tuple(p + (q - p) * t for p, q in zip(a, b))


def paso(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3 - 2 * t)


def variar(color, azar, tono=0.025, saturacion=0.18, valor=0.15):
    """Desplaza un color sin sacarlo de su familia."""
    h, s, v = colorsys.rgb_to_hsv(*color)
    h = (h + azar.uniform(-tono, tono)) % 1.0
    s = min(max(s * (1 + azar.uniform(-saturacion, saturacion)), 0.0), 1.0)
    v = min(max(v * (1 + azar.uniform(-valor, valor)), 0.0), 1.0)
    return colorsys.hsv_to_rgb(h, s, v)


def paleta_planeta(categoria, azar):
    tipo, a, b, c, atmosfera = PLANETAS[categoria]
    # Las familias con mas margen real de color reciben mas variacion.
    tono = 0.04 if categoria in ("gas", "icegiant", "hotjupiter", "desert", "terran") else 0.02
    aire = variar(atmosfera[:3], azar, tono) if atmosfera[3] > 0 else (0, 0, 0)
    return tipo, variar(a, azar, tono), variar(b, azar, tono), variar(c, azar, tono), aire + (atmosfera[3],)


def paleta_estrella(categoria, azar):
    temperatura = ESTRELLAS[categoria] * azar.uniform(0.92, 1.08)
    c = cuerpo_negro(temperatura)
    mancha = tuple(x * 0.62 for x in cuerpo_negro(temperatura * 0.75))
    caliente = tuple(0.5 + 0.5 * x for x in c)
    return ESTRELLA, mancha, c, caliente, (0, 0, 0, 0)


def superficie(tipo, a, b, c, ruido, nx, ny, nz, polo):
    """Version simplificada de sphere.hlsl. Devuelve el color de un punto de la esfera."""
    x, y, z = nx * 3, ny * 3, nz * 3
    if tipo == TERRESTRE:
        giro = ruido.fbm(x + 40, y + 40, z + 40, 2)
        altura = ruido.fbm(x + giro * 0.8, y + giro * 0.8, z, 5)
        color = a
        color = mezcla(color, b, paso(0.015, 0.05, altura))
        color = mezcla(color, c, paso(0.24, 0.30, altura))
        color = mezcla(color, (0.92, 0.95, 0.98), paso(0.84, 0.88, abs(ny) + giro * 0.1))
        nubes = ruido.fbm(nx * 3.4 + 9, ny * 3.4, nz * 3.4 + 5, 4, 0.60)
        return mezcla(color, (0.93, 0.94, 0.96), paso(0.12, 0.30, nubes) * 0.85)
    if tipo == GAS:
        turbulencia = ruido.fbm(nx * 3, ny * 8, nz * 3, 4)
        latitud = ny + turbulencia * 0.07
        bandas = 0.5 + 0.5 * math.sin(latitud * 15 + polo)
        finas = 0.5 + 0.5 * math.sin(latitud * 41 + polo * 3)
        color = mezcla(a, b, paso(0.2, 0.8, bandas))
        color = mezcla(color, c, paso(0.55, 0.95, finas) * 0.55)
        return tuple(v * (0.92 + turbulencia * 0.35) for v in color)
    if tipo == LAVA:
        forma = ruido.fbm(x, y, z, 5)
        grietas = 1 - abs(ruido.fbm(x * 1.4 + 23, y * 1.4, z * 1.4, 4)) * 3.2
        color = mezcla(a, b, paso(-0.25, 0.3, forma))
        return mezcla(color, c, paso(0.55, 0.96, grietas))
    if tipo == ESTRELLA:
        celdas = ruido.fbm(nx * 16, ny * 16, nz * 16, 3)
        corrientes = ruido.fbm(x, y, z, 3)
        color = mezcla(b, c, min(max(0.42 + celdas * 0.95 + corrientes * 0.35, 0.0), 1.0))
        return mezcla(color, a, paso(0.34, 0.50, -corrientes) * 0.7)
    altura = ruido.fbm(x, y, z, 5)
    crestas = abs(ruido.fbm(x * 2.1 + 31, y * 2.1, z * 2.1, 4))
    return tuple(v * (0.72 + crestas * 1.1) for v in mezcla(a, b, paso(-0.35, 0.35, altura)))


def dibujar(tipo, a, b, c, atmosfera, tam, semilla):
    """Devuelve filas RGBA. Sin sombra pintada: de lejos, la luz puede venir de cualquier lado."""
    ruido = Ruido(semilla)
    azar = random.Random(semilla)
    # Orientacion al azar: dos modelos con la misma paleta muestran caras distintas.
    giro = azar.uniform(0, math.tau)
    cg, sg = math.cos(giro), math.sin(giro)
    polo = azar.uniform(0, 10)
    disco = DISCO_ESTRELLA if tipo == ESTRELLA else DISCO_PLANETA
    medio = (tam - 1) / 2

    filas = []
    for fila in range(tam):
        datos = bytearray(tam * 4)
        v = (fila - medio) / medio / disco
        for columna in range(tam):
            u = (columna - medio) / medio / disco
            r2 = u * u + v * v
            distancia = math.sqrt(r2)
            indice = columna * 4
            if r2 <= 1:
                w = math.sqrt(1 - r2)
                color = superficie(tipo, a, b, c, ruido, u * cg + w * sg, v, w * cg - u * sg, polo)
                borde = (0.70 + 0.42 * math.sqrt(w)) if tipo == ESTRELLA else (0.55 + 0.5 * w)
                color = [x * borde for x in color]
                if tipo != ESTRELLA and atmosfera[3] > 0:
                    color = [x + y * atmosfera[3] * (1 - w) ** 3 for x, y in zip(color, atmosfera)]
                alfa = min((1 - distancia) * tam * disco * 0.5 + 0.5, 1.0)
            elif tipo == ESTRELLA:
                # Corona: brillo que cae fuera del disco.
                color = b
                alfa = max(0.0, 1 - (distancia - 1) / (1 / disco - 1)) ** 3 * 0.75
            else:
                color, alfa = (0, 0, 0), 0.0
            for canal, valor in enumerate(color):
                datos[indice + canal] = int(min(max(valor, 0.0), 1.0) * 255 + 0.5)
            datos[indice + 3] = int(alfa * 255 + 0.5)
        filas.append(bytes(datos))
    return filas


def escribir_png(ruta, filas, tam):
    def bloque(tipo, datos):
        crc = zlib.crc32(tipo + datos) & 0xFFFFFFFF
        return struct.pack(">I", len(datos)) + tipo + datos + struct.pack(">I", crc)

    crudo = b"".join(b"\x00" + fila for fila in filas)
    with open(ruta, "wb") as archivo:
        archivo.write(b"\x89PNG\r\n\x1a\n")
        archivo.write(bloque(b"IHDR", struct.pack(">IIBBBBB", tam, tam, 8, 6, 0, 0, 0)))
        archivo.write(bloque(b"IDAT", zlib.compress(crudo, 9)))
        archivo.write(bloque(b"IEND", b""))


def semilla_de(texto):
    # hash() de Python cambia en cada ejecucion; sha256 no.
    return int.from_bytes(hashlib.sha256(texto.encode()).digest()[:8], "big")


def generar(trabajo):
    categoria, indice, carpeta, tam, semilla = trabajo
    nombre = f"{categoria}_{indice:02d}.png"
    azar = random.Random(semilla_de(f"{semilla}/{nombre}"))
    tipo, a, b, c, atmosfera = paleta_estrella(categoria, azar) if categoria in ESTRELLAS else paleta_planeta(categoria, azar)
    escribir_png(os.path.join(carpeta, nombre), dibujar(tipo, a, b, c, atmosfera, tam, azar.getrandbits(32)), tam)
    columnas = [f"{v:.4f}" for v in a + b + c + atmosfera]
    return "\t".join([categoria, nombre, str(tipo)] + columnas)


def main():
    raiz = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    opciones = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    opciones.add_argument("--salida", default=os.path.join(raiz, "assets", "modelos"))
    opciones.add_argument("--tam", type=int, default=128, help="pixeles por lado")
    opciones.add_argument("--estrellas", type=int, default=3, help="variantes por clase de estrella")
    opciones.add_argument("--variantes", type=int, default=6, help="variantes por clase de planeta")
    opciones.add_argument("--semilla", type=int, default=20261105)
    args = opciones.parse_args()

    os.makedirs(args.salida, exist_ok=True)
    trabajos = [(c, i, args.salida, args.tam, args.semilla) for c in ESTRELLAS for i in range(args.estrellas)]
    trabajos += [(c, i, args.salida, args.tam, args.semilla) for c in PLANETAS for i in range(args.variantes)]

    with multiprocessing.Pool() as hilos:
        filas = []
        for n, fila in enumerate(hilos.imap(generar, trabajos), 1):
            filas.append(fila)
            print(f"\r{n}/{len(trabajos)}", end="", flush=True)
    print()

    with open(os.path.join(args.salida, "catalogo.tsv"), "w", encoding="utf-8", newline="\n") as archivo:
        archivo.write("# categoria archivo tipo a.rgb b.rgb c.rgb atmosfera.rgba\n")
        archivo.write("\n".join(filas) + "\n")
    print(f"{len(filas)} modelos en {args.salida}")


if __name__ == "__main__":
    sys.exit(main())
