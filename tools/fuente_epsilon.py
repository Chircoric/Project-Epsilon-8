# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Chircoric
"""Genera la fuente angular de Epsilon («Epsilon Sans») con FontForge, sin dibujar nada a mano.

Uso (con el Python que trae FontForge):
    "C:\\Program Files\\FontForgeBuilds\\bin\\ffpython.exe" tools/fuente_epsilon.py ^
        --salida game/assets/fonts/EpsilonSans-Regular.ttf --muestra muestra.png

Cada letra se define como unas pocas líneas rectas (el «esqueleto») sobre una cuadrícula en
milésimas de em. El script las engrosa con un trazo de 84 unidades, recorta lo que sobresale
(los vértices en punta quedan planos) y une todo en un contorno. Para cambiar el carácter de
la fuente basta con tocar las constantes de arriba o el esqueleto de una letra y volver a
ejecutarlo. Las diagonales de A, K, M, N, V, W, X, Y, Z, k, v, w, x, y, z se pasan de la línea
a propósito: el recorte las deja planas en la base y en la altura de las mayúsculas.

Con --muestra escribe un PNG con la fuente dibujada (sin librerías externas).
"""
import argparse
import os
import struct
import sys
import unicodedata
import zlib

import fontforge

# --- Medidas, en milésimas de em --------------------------------------------------------------
S = 84                       # grosor del trazo
H = S // 2
SB = 60                      # margen lateral de cada letra
YB, YX, YM = 42, 478, 260    # eje del trazo de abajo, del de la altura de x y de la barra de la e
YC, YMC, YA, YD = 678, 372, 718, -158   # eje del trazo de arriba (mayúsculas), de en medio, de las
                                         # ascendentes y de las descendentes
K = 104                      # chaflán de las minúsculas
KC = 120                     # chaflán de las mayúsculas

LOW, ASC, DESC, ASD = (0, 520), (0, 760), (-200, 520), (-200, 760)
CAP, PUN = (0, 720), (-200, 800)
ASCENDER, DESCENDER = 950, 250   # métricas de línea: ImGui escala la fuente para que ASCENDER+DESCENDER midan su tamaño


def P(*pts):
    """Línea abierta."""
    return (list(pts), False)


def Z(*pts):
    """Línea cerrada."""
    return (list(pts), True)


def D(xc, yc):
    """Punto: un cuadrado relleno de 84 por 92 (el trazo de FontForge falla con líneas tan cortas)."""
    return ([(xc - H, yc - 46), (xc - H, yc + 46), (xc + H, yc + 46), (xc + H, yc - 46)], "fill")


def octo(x0, y0, x1, y1, k):
    """Octógono: un rectángulo con las cuatro esquinas cortadas."""
    return Z((x0 + k, y0), (x0, y0 + k), (x0, y1 - k), (x0 + k, y1),
             (x1 - k, y1), (x1, y1 - k), (x1, y0 + k), (x1 - k, y0))


def spec(ow, clip, paths, parts=()):
    """Una letra: ancho exterior, recorte vertical, líneas normales y partes con recorte propio.

    Cada parte es (líneas, (x0, x1, y0, y1), grosor o None).
    """
    return {"ow": ow, "clip": clip, "paths": list(paths), "parts": list(parts)}


