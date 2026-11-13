# Diseño: economía, armamento y especie enemiga

Nada de esto está implementado todavía, salvo lo marcado como **hecho**. Las cifras con «(propuesta)» son de partida y se discuten; las demás las decidió el usuario. Las cifras de juego viven en [Balance.h](../game/include/game/Balance.h).

## Qué ya está hecho

- La estación de minería se eliminó (2026-11-12). Mina solo con drones. Las partidas viejas que tenían una la pierden al cargarse.
- **Fase A (2026-11-12):** `kCombatScale = 20` en `Balance.h`: casco del jugador, daño de las armas, escudos del jugador, cascos y escudos de naves y estaciones alienígenas, y los peligros ligados al casco (calor, embestida, sobrecalentamiento, reparación). Tabla `kShipClasses` (10 clases × ligera/normal/pesada). Las naves alienígenas son dron, corveta o fragata ligera según su id. Casco y escudo de la tabla en veces de la fragata ligera; `eslora`, `weaponSlots` y `heavyWeapons` son solo datos por ahora. Partidas anteriores a la versión 6 se escalan al cargar.

- **Fase B (2026-11-12):** catálogo de armas humanas en `ArmamentRules` (`Balance.h`) y reglas en `Armament.h`. Coilgun, railgun espinal, railgun 360, bahías de misiles de 10, 25 y 50 y misil nuclear, todos ligeros y pesados (14 módulos, `ShipModuleKind::CoilgunLight` en adelante). Daño nuevo `Explosive` y `Nuclear` (`DamageType`, con su fila en cada escudo). Las pesadas solo en clases con `heavyWeapons`; la espinal, solo en las clases de `spinalClass` y una por nave (propuesta). Montar pide plano (fabricado en la origami, `FabricateBlueprint`), metal con su masa real y los elementos de la tabla (W, Re, Co, Nb; los misiles, Pt/Pd/Ir o U/Th). Misiles: se fabrican atracada en una estación propia con energía, metal y elementos (`ManufactureMissiles`); el nuclear es de un solo uso. Defensa puntual: 5 puntos por hueco de arma (30 en la fragata ligera) más plataformas montables en huecos de torreta. Las bahías de drones de serie se mueven (`MoveModule`) pero no se sacan. Partidas versión 8. **Falta:** panel del taller para las armas nuevas, mallas propias (fase E), armas que usen los hostiles, el acelerador de partículas (el nuclear usa solo U y Th por ahora) y el coste de los módulos antiguos en masa real.

- **Fase D (2026-11-12):** los alienígenas tienen casco por zonas (`Anomaly::zones`), con las mismas reglas y constantes que la nave. Las zonas salen del nombre de las piezas de las maquetas (`AlienZoneOfPart`). En la nave hostil, el cuerpo es la espina, el ojo es el núcleo y las cuchillas son las góndolas. En la neutral, el disco es la espina, el centro es el núcleo y el aro es el anillo. En el bastión, el núcleo es el núcleo, el corazón es el puente, el anillo es el anillo y los brazos son las bodegas, por sectores. Cada maqueta tiene su `ZoneLayout`, con nombres propios. El proyectil cae en la zona que encuentra su trayectoria. Una zona periférica a 0 sale despedida (`LostPiece::model`). El núcleo a 0 destruye la nave; la espina o el puente a 0 la apagan: deja de moverse por sí misma y de disparar. Partidas versión 9. Modo cinemática con la N o `cinematica` (`CinematicRules`, cifras en propuesta). **Falta:** cráteres por impacto en los alienígenas (solo se ve el daño por zona) y efectos de las zonas en su velocidad. Una neutral dañada por los hostiles que luego ataca el jugador pasa al modelo hostil, y ese daño ya no se ve.

- **Fase E (2026-11-12):** modelos de Blender generados por `assets/modelos/combate/construir_combate.py`. Torretas humanas cuadradas (`torretas.gltf`): defensa puntual y coilgun, railgun espinal, railgun 360, bahía de misiles y misil nuclear, ligera y pesada (×1,6). Las tres bahías de un tamaño comparten lanzador. Naves alienígenas `alien_dron.gltf` (34 m), `alien_corveta.gltf` (300 m) y `alien_fragata.gltf` (1200 m), a la eslora de su clase. Cada clase tiene su `AlienModel` (hostil y neutral; la neutral es la misma con otros colores, `NeutralColors`) y sus zonas salen de sus piezas: cuerpo, núcleo (el ojo en el dron), cuchillas, corazón (corveta y fragata) y aro (fragata). Sin los archivos, la maqueta de `BuildAlienShip`. Las bolas de zona de una nave miden un décimo de su tamaño (entre 2 y 30 m). **Falta:** modelos del crucero en adelante (usan la maqueta), torretas que giren hacia el blanco y la espinal alineada con el eje, y cráteres en los alienígenas.

