# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Chircoric
"""Genera las dos fuentes de estilo holograma de Epsilon con FontForge:

* **Epsilon Holo**: la letra de Epsilon Basics cortada en franjas horizontales, con un hueco fino
  entre franja y franja y algunas franjas desplazadas, como una proyección que parpadea. Se lee
  igual que Basics; es la que usa el juego para los contactos alienígenas y los avisos.
* **Epsilon Xeno**: la cara del juego (el logotipo). Mayúsculas anchas y muy espaciadas, con
  una barra suelta bajo cada letra que hace de «escritura» ajena, y el mismo corte holográfico,
  más marcado. Las minúsculas dibujan las mismas formas que las mayúsculas.

Ambas salen del esqueleto de tools/fuente_epsilon.py, así que comparten los chaflanes angulares.

Uso (con el Python que trae FontForge):
    "C:\\Program Files\\FontForgeBuilds\\bin\\ffpython.exe" tools/fuentes_holograma.py ^
        [--carpeta game/assets/fonts] [--muestra carpeta_de_pngs]
"""
import argparse
import os
import sys

import fontforge

import fuente_epsilon as eb

# --- El corte holográfico --------------------------------------------------------------------------
Y_INICIO = -260        # las franjas empiezan aquí y suben hasta Y_FIN, iguales en todas las letras (barrido)
Y_FIN = 1000