# --- El esqueleto de cada letra ------------------------------------------------------------------
def minusculas():
    g = {}
    W = 450
    x1 = W - H

    def bowl_l(x1=x1, xl=H):
        return P((x1, YX), (xl + K, YX), (xl, YX - K), (xl, YB + K), (xl + K, YB), (x1, YB))

    def bowl_r(xr=x1):
        return P((H, YX), (xr - K, YX), (xr, YX - K), (xr, YB + K), (xr - K, YB), (H, YB))

    g["a"] = spec(W, LOW, [bowl_l(), P((x1, YX), (x1, YB))])
    g["b"] = spec(W, ASC, [P((H, YA), (H, YB)), bowl_r()])
    g["c"] = spec(400, LOW, [bowl_l(358)])
    g["d"] = spec(W, ASC, [P((x1, YA), (x1, YB)), bowl_l()])
    g["e"] = spec(W, LOW, [P((H, YM), (x1, YM), (x1, YX - K), (x1 - K, YX), (H + K, YX), (H, YX - K),
                              (H, YB + K), (H + K, YB), (x1, YB))])
    g["f"] = spec(320, ASC, [P((118, YB), (118, YA - K), (118 + K, YA), (278, YA)), P((H, YX), (278, YX))])
    g["g"] = spec(W, DESC, [bowl_l(), P((x1, YX), (x1, YD + K), (x1 - K, YD), (H + K, YD))])
    g["h"] = spec(430, ASC, [P((H, YA), (H, YB)), P((H, YX), (388 - K, YX), (388, YX - K), (388, YB))])
    g["dotlessi"] = spec(84, LOW, [P((H, YX), (H, YB))])
    g["i"] = spec(84, ASC, [P((H, YX), (H, YB)), D(H, 650)])
    g["j"] = spec(232, ASD, [P((190, YX), (190, YD + K), (190 - K, YD), (H, YD)), D(190, 650)])
    g["k"] = spec(440, ASC, [P((H, YA), (H, YB))],
                  [([P((112, 240), (430, 570)), P((200, 331), (430, -70))], (0, 440, 0, 520), None)])
    g["l"] = spec(240, ASC, [P((H, YA), (H, YB + K), (H + K, YB), (198, YB))])
    g["m"] = spec(700, LOW, [P((H, YB), (H, YX), (350 - K, YX), (350, YX - K), (350, YB)),
                              P((350, YX - K), (350 + K, YX), (658 - K, YX), (658, YX - K), (658, YB))])
    g["n"] = spec(430, LOW, [P((H, YB), (H, YX), (388 - K, YX), (388, YX - K), (388, YB))])
    g["o"] = spec(W, LOW, [octo(H, YB, x1, YX, K)])
    g["p"] = spec(W, DESC, [P((H, YX), (H, YD)), bowl_r()])
    g["q"] = spec(W, DESC, [P((x1, YX), (x1, YD)), bowl_l()])
    g["r"] = spec(330, LOW, [P((H, YB), (H, YX), (288 - K, YX), (288, YX - K))])
    g["s"] = spec(410, LOW, [P((368, YX), (H + 90, YX), (H, YX - 90), (H, YM + 50), (H + 50, YM), (318, YM),
                                (368, YM - 50), (368, YB + 90), (278, YB), (H, YB))])
    g["t"] = spec(310, ASC, [P((112, 640), (112, YB + K), (112 + K, YB), (268, YB)), P((H, YX), (268, YX))])
    g["u"] = spec(430, LOW, [P((H, YX), (H, YB + K), (H + K, YB), (388, YB)), P((388, YX), (388, YB))])
    g["v"] = spec(450, LOW, [], [([P((14, 590), (225, -40), (436, 590))], (0, 450, 0, 520), None)])
    g["w"] = spec(690, LOW, [], [([P((14, 590), (185, -40), (345, 640), (505, -40), (676, 590))],
                                  (0, 690, 0, 520), None)])
    g["x"] = spec(450, LOW, [], [([P((22, 580), (428, -40)), P((428, 580), (22, -40))], (0, 450, 0, 520), None)])
    g["y"] = spec(450, DESC, [], [([P((22, 580), (271, 230)), P((428, 580), (60, -240))],
                                   (0, 450, -200, 520), None)])
    g["z"] = spec(420, LOW, [P((H, YX), (378, YX), (H, YB), (378, YB))])
    return g


