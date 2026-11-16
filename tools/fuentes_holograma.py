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
PERIODO = 120          # altura de cada franja con su hueco, en milésimas de em (igual en las dos fuentes)
Y_INICIO = -240        # las franjas se alinean con esta altura en todas las letras: se ve como un barrido


def rect(x0, y0, x1, y1):
    return eb.contorno([(x0, y0), (x0, y1), (x1, y1), (x1, y0)], True)


def holograma(f, hueco, saltos, parpadeo=0):
    """Corta cada letra en franjas de PERIODO - hueco de alto, desplaza cada franja en x según la tabla
    `saltos` (que se recorre según la franja y la letra) y quita una de cada `parpadeo`."""
    tmp = fontforge.font().createChar(65, "A")
    for g in f.glyphs():
        capa = g.foreground
        if len(capa) == 0:
            continue
        x0, y0, x1, y1 = g.boundingBox()
        codigo = g.unicode if g.unicode > 0 else 7
        k0 = int((y0 - Y_INICIO) // PERIODO)
        k1 = int((y1 - Y_INICIO) // PERIODO)
        sal = fontforge.layer()
        for k in range(k0, k1 + 1):
            if parpadeo and (k * 13 + codigo * 5) % parpadeo == 0:
                continue
            ya = Y_INICIO + k * PERIODO
            tmp.foreground = capa
            c2 = tmp.foreground
            c2 += rect(x0 - 500, ya, x1 + 500, ya + PERIODO - hueco)
            tmp.foreground = c2
            tmp.intersect()
            trozo = tmp.foreground
            dx = saltos[(k * 7 + codigo) % len(saltos)]
            for c in trozo:
                if dx:
                    c.transform((1, 0, 0, 1, dx, 0))
                sal += c
        g.foreground = sal
        if len(sal):
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
    holograma(f, hueco=22, saltos=[0, 0, 0, 0, -24, 0, 0, 0, 30, 0, 0, -14, 0, 0, 0, 20])
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
    holograma(f, hueco=30, saltos=[0, 0, 40, 0, -32, 0, 0, 52, 0, -22, 0, 0, 28, 0, 0, -44], parpadeo=17)
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