- **Fase F (2026-11-12):** seis niveles de estación (`Anomaly::stationLevel`, `kStationLevelNames`) con la vida de la tabla en `AnomalyRules::stationLevelHull` (nivel 2 = ×30 de la fragata ligera). Las estaciones generadas reciben nivel 1 a 5 por su id (`StationLevelFor`, reparto en propuesta), sin mover las semillas. Alerta por sistema (`SpeciesState`, `AlertRules`): sube en toda la región (grupos de 4) al atacar, en proporción al daño, y al destruir, según el nivel; destruir un hangar militar (nivel 4) la lleva a los 20 sistemas más cercanos; baja con el tiempo, más deprisa tras 24 h sin atacar. Destruir una capital suprema o un planeta coraza: rendición temporal (6 h, la flota se detiene y nadie dispara) y, al acabar, todas las naves vuelven con +50 % de casco, escudo, daño y velocidad (`Anomaly::boost`); las que aparezcan después, también. Planeta coraza: estación de nivel 6, 70 000 km de radio, inmóvil, hostil, en una de cada tres galaxias como mucho (en un sistema de la mitad lejana, por fuera de sus planetas), con su rango de percepción (2·10⁶ km desde la superficie) y su maqueta de esfera blindada. Partidas versión 10; las anteriores reciben niveles y su planeta coraza al cargarse. Consola: `aparecer bastion1`…`bastion5`, `aparecer capital`, `aparecer coraza`, `objetivo coraza`, `alerta`. **Falta:** las flotas que envían las estaciones de nivel 4 o más con alerta (la alerta aún no tiene efecto en el combate), un indicador de alerta en la interfaz, escudo, botín, tamaño y armas propios de cada nivel (todas usan el escudo, el botín, los 2,5 km y el misil de siempre) y la superficie del planeta coraza como astro del motor.

## 1. Economía de materiales

- Material a granel (metal, silicato, hielo, gas): **barato**. Los costes de las piezas pasan a ser su masa real: una torreta pesa de 0,2 a 1,7 kt, no 260.
- Lo escaso son los **elementos raros** de la tabla periódica (118, ya generados por `AllElementsOf`, con variación por astro y vetas).
- Objetivo de minería: **10 kt/h por dron** nominales. La bodega del dron pasaría de 20 a ~5 kt (propuesta).
- Los elementos se guardan en un inventario aparte, en kg y por número atómico. Se muestran en la **unidad más cercana** (mg, g, kg, t, kt, Mt), y en el panel de almacén va al lado un **diagrama de prefijos del SI** que resalta la unidad de cada cantidad.
- **Extracción selectiva** (propuesta): el jugador elige un elemento en el análisis y los drones lo extraen con rendimiento = ritmo × fracción × recuperación. Una veta puede dar 100 veces más.
- **Depósitos en órbita:** los fabrica la estación origami y se anclan a la nave (hueco exterior). Variante a estudiar: desprendibles.

### Qué pide cada cosa

| Arma o sistema | Elementos |
|---|---|
| Coilgun y railgun humanas | W, Re, Co; bobinas de Nb |
| Láser y fotónico | Nd, Y, Yb |
| Misil explosivo | Pt, Pd, Ir (propuesta) |
| Sensores | Ge, Ga, In, Te |
| Motores iónicos | Xe, del gas de los gigantes |
| Misil nuclear | Radiactivos naturales (U, Th), más otros hechos en el acelerador |
| Armas de otros diseños | Un **plano alienígena** (único por diseño, de estructuras y restos) más elementos de Z ≥ 95, que no existen en la naturaleza |

- **Acelerador de partículas:** va en la **estación origami**, como módulo (será el primero que tenga, ya que hoy no es modular). Pide más radiactivos naturales y fabrica los elementos pesados.
- Armas humanas: no necesitan rastros de tecnología alienígena, pero sí **planos fabricados en la estación espacial principal** (la origami) y **muchos recursos** (elementos raros y masa).
- Armas ajenas: plano alienígena más elementos.

## 2. Armamento

### Humano
Torretas **cuadradas, de estilo industrial rígido**. Cada tipo tiene versión ligera y pesada; la pesada solo la montan las naves de categoría pesada.