def mayusculas():
    g = {}
    g["A"] = spec(560, CAP, [P((47, 0), (280, 800), (513, 0)), P((114, 230), (446, 230))])
    xu = 408
    g["B"] = spec(480, CAP, [P((H, YC), (H, YB)),
                              P((H, YC), (xu - 90, YC), (xu, YC - 90), (xu, YMC + 90), (xu - 90, YMC), (H, YMC)),
                              P((H, YMC), (438 - 90, YMC), (438, YMC - 90), (438, YB + 90), (438 - 90, YB), (H, YB))])
    g["C"] = spec(500, CAP, [P((458, YC), (H + KC, YC), (H, YC - KC), (H, YB + KC), (H + KC, YB), (458, YB))])
    g["D"] = spec(500, CAP, [P((H, YC), (H, YB)),
                              P((H, YC), (458 - KC, YC), (458, YC - KC), (458, YB + KC), (458 - KC, YB), (H, YB))])
    g["E"] = spec(420, CAP, [P((378, YC), (H, YC), (H, YB), (378, YB)), P((H, YMC), (338, YMC))])
    g["F"] = spec(420, CAP, [P((378, YC), (H, YC), (H, YB)), P((H, YMC), (338, YMC))])
    g["G"] = spec(520, CAP, [P((478, YC), (H + KC, YC), (H, YC - KC), (H, YB + KC), (H + KC, YB), (478, YB),
                                (478, YMC), (308, YMC))])
    g["H"] = spec(500, CAP, [P((H, YC), (H, YB)), P((458, YC), (458, YB)), P((H, YMC), (458, YMC))])
    g["I"] = spec(84, CAP, [P((H, YC), (H, YB))])
    g["J"] = spec(380, CAP, [P((338, YC), (338, YB + 100), (238, YB), (H + 100, YB), (H, YB + 100))])
    g["K"] = spec(500, CAP, [P((H, YC), (H, YB))],
                  [([P((112, 330), (470, 780)), P((215, 470), (470, -80))], (0, 500, 0, 720), None)])
    g["L"] = spec(420, CAP, [P((H, YC), (H, YB), (378, YB))])
    g["M"] = spec(640, CAP, [P((H, YB), (H, YC)), P((598, YB), (598, YC))],
                  [([P((60, 780), (320, 150), (580, 780))], (0, 640, 200, 720), None)])
    g["N"] = spec(520, CAP, [P((H, YB), (H, YC)), P((478, YB), (478, YC))],
                  [([P((42, 760), (478, -40))], (0, 520, 0, 720), None)])
    g["O"] = spec(540, CAP, [octo(H, YB, 498, YC, 130)])
    g["P"] = spec(460, CAP, [P((H, YC), (H, YB)),
                              P((H, YC), (318, YC), (418, YC - 100), (418, 430), (318, 330), (H, 330))])
    g["Q"] = spec(560, CAP, [octo(H, YB, 518, YC, 130)], [([P((380, 220), (540, -80))], (0, 560, -100, 720), None)])
    g["R"] = spec(480, CAP, [P((H, YC), (H, YB)),
                              P((H, YC), (338, YC), (438, YC - 100), (438, 430), (338, 330), (H, 330))],
                  [([P((230, 340), (450, -80))], (0, 480, 0, 720), None)])
    g["S"] = spec(460, CAP, [P((418, YC), (H + KC, YC), (H, YC - KC), (H, YMC + 60), (H + 60, YMC), (358, YMC),
                                (418, YMC - 60), (418, YB + KC), (418 - KC, YB), (H, YB))])
    g["T"] = spec(480, CAP, [P((H, YC), (438, YC)), P((240, YC), (240, YB))])
    g["U"] = spec(500, CAP, [P((H, YC), (H, YB + KC), (H + KC, YB), (458 - KC, YB), (458, YB + KC), (458, YC))])
    g["V"] = spec(520, CAP, [], [([P((12, 840), (260, -40), (508, 840))], (0, 520, 0, 720), None)])
    g["W"] = spec(760, CAP, [], [([P((12, 840), (185, -40), (380, 780), (575, -40), (748, 840))],
                                  (0, 760, 0, 720), None)])
    g["X"] = spec(520, CAP, [], [([P((20, 800), (500, -80)), P((500, 800), (20, -80))], (0, 520, 0, 720), None)])
    g["Y"] = spec(520, CAP, [P((260, 340), (260, YB))],
                  [([P((12, 840), (260, 340), (508, 840))], (0, 520, 0, 720), None)])
    g["Z"] = spec(460, CAP, [P((H, YC), (418, YC), (H, YB), (418, YB))])
    return g


