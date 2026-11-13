# Dron minero y bahía de drones

Los drones con los que sale la nave: dos bahías, cada una con dos drones, en las ranuras de torreta de popa (`hp_torreta_5` y `hp_torreta_6`). Van a planetas y campos de asteroides, minan y descargan en la bodega. Cómo se juega: [game/README.md](../../../game/README.md#drones-mineros).

- `construir_drones.py`: genera el dron y la bahía. Se ejecuta en Blender con `drones.blend` abierto: `exec(open(r"...\construir_drones.py", encoding="utf-8").read())`. Usa las funciones de geometría de [construir_estacion.py](../estacion_origami/construir_estacion.py), que carga con `SOLO_FUNCIONES = True`, sin construir la estación.
- `drones.blend`: el archivo fuente. Los materiales vienen de `nave_epsilon.blend`, más `laser_lente`.
- Versión de juego: [game/assets/modelos](../../../game/assets/modelos) `dron.gltf` y `bahia_drones.gltf`, con sus `.bin`. Las texturas son las de la nave, que ya están en `texturas/`. El juego los carga con `LoadStationAndDrones` (GameModels). Sin los archivos, usa maquetas hechas por código.

## Dron (34 m de proa a popa)

Proa a +Y y arriba +Z en Blender, como la nave (proa a +Z y −Y arriba en el motor).

| Pieza | Qué es |
|---|---|
| `dron_casco` | Cuerpo de sección redondeada, 9,6 × 7,8 m |
| `dron_bodega` | Tanque de mineral bajo el cuerpo, forrado de lámina dorada |
| `dron_estructura` | Bandas de la bodega, cuello y corona portabrocas con seis dientes, broca, dos brazos de láser de corte, cuatro motores en las esquinas de popa, cuatro patas con zapatas para posarse en un planeta, antena y brazos de los paneles |
| `dron_paneles` | Dos paneles solares pequeños |
| `dron_toberas` | Las cuatro toberas encendidas (material `escape`) |
| `dron_laser` | Lentes de los láseres de corte |
| `dron_aviso`, `dron_luces`, `dron_baliza` | Franja de aviso, focos de trabajo y baliza roja |

## Bahía (sobre la base de torreta, de 26 m de radio)

Apoyada en el origen, con +Z hacia fuera del casco, como una ranura `hp_` de la nave. En el motor, el −Y es lo que sale del casco: el juego le da media vuelta sobre Z al montarla.

| Pieza | Qué es |
|---|---|
| `bahia_zocalo` | Zócalo de 23 m de radio sobre la base de torreta |
| `bahia_hangar` | Losa del hangar, de 40 × 34 × 10 m, con los cantos redondeados |
| `bahia_estructura` | Cunas bajo las patas de los drones, carriles, dos grúas de pórtico y los conductos de carga hacia el casco |
| `bahia_compuertas` | Escotillas de descarga del mineral |
| `bahia_aviso`, `bahia_luces` | Franjas de aviso y cuatro luces verdes en las esquinas |
| `dron_1`, `dron_2` | Nodos vacíos: donde se posa cada dron (a ±13 m, 24,9 m sobre la base), con su proa hacia +Y |