| Arma | Papel |
|---|---|
| Coilgun | Débil, alta cadencia |
| Railgun | Más potente y más lenta que la coilgun |
| Railgun espinal | Fija al eje, la más potente. No se instala en todas las naves y obliga a girar la nave entera hacia el blanco: buena de lejos, malísima de cerca |
| Railgun 360 | En torreta móvil: peor en todo que la espinal, pero apunta a cualquier lado |
| Bahía de misiles | Explosivo clásico, derribable |
| Misil nuclear | Uno por torreta, caro, derribable, de un solo uso |

- Los misiles cuestan **energía y materiales** al fabricarse. Bahías: pequeña 10 misiles, mediana 25, grande 50.
- **Daño:** los misiles (explosivo y nuclear) son un **tipo de daño nuevo**.
- **Defensa puntual:** módulos aparte de las torretas, más plataformas de defensa puntual montables en huecos de torreta clásica.

### Alienígena
Láseres de pulso, plasma, torpedos de antimateria, railguns relativistas, cortadores de partículas, etc.

### Huecos de armas

- **Nave de inicio (fragata ligera):** 6 torretas normales y unos **30 puntos de defensa** repartidos por toda la nave. Las dos primeras (`hp_torreta_5` y `hp_torreta_6`) son las **bahías de drones mineros** con las que sale la nave. Se pueden cambiar de hueco a gusto del jugador, pero esas dos bahías **no se pueden sacar del inventario de la nave**, para que nadie se quede sin minería por accidente. Hoy quitar un módulo devuelve la mitad del material; hará falta un inventario de módulos y marcar esas dos como «de serie».
- Orden de magnitud por clase (propuesta): dron 1, corveta 2 a 3, fragata ligera 6, supercarguero ~**100**, capital suprema ~**600**. Las intermedias se interpolan.
- **Armas pesadas:** el jugador podrá usarlas, con **naves pesadas** que llegan más adelante en la partida.
- Habrá **naves sigilosas**; el sistema se diseñará después. Ya se sabe que, como excepción de algunos modelos de tecnología alienígena, podrán **cambiar varias armas ligeras por una pesada**, o por **misiles de antimateria o nucleares**. Esto rompe la regla de «pesadas solo en naves pesadas» y la tabla de clases debe poder expresarlo.

### Escala de combate
- Hostiles con casco y escudo ~**20 veces** los actuales. Se propone escalar todo ×20, también la nave del jugador, y el daño (la torreta actual, 20 por 3 s, es muy poco).
- La nave del jugador **no está hecha para pelear** con alienígenas: equivale a una **fragata ligera**.

## 3. Clases de nave alienígena

Designación humana, de menor a mayor: dron, corveta, fragata, crucero, destructor, acorazado, carguero, supercarguero, capital, capital suprema. Cada clase tiene versión **ligera, normal y pesada**, que cambia tamaño, velocidad máxima, nivel de armamento y resistencia. El dron minero (34 m) es el equivalente humano del dron alienígena más pequeño.

Casco aproximado (propuesta; la fragata ligera = 1): dron 0,01; corveta 0,15; fragata 1 a 2; crucero y destructor 4 a 10; acorazado y carguero 20 a 50; supercarguero y capital 100 a 1000; capital suprema 3000 o más. Ligera ×0,7, normal ×1, pesada ×1,5.

Los enemigos deben tener el **mismo sistema de zonas** vulnerables y prescindibles que la nave humana (`HullZones`), con partes que se desprenden.

### Naves mineras alienígenas (decidido 2026-11-12)

Las mineras **no tienen variante ligera ni pesada**: una sola forma por clase (corveta, fragata, crucero, carguero, supercarguero). Usan como base la fila «normal» de `kShipClasses` (eslora, casco, escudo, velocidad). Las bahías de drones son un tipo de hueco propio («hangar»): en corveta, fragata y crucero ocupan huecos que no son pesados; en carguero y supercarguero todos los huecos son pesados. Cada bahía lleva 2 drones (`DroneRules.perBay`) y cada dron da 10 kt/h.

| Clase minera | Eslora | Drones | Hangares | Minería (kt/h) | Armas |
|---|---|---|---|---|---|
| Corveta | 300 m | 2 | 1 | 20 | 1 |
| Fragata | 1,5 km | 6 | 3 | 60 | 4 |
| Crucero | 2,8 km | 12 | 6 | 120 | 6 |
| Carguero | 8 km | 24 | 12 | 240 | 14 (pesadas) |
| Supercarguero | 20 km | 60 | 30 | 600 | por decidir: 25 con 100 huecos, o hasta 70 si se mantienen los 100 |