def cifras():
    g = {}
    g["0"] = spec(460, CAP, [octo(H, YB, 418, YC, 130)])
    g["1"] = spec(300, CAP, [P((210, YC), (210, YB)), P((210, YC), (100, 570))])
    g["2"] = spec(440, CAP, [P((H, YC - 100), (H + 100, YC), (298, YC), (398, YC - 100), (398, 300), (H, YB), (398, YB))])
    g["3"] = spec(440, CAP, [P((H, YC), (298, YC), (398, YC - 100), (398, YB + 100), (298, YB), (H, YB)),
                              P((152, YMC), (398, YMC))])
    g["4"] = spec(480, CAP, [P((330, YC), (330, YB))],
                  [([P((330, 760), (H, 260), (438, 260))], (0, 480, 0, 720), None)])
    g["5"] = spec(440, CAP, [P((398, YC), (H, YC), (H, 380)),
                              P((H, 380), (298, 380), (398, 280), (398, YB + 100), (298, YB), (H, YB))])
    g["6"] = spec(450, CAP, [P((408, YC), (H + 100, YC), (H, YC - 100), (H, YB + 100), (H + 100, YB), (308, YB),
                                (408, YB + 100), (408, 300), (308, 400), (H, 400))])
    g["7"] = spec(440, CAP, [P((H, YC), (398, YC), (130, -80))])
    g["8"] = spec(440, CAP, [octo(H + 14, YMC, 398 - 14, YC, 90), octo(H, YB, 398, YMC, 100)])
    g["9"] = spec(450, CAP, [P((H, YB), (308, YB), (408, YB + 100), (408, YC - 100), (308, YC), (H + 100, YC),
                                (H, YC - 100), (H, 420), (H + 100, 320), (408, 320))])
    return g


