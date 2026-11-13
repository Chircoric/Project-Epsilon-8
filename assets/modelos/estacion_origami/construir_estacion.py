# Estación Origami (nivel 1) para Nebulosa.
#
# Se ejecuta dentro de Blender (con el .blend de la estación abierto):
#     exec(open(r"...\construir_estacion.py", encoding="utf-8").read())
# Borra la colección Estacion_Origami y la rehace entera, con la animación de despliegue.
#
# Mecanismo: 16 módulos rectos de largo fijo L unidos por 16 nudos (rótulas). Plegada, la
# estación es una corona en zigzag: los nudos pares arriba (+h) y los impares abajo (-h), sobre
# un círculo de radio R_PLEG. Al bajar h, el radio crece para que cada módulo conserve su largo
# (L² = (2·r·sin(π/N))² + (2h)²), hasta quedar plana como un anillo de radio R. No hay ningún
# estiramiento: el despliegue es cinemáticamente exacto, como un origami de un solo grado de libertad.
#
# Ejes de Blender: el anillo queda en el plano XY y su eje es Z (Y arriba en glTF). Metros reales.

import bpy, bmesh, math
from mathutils import Vector, Matrix, Quaternion

# ---------------------------------------------------------------- cifras
N = 16                                  # módulos y nudos
R = 470.0                               # radio de los nudos con el anillo desplegado
L = 2 * R * math.sin(math.pi / N)       # largo de un módulo, de nudo a nudo (183,4 m)
R_PLEG = 380.0                       # radio de los nudos plegada
H_PLEG = 0.5 * math.sqrt(L * L - (2 * R_PLEG * math.sin(math.pi / N)) ** 2)

F_INI, F0, F1 = 1, 30, 210              # quieta plegada, y despliegue del anillo
F2, F3, F_FIN = 220, 330, 360           # despliegue de los brazos

NUDO_R = 24.0                            # nudo: una rótula esférica de este radio, con cojinetes en los polos
POLO = 28.5                              # altura de las tapas de los polos
SEG_W, SEG_H, SEG_RAD = 44.0, 36.0, 11.0  # sección del módulo: ancho radial, alto, redondeo
BODY = 44.0                              # medio largo del tramo recto del módulo
DOME = 52.0                              # donde acaba el mamparo abombado: 15 m libres hasta la rótula
CUP_IN, CUP_OUT, CUP_ANG = 25.6, 27.6, 22.0  # copa del módulo sobre la rótula: radios y abertura (grados)
ESCLUSA_R = 10.5                         # puerta octogonal de la esclusa en cada extremo de módulo
RIEL_ANG = 14.0                          # rieles de la rótula, a cada lado del recorrido de la copa (grados)
# Plegados, los módulos salen del nudo a 36° sobre el ecuador: con 22° de abertura la copa llega a 58°
# y no toca los cojinetes de los polos, que empiezan a 59°.
UPPER, FORE = 150.0, 130.0               # brazo y antebrazo de los robots

SHIP_BLEND = r"C:\Users\Admin\Desktop\PROYECTO EPLSILON-0\EpsilonEngine\assets\modelos\nave_epsilon\nave_epsilon.blend"
MATS_NAVE = ["casco_blanco", "anillo_blanco", "metal_gris", "franja_aviso", "compuerta_carga",
             "ventana_luz", "luz_blanca", "luz_roja", "luz_verde", "lamina_dorada",
             "panel_solar_celulas", "franja_negra", "montaje_torreta"]

# ---------------------------------------------------------------- materiales
def materiales():
    falta = [m for m in MATS_NAVE if m not in bpy.data.materials]
    if falta:
        with bpy.data.libraries.load(SHIP_BLEND, link=False) as (src, dst):
            dst.materials = [m for m in src.materials if m in falta]
    def emisivo(nombre, color, fuerza):
        m = bpy.data.materials.get(nombre) or bpy.data.materials.new(nombre)
        m.use_nodes = True
        b = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
        b.inputs["Base Color"].default_value = (*color, 1)
        b.inputs["Emission Color"].default_value = (*color, 1)
        b.inputs["Emission Strength"].default_value = fuerza
        b.inputs["Roughness"].default_value = 0.4
        return m
    emisivo("metal_fundido", (1.0, 0.42, 0.08), 6.0)   # boquillas de las impresoras
    emisivo("laser_lente", (1.0, 0.08, 0.05), 8.0)     # lente de los láseres

# ---------------------------------------------------------------- geometría
def bm_new():
    bm = bmesh.new(); return bm, bm.loops.layers.uv.new("UVMap")

def _uv(f, uvl, uvs):
    for l, uv in zip(f.loops, uvs): l[uvl].uv = uv

def loft(bm, uvl, rings, cap0=True, cap1=True, tile=20.0, smooth=True):
    """Une anillos de puntos (mismo número) con quads y los cierra con abanicos."""
    n = len(rings[0])
    vr = [[bm.verts.new(p) for p in ring] for ring in rings]
    per = [0.0]
    for j in range(1, n + 1):
        per.append(per[-1] + (rings[0][j % n] - rings[0][j - 1]).length)
    along = [0.0]
    for i in range(1, len(rings)):
        c0 = sum(rings[i - 1], Vector()) / n; c1 = sum(rings[i], Vector()) / n
        along.append(along[-1] + max((c1 - c0).length, 1e-3))
    for i in range(len(rings) - 1):
        for j in range(n):
            j2 = (j + 1) % n
            f = bm.faces.new([vr[i][j], vr[i][j2], vr[i + 1][j2], vr[i + 1][j]])
            f.smooth = smooth
            _uv(f, uvl, [(per[j] / tile, along[i] / tile), (per[j + 1] / tile, along[i] / tile),
                         (per[j + 1] / tile, along[i + 1] / tile), (per[j] / tile, along[i + 1] / tile)])
    def cap(vs):
        c = bm.verts.new(sum((v.co for v in vs), Vector()) / len(vs))
        nrm = (vs[1].co - vs[0].co).cross(vs[2].co - vs[0].co)
        ax = max(range(3), key=lambda k: abs(nrm[k]))
        def p(v): co = v.co; return [(co.y, co.z), (co.x, co.z), (co.x, co.y)][ax]
        for j in range(n):
            a, b = vs[j], vs[(j + 1) % n]
            f = bm.faces.new([c, a, b]); f.smooth = False
            _uv(f, uvl, [(q[0] / tile, q[1] / tile) for q in (p(c), p(a), p(b))])
    if cap0: cap(vr[0])
    if cap1: cap(vr[-1])

def rrect(w, h, rad, n=4):
    """Rectángulo redondeado (w × h) como lista de (u, v), en sentido antihorario."""
    pts = []
    for cu, cv, a0 in ((w/2-rad, h/2-rad, 0), (-w/2+rad, h/2-rad, 90), (-w/2+rad, -h/2+rad, 180), (w/2-rad, -h/2+rad, 270)):
        for k in range(n + 1):
            a = math.radians(a0 + 90 * k / n)
            pts.append((cu + rad * math.cos(a), cv + rad * math.sin(a)))
    return pts