def azar(*valores):
    """Número entre 0 y 1 que depende solo de los valores dados (no cambia de una ejecución a otra)."""
    h = 2166136261
    for v in valores:
        h = ((h ^ (int(v) & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF
        h ^= h >> 13
        h = (h * 0x5BD1E995) & 0xFFFFFFFF
        h ^= h >> 15
    return (h & 0xFFFFFF) / float(1 << 24)


def franjas(alto_min, alto_max):
    """Franjas de altura irregular que cubren de Y_INICIO a Y_FIN. Salen de una semilla fija: las dos
    fuentes comparten la misma lista, así que se parecen. El juego anima el texto con franjas parecidas."""
    lista, y, k = [], Y_INICIO, 0
    while y < Y_FIN:
        alto = int(alto_min + (alto_max - alto_min) * azar(k, 77))
        lista.append((y, alto))
        y += alto
        k += 1
    return lista


def rect(x0, y0, x1, y1):
    return eb.contorno([(x0, y0), (x0, y1), (x1, y1), (x1, y0)], True)


def holograma(f, bandas, hueco, saltos, parpadeo=0.0, corte=0.0, sal=0, protegidas=""):
    """Corta cada letra en las franjas dadas y las desplaza en x.

    hueco = (mínimo, máximo) de lo que se quita al final de cada franja; saltos = desplazamientos
    posibles; parpadeo = probabilidad de que una franja desaparezca (queda transparente); corte =
    probabilidad de que una franja se parta en dos con un hueco vertical, cada mitad por su lado.
    """
    tmp = fontforge.font().createChar(65, "A")
    for g in f.glyphs():
        capa = g.foreground
        if len(capa) == 0:
            continue
        x0, y0, x1, y1 = g.boundingBox()
        codigo = g.unicode if g.unicode > 0 else 7
        nueva = fontforge.layer()
        for k, (ya, alto) in enumerate(bandas):
            if ya + alto < y0 or ya > y1:
                continue
            if chr(codigo) not in protegidas and azar(k, codigo, 1, sal) < parpadeo:
                continue
            tope = ya + alto - (hueco[0] + (hueco[1] - hueco[0]) * azar(k, 5, sal))
            if tope <= ya + 8:
                continue
            if azar(k, codigo, 2, sal) < corte:
                xc = x0 + (x1 - x0) * (0.25 + 0.5 * azar(k, codigo, 3, sal))
                lados = [(x0 - 500, xc), (xc + 50 + 40 * azar(k, codigo, 4, sal), x1 + 500)]
            else:
                lados = [(x0 - 500, x1 + 500)]
            for j, (xa, xb) in enumerate(lados):
                tmp.foreground = capa
                c2 = tmp.foreground
                c2 += rect(xa, ya, xb, tope)
                tmp.foreground = c2
                tmp.intersect()
                dx = saltos[int(azar(k, codigo, 6 + j, sal) * len(saltos))]
                for c in tmp.foreground:
                    if dx:
                        c.transform((1, 0, 0, 1, dx, 0))
                    nueva += c
        g.foreground = nueva
        if len(nueva):
            g.correctDirection()
            g.round()


# --- Epsilon Xeno: anchura y marcas ----------------------------------------------------------------
def ensanchar(sp, fx):
    """Estira el esqueleto de una letra en x (el trazo conserva su grosor)."""
    def pts(lista):
        return [(x * fx, y) for x, y in lista]

    def tr(paths):
        return [(pts(p), c) for p, c in paths]

    nuevo = {"ow": int(round(sp["ow"] * fx)), "clip": sp["clip"], "paths": tr(sp["paths"]),
             "parts": [(tr(ps), (c[0] * fx, c[1] * fx, c[2], c[3]), w) for ps, c, w in sp["parts"]]}
    return nuevo


# Barra suelta bajo cada letra: (inicio, largo) como fracción del ancho. Varía de letra en letra.
BARRAS = [(0.00, 0.55), (0.30, 0.70), (0.10, 0.40), (0.45, 0.55), (0.00, 0.80), (0.25, 0.45)]
Y_BARRA = -168
GRUESO_BARRA = 40


def con_barra(sp, indice):
    ini, largo = BARRAS[indice % len(BARRAS)]
    ow = sp["ow"]
    x0 = ini * ow + 20
    x1 = min(ow - 20, max(x0 + largo * ow, x0 + 150))
    partes = [([eb.P((x0, Y_BARRA), (x1, Y_BARRA))], (0, ow, Y_BARRA - 30, Y_BARRA + 30), GRUESO_BARRA)]
    nuevo = dict(sp)
    nuevo["parts"] = list(sp["parts"]) + partes
    return nuevo


def letras_xeno():
    base = eb.todas_las_letras()
    todas = {}
    for ch, sp in base.items():
        if ch == "dotlessi":
            continue
        if ch.isascii() and ch.isalpha():
            mayus = ch.upper()
            todas[ch] = con_barra(ensanchar(base[mayus], 1.6), ord(mayus) - 65)
        elif ch != " ":
            todas[ch] = ensanchar(sp, 1.6)
    # La I lleva remates arriba y abajo: una raya sola se confundiría con «!».
    h = eb.H
    i = con_barra(eb.spec(300, eb.CAP, [eb.P((h, eb.YC), (258, eb.YC)), eb.P((150, eb.YC), (150, eb.YB)),
                                        eb.P((h, eb.YB), (258, eb.YB))]), 8)
    for ch in ("I", "i", "dotlessi"):
        todas[ch] = i
    todas[" "] = eb.spec(420, eb.PUN, [])
    return todas


# --- Construcción ---------------------------------------------------------------------------------
def construir_holo(ruta):
    f = eb.crear_fuente("Epsilon Holo")
    eb.rellenar(f, eb.todas_las_letras())
    # Se lee casi igual que Basics: pocos cortes y franjas que se pierden de tarde en tarde.
    holograma(f, franjas(70, 150), hueco=(14, 36), saltos=[0, 0, 0, -26, 0, 34, 0, -14, 0, 22],
              parpadeo=0.04, corte=0.12)
    f.generate(ruta)
    print("fuente:", ruta, os.path.getsize(ruta), "bytes")
    return f


def construir_xeno(ruta):
    margen = eb.SB
    eb.SB = 150                      # más aire entre letras
    try:
        f = eb.crear_fuente("Epsilon Xeno")
        eb.rellenar(f, letras_xeno())
    finally:
        eb.SB = margen
    # El logotipo es el caos: franjas más finas e irregulares, huecos grandes, trozos que se pierden
    # y letras partidas en dos mitades desplazadas.
    holograma(f, franjas(50, 170), hueco=(16, 50), saltos=[0, -70, 0, 56, 0, -34, 0, 84, 0, -48, 0, 40],
              parpadeo=0.07, corte=0.22, protegidas="EPSILON-Zepsilonz")
    f.generate(ruta)
    print("fuente:", ruta, os.path.getsize(ruta), "bytes")
    return f


def muestras(holo, xeno, carpeta):
    def hoja(font, lineas, ruta):
        ancho = 1900
        alto = sum(int(px * 1.7) + 12 for px, _ in lineas) + 30
        img = [[0.0] * ancho for _ in range(alto)]
        y = 20
        for px, texto in lineas:
            y += int(px * 1.25)
            eb.dibujar_linea(img, ancho, texto, font, px, y, 20)
            y += int(px * 0.45) + 12
        eb.escribir_png(ruta, img, ancho)
        print("muestra:", ruta, ancho, "x", alto)

    hoja(holo, [
        (88, "ABCDEFGHIJKLMNOPQRSTUVWXYZ"),
        (88, "abcdefghijklmnopqrstuvwxyz"),
        (88, "0123456789 .,;:!?()+-=/%"),
        (32, "FUEGO ENTRANTE: 3 A Kepler-452b. Contacto alienígena: Vh'Ssorn."),
        (16, "FUEGO ENTRANTE: 3  A Kepler-452b"),
    ], os.path.join(carpeta, "muestra_holo.png"))
    hoja(xeno, [
        (160, "EPSILON-Z"),
        (110, "ABCDEFGHIJKLM"),
        (110, "NOPQRSTUVWXYZ"),
        (110, "0123456789 -!?"),
        (64, "Nebulosa  Epsilon-Z  Ñandú"),
    ], os.path.join(carpeta, "muestra_xeno.png"))


def main():
    ap = argparse.ArgumentParser(description="Genera Epsilon Holo y Epsilon Xeno.")
    ap.add_argument("--carpeta", default=os.path.join("game", "assets", "fonts"))
    ap.add_argument("--muestra", help="carpeta donde dejar los PNG de muestra")
    a = ap.parse_args()
    holo = construir_holo(os.path.join(a.carpeta, "EpsilonHolo-Regular.ttf"))
    xeno = construir_xeno(os.path.join(a.carpeta, "EpsilonXeno-Regular.ttf"))
    if a.muestra:
        muestras(holo, xeno, a.muestra)
    return 0


if __name__ == "__main__":
    sys.exit(main())
