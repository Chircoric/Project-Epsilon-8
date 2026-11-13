# Dron minero y su bahía, para Nebulosa.
#
# Se ejecuta dentro de Blender con drones.blend abierto:
#     exec(open(r"...\construir_drones.py", encoding="utf-8").read())
# Rehace las colecciones Dron_Minero y Bahia_Drones. Usa las funciones de geometría del script de la
# estación (construir_estacion.py), sin construirla.
#
# Ejes de Blender, en metros: el dron mira a +Y (la proa) con +Z arriba, como la nave; la bahía se
# apoya en el origen con +Z hacia fuera del casco, como una ranura hp_ de la nave.

import bpy, math
from mathutils import Vector

SOLO_FUNCIONES = True
exec(open(r"C:\Users\Admin\Desktop\PROYECTO EPLSILON-0\EpsilonEngine\assets\modelos\estacion_origami\construir_estacion.py", encoding="utf-8").read())

MATS_EXTRA = ["escape", "celula_solar"]

def materiales_drones():
    materiales()
    falta = [m for m in ("escape",) if m not in bpy.data.materials]
    if falta:
        with bpy.data.libraries.load(SHIP_BLEND, link=False) as (src, dst):
            dst.materials = [m for m in src.materials if m in falta]

def coleccion(nombre):
    global COL
    old = bpy.data.collections.get(nombre)
    if old:
        for o in list(old.all_objects): bpy.data.objects.remove(o, do_unlink=True)
        bpy.data.collections.remove(old)
    COL = bpy.data.collections.new(nombre); bpy.context.scene.collection.children.link(COL)
    root = bpy.data.objects.new(nombre, None); COL.objects.link(root)
    return root