def base_of(d):
    d = Vector(d).normalized()
    up = Vector((0, 0, 1)) if abs(d.z) < 0.9 else Vector((1, 0, 0))
    u = d.cross(up).normalized(); return u, d.cross(u)

def circle(c, d, r, segs, rot=0.0):
    u, v = base_of(d); c = Vector(c)
    return [c + (u * math.cos(rot + 2*math.pi*k/segs) + v * math.sin(rot + 2*math.pi*k/segs)) * r for k in range(segs)]

def cyl(bm, uvl, a, b, r, segs=16, tile=6.0, r1=None, caps=True, rot=0.0, smooth=True):
    a = Vector(a); b = Vector(b)
    loft(bm, uvl, [circle(a, b - a, r, segs, rot), circle(b, b - a, r if r1 is None else r1, segs, rot)],
         cap0=caps, cap1=caps, tile=tile, smooth=smooth and segs > 6)

def obox(bm, uvl, c, ax, ay, hx, hy, hz, tile=6.0):
    """Caja orientada: centro, eje X y eje Y (Z = X × Y), medias medidas."""
    c = Vector(c); ax = Vector(ax).normalized(); ay = Vector(ay)
    ay = (ay - ax * ax.dot(ay)).normalized(); az = ax.cross(ay)
    r0 = [c - ax*hx + ay*sy*hy + az*sz*hz for sy, sz in ((1, 1), (-1, 1), (-1, -1), (1, -1))]
    loft(bm, uvl, [r0, [p + ax * 2 * hx for p in r0]], tile=tile, smooth=False)

def box(bm, uvl, c, hx, hy, hz, tile=6.0):
    obox(bm, uvl, c, (1, 0, 0), (0, 1, 0), hx, hy, hz, tile)

def lathe_z(bm, uvl, prof, segs=48, c=(0, 0, 0), tile=20.0):
    c = Vector(c)
    rings = [[c + Vector((r * math.cos(2*math.pi*k/segs), r * math.sin(2*math.pi*k/segs), z)) for k in range(segs)] for r, z in prof]
    loft(bm, uvl, rings, tile=tile)

COL = None
def mesh_from(name, bm, mat):
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    for e in bm.edges:   # bordes vivos por ángulo, para el sombreado
        if len(e.link_faces) == 2 and e.calc_face_angle(0) > math.radians(35): e.smooth = False
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    me.materials.append(bpy.data.materials[mat])
    return me

def obj(name, data, parent, loc=(0, 0, 0), rot=(0, 0, 0), mode="XYZ"):
    o = bpy.data.objects.new(name, data); COL.objects.link(o); o.parent = parent
    o.location = loc; o.rotation_mode = mode; o.rotation_euler = rot
    return o

