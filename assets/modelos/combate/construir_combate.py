# Torretas humanas y naves alienígenas (fase E de docs/diseno-combate.md), para Nebulosa.
#
# Se ejecuta dentro de Blender con combate.blend abierto:
#     exec(open(r"...\construir_combate.py", encoding="utf-8").read())
# Rehace las colecciones Torretas, Alien_Dron, Alien_Corveta y Alien_Fragata y, con EXPORTAR = True,
# escribe los .gltf en game/assets/modelos. Usa las funciones de geometría de construir_estacion.py.
#
# Ejes de Blender, en metros reales:
# - Torretas: apoyadas en el origen, con +Z hacia fuera del casco (como una ranura hp_) y los cañones
#   hacia +Y. En el motor el juego les da media vuelta sobre Z, como a la bahía de drones.
# - Naves: proa a +Y, arriba +Z, origen a media eslora (en el motor, proa a +Z y -Y arriba).
#   La eslora es la de su clase en kShipClasses (Balance.h): dron 34 m, corveta 300 m, fragata 1200 m.
#   El nombre de cada pieza decide su zona (AlienZoneOfPart): cuerpo = espina, ojo/nucleo = núcleo,
#   corazon = puente, cuchilla/filo + babor/estribor = góndolas, aro = anillo. Ninguna otra pieza debe
#   llevar esas palabras (ni «luz», «radio», «punta», «centro», ni «ojo» dentro de «rojo»).

import bpy, bmesh, math, os
from mathutils import Vector

SOLO_FUNCIONES = True
exec(open(r"C:\Users\Admin\Desktop\PROYECTO EPLSILON-0\EpsilonEngine\assets\modelos\estacion_origami\construir_estacion.py", encoding="utf-8").read())

EXPORTAR = globals().get("EXPORTAR", False)
DESTINO = globals().get("DESTINO", r"C:\Users\Admin\Desktop\PROYECTO EPLSILON-0\EpsilonEngine\game\assets\modelos")
MATS_TORRETA = ["metal_gris", "casco_blanco", "franja_aviso", "franja_negra", "bobina_cobre",
                "montaje_torreta", "luz_roja", "compuerta_carga"]

# ---------------------------------------------------------------- materiales
def materiales_combate():
    falta = [m for m in MATS_TORRETA if m not in bpy.data.materials]
    if falta:
        with bpy.data.libraries.load(SHIP_BLEND, link=False) as (src, dst):
            dst.materials = [m for m in src.materials if m in falta]
    def liso(nombre, color, metal, rugoso, brillo=0.0):
        m = bpy.data.materials.get(nombre) or bpy.data.materials.new(nombre)
        m.use_nodes = True
        b = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
        b.inputs["Base Color"].default_value = (*color, 1)
        b.inputs["Metallic"].default_value = metal
        b.inputs["Roughness"].default_value = rugoso
        b.inputs["Emission Color"].default_value = (*color, 1)
        b.inputs["Emission Strength"].default_value = brillo
        return m
    # La especie hostil: piel oscura y rojiza, placas casi negras y cristales encendidos. Las neutrales
    # usan los mismos modelos con otros colores (el juego los cambia al cargarlos).
    liso("alien_piel", (0.16, 0.07, 0.09), 0.55, 0.35)
    liso("alien_placa", (0.08, 0.045, 0.055), 0.7, 0.5)
    liso("alien_cristal", (1.0, 0.18, 0.10), 0.0, 0.3, 8.0)

def coleccion(nombre):
    global COL
    old = bpy.data.collections.get(nombre)
    if old:
        for o in list(old.all_objects): bpy.data.objects.remove(o, do_unlink=True)
        bpy.data.collections.remove(old)
    COL = bpy.data.collections.new(nombre); bpy.context.scene.collection.children.link(COL)
    root = bpy.data.objects.new(nombre, None); COL.objects.link(root)
    return root

def guardar(root, nombre, M):
    for part, me in M.items():
        obj(f"{nombre}_{part}", me, root)

# ---------------------------------------------------------------- torretas humanas
# Industrial y rígido: todo son cajas y prismas de cantos vivos sobre un zócalo cuadrado. La ligera
# ocupa la base de torreta de la nave (unos 36 m de lado); la pesada es 1,6 veces mayor.

