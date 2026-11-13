# Torretas humanas y naves alienígenas

Fase E de [docs/diseno-combate.md](../../../docs/diseno-combate.md).

- `construir_combate.py`: genera todo. Se ejecuta en Blender con `combate.blend` abierto: `exec(open(r"...\construir_combate.py", encoding="utf-8").read())`. Con `EXPORTAR = True` (y `DESTINO`, por defecto `game/assets/modelos`) escribe los `.gltf`. Usa las funciones de geometría de [construir_estacion.py](../estacion_origami/construir_estacion.py) con `SOLO_FUNCIONES = True`. Se edita el script, no el `.blend`.
- Exportar a una carpeta temporal y copiar solo `.gltf` y `.bin`: las texturas son las de la nave, que ya están en `game/assets/modelos/texturas/`.
- El juego los carga con `LoadCombatModels` (GameModels). Sin los archivos usa las maquetas hechas por código.

## Torretas (`torretas.gltf`)

Industriales y rígidas: cajas y prismas de cantos vivos sobre un zócalo cuadrado de 36 m (la ligera), que cabe en la base de torreta de la nave. La pesada es 1,6 veces mayor. Apoyadas en el origen, con +Z hacia fuera del casco y los cañones a +Y, como la bahía de drones: el juego les da media vuelta sobre Z al montarlas.

Todas van en un solo archivo. Cada arma son las piezas con su prefijo (`TurretModelName`):

| Prefijo | Arma | Qué la distingue |
|---|---|---|
| `defensa_puntual` | Plataforma de defensa puntual | Montaje pequeño con un rotativo de seis tubos y un radar plano |
| `coilgun_ligera`, `coilgun_pesada` | Coilgun | Cuatro cañones cuadrados con cinco bobinas de cobre |
| `espinal_ligera`, `espinal_pesada` | Railgun espinal | Sin collar de giro, baja; raíles de 72 m y seis bancos de condensadores |
| `railgun360_ligera`, `railgun360_pesada` | Railgun 360 | Dos raíles con abrazaderas sobre la casamata y condensadores a los lados |
| `misiles_ligera`, `misiles_pesada` | Bahías de 10, 25 y 50 | Bloque con 4 × 4 compuertas en el techo |
| `nuclear_ligera`, `nuclear_pesada` | Misil nuclear | Silo alto con una compuerta grande, contrafuertes y franjas de aviso |

Piezas de cada una: `_zocalo` (montaje_torreta), `_cuerpo` (casco_blanco), `_estructura`, `_canones` (metal_gris), `_bobinas` (bobina_cobre), `_condensadores` y `_compuerta(s)`, `_aviso` y `_baliza` (luz roja).

## Naves alienígenas

Proa a +Y, arriba +Z y origen a media eslora, como la nave. La eslora es la de su clase en `kShipClasses` (la variante que les da `AssignShipClass`). Formas continuas, sin aristas rectas, con cristales encendidos. Colores de la especie hostil; el juego hace la neutral cambiando los colores.

El nombre de cada pieza decide su zona (`AlienZoneOfPart`), así que ninguna otra pieza debe llevar esas palabras (ni «luz», «radio», «punta», «centro», ni «ojo» dentro de «rojo»):

| Palabra | Zona |
|---|---|
| `cuerpo` | Espina: si cae, la nave se apaga |
| `ojo`, `nucleo` | Núcleo: si cae, la nave estalla |
| `corazon` | Puente |
| `cuchilla`, `filo` + `babor`/`estribor` | Góndolas, que se pueden arrancar |
| `aro` | Anillo |

| Archivo | Eslora | Piezas |
|---|---|---|
| `alien_dron.gltf` | 34 m | Punta de flecha, placa dorsal, ojo y dos cuchillas con su filo |
| `alien_corveta.gltf` | 300 m | Huso con quilla y placas, cresta dorsal (corazón), núcleo a popa, dos cuchillas y vetas encendidas |
| `alien_fragata.gltf` | 1200 m | Espina que se abre en dos horquillas, torre dorsal (corazón), núcleo de 164 m en una cuna de costillas, anillo de 500 m a popa, dos cuchillas de 700 m y vetas |