# ---------------------------------------------------------------- módulo del anillo
# Ejes locales: X a lo largo (de nudo a nudo), Y hacia el centro del anillo, Z según el eje.
# Origen en el punto medio entre sus dos nudos; la cara exterior está en y = -SEG_W/2.
def mallas_modulo():
    M = {}
    W2, H2 = SEG_W / 2, SEG_H / 2
    sec = rrect(SEG_W, SEG_H, SEG_RAD, 5)
    def ring(x, s=1.0, dw=0.0):
        return [Vector((x, u * s + math.copysign(dw, u), v * s + math.copysign(dw, v))) for u, v in sec]
    # casco presurizado: tramo recto hasta ±BODY y un mamparo abombado hasta ±DOME, lejos del nudo
    bm, uvl = bm_new()
    perfil = [(-DOME, .70), (-DOME + 1.4, .80), (-DOME + 3.2, .89), (-DOME + 5.4, .96), (-BODY, 1)]  # cara plana para la esclusa
    xs = perfil + [(-BODY + 11 * k, 1) for k in range(1, 8)] + [(-x, s) for x, s in reversed(perfil)]
    loft(bm, uvl, [ring(x, s) for x, s in xs], tile=80.0)
    M["casco"] = mesh_from("modulo_casco", bm, "casco_blanco")

    # estructura metálica: cuadernas, cuellos con fuelle, copas de las rótulas, amortiguadores,
    # viga de servicio, tuberías, soportes
    bm, uvl = bm_new()
    for x in (-33, -22, -11, 0, 11, 22, 33):
        loft(bm, uvl, [ring(x - 1.2, 1, 0.9), ring(x + 1.2, 1, 0.9)], tile=6.0)
    for x in (-BODY, BODY):
        loft(bm, uvl, [ring(x - 1.8, 1, 1.4), ring(x + 1.8, 1, 1.4)], tile=6.0)
    half = L / 2   # centro del nudo, en el eje del módulo
    door_bm, door_uvl = bm_new()      # las hojas de la esclusa (compuerta_carga)
    frame_bm, frame_uvl = bm_new()    # su marco de franjas de aviso
    lamp_bm, lamp_uvl = bm_new()      # luces verdes de la esclusa
    X = Vector((1, 0, 0))
    for sgn in (-1, 1):
        # Esclusa en el mamparo: una puerta octogonal entre el módulo y el túnel del cuello, con marco
        # de franjas, cerrojos y dos luces. Separa cada módulo de su nudo, como la esclusa de la nave.
        face = sgn * DOME
        loft(door_bm, door_uvl, [circle((face - sgn * 0.3, 0, 0), X, ESCLUSA_R, 8, math.pi / 8), circle((face + sgn * 0.35, 0, 0), X, ESCLUSA_R, 8, math.pi / 8)], tile=2 * ESCLUSA_R, smooth=False)
        r_mid = ESCLUSA_R + 0.85   # marco: ocho barras alrededor de la puerta, sin taparla
        for k in range(8):
            a = k * math.pi / 4
            rd = Vector((0, math.cos(a), math.sin(a)))
            obox(frame_bm, frame_uvl, Vector((face + sgn * 0.15, 0, 0)) + rd * (r_mid * math.cos(math.pi / 8)), rd, (1, 0, 0), 0.85, 0.45, r_mid * math.sin(math.pi / 8) + 0.4, tile=4.0)
        for k in range(4):   # cerrojos en el marco
            a = math.pi / 4 + k * math.pi / 2
            rd = Vector((0, math.cos(a), math.sin(a)))
            obox(bm, uvl, Vector((face + sgn * 0.8, 0, 0)) + rd * (ESCLUSA_R + 0.9), rd, (1, 0, 0), 1.1, 0.6, 1.6)
        for sy in (-1, 1):
            box(lamp_bm, lamp_uvl, (face + sgn * 0.5, sy * (ESCLUSA_R + 3.2), 0), 0.4, 0.6, 0.6, tile=1.0)
        # Cuello con fuelle: el túnel flexible donde dobla el origami. Sale de la puerta y entra un
        # metro en la copa; a medio camino lleva un anillo rígido con sus abrazaderas.
        x_end = half - CUP_OUT + 1.0
        n = int((x_end - DOME) / 1.1)
        rings = [circle((sgn * (DOME + (x_end - DOME) * k / n), 0, 0), X, 6.0 if k % 2 == 0 else 7.0, 24) for k in range(n + 1)]
        loft(bm, uvl, rings, tile=6.0)
        mid_x = sgn * (DOME + x_end) / 2
        cyl(bm, uvl, (mid_x - 1.2, 0, 0), (mid_x + 1.2, 0, 0), 8.2, 24)
        for k in range(4):
            a = k * math.pi / 2
            rd = Vector((0, math.cos(a), math.sin(a)))
            obox(bm, uvl, Vector((mid_x, 0, 0)) + rd * 8.6, rd, (1, 0, 0), 0.9, 1.8, 1.0)
        # Copa: un casquete esférico concéntrico con la rótula del nudo, 1,6 m por fuera de ella. La
        # lleva el módulo; la rótula gira dentro sin tocarla, se pliegue como se pliegue.
        center = Vector((sgn * half, 0, 0)); toward = Vector((-sgn, 0, 0))
        def cap_ring(radius, phi):
            return circle(center + toward * (radius * math.cos(math.radians(phi))), toward, radius * math.sin(math.radians(phi)), 32)
        phis = (3, 8, 14, 20, CUP_ANG)
        loft(bm, uvl, [cap_ring(CUP_OUT, p) for p in phis] + [cap_ring(CUP_IN, p) for p in reversed(phis)], tile=8.0)
        # Pasadores de cierre en el borde de la copa.
        for k in range(8):
            a = 2 * math.pi * (k + 0.5) / 8
            dirv = (toward * math.cos(math.radians(CUP_ANG - 2)) + Vector((0, math.cos(a), math.sin(a))) * math.sin(math.radians(CUP_ANG - 2))).normalized()
            cyl(bm, uvl, center + dirv * (CUP_OUT - 0.3), center + dirv * (CUP_OUT + 1.6), 0.9, 8)
        # Patines: la copa corre sobre los dos rieles de la rótula. Se mueve siempre en el plano XZ del
        # módulo, así que los rieles van a ±RIEL_ANG hacia Y y los patines, debajo de ellos.
        for sy in (-1, 1):
            radial = (toward * math.cos(math.radians(RIEL_ANG)) + Vector((0, sy, 0)) * math.sin(math.radians(RIEL_ANG))).normalized()
            obox(bm, uvl, center + radial * (CUP_IN - 0.18), (0, 0, 1), radial, 3.0, 0.18, 1.2)
            for sz in (-1, 1):   # pernos que lo sujetan a la copa
                cyl(bm, uvl, center + radial * (CUP_IN - 0.1) + Vector((0, 0, sz * 2.2)), center + radial * (CUP_OUT + 0.5) + Vector((0, 0, sz * 2.2)), 0.35, 6)
        # Cuatro amortiguadores, del mamparo al borde de la copa: cilindro, vástago y dos ojos.
        rim = math.radians(CUP_ANG - 3)
        for sy in (-1, 1):
            for sz in (-1, 1):
                base = Vector((sgn * (DOME - 5.0), sy * 12.0, sz * 10.0))
                tip = center + toward * (CUP_OUT * math.cos(rim)) + Vector((0, sy, sz)).normalized() * (CUP_OUT * math.sin(rim))
                mid = base + (tip - base) * 0.58
                cyl(bm, uvl, base, mid, 1.3, 10)
                cyl(bm, uvl, mid, tip, 0.65, 8)
                for p in (base, tip):
                    cyl(bm, uvl, p - Vector((0, 0, 1.0)), p + Vector((0, 0, 1.0)), 1.5, 10)
    yo = -W2 - 0.6   # viga de servicio de celosía triangular, en la cara exterior
    cs = [Vector((0, yo, 5.2)), Vector((0, yo, -5.2)), Vector((0, yo - 9.0, 0))]
    x0, x1, paso = -42.0, 42.0, 7.0
    for c in cs: cyl(bm, uvl, c + Vector((x0, 0, 0)), c + Vector((x1, 0, 0)), 0.8, 6)
    k = 0; x = x0
    while x < x1 - 1e-3:
        for j in range(3):
            a = cs[j] + Vector((x, 0, 0)); b = cs[(j + 1) % 3] + Vector((x, 0, 0))
            cyl(bm, uvl, a, b, 0.45, 6)
            cyl(bm, uvl, a, cs[(j + 1) % 3] + Vector((x + paso, 0, 0)), 0.4, 6)
        x += paso; k += 1
    for j in range(3):
        cyl(bm, uvl, cs[j] + Vector((x1, 0, 0)), cs[(j + 1) % 3] + Vector((x1, 0, 0)), 0.45, 6)
    for xx in (-38.5, -24.5, -10.5, 3.5, 17.5, 31.5):   # placas de apoyo de la viga sobre el casco
        box(bm, uvl, (xx, -W2 + 0.1, 0), 1.6, 0.6, 7.0)
    cu = W2 - SEG_RAD + SEG_RAD * 0.7071; cv = H2 - SEG_RAD + SEG_RAD * 0.7071
    for sv in (1, -1):   # tuberías en los cantos exteriores, con abrazaderas
        p = Vector((0, -cu - 1.5, sv * (cv + 1.5)))
        cyl(bm, uvl, p + Vector((-43, 0, 0)), p + Vector((43, 0, 0)), 1.1, 10)
        q = Vector((0, -cu - 0.9, sv * (cv + 0.9)))
        cyl(bm, uvl, q + Vector((-43, 0, 0)), q + Vector((43, 0, 0)), 0.6, 8)
    for x in (-32, 32):   # postes del panel solar (+Z) y del radiador (-Z)
        for y in (-8, 8):
            cyl(bm, uvl, (x, y, H2 - 0.5), (x, y, H2 + 3.0), 0.8, 8)
            cyl(bm, uvl, (x, y, -H2 + 0.5), (x, y, -H2 - 2.7), 0.8, 8)
    box(bm, uvl, (0, 0, H2 + 2.6), 38, 0.9, 0.5)   # largueros bajo el panel y el radiador
    box(bm, uvl, (0, 0, -H2 - 2.3), 38, 0.9, 0.5)
    M["estructura"] = mesh_from("modulo_estructura", bm, "metal_gris")

    # ventanas del habitáculo, en la cara que mira al centro, entre cuadernas
    bm, uvl = bm_new()
    for bx in (-38.5, -27.5, -16.5, -5.5, 5.5, 16.5, 27.5, 38.5):
        for ox in (-3.3, -1.1, 1.1, 3.3):
            for z in (3.2, -3.2):
                box(bm, uvl, (bx + ox, W2 + 0.02, z), 0.9, 0.25, 0.8, tile=2.4)
    M["ventanas"] = mesh_from("modulo_ventanas", bm, "ventana_luz")

    bm, uvl = bm_new()   # panel solar
    box(bm, uvl, (0, 0, H2 + 3.35), 40, 14, 0.25, tile=8.0)
    M["panel"] = mesh_from("modulo_panel", bm, "panel_solar_celulas")

    bm, uvl = bm_new()   # radiador con costillas
    box(bm, uvl, (0, 0, -H2 - 3.1), 40, 15, 0.3, tile=20.0)
    for k in range(11):
        box(bm, uvl, (-37.5 + 7.5 * k, 0, -H2 - 3.7), 0.4, 15, 0.35, tile=6.0)
    M["radiador"] = mesh_from("modulo_radiador", bm, "anillo_blanco")

    bm, uvl = bm_new()   # luces de trabajo junto a las cuadernas de los extremos
    for x in (-40, 40):
        for z in (9.5, -9.5):
            box(bm, uvl, (x, W2 + 0.3, z), 0.5, 0.4, 0.5, tile=1.0)
    M["luces"] = mesh_from("modulo_luces", bm, "luz_blanca")
    M["esclusa"] = mesh_from("modulo_esclusa", door_bm, "compuerta_carga")
    M["esclusa_aviso"] = mesh_from("modulo_esclusa_aviso", frame_bm, "franja_aviso")
    M["esclusa_luces"] = mesh_from("modulo_esclusa_luces", lamp_bm, "luz_verde")
    return M