def zocalo(bm, uvl, s, lado=18.0, alto=2.5):
    """Zócalo cuadrado con bisel: dos cajas, la de arriba algo más estrecha."""
    box(bm, uvl, (0, 0, alto * s * 0.5), lado * s, lado * s, alto * s * 0.5)
    box(bm, uvl, (0, 0, alto * s * 1.25), lado * s * 0.86, lado * s * 0.86, alto * s * 0.25)

def collar(bm, uvl, s, z0, lado=13.0, alto=3.0):
    """Collar de giro cuadrado sobre el zócalo."""
    box(bm, uvl, (0, 0, z0 + alto * s * 0.5), lado * s, lado * s, alto * s * 0.5)

def casamata(bm, uvl, s, z0, ancho, largo, alto, frente=0.55):
    """Cuerpo de la torreta: un prisma con el frente en pendiente, como un blindaje soldado."""
    w, l, h = ancho * s, largo * s, alto * s
    def sec(y, ww, hh, dz=0.0):
        return [Vector((u, y, z0 + dz + v)) for u, v in ((ww, 0), (ww, hh), (-ww, hh), (-ww, 0))]
    loft(bm, uvl, [sec(-l, w, h), sec(l * 0.35, w, h), sec(l, w * 0.9, h * frente)], tile=8.0, smooth=False)

def nervios(bm, uvl, s, z0, ancho, largo, alto, n=3):
    """Costillas de refuerzo a los lados del cuerpo."""
    for k in range(n):
        y = (-0.7 + 1.0 * k / max(n - 1, 1)) * largo * s
        for sx in (-1, 1):
            box(bm, uvl, (sx * (ancho * s + 0.5 * s), y, z0 + alto * s * 0.5), 0.5 * s, 0.8 * s, alto * s * 0.5)

def torreta_base(nombre, s, ancho=11.0, largo=10.0, alto=8.0, collarin=True):
    """Zócalo, collar y casamata comunes. Devuelve (M, z del techo, medidas)."""
    M = {}
    bm, uvl = bm_new()
    zocalo(bm, uvl, s)
    M["zocalo"] = mesh_from(f"{nombre}_zocalo", bm, "montaje_torreta")
    z0 = 3.125 * s
    bm, uvl = bm_new()
    if collarin:
        collar(bm, uvl, s, z0)
        z0 += 3.0 * s
    casamata(bm, uvl, s, z0, ancho, largo, alto)
    M["cuerpo"] = mesh_from(f"{nombre}_cuerpo", bm, "casco_blanco")
    bm, uvl = bm_new()
    nervios(bm, uvl, s, z0, ancho, largo, alto)
    # Escotilla de servicio y caja de sensores en el techo.
    box(bm, uvl, (0, -largo * s * 0.55, z0 + alto * s + 0.6 * s), 3.0 * s, 2.5 * s, 0.6 * s)
    box(bm, uvl, (ancho * s * 0.6, -largo * s * 0.2, z0 + alto * s + 1.2 * s), 1.6 * s, 1.6 * s, 1.2 * s)
    M["estructura"] = mesh_from(f"{nombre}_estructura", bm, "metal_gris")
    bm, uvl = bm_new()
    for sx in (-1, 1):   # franjas de aviso en los costados de la casamata
        box(bm, uvl, (sx * (ancho * s + 0.06 * s), -largo * s * 0.85, z0 + alto * s * 0.5), 0.08 * s, 0.9 * s, alto * s * 0.42)
    M["aviso"] = mesh_from(f"{nombre}_aviso", bm, "franja_aviso")
    bm, uvl = bm_new()
    box(bm, uvl, (ancho * s * 0.6, -largo * s * 0.2, z0 + alto * s + 2.6 * s), 0.4 * s, 0.4 * s, 0.25 * s)
    M["baliza"] = mesh_from(f"{nombre}_baliza", bm, "luz_roja")
    return M, z0, (ancho * s, largo * s, alto * s)

def canon_cuadrado(bm, uvl, y0, y1, z, x, lado):
    """Tubo de sección cuadrada, de y0 a y1."""
    box(bm, uvl, (x, (y0 + y1) / 2, z), lado, (y1 - y0) / 2, lado)

