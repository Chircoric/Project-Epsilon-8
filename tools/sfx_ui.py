"""Genera efectos de sonido de interfaz de Nebulosa por síntesis (sin samples de terceros).

Uso (con cualquier Python 3):
    python tools/sfx_ui.py [carpeta_salida]

Salida por defecto: game/assets/audio/ui/*.wav  (44.1 kHz, 16 bits, mono)
Todo el audio es original: se sintetiza aquí mismo, así que no hay licencia externa que revisar.
"""
import math
import os
import random
import struct
import sys
import wave

RATE = 44100
PEAK = 0.7  # pico máximo de cada efecto (≈ -3 dBFS), deja margen para la mezcla


def tono(freq, dur, forma="seno", freq_fin=None):
    """Oscilador con barrido opcional de frecuencia (lineal)."""
    n = int(RATE * dur)
    out = []
    fase = 0.0
    for i in range(n):
        t = i / max(1, n - 1)
        f = freq if freq_fin is None else freq + (freq_fin - freq) * t
        fase += 2 * math.pi * f / RATE
        s = math.sin(fase)
        if forma == "cuadrada":
            s = 1.0 if s >= 0 else -1.0
        elif forma == "triangular":
            s = (2 / math.pi) * math.asin(s)
        out.append(s)
    return out


def ruido(dur, seed=1):
    rnd = random.Random(seed)
    return [rnd.uniform(-1.0, 1.0) for _ in range(int(RATE * dur))]


def envolvente(señal, ataque=0.003, caida=0.05, exp=3.0):
    """Ataque lineal corto y caída exponencial hasta el final."""
    n = len(señal)
    a = max(1, int(RATE * ataque))
    out = []
    for i, s in enumerate(señal):
        g = min(1.0, i / a)
        t = i / max(1, n - 1)
        g *= (1 - t) ** exp
        out.append(s * g)
    return out


def mezclar(*pistas):
    n = max(len(p) for p in pistas)
    out = [0.0] * n
    for p in pistas:
        for i, s in enumerate(p):
            out[i] += s
    return out


def sumar_en(base, pista, inicio_s):
    """Suma 'pista' dentro de 'base' empezando en inicio_s segundos."""
    off = int(RATE * inicio_s)
    need = off + len(pista)
    if need > len(base):
        base.extend([0.0] * (need - len(base)))
    for i, s in enumerate(pista):
        base[off + i] += s
    return base


def normalizar(señal, pico=PEAK):
    m = max(abs(s) for s in señal) or 1.0
    k = pico / m
    return [s * k for s in señal]


def bordes(señal, fundido=0.008):
    """Fundido de entrada y salida para evitar clics."""
    n = len(señal)
    f = min(int(RATE * fundido), n // 2)
    for i in range(f):
        g = i / f
        señal[i] *= g
        señal[n - 1 - i] *= g
    return señal


def guardar(ruta, señal, pico=PEAK):
    señal = bordes(normalizar(señal, pico))
    with wave.open(ruta, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        datos = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32767)) for s in señal)
        w.writeframes(datos)
    return len(señal) / RATE


# --- Los cinco efectos -------------------------------------------------------

def aliento(dur, corte, seed):
    """Soplo: ruido filtrado en paso bajo, para dar textura de susurro."""
    a = 1 - math.exp(-2 * math.pi * corte / RATE)
    y, out = 0.0, []
    for x in ruido(dur, seed=seed):
        y += a * (x - y)
        out.append(y)
    return out


def ui_click():
    """Pulsación: chasquido corto y seco."""
    cuerpo = envolvente(tono(1800, 0.03, "triangular", 900), ataque=0.001, caida=0.02, exp=4)
    chasquido = envolvente(ruido(0.012, seed=7), ataque=0.0005, exp=6)
    return mezclar(cuerpo, [0.6 * s for s in chasquido])


def ui_hover():
    """Pasar el cursor: pulso suave y agudo, casi silencioso."""
    return envolvente(tono(1320, 0.05, "seno"), ataque=0.004, exp=2)


def ui_confirmar():
    """Cristal tintineante: dos toques de campanilla con parciales inarmónicos y caída lenta."""
    def campanilla(f, dur):
        parciales = []
        for razon, amp in ((1.0, 1.0), (2.76, 0.12), (5.40, 0.03)):
            parciales.append([amp * s for s in envolvente(tono(f * razon, dur, "seno"), ataque=0.004, exp=2)])
        return mezclar(*parciales)
    pista = []
    sumar_en(pista, campanilla(523, 0.7), 0.0)
    sumar_en(pista, campanilla(659, 0.8), 0.1)
    return mezclar(pista, [0.05 * s for s in aliento(0.9, 3000, 3)])


def ui_error():
    """Alarma caótica: notas que saltan entre dos tonos, con desafinación aleatoria y trémolo rápido."""
    rnd = random.Random(42)
    pista = []
    paso = 0.11
    for k in range(4):
        f = (440 if k % 2 == 0 else 587) + rnd.uniform(-50, 50)
        seg = tono(f, 0.22, "triangular")  # cada nota dura más que el paso: se solapan y arrastran
        trémolo = [s * (0.6 + 0.4 * math.sin(2 * math.pi * 24 * i / RATE)) for i, s in enumerate(seg)]
        sumar_en(pista, envolvente(trémolo, ataque=0.003, exp=2.5), k * paso)
    return mezclar(pista, [0.06 * s for s in aliento(0.55, 2500, 4)])


def ui_panel():
    """Dedo deslizándose sobre cristal: silbido suave que sube de tono, con vibrato leve y roce filtrado."""
    dur = 0.45
    n = int(RATE * dur)
    silbido, fase = [], 0.0
    for i in range(n):
        t = i / (n - 1)
        # Sube hasta la mitad y luego baja: termina en ~260 Hz, más grave que antes.
        f = 380 + 180 * math.sin(math.pi * t) - 120 * t + 12 * math.sin(2 * math.pi * 5 * i / RATE)
        fase += 2 * math.pi * f / RATE
        silbido.append(math.sin(fase))
    # Roce: ruido muy filtrado, con poco peso.
    roce, y = [], 0.0
    a = 1 - math.exp(-2 * math.pi * 2500 / RATE)
    for x in ruido(dur, seed=11):
        y += a * (x - y)
        roce.append(y)
    mezcla = mezclar([0.8 * s for s in silbido], [0.15 * s for s in roce])
    return envolvente(mezcla, ataque=0.1, exp=1.2)


# (función, pico). El pico controla el volumen de cada efecto.
EFECTOS = {
    "ui_click": (ui_click, PEAK),
    "ui_hover": (ui_hover, PEAK),
    "ui_confirmar": (ui_confirmar, 0.08),
    "ui_error": (ui_error, 0.12),
    "ui_panel": (ui_panel, 0.07),
}


def main(argv):
    carpeta = argv[1] if len(argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "game", "assets", "audio", "ui")
    os.makedirs(carpeta, exist_ok=True)
    for nombre, (fn, pico) in EFECTOS.items():
        ruta = os.path.join(carpeta, nombre + ".wav")
        dur = guardar(ruta, fn(), pico)
        print(f"{ruta}  {dur:.3f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