def signos():
    g = {}
    g[" "] = spec(250, PUN, [])
    g["!"] = spec(84, CAP, [P((H, YC), (H, 250)), D(H, 46)])
    g['"'] = spec(250, (0, 800), [P((H, 708), (H, 520)), P((208, 708), (208, 520))])
    g["'"] = spec(84, (0, 800), [P((H, 708), (H, 520))])
    g["#"] = spec(500, CAP, [P((165, 700), (165, 20)), P((335, 700), (335, 20)), P((H, 250), (458, 250)),
                              P((H, 450), (458, 450))])
    g["$"] = spec(450, (-60, 780), [P((408, YC), (H + KC, YC), (H, YC - KC), (H, YMC + 60), (H + 60, YMC), (348, YMC),
                                       (408, YMC - 60), (408, YB + KC), (408 - KC, YB), (H, YB)),
                                    P((225, 820), (225, -100))])
    g["%"] = spec(620, CAP, [octo(H, 478, 172, YC, 40), octo(448, YB, 578, 242, 40)],
                  [([P((75, -30), (545, 750))], (0, 620, 0, 720), None)])
    g["&"] = spec(560, CAP, [octo(150, YMC, 350, YC, 70), octo(H, YB, 400, YMC, 90)],
                  [([P((300, 300), (520, -60))], (0, 560, 0, 720), None)])
    g["("] = spec(230, (-200, 840), [P((188, 790), (H, 690), (H, 0), (188, -100))])
    g[")"] = spec(230, (-200, 840), [P((H, 790), (188, 690), (188, 0), (H, -100))])
    g["*"] = spec(300, (0, 780), [P((150, 690), (150, 430)), P((52, 640), (248, 480)), P((52, 480), (248, 640))])
    g["+"] = spec(460, PUN, [P((H, 330), (418, 330)), P((230, 518), (230, 142))])
    g[","] = spec(150, PUN, [P((100, 60), (100, -10), (40, -130))])
    g["-"] = spec(330, PUN, [P((H, 300), (288, 300))])
    g["."] = spec(84, PUN, [D(H, 46)])
    g["/"] = spec(340, (-100, 780), [], [([P((60, -120), (280, 800))], (0, 340, -100, 780), None)])
    g["\\"] = spec(340, (-100, 780), [], [([P((280, -120), (60, 800))], (0, 340, -100, 780), None)])
    g[":"] = spec(84, PUN, [D(H, 46), D(H, 432)])
    g[";"] = spec(150, PUN, [D(100, 432), P((100, 60), (100, -10), (40, -130))])
    g["<"] = spec(420, CAP, [], [([P((378, 560), (H, 300), (378, 40))], (0, 420, 0, 720), None)])
    g["="] = spec(430, PUN, [P((H, 210), (388, 210)), P((H, 400), (388, 400))])
    g[">"] = spec(420, CAP, [], [([P((H, 560), (378, 300), (H, 40))], (0, 420, 0, 720), None)])
    g["?"] = spec(420, CAP, [P((H, YC - 100), (H + 100, YC), (278, YC), (378, YC - 100), (378, 430), (210, 330),
                                (210, 240)), D(210, 46)])
    g["@"] = spec(720, CAP, [octo(H, YB, 678, YC, 150), octo(230, 196, 410, 484, 60)])
    g["["] = spec(200, (-200, 860), [P((158, 780), (H, 780), (H, -100), (158, -100))])
    g["]"] = spec(200, (-200, 860), [P((H, 780), (158, 780), (158, -100), (H, -100))])
    g["^"] = spec(440, (0, 780), [P((H, 380), (220, 700), (398, 380))])
    g["_"] = spec(500, PUN, [P((H, -110), (458, -110))])
    g["`"] = spec(200, (560, 780), [P((50, 730), (150, 600))])
    g["{"] = spec(240, (-200, 840), [P((198, 790), (140, 730), (140, 400), (H, 345), (140, 290), (140, -40),
                                       (198, -100))])
    g["}"] = spec(240, (-200, 840), [P((H, 790), (100, 730), (100, 400), (198, 345), (100, 290), (100, -40),
                                       (H, -100))])
    g["|"] = spec(84, (-200, 840), [P((H, 800), (H, -140))])
    g["~"] = spec(440, PUN, [P((H, 250), (150, 370), (290, 250), (398, 370))])
    # Latín-1 y tipografía que usa el juego.
    g["\u00b7"] = spec(84, PUN, [D(H, 302)])
    g["\u00b0"] = spec(190, (0, 800), [], [([octo(30, 540, 160, 670, 30)], (0, 190, 0, 800), 60)])
    g["\u00b1"] = spec(420, CAP, [P((H, 400), (378, 400)), P((210, 550), (210, 250)), P((H, 100), (378, 100))])
    g["\u00ab"] = spec(340, CAP, [], [([P((130, 480), (H, 300), (130, 120)), P((298, 480), (210, 300), (298, 120))],
                                       (0, 340, 0, 720), None)])
    g["\u00bb"] = spec(340, CAP, [], [([P((210, 480), (298, 300), (210, 120)), P((42, 480), (130, 300), (42, 120))],
                                       (0, 340, 0, 720), None)])
    g["\u00d7"] = spec(400, (130, 530), [], [([P((30, 95), (370, 565)), P((370, 95), (30, 565))],
                                              (0, 400, 130, 530), None)])
    g["\u00f7"] = spec(420, PUN, [P((H, 330), (378, 330)), D(210, 502), D(210, 162)])
    g["\u2212"] = spec(420, PUN, [P((H, 330), (378, 330))])
    g["\u2013"] = spec(520, PUN, [P((H, 300), (478, 300))])
    g["\u2014"] = spec(900, PUN, [P((H, 300), (858, 300))])
    g["\u2026"] = spec(500, PUN, [D(H, 46), D(250, 46), D(458, 46)])
    g["\u00a7"] = spec(420, (0, 800), [P((378, 700), (H + 70, 700), (H, 630), (H, 560), (H + 70, 490), (308, 330),
                                         (378, 260), (378, 190), (308, 120), (H, 120))])
    return g