def coilgun(nombre, s):
    M, z0, (w, l, h) = torreta_base(nombre, s)
    zc = z0 + h * 0.5
    bm, uvl = bm_new()
    for sx in (-1, 1):            # cuatro cañones en rejilla de 2 × 2
        for sz in (-1, 1):
            canon_cuadrado(bm, uvl, l * 0.6, l + 26.0 * s, zc + sz * 1.6 * s, sx * 2.4 * s, 0.75 * s)
    box(bm, uvl, (0, l + 26.0 * s, zc), 4.4 * s, 0.6 * s, 3.4 * s)        # boca común
    M["canones"] = mesh_from(f"{nombre}_canones", bm, "metal_gris")
    bm, uvl = bm_new()
    for k in range(5):            # bobinas de aceleración, cuadradas
        y = l + (3.0 + 4.6 * k) * s
        box(bm, uvl, (0, y, zc), 4.2 * s, 0.7 * s, 3.2 * s)
    M["bobinas"] = mesh_from(f"{nombre}_bobinas", bm, "bobina_cobre")
    guardar(RAIZ, nombre, M)

def railgun360(nombre, s):
    M, z0, (w, l, h) = torreta_base(nombre, s, ancho=12.0, largo=12.0, alto=9.0)
    zc = z0 + h * 0.55
    bm, uvl = bm_new()
    for sx in (-1, 1):            # dos raíles planos con su hueco
        box(bm, uvl, (sx * 1.6 * s, l + 20.0 * s, zc), 0.7 * s, 22.0 * s, 2.2 * s)
    for k in range(6):            # abrazaderas que sujetan los raíles
        y = l + (2.0 + 7.0 * k) * s
        box(bm, uvl, (0, y, zc), 3.4 * s, 0.8 * s, 3.0 * s)
    M["canones"] = mesh_from(f"{nombre}_canones", bm, "metal_gris")
    bm, uvl = bm_new()
    for sx in (-1, 1):            # bancos de condensadores a los lados
        box(bm, uvl, (sx * (w + 2.6 * s), -l * 0.2, z0 + h * 0.45), 2.0 * s, l * 0.7, h * 0.4)
    M["condensadores"] = mesh_from(f"{nombre}_condensadores", bm, "compuerta_carga")
    bm, uvl = bm_new()
    for sx in (-1, 1):
        box(bm, uvl, (sx * 1.6 * s, l + 20.0 * s, zc), 0.15 * s, 21.0 * s, 1.0 * s)
    M["bobinas"] = mesh_from(f"{nombre}_bobinas", bm, "bobina_cobre")
    guardar(RAIZ, nombre, M)

def espinal(nombre, s):
    """Fija al eje: sin collar de giro, baja y muy larga."""
    M, z0, (w, l, h) = torreta_base(nombre, s, ancho=10.0, largo=18.0, alto=6.0, collarin=False)
    zc = z0 + h * 0.5
    bm, uvl = bm_new()
    for sx in (-1, 1):
        box(bm, uvl, (sx * 2.0 * s, l + 34.0 * s, zc), 0.9 * s, 36.0 * s, 2.6 * s)
    for k in range(9):
        y = l + (2.0 + 8.4 * k) * s
        box(bm, uvl, (0, y, zc), 4.0 * s, 0.9 * s, 3.4 * s)
    box(bm, uvl, (0, l + 70.0 * s, zc), 4.6 * s, 1.2 * s, 4.0 * s)
    M["canones"] = mesh_from(f"{nombre}_canones", bm, "metal_gris")
    bm, uvl = bm_new()
    for sx in (-1, 1):
        for k in range(3):
            box(bm, uvl, (sx * (w + 2.4 * s), -l * 0.6 + k * l * 0.55, z0 + h * 0.4), 1.8 * s, l * 0.24, h * 0.4)
    M["condensadores"] = mesh_from(f"{nombre}_condensadores", bm, "compuerta_carga")
    bm, uvl = bm_new()
    for sx in (-1, 1):
        box(bm, uvl, (sx * 2.0 * s, l + 34.0 * s, zc), 0.2 * s, 35.0 * s, 1.2 * s)
    M["bobinas"] = mesh_from(f"{nombre}_bobinas", bm, "bobina_cobre")
    guardar(RAIZ, nombre, M)