# ---------------------------------------------------------------- nudos
# Ejes locales: X tangente, Y hacia el centro, Z según el eje. Origen en el centro del nudo.
def mallas_nudo():
    M = {}
    h = POLO - 2.0   # las balizas van sobre la tapa del polo
    bm, uvl = bm_new()
    # Rótula: una esfera. Los módulos la abrazan con sus copas desde cualquier ángulo.
    prof = [(NUDO_R * math.cos(math.radians(a)), NUDO_R * math.sin(math.radians(a))) for a in range(-80, 81, 8)]
    lathe_z(bm, uvl, prof, segs=48, tile=80.0)
    M["nucleo"] = mesh_from("nudo_nucleo", bm, "casco_blanco")
    bm, uvl = bm_new()   # bandas de rodadura, cojinetes de los polos y sus tapas
    for lat in (0.0, 25.0, -25.0):   # bandas de rodadura: casquetes de 6° que siguen la esfera, 0,45 m por fuera
        arc = [math.radians(lat + d) for d in (-3.0, -1.0, 1.0, 3.0)]
        prof = [((NUDO_R + 0.45) * math.cos(a), (NUDO_R + 0.45) * math.sin(a)) for a in arc]
        prof += [((NUDO_R - 0.4) * math.cos(a), (NUDO_R - 0.4) * math.sin(a)) for a in reversed(arc)]
        lathe_z(bm, uvl, prof, segs=48, tile=6.0)
    for s in (1, -1):
        lathe_z(bm, uvl, [(13.0, s * 19.0), (13.0, s * (POLO - 1.5)), (15.0, s * (POLO - 1.5)), (15.0, s * POLO), (9.0, s * (POLO + 1.0))], segs=40, tile=6.0)
        for k in range(12):   # pernos de la brida
            a = 2 * math.pi * k / 12
            cyl(bm, uvl, (14.2 * math.cos(a), 14.2 * math.sin(a), s * POLO), (14.2 * math.cos(a), 14.2 * math.sin(a), s * (POLO + 0.7)), 0.5, 6)
    M["rotula"] = mesh_from("nudo_rotula", bm, "metal_gris")

    # Rieles: la copa de cada módulo corre sobre la rótula por un arco de círculo máximo, de la pose
    # plegada (36° sobre o bajo el ecuador) a la abierta, siempre en el plano del módulo. Lleva dos
    # rieles a ±RIEL_ANG de ese arco, con una franja de aviso por fuera de cada uno y topes en los
    # extremos. Los nudos pares (arriba plegados) y los impares (abajo) son simétricos: un juego de
    # mallas para cada uno.
    margin = math.radians(7.0)
    Vf, Vd = vertices(H_PLEG), vertices(0.0)
    for parity in (0, 1):
        rb, ruvl = bm_new()
        ab, auvl = bm_new()
        rot = Matrix.Rotation(-(2 * math.pi * parity / N + math.pi / 2), 3, 'Z')   # del mundo al nudo
        for nb in (parity + 1, parity - 1):
            d0 = (rot @ (Vf[nb % N] - Vf[parity])).normalized()   # hacia el módulo, plegada
            d1 = (rot @ (Vd[nb % N] - Vd[parity])).normalized()   # abierta
            nrm = d1.cross(d0).normalized()
            e1 = d1; e2 = nrm.cross(e1).normalized()
            phi0 = math.atan2(d0.dot(e2), d0.dot(e1))
            # Siguen 30° más allá de la pose abierta y 15° más allá de la plegada (sin llegar a los
            # cojinetes de los polos), para que se vean por fuera de la copa, que cubre 22° a cada lado.
            beyond_open, beyond_folded = math.radians(30.0), math.radians(15.0)
            lo = (phi0 - beyond_folded) if phi0 < 0.0 else -beyond_open
            hi = beyond_open if phi0 < 0.0 else (phi0 + beyond_folded)
            def arc(alpha, r0, r1, hw, bmx, uvx, steps=28):
                rings = []
                for k in range(steps + 1):
                    phi = lo + (hi - lo) * k / steps
                    c = (e1 * math.cos(phi) + e2 * math.sin(phi)) * math.cos(alpha) + nrm * math.sin(alpha)
                    tan = -e1 * math.sin(phi) + e2 * math.cos(phi)
                    w = c.cross(tan).normalized()
                    rings.append([c * r0 - w * hw, c * r1 - w * hw, c * r1 + w * hw, c * r0 + w * hw])
                loft(bmx, uvx, rings, tile=6.0, smooth=False)
            for s in (-1, 1):
                arc(s * math.radians(RIEL_ANG), NUDO_R - 0.5, NUDO_R + 1.15, 1.0, rb, ruvl)
                arc(s * math.radians(RIEL_ANG + 4.5), NUDO_R - 0.05, NUDO_R + 0.35, 0.9, ab, auvl)
            for phi in (lo, hi):   # topes de fin de carrera: una barra curva sobre la esfera, de riel a riel
                tan = -e1 * math.sin(phi) + e2 * math.cos(phi)
                rings = []
                span = math.radians(RIEL_ANG + 2.0)
                for k in range(9):
                    alpha = -span + 2 * span * k / 8
                    c = (e1 * math.cos(phi) + e2 * math.sin(phi)) * math.cos(alpha) + nrm * math.sin(alpha)
                    rings.append([c * (NUDO_R - 0.5) - tan * 0.8, c * (NUDO_R + 1.1) - tan * 0.8, c * (NUDO_R + 1.1) + tan * 0.8, c * (NUDO_R - 0.5) + tan * 0.8])
                loft(rb, ruvl, rings, tile=4.0, smooth=False)
        M[f"rieles_{parity}"] = mesh_from(f"nudo_rieles_{parity}", rb, "metal_gris")
        M[f"rieles_aviso_{parity}"] = mesh_from(f"nudo_rieles_aviso_{parity}", ab, "franja_aviso")
    bm, uvl = bm_new()   # mástil de la baliza
    cyl(bm, uvl, (0, 0, h + 2.0), (0, 0, h + 10.0), 0.7, 8)
    cyl(bm, uvl, (0, 0, h + 10.0), (0, 0, h + 11.0), 1.4, 12)
    M["baliza_mastil"] = mesh_from("nudo_baliza_mastil", bm, "metal_gris")
    bm, uvl = bm_new()
    cyl(bm, uvl, (0, 0, h + 11.0), (0, 0, h + 13.2), 1.2, 12, r1=0.6)
    M["baliza"] = mesh_from("nudo_baliza", bm, "luz_blanca")
    bm, uvl = bm_new()
    cyl(bm, uvl, (0, 0, h + 11.0), (0, 0, h + 13.2), 1.2, 12, r1=0.6)
    M["baliza_roja"] = mesh_from("nudo_baliza_roja", bm, "luz_roja")

    # esclusa de atraque, como la de la nave: módulo, marco de franjas, collarín, compuerta y garras
    ye = -40.0
    bm, uvl = bm_new()
    sec = rrect(34, 34, 6, 4)
    loft(bm, uvl, [[Vector((u, y, v)) for u, v in sec] for y in (-2, -36)], tile=20.0)  # nace dentro de la esfera
    loft(bm, uvl, [[Vector((u * 0.94, y, v * 0.94)) for u, v in sec] for y in (-36, ye)], tile=20.0)
    M["esclusa"] = mesh_from("nudo_esclusa", bm, "casco_blanco")
    bm, uvl = bm_new()
    box(bm, uvl, (0, ye - 0.3, 15.0), 17.0, 0.5, 1.3, tile=4.0)
    box(bm, uvl, (0, ye - 0.3, -15.0), 17.0, 0.5, 1.3, tile=4.0)
    box(bm, uvl, (15.0, ye - 0.3, 0), 1.3, 0.5, 13.7, tile=4.0)
    box(bm, uvl, (-15.0, ye - 0.3, 0), 1.3, 0.5, 13.7, tile=4.0)
    M["esclusa_aviso"] = mesh_from("nudo_esclusa_aviso", bm, "franja_aviso")
    bm, uvl = bm_new()
    d = Vector((0, -1, 0))
    loft(bm, uvl, [circle((0, y, 0), d, 10.2, 8, math.pi / 8) for y in (ye + 0.5, ye - 2.6)], tile=6.0, smooth=False)
    for k in range(4):
        a = math.pi / 4 + k * math.pi / 2
        rd = Vector((math.cos(a), 0, math.sin(a)))
        obox(bm, uvl, rd * 12.6 + Vector((0, ye - 1.4, 0)), rd, (0, 1, 0), 1.9, 1.5, 1.3)
        obox(bm, uvl, rd * 11.0 + Vector((0, ye - 2.6, 0)), rd, (0, 1, 0), 1.6, 0.6, 1.0)
    box(bm, uvl, (0, ye - 0.15, 12.6), 6.0, 0.4, 0.5)
    box(bm, uvl, (0, ye - 0.15, -12.6), 6.0, 0.4, 0.5)
    M["esclusa_collar"] = mesh_from("nudo_esclusa_collar", bm, "metal_gris")
    bm, uvl = bm_new()
    loft(bm, uvl, [circle((0, y, 0), d, 8.7, 8, math.pi / 8) for y in (ye - 2.0, ye - 2.9)], tile=17.4, smooth=False)
    M["esclusa_compuerta"] = mesh_from("nudo_esclusa_compuerta", bm, "compuerta_carga")
    bm, uvl = bm_new()
    for sx in (-15.0, 15.0):
        for sz in (-15.0, 15.0):
            box(bm, uvl, (sx, ye - 0.9, sz), 0.6, 0.4, 0.6, tile=1.0)
    M["esclusa_luces"] = mesh_from("nudo_esclusa_luces", bm, "luz_verde")

    # base del brazo: pedestal y tolva de material (lo que gira va aparte)
    bm, uvl = bm_new()
    cyl(bm, uvl, (0, 18, 0), (0, 30, 0), 9.0, 32)
    cyl(bm, uvl, (0, 26.5, 0), (0, 28.2, 0), 11.5, 32)
    for k in range(16):
        a = 2 * math.pi * k / 16
        cyl(bm, uvl, (10.6 * math.cos(a), 28.2, 10.6 * math.sin(a)), (10.6 * math.cos(a), 29.0, 10.6 * math.sin(a)), 0.5, 6)
    M["pedestal"] = mesh_from("nudo_pedestal", bm, "metal_gris")
    bm, uvl = bm_new()   # tolva: aquí entra el material para imprimir
    obox(bm, uvl, (0, 17.0, 16.0), (1, 0, 0), (0, 1, 0), 8.0, 4.5, 4.5)   # media caja dentro de la esfera
    sec = rrect(12, 5, 1.2, 3)
    loft(bm, uvl, [[Vector((u, 19.0 + v, 20.0)) for u, v in sec], [Vector((u * 1.3, 19.5 + v * 1.3, 22.5)) for u, v in sec]], tile=6.0)
    M["tolva"] = mesh_from("nudo_tolva", bm, "anillo_blanco")
    bm, uvl = bm_new()
    box(bm, uvl, (0, 21.6, 13.5), 6.5, 0.2, 1.0, tile=4.0)
    M["tolva_aviso"] = mesh_from("nudo_tolva_aviso", bm, "franja_aviso")
    return M