# --- Acentos: vienen de los de Latin-1 (á, ñ, ü…) por descomposición Unicode ---------------------
def acento(marca, xc, dy):
    """Partes que añade una marca encima (o debajo, la cedilla) de una letra centrada en xc."""
    a = lambda y: y + dy
    if marca == "\u0301":    # agudo
        return [([P((xc - 80, a(526)), (xc + 80, a(734)))], (0, 2000, a(580), a(690)), None)]
    if marca == "\u0300":    # grave
        return [([P((xc + 80, a(526)), (xc - 80, a(734)))], (0, 2000, a(580), a(690)), None)]
    if marca == "\u0308":    # diéresis
        return [([D(xc - 68, a(632)), D(xc + 68, a(632))],
                 (0, 2000, -200, 2000), None)]
    if marca == "\u0302":    # circunflejo
        return [([P((xc - 100, a(540)), (xc, a(720)), (xc + 100, a(540)))], (0, 2000, a(580), a(710)), None)]
    if marca == "\u0303":    # tilde
        return [([P((xc - 100, a(590)), (xc - 34, a(670)), (xc + 34, a(590)), (xc + 100, a(670)))],
                 (0, 2000, -200, 2000), None)]
    if marca == "\u0327":    # cedilla
        return [([P((xc + 10, 30), (xc + 10, -98))], (0, 2000, -200, 2000), None)]
    return None


def componer(base, marca):
    ow = base["ow"]
    extra = acento(marca, ow / 2.0, 170 if base["clip"] == CAP else 0)
    if extra is None:
        return None
    # Los recortes de las marcas son para x relativa: se ajustan al ancho de la letra.
    partes = [(p, (-300, ow + 300, c[2], c[3]), w) for p, c, w in extra]
    nuevo = dict(base)
    nuevo["parts"] = list(base["parts"]) + partes
    nuevo["clip"] = (min(base["clip"][0], -200), max(base["clip"][1], 900))
    # El recorte vertical de las partes normales se sigue aplicando a la letra base:
    nuevo["base_clip"] = base["clip"]
    return nuevo


# --- Construcción con FontForge --------------------------------------------------------------------
def contorno(pts, cerrado, dx=0):
    c = fontforge.contour()
    c.moveTo(pts[0][0] + dx, pts[0][1])
    for p in pts[1:]:
        c.lineTo(p[0] + dx, p[1])
    c.closed = cerrado
    return c


def trazar(tmp, paths, clip, ancho):
    """Engrosa las líneas, junta lo que se solapa y recorta al rectángulo clip=(x0, x1, y0, y1)."""
    capa = fontforge.layer()
    for pts, cerrado in paths:
        capa += contorno(pts, cerrado, SB)
    tmp.foreground = capa
    tmp.stroke("circular", ancho, "square", "miter")
    tmp.removeOverlap()
    capa = tmp.foreground
    x0, x1, y0, y1 = clip
    capa += contorno([(x0, y0), (x0, y1), (x1, y1), (x1, y0)], True, SB)
    tmp.foreground = capa
    tmp.intersect()
    return tmp.foreground


def aportar(final, tmp, paths, clip, ancho):
    """Añade a la capa los puntos (rellenos tal cual) y las líneas (engrosadas y recortadas)."""
    for pts, cerrado in paths:
        if cerrado == "fill":
            final += contorno(pts, True, SB)
    trazos = [p for p in paths if p[1] != "fill"]
    if trazos:
        for c in trazar(tmp, trazos, clip, ancho):
            final += c


def construir_glifo(font, tmp, codigo, nombre, sp):
    ow = sp["ow"]
    final = fontforge.layer()
    clip_y = sp.get("base_clip", sp["clip"])
    aportar(final, tmp, sp["paths"], (0, ow, clip_y[0], clip_y[1]), S)
    for paths, clip, ancho in sp["parts"]:
        aportar(final, tmp, paths, clip, ancho or S)
    g = font.createChar(codigo, nombre) if codigo >= 0 else font.createChar(-1, nombre)
    g.foreground = final
    if len(final) > 0:
        g.removeOverlap()
        g.correctDirection()
        g.round()
    g.width = ow + 2 * SB
    return g