def bahia_misiles(nombre, s):
    """Lanzador vertical: un bloque con celdas en el techo, sin cañones."""
    M = {}
    bm, uvl = bm_new()
    zocalo(bm, uvl, s)
    M["zocalo"] = mesh_from(f"{nombre}_zocalo", bm, "montaje_torreta")
    z0, w, l, h = 3.125 * s, 14.0 * s, 14.0 * s, 9.0 * s
    bm, uvl = bm_new()
    box(bm, uvl, (0, 0, z0 + h / 2), w, l, h / 2)
    M["cuerpo"] = mesh_from(f"{nombre}_cuerpo", bm, "casco_blanco")
    bm, uvl = bm_new()
    for sx in (-1, 1):
        for sy in (-1, 0, 1):
            box(bm, uvl, (sx * (w + 0.5 * s), sy * l * 0.7, z0 + h / 2), 0.5 * s, 0.8 * s, h / 2)
    box(bm, uvl, (0, 0, z0 + h + 0.3 * s), w * 0.96, 0.6 * s, 0.3 * s)            # viga central del techo
    box(bm, uvl, (0, -l - 1.2 * s, z0 + h * 0.4), w * 0.5, 1.2 * s, h * 0.4)     # caja de recarga
    M["estructura"] = mesh_from(f"{nombre}_estructura", bm, "metal_gris")
    bm, uvl = bm_new()
    for i in range(4):            # 4 × 4 celdas: compuertas cuadradas
        for j in range(4):
            x = (-0.75 + 0.5 * i) * w * 0.9
            y = (-0.75 + 0.5 * j) * l * 0.9
            box(bm, uvl, (x, y + (0.7 * s if y > 0 else -0.7 * s), z0 + h + 0.12 * s), 2.6 * s, 2.3 * s, 0.12 * s, tile=4.0)
    M["compuertas"] = mesh_from(f"{nombre}_compuertas", bm, "franja_negra")
    bm, uvl = bm_new()
    for sx in (-1, 1):
        box(bm, uvl, (sx * (w + 0.06 * s), 0, z0 + h * 0.85), 0.08 * s, l * 0.9, 0.7 * s)
    M["aviso"] = mesh_from(f"{nombre}_aviso", bm, "franja_aviso")
    bm, uvl = bm_new()
    box(bm, uvl, (w * 0.85, -l * 0.85, z0 + h + 0.5 * s), 0.4 * s, 0.4 * s, 0.4 * s)
    M["baliza"] = mesh_from(f"{nombre}_baliza", bm, "luz_roja")
    guardar(RAIZ, nombre, M)

def nuclear(nombre, s):
    """Un solo silo: bloque alto con una compuerta grande y franjas de aviso por todo el canto."""
    M = {}
    bm, uvl = bm_new()
    zocalo(bm, uvl, s)
    M["zocalo"] = mesh_from(f"{nombre}_zocalo", bm, "montaje_torreta")
    z0, w, h = 3.125 * s, 9.0 * s, 16.0 * s
    bm, uvl = bm_new()
    box(bm, uvl, (0, 0, z0 + h / 2), w, w, h / 2)
    M["cuerpo"] = mesh_from(f"{nombre}_cuerpo", bm, "casco_blanco")
    bm, uvl = bm_new()
    for sx in (-1, 1):            # contrafuertes en las cuatro caras
        box(bm, uvl, (sx * (w + 1.2 * s), 0, z0 + h * 0.35), 1.2 * s, w * 0.5, h * 0.35)
        box(bm, uvl, (0, sx * (w + 1.2 * s), z0 + h * 0.35), w * 0.5, 1.2 * s, h * 0.35)
    for sx in (-1, 1):            # bisagras de la compuerta
        box(bm, uvl, (sx * w * 0.5, -w * 0.82, z0 + h + 0.8 * s), 1.4 * s, 0.9 * s, 0.8 * s)
    M["estructura"] = mesh_from(f"{nombre}_estructura", bm, "metal_gris")
    bm, uvl = bm_new()
    box(bm, uvl, (0, 0, z0 + h + 0.25 * s), w * 0.78, w * 0.78, 0.25 * s, tile=6.0)
    M["compuerta"] = mesh_from(f"{nombre}_compuerta", bm, "compuerta_carga")
    bm, uvl = bm_new()
    for sx in (-1, 1):
        box(bm, uvl, (sx * (w + 0.06 * s), 0, z0 + h * 0.88), 0.08 * s, w, 1.0 * s)
        box(bm, uvl, (0, sx * (w + 0.06 * s), z0 + h * 0.88), w, 0.08 * s, 1.0 * s)
    M["aviso"] = mesh_from(f"{nombre}_aviso", bm, "franja_aviso")
    bm, uvl = bm_new()
    for sx in (-1, 1):
        for sy in (-1, 1):
            box(bm, uvl, (sx * w * 0.92, sy * w * 0.92, z0 + h + 0.4 * s), 0.4 * s, 0.4 * s, 0.4 * s)
    M["baliza"] = mesh_from(f"{nombre}_baliza", bm, "luz_roja")
    guardar(RAIZ, nombre, M)