# ---------------------------------------------------------------- brazos robóticos
def mallas_brazo():
    M = {}
    bm, uvl = bm_new()   # giro: torreta y horquilla del hombro (gira sobre Y)
    cyl(bm, uvl, (0, 0, 0), (0, 3.6, 0), 8.0, 32)
    for sx in (-7.2, 7.2):
        obox(bm, uvl, (sx, 6.5, 0), (1, 0, 0), (0, 1, 0), 0.9, 4.0, 6.0)
    M["giro"] = mesh_from("brazo_giro", bm, "metal_gris")

    # brazo (en el marco del hombro: a lo largo de +Y, gira sobre X)
    bm, uvl = bm_new()
    rings = []
    for y, w, h in ((5, 9, 8), (10, 10, 8.5), (UPPER - 12, 8, 6.5), (UPPER - 6, 7, 6)):
        rings.append([Vector((u, y, v)) for u, v in rrect(w, h, 1.6, 3)])
    loft(bm, uvl, rings, tile=20.0)
    M["brazo"] = mesh_from("brazo_brazo", bm, "anillo_blanco")
    bm, uvl = bm_new()
    cyl(bm, uvl, (-6.3, 0, 0), (6.3, 0, 0), 5.6, 24)                 # eje del hombro
    cyl(bm, uvl, (-6.0, UPPER, 0), (16.5, UPPER, 0), 5.6, 24)        # eje del codo
    for sx in (-4.0, 4.0):
        obox(bm, uvl, (sx, UPPER - 4, 0), (1, 0, 0), (0, 1, 0), 0.8, 6.0, 4.5)
    cyl(bm, uvl, (0, 12, 5.0), (0, 96, 5.0), 1.7, 12)                # actuador
    cyl(bm, uvl, (0, 96, 5.0), (0, UPPER - 10, 4.5), 1.0, 10)
    box(bm, uvl, (0, 12, 4.4), 2.0, 2.0, 1.2); box(bm, uvl, (0, UPPER - 10, 3.9), 1.6, 1.6, 1.0)
    for k in range(9):   # bandas de refuerzo
        y = 20 + k * 14
        w = 10 - 2 * (y - 10) / (UPPER - 22); hh = 8.5 - 2 * (y - 10) / (UPPER - 22)
        loft(bm, uvl, [[Vector((u, yy, v)) for u, v in rrect(w + 0.6, hh + 0.6, 1.8, 3)] for yy in (y - 0.6, y + 0.6)], tile=4.0)
    M["brazo_estructura"] = mesh_from("brazo_brazo_estructura", bm, "metal_gris")
    bm, uvl = bm_new()   # manguera de material, forrada
    cyl(bm, uvl, (-5.6, 8, -2.0), (-4.6, UPPER - 8, -1.6), 1.25, 12)
    M["brazo_manguera"] = mesh_from("brazo_brazo_manguera", bm, "lamina_dorada")

    # antebrazo (en el marco del codo: a lo largo de +Y, desplazado a x = 10)
    bm, uvl = bm_new()
    rings = [[Vector((10 + u, y, v)) for u, v in rrect(w, h, 1.3, 3)] for y, w, h in ((5, 7, 6), (FORE - 8, 6, 5))]
    loft(bm, uvl, rings, tile=20.0)
    M["antebrazo"] = mesh_from("brazo_antebrazo", bm, "anillo_blanco")
    bm, uvl = bm_new()
    cyl(bm, uvl, (5.8, FORE, 0), (14.2, FORE, 0), 4.2, 20)           # eje de la muñeca
    for sx in (6.6, 13.4):
        obox(bm, uvl, (sx, FORE - 3.5, 0), (1, 0, 0), (0, 1, 0), 0.6, 4.5, 3.4)
    cyl(bm, uvl, (10, 10, 3.9), (10, 80, 3.9), 1.2, 10)
    cyl(bm, uvl, (10, 80, 3.9), (10, FORE - 9, 3.4), 0.7, 8)
    cyl(bm, uvl, (14.0, 6, -1.0), (13.4, FORE - 6, -1.0), 0.9, 10)   # cables
    M["antebrazo_estructura"] = mesh_from("brazo_antebrazo_estructura", bm, "metal_gris")

    # impresora 3D (en el marco de la muñeca, a lo largo de +Y)
    bm, uvl = bm_new()
    cyl(bm, uvl, (0, 0, 0), (0, 6, 0), 4.0, 24)
    box(bm, uvl, (0, 13, 0), 6.0, 7.0, 5.0)
    for sx in (-7.6, 7.6):   # bobinas de material de aporte
        cyl(bm, uvl, (sx, 8, 0), (sx, 18, 0), 2.6, 16)
    M["impresora"] = mesh_from("brazo_impresora", bm, "anillo_blanco")
    bm, uvl = bm_new()
    for sx in (-7.6, 7.6):
        cyl(bm, uvl, (sx, 7.2, 0), (sx, 8.0, 0), 3.0, 16)
        cyl(bm, uvl, (sx, 18.0, 0), (sx, 18.8, 0), 3.0, 16)
    noz = [(0, 0)] + [(2.6 * math.cos(a), 2.6 * math.sin(a)) for a in [k * math.pi / 3 for k in range(6)]]
    for nx, nz in noz:
        cyl(bm, uvl, (nx, 20, nz), (nx, 23.2, nz), 0.8, 10, r1=0.35)
    box(bm, uvl, (0, 18.5, 5.8), 2.0, 1.5, 0.8)                     # cámara de control
    cyl(bm, uvl, (0, 19.9, 5.8), (0, 20.6, 5.8), 0.6, 10)
    M["impresora_estructura"] = mesh_from("brazo_impresora_estructura", bm, "metal_gris")
    bm, uvl = bm_new()
    for nx, nz in noz:
        cyl(bm, uvl, (nx, 23.2, nz), (nx, 23.9, nz), 0.35, 8, r1=0.15)
    M["impresora_boquillas"] = mesh_from("brazo_impresora_boquillas", bm, "metal_fundido")

    # láser de corte y soldadura
    bm, uvl = bm_new()
    cyl(bm, uvl, (0, 0, 0), (0, 6, 0), 4.0, 24)
    cyl(bm, uvl, (0, 6, 0), (0, 22, 0), 4.4, 24)
    M["laser"] = mesh_from("brazo_laser", bm, "anillo_blanco")
    bm, uvl = bm_new()
    for k in range(7):   # aletas de refrigeración
        y = 8 + k * 2.0
        cyl(bm, uvl, (0, y, 0), (0, y + 0.6, 0), 6.2, 24)
    cyl(bm, uvl, (0, 22, 0), (0, 29.5, 0), 2.3, 20, r1=1.9)
    cyl(bm, uvl, (0, 29.5, 0), (0, 30.4, 0), 2.4, 20)
    box(bm, uvl, (5.0, 15, 0), 0.8, 6.0, 1.6)                       # conducto del refrigerante
    M["laser_estructura"] = mesh_from("brazo_laser_estructura", bm, "metal_gris")
    bm, uvl = bm_new()
    cyl(bm, uvl, (0, 30.3, 0), (0, 30.7, 0), 1.6, 20)
    M["laser_lente"] = mesh_from("brazo_laser_lente", bm, "laser_lente")
    bm, uvl = bm_new()   # foco de trabajo bajo la herramienta
    box(bm, uvl, (0, 4.0, -4.6), 1.0, 0.8, 0.5, tile=1.0)
    M["luces"] = mesh_from("brazo_luces", bm, "luz_blanca")
    return M