def construir(salida):
    f = fontforge.font()
    f.em = 1000
    f.ascent, f.descent = 800, 200
    f.fontname = "EpsilonSans-Regular"
    f.familyname = "Epsilon Sans"
    f.fullname = "Epsilon Sans Regular"
    f.weight = "Regular"
    f.version = "0.1"
    f.copyright = "Copyright (C) 2026 Chircoric"
    f.os2_weight = 400
    f.os2_fstype = 0
    # Por defecto FontForge toma estos valores como desplazamientos sobre ascent/descent: hay que
    # pedirle valores absolutos, o la línea sale mucho más alta y el texto, más pequeño.
    f.hhea_ascent_add = f.hhea_descent_add = False
    f.os2_typoascent_add = f.os2_typodescent_add = False
    f.os2_winascent_add = f.os2_windescent_add = False
    f.hhea_ascent, f.hhea_descent, f.hhea_linegap = ASCENDER, -DESCENDER, 0
    f.os2_winascent, f.os2_windescent = ASCENDER, DESCENDER
    f.os2_typoascent, f.os2_typodescent, f.os2_typolinegap = 800, -200, 200

    tmpfont = fontforge.font()
    tmp = tmpfont.createChar(65, "A")

    todas = {}
    for tabla in (minusculas(), mayusculas(), cifras(), signos()):
        todas.update(tabla)

    # .notdef: un marco para ver cuándo falta una letra.
    nd = spec(420, CAP, [Z((H, YB), (H, YC), (378, YC), (378, YB))])
    construir_glifo(f, tmp, -1, ".notdef", nd).width = 420 + 2 * SB

    n = 0
    for ch, sp in todas.items():
        if ch == "dotlessi":
            construir_glifo(f, tmp, 0x131, "dotlessi", sp)
        else:
            construir_glifo(f, tmp, ord(ch), fontforge.nameFromUnicode(ord(ch)), sp)
        n += 1

    # Letras con acento de Latin-1: se componen con la base y la marca.
    base_de = dict(todas)
    base_de["i"] = todas["dotlessi"]
    compuestas = 0
    for code in range(0xC0, 0x100):
        ch = chr(code)
        d = unicodedata.normalize("NFD", ch)
        if len(d) != 2 or d[0] not in base_de:
            continue
        sp = componer(base_de[d[0]], d[1])
        if sp is not None:
            construir_glifo(f, tmp, code, fontforge.nameFromUnicode(code), sp)
            compuestas += 1

    # ¡ y ¿ son ! y ? girados 180 grados.
    for origen, destino in (("!", "\u00a1"), ("?", "\u00bf")):
        g = construir_glifo(f, tmp, ord(destino), fontforge.nameFromUnicode(ord(destino)), todas[origen])
        g.transform((-1, 0, 0, -1, g.width, 520))
        g.correctDirection()

    print("glifos base:", n, "| con acento:", compuestas, "| total:", len(list(f.glyphs())))
    f.generate(salida)
    print("fuente:", salida, os.path.getsize(salida), "bytes")
    return f


# --- Muestra en PNG (sin librerías externas) ---------------------------------------------------------
def poligonos(g):
    out = []
    for c in g.foreground:
        pts = [(p.x, p.y) for p in c]
        if len(pts) >= 3:
            out.append(pts)
    return out