def defensa_puntual(nombre, s):
    """Plataforma de defensa puntual: un montaje pequeño con un rotativo de seis tubos y un radar."""
    M, z0, (w, l, h) = torreta_base(nombre, s, ancho=6.0, largo=6.0, alto=5.0)
    zc = z0 + h * 0.5
    bm, uvl = bm_new()
    for k in range(6):
        a = k * math.pi / 3
        cyl(bm, uvl, (math.cos(a) * 1.0 * s, l * 0.8, zc + math.sin(a) * 1.0 * s), (math.cos(a) * 1.0 * s, l + 14.0 * s, zc + math.sin(a) * 1.0 * s), 0.35 * s, 8)
    box(bm, uvl, (0, l + 4.0 * s, zc), 1.8 * s, 0.4 * s, 1.8 * s)
    box(bm, uvl, (0, l + 12.0 * s, zc), 1.8 * s, 0.4 * s, 1.8 * s)
    M["canones"] = mesh_from(f"{nombre}_canones", bm, "metal_gris")
    bm, uvl = bm_new()   # radar plano sobre un mástil
    box(bm, uvl, (-w * 0.5, -l * 0.5, z0 + h + 3.0 * s), 0.4 * s, 0.4 * s, 3.0 * s)
    box(bm, uvl, (-w * 0.5, -l * 0.5, z0 + h + 6.6 * s), 3.2 * s, 0.3 * s, 1.8 * s)
    M["radar"] = mesh_from(f"{nombre}_radar", bm, "franja_negra")
    guardar(RAIZ, nombre, M)

TORRETAS = [  # (nombre, función, escala): el juego busca las piezas por este prefijo
    ("defensa_puntual", defensa_puntual, 1.0),
    ("coilgun_ligera", coilgun, 1.0), ("coilgun_pesada", coilgun, 1.6),
    ("espinal_ligera", espinal, 1.0), ("espinal_pesada", espinal, 1.6),
    ("railgun360_ligera", railgun360, 1.0), ("railgun360_pesada", railgun360, 1.6),
    ("misiles_ligera", bahia_misiles, 1.0), ("misiles_pesada", bahia_misiles, 1.6),
    ("nuclear_ligera", nuclear, 1.0), ("nuclear_pesada", nuclear, 1.6),
]

def torretas():
    global RAIZ
    RAIZ = coleccion("Torretas")
    for nombre, f, s in TORRETAS:
        f(nombre, s)
    return RAIZ

# ---------------------------------------------------------------- naves alienígenas
# Formas continuas, sin una sola arista recta: secciones elípticas que se estrechan, cuchillas que
# barren hacia delante y cristales encendidos.

def elipse(y, w, h, z=0.0, n=24, p=2.2, x=0.0):
    """Sección superelíptica de semiejes w (X) y h (Z) a la altura y."""
    pts = []
    for k in range(n):
        a = 2 * math.pi * k / n
        c, s_ = math.cos(a), math.sin(a)
        pts.append(Vector((x + w * math.copysign(abs(c) ** (2 / p), c), y, z + h * math.copysign(abs(s_) ** (2 / p), s_))))
    return pts

def casco(bm, uvl, perfil, n=28, p=2.2, tile=40.0):
    """Loft de secciones (y, w, h, z[, x]): un casco continuo, cerrado por los dos extremos."""
    loft(bm, uvl, [elipse(s[0], s[1], s[2], s[3], n, p, s[4] if len(s) > 4 else 0.0) for s in perfil], tile=tile)

def cuchilla(bm, uvl, raiz, punta, ancho, grueso, n=10):
    """Hoja curva de raíz a punta: secciones en rombo que adelgazan hasta casi nada."""
    raiz, punta = Vector(raiz), Vector(punta)
    d = (punta - raiz).normalized()
    cuerda = Vector((0, 0, 1)).cross(d).normalized()   # horizontal y perpendicular a la hoja
    if cuerda.y < 0: cuerda = -cuerda                    # el borde de ataque, hacia proa
    rings = []
    for k in range(n + 1):
        t = k / n
        c = raiz.lerp(punta, t) + Vector((0, 0, math.sin(t * math.pi) * grueso * 0.6))
        w = ancho * (1.0 - 0.92 * t)
        g = grueso * (1.0 - 0.85 * t)
        rings.append([c + cuerda * w, c + Vector((0, 0, g)), c - cuerda * w * 0.6, c - Vector((0, 0, g))])
    loft(bm, uvl, rings, tile=20.0, smooth=True)
    return rings

