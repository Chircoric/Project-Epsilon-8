# Estación Origami (estación de nivel 1)

La primera estación de Nebulosa. Llega plegada como una corona en zigzag y se despliega en un anillo de ~1 km con esclusas de atraque y ocho brazos robóticos (impresoras 3D y láseres) que construyen naves con el material que se les da. Todavía **no es modular**: los módulos de ampliación llegarán al construir la primera nave minera.

- `construir_estacion.py`: genera toda la estación y su animación. Se ejecuta dentro de Blender con `estacion_origami.blend` abierto: `exec(open(r"...\construir_estacion.py", encoding="utf-8").read())`. Borra la colección `Estacion_Origami` y la rehace, así que los cambios se hacen en el script, no a mano.
- `estacion_origami.blend`: el archivo fuente. Los materiales se importan de `nave_epsilon.blend` (las mismas texturas que la nave), más `metal_fundido` y `laser_lente`, que son emisivos sin textura.
- Versión de juego: [game/assets/modelos/estacion.gltf](../../../game/assets/modelos/estacion.gltf) y `estacion.bin`. Usa texturas que ya están en `texturas/` por la nave. El juego la carga junto al ejecutable (`LoadStationAndDrones`, en GameModels) con su animación (`LoadGltfAnimation`, en el motor). Cómo se juega: [game/README.md](../../../game/README.md#estaciones).
- `capturas/`: `juego_*.png` son la estación en el juego (a bordo, abriéndose, con la nave atracada) y los drones fuera. `desplegada.png`, `brazos_cerca.png` y `plegada.png` son la primera versión, plegada a 275 m, cargada en lugar de la nave con `--nave`.

## Medidas (metros reales)

| | |
|---|---|
| Diámetro desplegada | 1026 m de punta a punta (nudos a 470 m del eje) |
| Plegada | 847 × 847 × 184 m, con un hueco de 583 m en el centro (desplegada mide 138 m de alto, con los brazos) |
| Módulos | 16, de 183,4 m de nudo a nudo; sección de 44 × 36 m |
| Nudos | 16 rótulas esféricas de 24 m de radio, con cojinetes en los polos (57 m de polo a polo) |
| Brazos | 150 m de brazo y 130 m de antebrazo, con un cabezal de ~40 m; alcanzan hasta ~300 m hacia el centro |

El anillo queda en el plano XY de Blender, con el eje en Z (en glTF, el eje es Y).

## El plegado

Es un mecanismo de un solo grado de libertad y cinemáticamente exacto: ninguna pieza cambia de largo.
- Plegada, los nudos pares están arriba (+h) y los impares abajo (−h), sobre un círculo de 380 m (h = 54 m).
  - Al principio era de 275 m, más compacta. Se abrió para que la corona, montada en la nave, deje libres las cápsulas de sensores de la espina, que salen hasta 285 m del eje.
- Al bajar h, el radio crece de forma que cada módulo conserva su largo: L² = (2·r·sin(π/16))² + (2h)².
- Con h = 0, el anillo queda plano, con 470 m de radio.
- Cada módulo acaba en un cuello con fuelle que entra en el nudo: es la rótula donde dobla el origami.

Se comprobó con árboles BVH, cada 15 fotogramas de toda la animación, que no choca ninguna pieza. Las únicas que se cruzan son los fuelles de dos módulos vecinos, y solo dentro del nudo que comparten, donde no se ven.

## Animación `despliegue` (24 fps)

| Fotogramas | Qué pasa |
|---|---|
| 1–30 | Plegada y quieta |
| 30–210 | El anillo se abre (horneado cada 2 fotogramas, lineal) |
| 220–330 | Los brazos se despliegan: primero el hombro, luego el codo, la muñeca y el giro |
| 330–360 | Desplegada y quieta |

Plegados, los brazos van verticales dentro de la corona: el hombro a 90°, el antebrazo doblado a 180° junto al brazo y la herramienta a 90°, hacia dentro. Desplegados, cada uno tiene una postura de trabajo distinta, la mitad por encima del plano del anillo y la otra mitad por debajo.

- En el glTF, la **pose de reposo es la plegada**.
- Hay una sola animación, `despliegue`, que sale de las pistas NLA que tienen ese nombre (exportación en modo `NLA_TRACKS`).

## Piezas

Las mallas se comparten: los 16 módulos usan las mismas mallas, igual que los nudos y los brazos.

| Nodo | Qué es |
|---|---|
| `modulo_NN` (vacío animado) | `_casco` (presurizado, con mamparos abombados en los extremos), `_esclusa`, `_esclusa_aviso` y `_esclusa_luces` (las puertas de los extremos), `_estructura` (cuadernas, cerrojos, túneles con fuelle y anillo, copas sobre las rótulas con pasadores y patines, amortiguadores, viga de celosía en la cara exterior, tuberías y postes), `_ventanas` (habitáculo, mirando al centro), `_panel` (solar, arriba), `_radiador` (abajo), `_luces` |
| `nudo_NN` (vacío animado) | `_nucleo` (la esfera de la rótula), `_rotula` (bandas de rodadura y cojinetes de los polos con su brida), `_rieles` y `_rieles_aviso` (los rieles de las copas), `_baliza_mastil` y `_baliza` (blanca en los de atraque, roja en los de brazo) |
| Nudos impares (1, 3…15) | Esclusa de atraque como la de la nave: `_esclusa`, `_esclusa_aviso` (marco de franjas), `_esclusa_collar` (collarín octogonal de ~20 m y cuatro garras), `_esclusa_compuerta`, `_esclusa_luces` (verdes). El vacío `atraque_N` (N = 1 a 8) está en el centro del collarín, con su +Z local hacia fuera (su +Y en glTF), como el `atraque` de la nave |
| Nudos pares (2, 4…16) | Brazo robótico: `_pedestal`, `_tolva` (entrada de material) y la cadena `brazo_N_giro` → `_hombro` → `_codo` → `_muneca` |
| Herramientas | Brazos impares: `brazo_N_impresora` con siete boquillas de metal fundido (`_boquillas`, emisivas) y dos bobinas de material de aporte. Brazos pares: `brazo_N_laser` con aletas de refrigeración y lente roja (`_laser_lente`). Todos llevan un foco de trabajo (`brazo_N_luces`) |

Las lámparas siguen los nombres que reconoce el juego: «baliza» y los que acaban en «_luces».

## Rótulas y separación entre piezas (2026-11-12)

Los módulos ya no se meten en los nudos:
- **Nudos:** cada nudo es una **rótula esférica** de 24 m de radio. Tiene tres bandas de rodadura y, en los polos, cojinetes con brida y pernos donde van la baliza, la esclusa o el pedestal del brazo.
- **Módulos:** el casco acaba en ±44 m con una cuaderna gruesa y un mamparo abombado hasta ±52 m, con su escotilla. Así quedan unos 15 m libres hasta la rótula.
- **Cuello y copa:** por ese hueco pasa el cuello con fuelle. Termina en una **copa**: un casquete esférico concéntrico con la rótula, 1,6 m por fuera de ella, con 22° de abertura y ocho pasadores de cierre. La copa la lleva el módulo; la rótula gira dentro sin tocarla, se pliegue como se pliegue.
- **Amortiguadores:** cuatro por extremo, cada uno con cilindro, vástago y ojos, del mamparo al borde de la copa.
- **Rieles:** cada copa se mueve siempre en el plano de su módulo (comprobado: la normal del recorrido es el eje Y del módulo en todo el despliegue). Así que recorre la rótula por un arco de círculo máximo, de la pose plegada (36° sobre o bajo el ecuador) a la abierta.
  - Cada rótula lleva dos rieles por módulo, a ±14° de ese arco (`RIEL_ANG`). Siguen 30° más allá de la pose abierta y 15° más allá de la plegada, para que se vean por fuera de la copa.
  - Cada riel tiene una franja de aviso por fuera y topes curvos de fin de carrera en los extremos (`nudo_NN_rieles`, `nudo_NN_rieles_aviso`). Hay un juego de mallas para los nudos pares y otro para los impares, que son simétricos.
  - La copa lleva dos patines atornillados que corren sobre los rieles, a 0,1 m de ellos.
  - Las bandas de rodadura son ahora casquetes que siguen la esfera; las de antes eran cilindros y sus bordes llegaban a los patines.
- **Esclusas:** cada extremo de módulo es una cara plana con una puerta octogonal de 21 m (`modulo_NN_esclusa`, textura `compuerta_carga`). Lleva un marco de ocho barras con franjas de aviso (`_esclusa_aviso`), cuatro cerrojos y dos luces verdes (`_esclusa_luces`). Así hay una puerta entre cada módulo y su nudo, como la esclusa de la nave.
  - El túnel con fuelle (radio 6 a 7 m) sale de la puerta hasta la copa, con un anillo rígido y abrazaderas a medio camino.
- **Comprobado** con árboles BVH, cada 15 fotogramas de toda la animación: ningún módulo toca un nudo ni a su vecino. Los fuelles de dos módulos ya no se cruzan ni dentro de la rótula.
- Plegados, los módulos salen de la rótula a 36° sobre el ecuador. La copa llega a 58° y los cojinetes de los polos empiezan a 59°: por eso tiene 22° de abertura y no más.

## En la nave: el andamio de transporte

Plegada, la estación viaja alrededor de las bodegas de la nave, entre sus dos anillos de curvatura. Hay 280 m libres entre ellos, y la corona mide 184.
- **Montaje** (`CUNA_EN_NAVE` en el script, `kOrigamiMount` y `kOrigamiAft` en GameLayer y Session): la estación va girada −90° sobre X, así que su eje es el de la nave, y su centro queda 135,5 m a popa del centro de la nave.
- **Comprobado contra `nave.gltf`** con árboles BVH: la estación plegada no toca la nave. El andamio solo toca la espina, en sus abrazaderas.
- **El andamio** sustituye a la cuna de antes, cuyos puntales cruzaban en diagonal por delante de los tanques. Se midió con rayos contra la nave, en el marco de la estación:
  - los tanques llegan a 196 m del eje;
  - entre ellos quedan huecos libres hasta la espina (64 m de radio) a 30°, 90°, 150°…;
  - las torres de sensores salen hasta 271 m, pero solo a 90° y 270°, entre −15 y +35 m de altura.
- **Sus piezas** (`mallas_andamio`):
  - dos anillos de celosía de 16 lados a 228 m del eje: uno a la altura de los nudos de arriba (+54 m) y otro 18 m por debajo de los de abajo (−72 m), para dejar libres los pedestales de los brazos;
  - montantes entre los dos anillos, salvo en los vértices 4 y 12, junto a las torres;
  - en cada altura, una abrazadera de seis trozos hundida un metro en la espina, con pernos y franjas de aviso, y seis radios de celosía por los huecos entre tanques;
  - un puntal recto y horizontal por nudo, con bisagra en el vértice del anillo y una garra que se apoya en la rótula.
- **Nombres** para el juego:
  - `andamio_NN` es un sector, del vértice NN−1 al NN, con su trozo de anillo, su montante, sus radios y su trozo de abrazadera (`_aviso`, las franjas);
  - `andamio_NN_puntal` (y `_puntal_aviso`) es el puntal del nudo NN.
- **En el juego** (`GameLayer::ScaffoldMotion`):
  - va con la nave mientras la estación esté a bordo;
  - al soltarla se queda donde estaba la nave, y en el primer segundo cada puntal gira 90° sobre su bisagra hasta quedar pegado al anillo, antes de que se muevan los nudos;
  - cuando la estación ya se ha apartado (a los 21 s), cada sector sale despedido hacia fuera a 8 m/s², girando despacio, y a los 25 s deja de dibujarse, ya a 2,5 km.

## En el juego

`GameSession::ShipyardDeploy` reparte los 27 s reales del despliegue en tres fases:

| Fase | Segundos | Animación | Qué pasa |
|---|---|---|---|
| Se abre | 0–9 | 0 a 9 s | El anillo se abre alrededor de la nave |
| Se aparta | 9–21 | quieta en 9 s | Se desplaza 1,6 km hacia popa, a lo largo de su eje, y deja la nave fuera |
| Saca los brazos | 21–27 | 9 a 15 s | Los ocho brazos se despliegan |

- La estación desplegada se queda quieta respecto al planeta que domina ese punto o, si no hay ninguno, al sistema. No orbita, así que solo se suelta fuera de órbita.
- La nave atraca en `atraque_1` (512,6 m del eje, por fuera), paralela al eje del anillo.

## Pendiente

- Construir naves con el material entregado: la primera nave minera.
- Ranuras `hp_…` para los módulos de ampliación cuando exista la primera nave minera.