# ---------------------------------------------------------------- dron (unos 34 m de proa a popa)
def dron():
    root = coleccion("Dron_Minero")
    def ring_y(y, w, h, rad, z0=0.0):
        return [Vector((u, y, z0 + v)) for u, v in rrect(w, h, rad, 4)]
    # cuerpo
    bm, uvl = bm_new()
    secs = [(-13, 6.5, 5.5), (-12, 8.6, 7.0), (-10, 9.6, 7.8), (6, 9.6, 7.8), (9, 8.6, 7.0), (11, 6.8, 5.8)]
    loft(bm, uvl, [ring_y(y, w, h, 2.2, 1.0) for y, w, h in secs], tile=20.0)
    M = {"casco": mesh_from("dron_casco", bm, "casco_blanco")}
    # bodega de mineral, forrada, bajo el cuerpo
    bm, uvl = bm_new()
    cyl(bm, uvl, (0, -11, -5.0), (0, 5, -5.0), 5.2, 28)
    lathe = [circle((0, y, -5.0), (0, 1, 0), r, 28) for y, r in ((5, 5.2), (6.6, 4.4), (7.6, 2.6))]
    loft(bm, uvl, lathe, cap0=False, tile=6.0)
    M["bodega"] = mesh_from("dron_bodega", bm, "lamina_dorada")
    # estructura: bandas de la bodega, cabeza de minería, motores, patas, antena
    bm, uvl = bm_new()
    for y in (-10, -5, 0, 4):
        cyl(bm, uvl, (0, y - 0.5, -5.0), (0, y + 0.5, -5.0), 5.6, 28)
    cyl(bm, uvl, (0, 11, 1.0), (0, 13, 1.0), 3.6, 20)                 # cuello de la cabeza
    cyl(bm, uvl, (0, 13, 1.0), (0, 15, 1.0), 4.4, 20)                 # corona portabrocas
    cyl(bm, uvl, (0, 15, 1.0), (0, 22, 1.0), 3.4, 20, r1=0.5)         # broca
    for k in range(6):                                                # dientes de la corona
        a = k * math.pi / 3
        d = Vector((math.cos(a), 0, math.sin(a)))
        cyl(bm, uvl, Vector((0, 15, 1.0)) + d * 4.0, Vector((0, 17.5, 1.0)) + d * 2.6, 0.6, 6)
    for sx in (-1, 1):                                                # brazos de los láseres de corte
        cyl(bm, uvl, (sx * 4.2, 8, 3.2), (sx * 5.6, 14, 3.6), 0.7, 8)
        cyl(bm, uvl, (sx * 5.6, 14, 3.6), (sx * 5.6, 16.4, 3.6), 1.0, 12)
    for sx in (-1, 1):                                                # cuatro motores en las esquinas de popa
        for sz in (-1, 1):
            c = Vector((sx * 6.0, 0, 1.0 + sz * 3.6))
            cyl(bm, uvl, c + Vector((0, -11, 0)), c + Vector((0, -16, 0)), 2.0, 16)
            cyl(bm, uvl, c + Vector((0, -16, 0)), c + Vector((0, -18.5, 0)), 1.7, 16, r1=2.3)
            cyl(bm, uvl, c + Vector((-sx * 0.5, -9, 0)), c + Vector((-sx * 2.6, -9, 0)), 0.9, 8)
    for sx in (-1, 1):                                                # patas para posarse en un planeta
        for y in (-8, 3):
            top = Vector((sx * 4.0, y, -6.5)); foot = Vector((sx * 7.4, y, -11.5))
            cyl(bm, uvl, top, foot, 0.55, 8)
            cyl(bm, uvl, foot + Vector((0, 0, -0.2)), foot + Vector((0, 0, 0.6)), 1.6, 12)
    cyl(bm, uvl, (0, -4, 4.8), (0, -4, 7.5), 0.4, 8)                  # mástil de la antena
    lath = [circle((0, -4, 7.5 + dz), (0, 0, 1), r, 20) for dz, r in ((0, 0.4), (0.6, 2.4), (0.9, 2.6))]
    loft(bm, uvl, lath, tile=4.0)
    for sx in (-1, 1):                                                # brazos de los paneles
        cyl(bm, uvl, (sx * 4.6, -3, 2.0), (sx * 7.6, -3, 2.0), 0.5, 8)
    M["estructura"] = mesh_from("dron_estructura", bm, "metal_gris")
    bm, uvl = bm_new()
    for sx in (-1, 1):
        box(bm, uvl, (sx * 10.0, -3, 2.0), 2.5, 2.6, 0.12, tile=4.0)
    M["paneles"] = mesh_from("dron_paneles", bm, "panel_solar_celulas")
    bm, uvl = bm_new()
    for sx in (-1, 1):
        for sz in (-1, 1):
            c = Vector((sx * 6.0, 0, 1.0 + sz * 3.6))
            cyl(bm, uvl, c + Vector((0, -18.3, 0)), c + Vector((0, -18.6, 0)), 1.9, 16)
    M["toberas"] = mesh_from("dron_toberas", bm, "escape")
    bm, uvl = bm_new()
    for sx in (-1, 1):
        cyl(bm, uvl, (sx * 5.6, 16.3, 3.6), (sx * 5.6, 16.6, 3.6), 0.7, 12)
    M["laser"] = mesh_from("dron_laser", bm, "laser_lente")
    bm, uvl = bm_new()
    loft(bm, uvl, [ring_y(y, 9.8, 8.0, 2.3, 1.0) for y in (7.0, 8.0)], tile=4.0)
    M["aviso"] = mesh_from("dron_aviso", bm, "franja_aviso")
    bm, uvl = bm_new()
    box(bm, uvl, (0, 10.6, 4.6), 1.6, 0.4, 0.5, tile=1.0)
    for sx in (-1, 1): box(bm, uvl, (sx * 4.6, 10.2, -1.2), 0.5, 0.4, 0.5, tile=1.0)
    M["luces"] = mesh_from("dron_luces", bm, "luz_blanca")
    bm, uvl = bm_new()
    cyl(bm, uvl, (0, -12, 4.9), (0, -12, 6.0), 0.7, 10, r1=0.35)
    M["baliza"] = mesh_from("dron_baliza", bm, "luz_roja")
    for part, me in M.items():
        obj(f"dron_{part}", me, root)
    return root