# ---------------------------------------------------------------- andamio de transporte
# Plegada, la estación viaja alrededor de las bodegas de la nave, entre sus dos anillos de curvatura,
# sujeta por un andamio provisional que se desecha al desplegarla:
#   - dos anillos de celosía de 16 lados, a ANDAMIO_R del eje, por fuera de los tanques (llegan a 196 m),
#     uno a la altura de los nudos de arriba y otro 18 m por debajo de los de abajo (deja libres los
#     pedestales de los brazos);
#   - montantes entre los dos anillos, salvo donde salen las torres de sensores de la espina (90° y 270°);
#   - abrazaderas en la espina, a cada altura, y radios que pasan por los huecos entre tanques (cada
#     60°, a partir de 30°), donde un rayo llega limpio de los anillos a la espina;
#   - un puntal recto por nudo, del vértice del anillo a la rótula, con su garra.
# Todo se midió contra nave.gltf con la estación montada como dice CUNA_EN_NAVE.
# Para el juego, cada pieza "andamio_NN" es un sector (del vértice NN-1 al NN, con sus radios y su
# trozo de abrazadera) que sale despedido hacia fuera; "andamio_NN_puntal" es el puntal del nudo NN,
# que antes se pliega 90° sobre su bisagra, el vértice NN-1 del anillo, hacia el vértice siguiente.
# En la nave (Blender): estación girada -90° sobre X (su eje Z pasa a ser el eje Y de la nave) y con
# su origen en y = -135,5 m, entre las bodegas. El juego usa lo mismo (GameLayer y Session).
CUNA_EN_NAVE = (-135.5, -90.0)
ANDAMIO_R = 228.0
ANDAMIO_Z = (H_PLEG, -H_PLEG - 18.0)         # alturas de los dos anillos
ESPINA_R = (64.0, 68.0)                       # radio de la espina a esas alturas (en la de abajo, un collar)
RADIOS = (30.0, 90.0, 150.0, 210.0, 270.0, 330.0)  # grados de los huecos entre tanques
SIN_MONTANTE = (4, 12)                        # vértices junto a las torres de sensores