def filo(bm, uvl, rings, r):
    """El canto delantero de una cuchilla, encendido: un tubo fino por su borde de ataque."""
    pts = [ring[0] for ring in rings]
    for a, b in zip(pts, pts[1:]):
        cyl(bm, uvl, a, b, r, 6, caps=True)

def ojo(bm, uvl, c, rx, ry, rz, n=20):
    rings = []
    for k in range(1, n):
        t = math.pi * k / n
        rings.append(elipse(c[1] - ry * math.cos(t), rx * math.sin(t), rz * math.sin(t), c[2], 20, 2.0, c[0]))
    loft(bm, uvl, rings, tile=10.0)

def aro(bm, uvl, c, R, r, segs=64, tube=12):
    """Toro con el eje en Y (el de la nave)."""
    c = Vector(c)
    rings = []
    for k in range(segs + 1):
        a = 2 * math.pi * k / segs
        cen = c + Vector((math.cos(a) * R, 0, math.sin(a) * R))
        d = Vector((math.cos(a), 0, math.sin(a)))
        rings.append([cen + d * math.cos(2 * math.pi * j / tube) * r + Vector((0, 1, 0)) * math.sin(2 * math.pi * j / tube) * r for j in range(tube)])
    loft(bm, uvl, rings[:-1] + [rings[0]], cap0=False, cap1=False, tile=40.0)

def alien_dron():
    root = coleccion("Alien_Dron")
    M = {}
    bm, uvl = bm_new()   # punta de flecha de 34 m
    casco(bm, uvl, [(-17.0, 1.2, 0.8, 0.4), (-15.0, 3.6, 2.2, 0.4), (-9.0, 5.0, 3.0, 0.6), (-1.0, 4.6, 2.8, 0.4), (7.0, 3.0, 1.9, 0.0), (13.0, 1.4, 0.9, -0.2), (17.0, 0.3, 0.2, -0.3)], tile=8.0)
    M["cuerpo"] = mesh_from("alien_dron_cuerpo", bm, "alien_piel")
    bm, uvl = bm_new()
    casco(bm, uvl, [(-12.0, 0.5, 0.4, 2.6), (-8.0, 2.4, 1.0, 3.3), (2.0, 1.6, 0.7, 2.9), (8.0, 0.4, 0.2, 2.0)], tile=8.0)
    M["cuerpo_placa"] = mesh_from("alien_dron_cuerpo_placa", bm, "alien_placa")
    bm, uvl = bm_new()
    ojo(bm, uvl, (0, 9.5, 1.5), 1.4, 2.4, 1.2)
    M["ojo"] = mesh_from("alien_dron_ojo", bm, "alien_cristal")
    for lado, sx in (("babor", -1), ("estribor", 1)):
        bm, uvl = bm_new()
        rings = cuchilla(bm, uvl, (sx * 3.6, -6.0, 0.0), (sx * 12.0, 12.0, -0.6), 4.0, 0.9)
        M[f"cuchilla_{lado}"] = mesh_from(f"alien_dron_cuchilla_{lado}", bm, "alien_piel")
        bm, uvl = bm_new()
        filo(bm, uvl, rings, 0.28)
        M[f"filo_{lado}"] = mesh_from(f"alien_dron_filo_{lado}", bm, "alien_cristal")
    guardar(root, "alien_dron", M)
    return root

