# Nave Epsilon (nave inicial)

- `nave_epsilon.blend`: el archivo fuente. El texto interno `nave_lib` guarda las funciones de geometría.
- `nave_epsilon_v1.blend`: la primera versión, con un anillo fino. Usa texturas antiguas de `texturas/` que la nave actual ya no usa.
- `nave_epsilon_v2.blend` y `nave_epsilon_v3.blend`: copias anteriores. La v3 es el estado justo antes de los arreglos del 2026-11-10 (cuello, uniones, bahías, bases de torreta, paneles y ventanas del puente).
- La versión que carga el juego está en [game/assets/modelos](../../../game/assets/modelos): `nave.gltf`, `nave.bin` y `texturas/`, con solo las texturas que usa. Al cambiar el modelo, se exporta de nuevo ahí con **glTF Separate (.gltf + .bin + texturas)**, la carpeta de texturas `texturas` y "Incluir: selección" con la colección `Nave_Epsilon`.
- Escala real, en metros: 1591 m de proa a popa (contando el mástil del deflector) y anillos de 690 m de diámetro. El origen queda a media eslora, sobre el eje. La proa mira a +Y en Blender, que es -Z en glTF y +Z en el motor.

## Zonas del casco

El juego reparte las piezas en sus 15 zonas por el principio del nombre ([game/README.md](../../../game/README.md#tu-nave-en-gltf)):

| Zona | Piezas |
|---|---|
| Proa | `proa_escudo` (losetas ablativas del borde de ataque), `proa_emisor`, `proa_emisor_bulbo` |
| Puente | `puente_platillo`, `puente_cupula`, `puente_ventanas` y `puente_ventanas_apagadas` (filas de ventanas de 1,8 × 1,2 m), `puente_ventanas_platillo`, `puente_franja`, `puente_cupula_inferior`, `puente_bahia_*`, `puente_luz_babor`, `puente_luz_estribor`, `puente_ranura_torreta_1..4` |
| Bodegas 1 a 6 | `bodega_N`, `bodega_N_herrajes`, `bodega_N_bridas`, `bodega_N_blindaje`, y la zona de carga y descarga: `bodega_N_compuerta`, `bodega_N_marco` (marco y grúa) y `bodega_N_luces`. Son seis tanques alrededor del núcleo, cada 60° |
| Anillo de curvatura | Todo lo que empieza por `rotor1_anillo_` o `rotor2_anillo_`: banda, carenado, luces, habitáculos, radios, uniones, cubo y balizas |
| Góndolas | `gondola_babor_*` y `gondola_estribor_*`: motor, tobera y escape de los dos motores laterales |
| Radiadores | `radiador_babor*` y `radiador_estribor*`: módulos solares, dorso con marco y costillas, mástil y bridas, a popa |
| Espina | `espina_central`, `espina_cuello` (carenado que funde el platillo con el cojinete), `espina_cojinete_*`, `espina_torre_*` y sus `_bridas`, `espina_capsula_*`, `espina_baliza_*`, `espina_ventanales` |
| Núcleo | `nucleo_modulo_popa`, `nucleo_blindaje_popa`, los motores `nucleo_*_principal*`, `nucleo_rcs_*`, `nucleo_antena_*`, `nucleo_ranura_torreta_5..6` |

Cuando una zona periférica llega a 0, sus piezas se sueltan juntas: la proa pierde el escudo y el emisor, una bodega se va con su tanque, y el anillo se lleva los dos rotores.

## Anillos giratorios

- Las piezas `rotor1_…` y `rotor2_…` giran sobre el eje de la nave, en sentidos contrarios para que el casco no reciba par. Van a la velocidad que da 1 g en la cara interior, una vuelta cada ~36 s.
- Lo hace el juego (`GameLayer`) con el prefijo del nombre. El glTF lleva además las animaciones `giro_anillo_1` y `giro_anillo_2` para otros visores, pero el juego no las usa.
- Los cojinetes (`espina_cojinete_*`) son fijos.
- Cada anillo lleva 6 pares de radios en V, de celosía de sección triangular, y un anillo de tensión hexagonal.
- `rotorN_anillo_uniones` lleva los herrajes de esa estructura:
  - en el cubo, una brida y un manguito hexagonal por radio;
  - en el anillo, una placa de apoyo, orejetas y un bulón donde se juntan los dos radios de cada V;
  - una rótula esférica en cada nudo con el anillo de tensión.
- Ninguna viga de los rotores toca piezas fijas en ninguna posición del giro: se comprobó con árboles BVH en 24 posiciones.

## Uniones fijas

Ninguna viga se clava sin más en el casco:
- Las torres de sensores llevan una brida con cartelas sobre la espina y un collarín bajo la cápsula (`espina_torre_*_bridas`).
- Los soportes de cada bodega llevan una placa en la espina y otra en el tanque (`bodega_N_bridas`).
- Los mástiles de los paneles llevan una placa en el módulo de popa y una abrazadera en el panel (`radiador_*_bridas`).

## Luces

Ninguna lámpara flota; todas tienen algo que las sujeta al casco:

| Lámpara | Soporte | En el juego |
|---|---|---|
| `espina_baliza_dorsal`, `espina_baliza_ventral` | `espina_baliza_*_mastil`: base sobre la cápsula, mástil y casquillo | Destello doble blanco cada 1,6 s |
| `rotorN_anillo_baliza_1..4` | `rotorN_anillo_baliza_soportes`: brida y pedestal en la cara exterior del anillo | Igual, y giran con el anillo |
| `puente_luz_babor`, `puente_luz_estribor` | `puente_luz_*_soporte`: pilono desde el canto del platillo y copa | Rojo y verde, laten a la vez |
| `puente_bahia_luces`, `bodega_N_luces` | Son cajitas de 0,75 m apoyadas en el casco | Fijas, luz cálida |

El juego reconoce las lámparas por el nombre (`GameLayer::LampOfPart`): «baliza», «luz_babor» o «luz_estribor», o un nombre que acabe en «_luces», salvo las franjas del anillo. Los nombres con «soporte», «mastil» o «base» no son lámparas. Cada lámpara da una luz puntual que alumbra el casco de alrededor (`FramePacket::pointLights` y `mesh.hlsl`, hasta 64 a la vez), sobre todo lo que queda a la sombra. Las balizas y las de navegación llevan además un halo. Se apagan con el daño de su zona o si la nave se queda sin energía.

## Ventanal del puente

El puente tiene un ventanal corrido de 224 m de ancho y 11 m de alto (de z = 51 a 62), pegado a la cúpula:
- `puente_ventanal` es el cristal, con la textura `ventanal_puente`: cristal oscuro y, detrás, cuatro cubiertas con consolas, luces de techo y alguna silueta.
- `puente_ventanal_marco` lleva montantes cada 4,2 m, un travesaño en cada cubierta (cada 2,75 m), un marco grueso y, delante, un enrejado protector a 2,6 m del cristal, anclado al antepecho, a la cornisa y a los montantes.
- `puente_ventanal_cornisa` es la visera de encima y el antepecho de debajo.

La cúpula (`puente_cupula`) se subdividió (2160 caras) para que el cristal siga su curva sin facetas.

## Forma de la cabeza (2026-11-10)

- **Convergencia:** la parte trasera del platillo se deformó con una función suave (`warp_platillo`, aplicada a `puente_platillo`, `puente_franja`, las ventanitas y las luces de navegación).
  - En planta pasa de 444 m de ancho a los 116 m de la columna, en forma de gota.
  - En sección engorda hasta quedar redonda (radio 58 m) donde entra en el cojinete.
  - `espina_cuello` es ahora solo un tapón cilíndrico dentro de la cola.
- **Redondeos:** `puente_cupula_redondeo` y `puente_cupula_inferior_redondeo` son redondeos cóncavos que funden la cabina y la cúpula de sensores con el platillo, sin escalón. Delante, el redondeo se queda por debajo del antepecho del ventanal.
- **Ventanal:** brilla por dentro: la textura `ventanal_puente` tiene un resplandor cálido por cubierta y la emisión del material es 3,5. Además, `puente_ventanal_1_luces` a `_5_luces` son cinco lámparas escondidas detrás del cristal: el juego las trata como focos de trabajo y alumbran la cornisa y el casco de delante.

## Cambios del 2026-11-11

- **Popa:** `nucleo_carenado_motores` es una carcasa que funde el módulo de popa con los tres motores. Su sección es la envolvente convexa de tres círculos, así que sale lobulada y sin escalones, y cubre los conductos de los motores, que antes asomaban sueltos.
- **Bodegas:** sus centros pasaron de 122,7 m a 150 m del eje, para no chocar con los cojinetes negros de los anillos. Se rehicieron sus soportes y bridas. Se comprobó por cálculo que ya no tocan los cojinetes, los cubos ni las torres.
- **Cerámica:** las cúpulas blindadas de las bodegas y de la popa tienen un mapeo polar: las losetas van en anillos alrededor del polo, a 80 m por repetición, sin estirarse.
- **Más luces:** `espina_foco_1..8` y `nucleo_foco_1..3` son focos con zócalo y carcasa (`*_soporte`) que alumbran la espina y la popa. El motor admite ahora 64 lámparas por fotograma; si hay más, usa las más cercanas a la cámara.
- **Puentes de acceso:** cada anillo tiene tres puentes presurizados, a 120° unos de otros, entre el cubo y la banda (`rotorN_anillo_puente_1..3`):
  - un tubo de 13 × 11 m de sección redondeada que se ensancha en los dos extremos para fundirse con el cubo y con el anillo;
  - chapa blanca mate y la cara de proa en cerámica;
  - collarines cada 24 m y tirantes al anillo de tensión (`_collarines`);
  - filas de ventanitas en los costados (`_ventanas`).

  Giran con su anillo. Para dejarles paso se quitaron las diagonales del anillo de tensión que los cruzaban.

## Bases de las vigas

- Todas las bases son redondeadas y macizas (`pad()` en `nave_lib`): planta de esquinas redondeadas, canto curvo y un tramo hundido en el casco.
  - Donde el casco tiene escalones (los collares de la espina), la base se apoya en lo más alto y su faldón inclinado baja hasta lo más hondo.
- **Radios de los anillos:**
  - Cada cordón de la celosía se alarga o recorta hasta tocar exactamente el cubo (r = 88 m) y la cara interior del anillo (r = 309 m) (`tri_truss_snap`).
  - Los manguitos hexagonales arrancan 3,5 m dentro del cubo.
  - Los arranques están a ±42 m del centro del cubo, dentro de su parte recta.

## Bahía de atraque

Bajo el puente hay una bahía hundida en la panza, de 64 × 84 m y 15 m de fondo (`puente_bahia_*`):
- cuadernas en las paredes y vigas en el techo;
- un collarín octogonal con su compuerta y cuatro garras de sujeción;
- carriles para las puertas;
- un marco de franjas de aviso (`franja_aviso`) y luces de guía pequeñas.

El nodo vacío `atraque` está en el centro del collarín, con su +Y local en glTF hacia abajo. El juego saca de ahí el ducto de atraque y, al atracar, tumba la nave hasta que la panza mira a la escotilla (`ShipDesign::dockNormal`, `GameSession::DockAttitude`).

## Zonas de carga

Cada bodega tiene en su cara exterior una compuerta curva de dos hojas (`bodega_N_compuerta`, textura `compuerta_carga`), entre dos cinchos del tanque, con su marco, una grúa de carga y luces a los lados.

## Torretas

Cada ranura es un nodo vacío `hp_torreta_N` sobre una base de 26 m de radio: `puente_ranura_torreta_1..4` y `nucleo_ranura_torreta_5..6`. La base tiene una corona con franjas de aviso, un pozo con pernos y un pivote central, y se recuperó el 2026-11-10. El anclaje queda sobre la cara superior de la base, con su +Y local en glTF hacia fuera del casco y los extras `tipo = "torreta"` y `ranura = N`. El juego los usa como **ranuras exteriores de módulos** (torretas, radiadores, sensores, blindaje): cualquier nodo vacío cuyo nombre empiece por `hp_` es una ranura, y el módulo se monta en la pieza que tiene debajo, que ahora es su base ([game/README.md](../../../game/README.md#módulos)). El anillo de giro de la torreta del juego (~21 m de radio) cubre el pozo de la base.

| Ranura | Sitio | Hacia |
|---|---|---|
| 1, 2 | Platillo, a babor y estribor | arriba |
| 3, 4 | Platillo, a babor y estribor | abajo |
| 5, 6 | Módulo de popa | arriba, abajo |

## Texturas

Son PBR: `*_color` (sRGB), `*_orm` (R oclusión, G rugosidad, B metal) y `*_normal` (tipo OpenGL), y se repiten, así que necesitan el sampler en repetición.

- Las piezas que brillan (luces, ventanas, escapes, emisor, hangar) llevan la emisión en el material. El juego la lee como brillo propio y la apaga con el daño de su zona.
- El juego usa los tres mapas: al cargar `x_color.png` busca a su lado `x_normal.png` y `x_orm.png` (`SdlGpuBackend::SetTextureMaps`), y `mesh.hlsl` los sombrea con microfacetas.
- Se generan con numpy desde el texto interno `nave_texturas` del `.blend` (`todo()` las reescribe todas en `texturas/`). Las chapas del casco, del anillo y la cerámica miden 2048 px y se repiten cada 80 m; el resto, cada 6 a 20 m.
- Al final del texto, `realistas()` rehace después las del casco (`casco_blanco`), el anillo (`anillo_blanco`), el puente (`casco_puente`), los paneles solares (`celula_solar`) y las ventanas (`ventanas`, `ventana_luz`). Hay que ejecutarla tras `todo()`, porque reescribe sus mismos archivos. `chapa()` dibuja chapas en hileras con juntas biseladas, remaches, escotillas, rejillas, mantas térmicas, rótulos, franjas de aviso, tizne de propulsores y polvo suave.
- El puente (`puente_platillo`, `puente_cupula`, `puente_cupula_inferior`) lleva su propio material, `casco_puente`: chapa más pequeña, más limpia y algo más brillante. La cristalera (`ventanas`) tiene marcos, cristal oscuro y paneles con el interior encendido.
- Los paneles de popa (`radiador_babor`, `radiador_estribor`) llevan el material `panel_solar_celulas`: módulos de 4 m con 8 × 8 células de silicio de esquinas cortadas, dedos, barras colectoras y bisagras de kapton. Para el juego siguen siendo la zona de radiadores.
  - Cada panel es una rejilla de 21 × 16 módulos de 8 m, cada uno inclinado al azar hasta 1,4°: al darles el sol, unos destellan y otros no, como en las fotos de paneles reales.
  - El silicio tiene una rugosidad de 0,08 y un metal de 0,25 (en `celulas_solares()` de `nave_texturas`), así que refleja con un tinte azul.
- Texturas nuevas del 2026-11-10, generadas aparte de `nave_texturas`: `franja_aviso` (franjas amarillas y negras), `compuerta_carga` (las compuertas de las bodegas) y `montaje_torreta` (la base, con las franjas en la corona).
- El canto del platillo y el escudo cerámico usan otro mapeo: el canto, a lo largo del contorno por altura; el escudo, desde arriba, a 80 m. Así la textura no se estira en el filo de la proa.

## Motores

Tres motores iónicos de rejilla iguales, en triángulo: `nucleo_*_principal` arriba y `gondola_babor_*` y `gondola_estribor_*` abajo. Cada uno tiene:

| Pieza | Qué es |
|---|---|
| `…_motor` | Cuerpo: unidad de potencia con dos bridas y el cuello del cardán |
| `…_motor_tanques` | Ocho tanques esféricos de xenón, forrados de lámina dorada |
| `…_motor_estructura` | Conductos, tirantes, el hexápodo que orienta la cámara y los cátodos neutralizadores |
| `…_tobera` | Tambor de la cámara de descarga, con la placa hundida y los aros de las siete rejillas |
| `…_tobera_bobinas` | Tres bobinas de cobre del confinamiento magnético |
| `…_escape` | Las siete rejillas aceleradoras, que brillan. **Solo esta pieza lleva «escape» en el nombre**: el juego saca una estela de cada una |

`nucleo_colector_popa` y `nucleo_riostras_popa` cierran el hueco entre los tres motores.

## Mallas cerradas

El juego descarta las caras traseras, así que un tubo sin tapa se ve hueco desde dentro. Toda pieza de casco que pueda verse por un extremo debe ir cerrada (`cap_open` en el historial: un abanico de triángulos al centro de cada borde abierto).