def tri_truss(bm, uvl, a, b, w=6.0, chord=0.8, bar=0.4, step=8.0, up=None):
    a = Vector(a); b = Vector(b); d = (b - a).normalized()
    up = Vector(up) if up else (Vector((0, 0, 1)) if abs(d.z) < 0.9 else Vector((1, 0, 0)))
    u = d.cross(up).normalized(); v = d.cross(u)
    r = w / math.sqrt(3.0)
    offs = [(u * math.cos(t) + v * math.sin(t)) * r for t in (math.pi / 2, math.pi * 7 / 6, math.pi * 11 / 6)]
    for o in offs: cyl(bm, uvl, a + o, b + o, chord, 6)
    n = max(int((b - a).length / step), 1)
    for k in range(n + 1):
        q = a + (b - a) * (k / n)
        for j in range(3):
            cyl(bm, uvl, q + offs[j], q + offs[(j + 1) % 3], bar, 6)
            if k < n: cyl(bm, uvl, q + offs[j], a + (b - a) * ((k + 1) / n) + offs[(j + 1) % 3], bar * 0.9, 6)

def mallas_andamio():
    out = {}
    def vert(i, z):
        a = 2 * math.pi * i / N
        return Vector((ANDAMIO_R * math.cos(a), ANDAMIO_R * math.sin(a), z))
    for i in range(N):
        bm, uvl = bm_new()
        a0, a1 = 360.0 * i / N, 360.0 * (i + 1) / N
        for lvl, z in enumerate(ANDAMIO_Z):
            tri_truss(bm, uvl, vert(i, z), vert(i + 1, z), up=(0, 0, 1))
            cyl(bm, uvl, vert(i, z) + Vector((0, 0, -4.0)), vert(i, z) + Vector((0, 0, 4.0)), 4.2, 12)  # nudo del anillo
        if i not in SIN_MONTANTE:
            tri_truss(bm, uvl, vert(i, ANDAMIO_Z[1] + 4.0), vert(i, ANDAMIO_Z[0] - 4.0))
        aviso_bm, aviso_uvl = bm_new()
        for phi in RADIOS:
            if not (a0 <= phi < a1): continue
            p = math.radians(phi); rad = Vector((math.cos(p), math.sin(p), 0))
            chord = ANDAMIO_R * math.cos(math.pi / N) / math.cos(p - math.radians(a0 + 180.0 / N))
            for lvl, z in enumerate(ANDAMIO_Z):
                rs = ESPINA_R[lvl]
                tri_truss(bm, uvl, rad * (rs + 8.0) + Vector((0, 0, z)), rad * (chord - 2.0) + Vector((0, 0, z)), up=(0, 0, 1))
                # trozo de abrazadera alrededor de la espina, 60° centrados en el radio
                secs = []
                for k in range(13):
                    t = p + math.radians(-30.0 + 5.0 * k)
                    c, s = math.cos(t), math.sin(t)
                    secs.append([Vector((c * r_, s * r_, z + dz)) for r_, dz in ((rs - 1.0, -5.0), (rs + 8.0, -5.0), (rs + 8.0, 5.0), (rs - 1.0, 5.0))])
                loft(bm, uvl, secs, tile=6.0, smooth=False)
                for k in (1, 11):   # pernos de cierre de la abrazadera
                    t = p + math.radians(-30.0 + 5.0 * k)
                    cyl(bm, uvl, Vector((math.cos(t), math.sin(t), 0)) * (rs + 8.0) + Vector((0, 0, z)), Vector((math.cos(t), math.sin(t), 0)) * (rs + 10.0) + Vector((0, 0, z)), 1.2, 8)
                band = []
                for k in range(13):
                    t = p + math.radians(-30.0 + 5.0 * k)
                    c, s = math.cos(t), math.sin(t)
                    band.append([Vector((c * r_, s * r_, z + dz)) for r_, dz in ((rs + 7.9, -1.5), (rs + 8.3, -1.5), (rs + 8.3, 1.5), (rs + 7.9, 1.5))])
                loft(aviso_bm, aviso_uvl, band, tile=4.0, smooth=False)
        out[f"andamio_{i+1:02d}"] = mesh_from(f"andamio_{i+1:02d}", bm, "metal_gris")
        if aviso_bm.verts:
            out[f"andamio_{i+1:02d}_aviso"] = mesh_from(f"andamio_{i+1:02d}_aviso", aviso_bm, "franja_aviso")
        else:
            aviso_bm.free()
        # Puntal del nudo i: del vértice i del anillo a la rótula, horizontal y radial.
        a = 2 * math.pi * i / N
        rad = Vector((math.cos(a), math.sin(a), 0))
        top = i % 2 == 0
        z = ANDAMIO_Z[0] if top else ANDAMIO_Z[1]
        node = Vector((R_PLEG * math.cos(a), R_PLEG * math.sin(a), H_PLEG if top else -H_PLEG))
        dz = z - node.z
        contact = Vector((node.x, node.y, z)) - rad * math.sqrt(NUDO_R ** 2 - dz ** 2)
        bm, uvl = bm_new()
        hinge = vert(i, z)
        cyl(bm, uvl, hinge + Vector((0, 0, -6.0)), hinge + Vector((0, 0, 6.0)), 3.2, 12)    # bisagra
        tri_truss(bm, uvl, hinge + rad * 4.0, contact - rad * 6.0, w=5.0, chord=0.7, up=(0, 0, 1))
        cyl(bm, uvl, contact - rad * 6.0, contact - rad * 2.0, 3.5, 16, r1=5.5)            # garra
        cyl(bm, uvl, contact - rad * 2.0, contact + rad * 1.2, 5.5, 16)
        out[f"andamio_{i+1:02d}_puntal"] = mesh_from(f"andamio_{i+1:02d}_puntal", bm, "metal_gris")
        bm, uvl = bm_new()
        cyl(bm, uvl, contact - rad * 4.4, contact - rad * 3.4, 4.9, 16)
        out[f"andamio_{i+1:02d}_puntal_aviso"] = mesh_from(f"andamio_{i+1:02d}_puntal_aviso", bm, "franja_aviso")
    return out

# ---------------------------------------------------------------- despliegue
def smooth(t): t = min(max(t, 0.0), 1.0); return t * t * (3 - 2 * t)

def vertices(hgt):
    c = math.sqrt(max(L * L - 4 * hgt * hgt, 0.0))
    r = c / (2 * math.sin(math.pi / N))
    return [Vector((r * math.cos(2*math.pi*i/N), r * math.sin(2*math.pi*i/N), hgt * (1 if i % 2 == 0 else -1))) for i in range(N)]

def pose_modulo(V, i):
    a, b = V[i], V[(i + 1) % N]
    m = (a + b) / 2
    X = (b - a).normalized()
    inward = -Vector((m.x, m.y, 0)).normalized()
    Y = (inward - X * X.dot(inward)).normalized()
    Z = X.cross(Y)
    rot = Matrix(((X.x, Y.x, Z.x), (X.y, Y.y, Z.y), (X.z, Y.z, Z.z))).to_quaternion()
    return m, rot