# ---------------------------------------------------------------- bahía (sobre la base de torreta, de 26 m)
def bahia():
    root = coleccion("Bahia_Drones")
    H0, H1 = 3.0, 13.0
    bm, uvl = bm_new()
    lathe_z(bm, uvl, [(23.0, -1.5), (23.0, H0), (21.5, H0 + 0.6)], segs=48, tile=6.0)
    M = {"zocalo": mesh_from("bahia_zocalo", bm, "metal_gris")}
    bm, uvl = bm_new()   # hangar presurizado: losa con los cantos redondeados
    sec = rrect(40, 34, 6, 5)
    rings = [[Vector((u, v, z)) for u, v in sec] for z in (H0 - 0.5, H1 - 1.2)]
    rings.append([Vector((u * 0.95, v * 0.94, H1)) for u, v in sec])
    loft(bm, uvl, rings, tile=20.0)
    M["hangar"] = mesh_from("bahia_hangar", bm, "casco_blanco")
    bm, uvl = bm_new()   # cunas de lanzamiento, carriles, grúas y conductos
    for sx in (-13.0, 13.0):
        for sy in (-8.0, 3.0):
            box(bm, uvl, (sx, sy, H1 + 0.6), 8.2, 2.0, 0.6)          # cunas bajo las patas del dron
        for ex in (-3.5, 3.5):
            box(bm, uvl, (sx + ex, -2, H1 + 0.25), 0.4, 15.5, 0.25)  # carriles
        cyl(bm, uvl, (sx, 14.5, H1), (sx, 14.5, H1 + 9.0), 0.8, 10)  # pórtico de la grúa
        cyl(bm, uvl, (sx, 14.5, H1 + 9.0), (sx, 8.0, H1 + 9.0), 0.6, 8)
        box(bm, uvl, (sx, 8.0, H1 + 8.3), 1.2, 1.2, 0.8)
    for sx in (-1, 1):   # conductos de carga hacia el casco
        cyl(bm, uvl, (sx * 19.5, -14, H0), (sx * 19.5, -14, H1 - 2), 1.4, 12)
        cyl(bm, uvl, (sx * 19.5, 14, H0), (sx * 19.5, 14, H1 - 2), 1.4, 12)
    M["estructura"] = mesh_from("bahia_estructura", bm, "metal_gris")
    bm, uvl = bm_new()   # escotillas de descarga del mineral
    for sx in (-13.0, 13.0):
        box(bm, uvl, (sx, 13.5, H1 + 0.05), 5.0, 2.2, 0.15, tile=10.0)
    M["compuertas"] = mesh_from("bahia_compuertas", bm, "compuerta_carga")
    bm, uvl = bm_new()
    box(bm, uvl, (0, -16.8, H1 - 0.3), 18.0, 0.3, 0.6, tile=4.0)
    box(bm, uvl, (0, 16.8, H1 - 0.3), 18.0, 0.3, 0.6, tile=4.0)
    box(bm, uvl, (0, -2, H1 + 0.06), 0.8, 15.0, 0.1, tile=4.0)
    M["aviso"] = mesh_from("bahia_aviso", bm, "franja_aviso")
    bm, uvl = bm_new()
    for sx in (-19.0, 19.0):
        for sy in (-15.5, 15.5):
            box(bm, uvl, (sx, sy, H1 + 0.3), 0.5, 0.5, 0.35, tile=1.0)
    M["luces"] = mesh_from("bahia_luces", bm, "luz_verde")
    for part, me in M.items():
        obj(f"bahia_{part}", me, root)
    # Donde se posan los drones: nodos vacíos con la proa del dron hacia +Y y +Z arriba.
    for k, sx in enumerate((-13.0, 13.0)):
        e = bpy.data.objects.new(f"dron_{k+1}", None); COL.objects.link(e); e.parent = root
        e.location = (sx, 0.0, H1 + 1.2 + 11.7)
        e.empty_display_type = "ARROWS"; e.empty_display_size = 6
    return root

materiales_drones()
dron()
bahia()
print("Dron y bahía construidos")