- Las cifras de drones de carguero y supercarguero son el **rebalance de un amigo del usuario**, aceptado: la progresión es más suave (20, 60, 120, 240, 600 kt/h) que la propuesta inicial (…320, 1500).
- **Lag:** lo que pesa es cuántos drones hay activos cerca del jugador, no cuántos tiene la nave. Propuesta: tope de **drones simulados y dibujados por nave** (~24) en `Balance.h`; los demás cuentan en el ritmo de minería sin simularse, y fuera de rango la minería es solo un ritmo (drones × 10 kt/h). Los mineros enemigos pueden ser un efecto visual, sin lógica por dron.
- **Modelos:** anclajes `hp_torreta_N` para armas y `hp_hangar_N` para hangares (nombre nuevo); bodegas visibles 1, 3, 4, 6 y 6 (el motor limita a 6 zonas de bodega). El supercarguero debe generarse por script. Silueta propuesta: más angular y con bodegas muy visibles, distinta de la nave humana.
- **Por implementar:** tabla `kMiningShips[5]` en `Balance.h`, tipo de hueco «hangar» en `Armament.cpp`, y que el generador de anomalías las use en lugar de ligera/normal/pesada.

## 4. Estaciones alienígenas

Seis niveles. Son estaciones espaciales concretadas, de enorme tamaño.

| Nivel | Estación | Vida (×fragata ligera) |
|---|---|---|
| 1 | Drones de defensa (relativamente sencilla) | 10 (propuesta) |
| 2 | Defensa industrial ligera | **30** |
| 3 | Urbana | 100 (propuesta) |
| 4 | Instalación de hangares militares | 300 (propuesta) |
| 5 | Instalación acorazada de defensa suprema | 1500 (propuesta) |
| 6 | Planeta coraza | 30 000 o más (propuesta) |

### Alerta de la especie
- La alerta se guarda en la partida y es **por región de sistemas**, en grupos de **4 o 10**.
- Sube al atacar. Destruir un nivel 1 casi no la sube; un nivel 5 se nota en toda la región.
- **Destruir un hangar militar (nivel 4)** extiende la alerta hasta **20 sistemas**, para castigar más la violencia.
- Con alerta, las estaciones de nivel 4 o superior envían flotas a perseguir al jugador.
- Baja con el tiempo, más rápido si no se vuelve a atacar.

### Rendición y revancha
Si el jugador destruye un **planeta coraza** o una **capital suprema**, la flota se detiene un rato en señal de **rendición temporal**. La siguiente vez, todas sus naves vuelven con **+50 %** en casi todas las estadísticas.

### Jefes finales
El planeta coraza y la capital suprema son **jefes finales**, detrás de los Unicornos.

- **Planeta coraza:** del tamaño de un gigante como Júpiter (unos 70 000 km de radio). Es un huevo de pascua muy raro de encontrar. Si el jugador lo encuentra, debe huir de su rango de percepción muy deprisa. No se mueve. Es mucho más difícil que la capital suprema.
- **Capital suprema:** se mueve y es el jefe final más accesible.

## 5. Modo cinemática

Cámara que sigue el impacto en el oponente, con tiempo lento y órbita alrededor del blanco, para apreciar el daño. Necesita las zonas en enemigos y modelos detallados de nave alienígena; ahora todos los alienígenas son maquetas de primitivas (`BuildAlienShip` y compañía).

## 6. Fases propuestas

| Fase | Contenido |
|---|---|
| A | Cifras y tabla de clases: escala ×20, `ShipClass` |
| B | Catálogo de armas humanas, tipos de daño nuevos y reglas de montaje |
| C | Inventario de elementos, unidades y diagrama del SI |
| D | Zonas en enemigos y modo cinemática |
| E | Modelos: torretas humanas cuadradas y naves alienígenas por clase |
| F | Niveles de estación y alerta por región |

## Pendiente de decidir

- El planeta coraza a nivel de código: al medir como Júpiter, lo natural es un astro del sistema con superficie blindada (el motor ya dibuja gigantes), más una vida y una percepción propias. Las anomalías de hoy se mueven en escalas de km.
- Quién fabrica los misiles (la nave o la origami) y su coste de energía.
- Cuántos huecos de armas montan las clases intermedias.
- El sistema de naves sigilosas.