def dibujar_linea(img, ancho, texto, font, px, base_y, x_ini):
    esc = px / 1000.0
    x = x_ini
    for ch in texto:
        try:
            g = font[ord(ch)]
        except (TypeError, KeyError):
            x += 300 * esc
            continue
        polys = poligonos(g)
        if polys:
            ys = [p[1] for poly in polys for p in poly]
            top = int(base_y - max(ys) * esc) - 1
            bot = int(base_y - min(ys) * esc) + 2
            aristas = []
            for poly in polys:
                for i in range(len(poly)):
                    (xa, ya), (xb, yb) = poly[i], poly[(i + 1) % len(poly)]
                    if ya != yb:
                        aristas.append((xa, ya, xb, yb))
            for fila in range(max(top, 0), min(bot, len(img))):
                for sub in range(4):
                    yf = (base_y - (fila + (sub + 0.5) / 4.0)) / esc
                    cruces = []
                    for xa, ya, xb, yb in aristas:
                        if (ya <= yf < yb) or (yb <= yf < ya):
                            t = (yf - ya) / (yb - ya)
                            cruces.append((xa + t * (xb - xa), 1 if yb > ya else -1))
                    cruces.sort()
                    giro = 0
                    for i in range(len(cruces) - 1):
                        giro += cruces[i][1]
                        if giro != 0:
                            a = x + cruces[i][0] * esc
                            b = x + cruces[i + 1][0] * esc
                            for ix in range(max(int(a), 0), min(int(b) + 1, ancho)):
                                cub = min(b, ix + 1) - max(a, ix)
                                if cub > 0:
                                    img[fila][ix] += cub / 4.0
        x += g.width * esc


def escribir_png(ruta, img, ancho):
    crudo = bytearray()
    for fila in img:
        crudo.append(0)
        crudo.extend(int(255 - 255 * min(1.0, v)) for v in fila)

    def trozo(tipo, datos):
        c = struct.pack(">I", len(datos)) + tipo + datos
        return c + struct.pack(">I", zlib.crc32(tipo + datos) & 0xFFFFFFFF)

    with open(ruta, "wb") as a:
        a.write(b"\x89PNG\r\n\x1a\n")
        a.write(trozo(b"IHDR", struct.pack(">IIBBBBB", ancho, len(img), 8, 0, 0, 0, 0)))
        a.write(trozo(b"IDAT", zlib.compress(bytes(crudo), 6)))
        a.write(trozo(b"IEND", b""))


def muestra(font, ruta):
    lineas = [
        (88, "ABCDEFGHIJKLMNOPQRSTUVWXYZ"),
        (88, "abcdefghijklmnopqrstuvwxyz"),
        (88, "0123456789 .,;:!?\u00a1\u00bf()[]{}"),
        (88, "+-=/\\%&*#<>@$|~^_ \u00ab\u00bb\u00b0\u00b1\u00d7\u00f7\u00a7\u2014"),
        (88, "\u00e1\u00e9\u00ed\u00f3\u00fa\u00f1\u00fc \u00c1\u00c9\u00cd\u00d3\u00da\u00d1 \u00e0\u00e8\u00e2\u00ea\u00e4\u00ef\u00e7"),
        (30, "Nebulosa. Epsilon-Z: la nave llega a 12,5 UA del planeta Kepler-452b en 3 horas y 40 minutos."),
        (22, "La energía de la estación es 3,2 EJ; el escudo resiste 1.500 impactos de 40 kt. ¡Atención! ¿Seguro?"),
        (16, "Texto pequeño: Los motores de curvatura necesitan antimateria para viajar entre sistemas solares."),
    ]
    ancho = 1900
    alto = sum(int(px * 1.7) + 12 for px, _ in lineas) + 30
    img = [[0.0] * ancho for _ in range(alto)]
    y = 20
    for px, texto in lineas:
        y += int(px * 1.25)
        dibujar_linea(img, ancho, texto, font, px, y, 20)
        y += int(px * 0.45) + 12
    escribir_png(ruta, img, ancho)
    print("muestra:", ruta, ancho, "x", alto)


def main():
    ap = argparse.ArgumentParser(description="Genera la fuente angular Epsilon Sans.")
    ap.add_argument("--salida", default=os.path.join("game", "assets", "fonts", "EpsilonSans-Regular.ttf"))
    ap.add_argument("--muestra", help="ruta de un PNG con la fuente dibujada")
    a = ap.parse_args()
    f = construir(a.salida)
    if a.muestra:
        muestra(f, a.muestra)
    return 0


if __name__ == "__main__":
    sys.exit(main())