def alien_corveta():
    root = coleccion("Alien_Corveta")
    M = {}
    bm, uvl = bm_new()   # 300 m: un huso con quilla, más grueso a popa
    casco(bm, uvl, [(-150.0, 10.0, 8.0, 0.0), (-140.0, 26.0, 18.0, 2.0), (-100.0, 34.0, 24.0, 2.0), (-40.0, 30.0, 22.0, 0.0),
                    (30.0, 22.0, 16.0, -2.0), (90.0, 14.0, 10.0, -3.0), (135.0, 6.0, 4.0, -4.0), (150.0, 1.0, 0.8, -4.0)], tile=30.0)
    M["cuerpo"] = mesh_from("alien_corveta_cuerpo", bm, "alien_piel")
    bm, uvl = bm_new()   # quilla ventral y placas del lomo
    casco(bm, uvl, [(-120.0, 3.0, 2.0, -20.0), (-80.0, 8.0, 10.0, -26.0), (20.0, 6.0, 8.0, -20.0), (80.0, 1.0, 1.0, -12.0)], tile=30.0)
    for k in range(4):
        y = -110.0 + k * 34.0
        casco(bm, uvl, [(y, 6.0, 2.0, 21.0 - k), (y + 12.0, 16.0, 4.0, 23.0 - k), (y + 26.0, 10.0, 2.5, 21.0 - k)], tile=30.0)
    M["cuerpo_placas"] = mesh_from("alien_corveta_cuerpo_placas", bm, "alien_placa")
    bm, uvl = bm_new()   # cresta dorsal: el corazón, donde va la tripulación
    casco(bm, uvl, [(20.0, 2.0, 2.0, 14.0), (34.0, 9.0, 8.0, 19.0), (56.0, 8.0, 7.0, 18.0), (80.0, 2.0, 2.0, 12.0)], tile=20.0)
    M["corazon"] = mesh_from("alien_corveta_corazon", bm, "alien_placa")
    bm, uvl = bm_new()
    ojo(bm, uvl, (0, 52.0, 23.5), 5.0, 9.0, 2.5)
    M["corazon_cristal"] = mesh_from("alien_corveta_corazon_cristal", bm, "alien_cristal")
    bm, uvl = bm_new()   # el núcleo asoma por arriba y por abajo, a popa
    ojo(bm, uvl, (0, -96.0, 0.0), 22.0, 22.0, 30.0)
    M["nucleo"] = mesh_from("alien_corveta_nucleo", bm, "alien_cristal")
    for lado, sx in (("babor", -1), ("estribor", 1)):
        bm, uvl = bm_new()
        rings = cuchilla(bm, uvl, (sx * 26.0, -70.0, 0.0), (sx * 92.0, 110.0, -6.0), 34.0, 8.0)
        M[f"cuchilla_{lado}"] = mesh_from(f"alien_corveta_cuchilla_{lado}", bm, "alien_piel")
        bm, uvl = bm_new()
        filo(bm, uvl, rings, 1.6)
        M[f"filo_{lado}"] = mesh_from(f"alien_corveta_filo_{lado}", bm, "alien_cristal")
    bm, uvl = bm_new()   # vetas encendidas a lo largo del casco
    for sx in (-1, 1):
        pts = [Vector((sx * w * 0.98, y, z)) for y, w, z in ((-130.0, 30.0, 2.0), (-100.0, 34.0, 2.0), (-40.0, 30.0, 0.0), (30.0, 22.0, -2.0), (90.0, 14.0, -3.0))]
        for a, b in zip(pts, pts[1:]):
            cyl(bm, uvl, a, b, 1.2, 6)
    M["cuerpo_vetas"] = mesh_from("alien_corveta_cuerpo_vetas", bm, "alien_cristal")
    guardar(root, "alien_corveta", M)
    return root