def construir():
    global COL
    materiales()
    old = bpy.data.collections.get("Estacion_Origami")
    if old:
        for o in list(old.all_objects): bpy.data.objects.remove(o, do_unlink=True)
        bpy.data.collections.remove(old)
    for me in list(bpy.data.meshes):
        if me.users == 0: bpy.data.meshes.remove(me)
    COL = bpy.data.collections.new("Estacion_Origami"); bpy.context.scene.collection.children.link(COL)
    root = bpy.data.objects.new("Estacion_Origami", None); COL.objects.link(root)
    root.empty_display_size = 50

    MM, MN, MB = mallas_modulo(), mallas_nudo(), mallas_brazo()
    for name, me in mallas_andamio().items():   # el andamio, fijo en la pose plegada
        obj(name, me, root)
    sc = bpy.context.scene
    sc.frame_start, sc.frame_end = F_INI, F_FIN
    sc.render.fps = 24

    modulos, nudos = [], []
    for i in range(N):
        e = bpy.data.objects.new(f"modulo_{i+1:02d}", None); COL.objects.link(e); e.parent = root
        e.rotation_mode = "QUATERNION"; e.empty_display_size = 20
        for part, me in MM.items():
            obj(f"modulo_{i+1:02d}_{part}", me, e)
        modulos.append(e)
        n = bpy.data.objects.new(f"nudo_{i+1:02d}", None); COL.objects.link(n); n.parent = root
        n.empty_display_size = 20
        n.rotation_euler = (0, 0, 2 * math.pi * i / N + math.pi / 2)
        for part in ("nucleo", "rotula", "baliza_mastil"):
            obj(f"nudo_{i+1:02d}_{part}", MN[part], n)
        obj(f"nudo_{i+1:02d}_rieles", MN[f"rieles_{i % 2}"], n)
        obj(f"nudo_{i+1:02d}_rieles_aviso", MN[f"rieles_aviso_{i % 2}"], n)
        if i % 2 == 0:   # nudo de atraque
            obj(f"nudo_{i+1:02d}_baliza", MN["baliza"], n)
            for part in ("esclusa", "esclusa_aviso", "esclusa_collar", "esclusa_compuerta", "esclusa_luces"):
                obj(f"nudo_{i+1:02d}_{part}", MN[part], n)
            a = bpy.data.objects.new(f"atraque_{i//2+1}", None); COL.objects.link(a); a.parent = n
            a.location = (0, -42.6, 0); a.rotation_euler = (math.pi / 2, 0, 0)   # +Z local hacia fuera
            a.empty_display_type = "SINGLE_ARROW"; a.empty_display_size = 12
        else:            # nudo con brazo robótico
            k = i // 2; nb = f"brazo_{k+1}"
            obj(f"nudo_{i+1:02d}_baliza", MN["baliza_roja"], n)
            for part in ("pedestal", "tolva", "tolva_aviso"):
                obj(f"nudo_{i+1:02d}_{part}", MN[part], n)
            giro = obj(f"{nb}_giro", MB["giro"], n, loc=(0, 30.0, 0))
            hombro = bpy.data.objects.new(f"{nb}_hombro", None); COL.objects.link(hombro)
            hombro.parent = giro; hombro.location = (0, 6.5, 0); hombro.empty_display_size = 8
            for part in ("brazo", "brazo_estructura", "brazo_manguera"):
                obj(f"{nb}_{part}", MB[part], hombro)
            codo = bpy.data.objects.new(f"{nb}_codo", None); COL.objects.link(codo)
            codo.parent = hombro; codo.location = (0, UPPER, 0); codo.empty_display_size = 8
            for part in ("antebrazo", "antebrazo_estructura"):
                obj(f"{nb}_{part}", MB[part], codo)
            mun = bpy.data.objects.new(f"{nb}_muneca", None); COL.objects.link(mun)
            mun.parent = codo; mun.location = (10, FORE, 0); mun.empty_display_size = 6
            mun.scale = (1.7, 1.7, 1.7)   # cabezal de ~40 m: herramienta pesada a la escala del brazo
            tool = ("impresora", "impresora_estructura", "impresora_boquillas") if k % 2 == 0 else ("laser", "laser_estructura", "laser_lente")
            for part in tool:
                obj(f"{nb}_{part}", MB[part], mun)
            obj(f"{nb}_luces", MB["luces"], mun)
            # poses: plegado (hombro 90°, codo 180°, muñeca 90°) y de trabajo, distinta en cada brazo
            s = 1 if k % 2 == 0 else -1
            dep = dict(giro=math.radians(16 * math.sin(2.1 * k + 0.5)),
                       hombro=math.radians(s * (14 + 6 * math.sin(1.3 * k))),
                       codo=math.radians(-s * (48 + 8 * math.cos(0.9 * k))),
                       muneca=math.radians(s * (22 + 6 * math.sin(k))))
            plg = dict(giro=0.0, hombro=math.radians(90), codo=math.radians(180), muneca=math.radians(90))
            tiempos = dict(giro=(F2 + 50, F3), hombro=(F2, F3 - 30), codo=(F2 + 25, F3 - 5), muneca=(F2 + 55, F3))
            for nm, o, axis in (("giro", giro, 1), ("hombro", hombro, 0), ("codo", codo, 0), ("muneca", mun, 0)):
                ta, tb = tiempos[nm]
                for f, v in ((F_INI, plg[nm]), (ta, plg[nm]), (tb, dep[nm]), (F_FIN, dep[nm])):
                    o.rotation_euler[axis] = v
                    o.keyframe_insert("rotation_euler", index=axis, frame=f)
        nudos.append(n)

    # despliegue del anillo, horneado cada 2 fotogramas
    frames = [F_INI] + list(range(F0, F1 + 1, 2)) + [F_FIN]
    prev = [None] * N
    for f in frames:
        t = (f - F0) / (F1 - F0)
        V = vertices(H_PLEG * (1 - smooth(t)))
        for i in range(N):
            nudos[i].location = V[i]; nudos[i].keyframe_insert("location", frame=f)
            m, q = pose_modulo(V, i)
            if prev[i] is not None and prev[i].dot(q) < 0: q.negate()
            prev[i] = q.copy()
            modulos[i].location = m; modulos[i].rotation_quaternion = q
            modulos[i].keyframe_insert("location", frame=f)
            modulos[i].keyframe_insert("rotation_quaternion", frame=f)
    for o in modulos + nudos:   # lo horneado, lineal
        ad = o.animation_data
        if ad and ad.action:
            for fc in _fcurves(ad):
                for kp in fc.keyframe_points: kp.interpolation = "LINEAR"
    sc.frame_set(F_FIN)
    return root

def _fcurves(ad):
    act = ad.action
    if hasattr(act, "fcurves") and len(getattr(act, "fcurves", [])):
        return list(act.fcurves)
    out = []   # acciones por capas (Blender 4.4+)
    for layer in getattr(act, "layers", []):
        for strip in layer.strips:
            for bag in strip.channelbags:
                out.extend(bag.fcurves)
    return out

if not globals().get("SOLO_FUNCIONES"):   # el script de los drones solo quiere las funciones
    construir()
    print("Estación construida:", N, "módulos de", round(L, 1), "m; plegada r =", R_PLEG, "h =", round(H_PLEG, 1))