def alien_fragata():
    root = coleccion("Alien_Fragata")
    M = {}
    bm, uvl = bm_new()   # 1200 m: espina larga que se abre en dos horquillas a proa
    casco(bm, uvl, [(-600.0, 30.0, 26.0, 0.0), (-580.0, 70.0, 56.0, 0.0), (-450.0, 84.0, 66.0, 0.0), (-250.0, 70.0, 56.0, 0.0),
                    (0.0, 56.0, 46.0, 0.0), (200.0, 48.0, 38.0, 0.0), (330.0, 40.0, 30.0, 0.0), (380.0, 20.0, 16.0, 0.0)], tile=60.0)
    for sx in (-1, 1):   # las dos horquillas, que nacen dentro de la espina y se abren
        casco(bm, uvl, [(300.0, 2.0, 2.0, 0.0, sx * 14.0), (330.0, 22.0, 24.0, 0.0, sx * 24.0), (420.0, 20.0, 22.0, -4.0, sx * 40.0),
                        (520.0, 12.0, 14.0, -8.0, sx * 52.0), (600.0, 1.5, 2.0, -10.0, sx * 56.0)], tile=60.0)
    M["cuerpo"] = mesh_from("alien_fragata_cuerpo", bm, "alien_piel")
    bm, uvl = bm_new()   # placas del lomo y del vientre
    for k in range(7):
        y = -520.0 + k * 110.0
        for sz in (-1, 1):
            casco(bm, uvl, [(y, 14.0, 4.0, sz * (56.0 - k * 2.5)), (y + 36.0, 40.0, 9.0, sz * (60.0 - k * 2.5)), (y + 80.0, 24.0, 5.0, sz * (56.0 - k * 2.5))], tile=40.0)
    M["cuerpo_placas"] = mesh_from("alien_fragata_cuerpo_placas", bm, "alien_placa")
    bm, uvl = bm_new()   # el corazón: una torre dorsal larga
    casco(bm, uvl, [(140.0, 4.0, 4.0, 40.0), (180.0, 26.0, 28.0, 60.0), (260.0, 22.0, 24.0, 62.0), (320.0, 4.0, 4.0, 44.0)], tile=40.0)
    M["corazon"] = mesh_from("alien_fragata_corazon", bm, "alien_placa")
    bm, uvl = bm_new()
    ojo(bm, uvl, (0, 250.0, 84.0), 12.0, 30.0, 5.0)
    M["corazon_cristal"] = mesh_from("alien_fragata_corazon_cristal", bm, "alien_cristal")
    bm, uvl = bm_new()   # el núcleo, en una cuna abierta de costillas
    ojo(bm, uvl, (0, -200.0, 0.0), 82.0, 82.0, 82.0, 24)
    M["nucleo"] = mesh_from("alien_fragata_nucleo", bm, "alien_cristal")
    bm, uvl = bm_new()
    for k in range(5):
        y = -270.0 + k * 35.0
        aro(bm, uvl, (0, y, 0), 90.0 - abs(k - 2) * 10.0, 7.0, 48, 8)
    M["cuerpo_costillas"] = mesh_from("alien_fragata_cuerpo_costillas", bm, "alien_placa")
    bm, uvl = bm_new()   # el anillo de popa
    aro(bm, uvl, (0, -470.0, 0), 250.0, 20.0, 96, 14)
    for k in range(4):
        a = math.pi / 4 + k * math.pi / 2
        d = Vector((math.cos(a), 0, math.sin(a)))
        cyl(bm, uvl, Vector((0, -470.0, 0)) + d * 70.0, Vector((0, -470.0, 0)) + d * 236.0, 9.0, 10)
    M["aro"] = mesh_from("alien_fragata_aro", bm, "alien_piel")
    bm, uvl = bm_new()
    aro(bm, uvl, (0, -470.0, 0), 250.0, 8.0, 96, 8)
    M["aro_cristal"] = mesh_from("alien_fragata_aro_cristal", bm, "alien_cristal")
    for lado, sx in (("babor", -1), ("estribor", 1)):
        bm, uvl = bm_new()
        rings = cuchilla(bm, uvl, (sx * 60.0, -320.0, 0.0), (sx * 330.0, 360.0, -20.0), 130.0, 28.0, 12)
        M[f"cuchilla_{lado}"] = mesh_from(f"alien_fragata_cuchilla_{lado}", bm, "alien_piel")
        bm, uvl = bm_new()
        filo(bm, uvl, rings, 5.0)
        M[f"filo_{lado}"] = mesh_from(f"alien_fragata_filo_{lado}", bm, "alien_cristal")
    bm, uvl = bm_new()
    for sx in (-1, 1):
        pts = [Vector((sx * w * 0.98, y, 0.0)) for y, w in ((-560.0, 72.0), (-450.0, 84.0), (-250.0, 70.0), (0.0, 56.0), (200.0, 48.0), (330.0, 40.0))]
        for a, b in zip(pts, pts[1:]):
            cyl(bm, uvl, a, b, 4.0, 6)
    M["cuerpo_vetas"] = mesh_from("alien_fragata_cuerpo_vetas", bm, "alien_cristal")
    guardar(root, "alien_fragata", M)
    return root

# ---------------------------------------------------------------- exportación
def exportar(coleccion_nombre, archivo):
    col = bpy.data.collections[coleccion_nombre]
    bpy.ops.object.select_all(action="DESELECT")
    for o in col.all_objects:
        o.select_set(True)
    bpy.ops.export_scene.gltf(filepath=os.path.join(DESTINO, archivo), export_format="GLTF_SEPARATE",
                              use_selection=True, export_texture_dir="texturas", export_yup=True,
                              export_apply=True, export_animations=False)
    print("Exportado", archivo)

materiales_combate()
torretas()
alien_dron()
alien_corveta()
alien_fragata()
if EXPORTAR:
    exportar("Torretas", "torretas.gltf")
    exportar("Alien_Dron", "alien_dron.gltf")
    exportar("Alien_Corveta", "alien_corveta.gltf")
    exportar("Alien_Fragata", "alien_fragata.gltf")
print("Torretas y naves alienígenas construidas")
