# Multijugador: análisis y plan

Estado: **propuesta, sin código**. Escrito tras leer `Session.h`, `Session.cpp` (`Step` y los miembros), `Ship.h`, `SaveGame.h/.cpp`, `main.cpp` y, del motor, `Engine.h`, `Engine.cpp` (`Advance`) y `Universe.h`.

Objetivo: cooperativo (varios jugadores en la misma nave) y, después, varias naves distintas en el mismo universo, en LAN y con servidor dedicado. No hay que conservar partidas antiguas.

Las decisiones que hacen falta están al final ([§7](#7-decisiones-que-necesito)). Donde el plan depende de una, lo dice y propone un valor por defecto.

---

## 0. Cómo está hoy, en cinco hechos

1. **`GameSession` lo es todo a la vez**: el mundo (estaciones, anomalías, proyectiles, reloj), la nave (`ship_` y unas 50 variables más) y el jugador (tutorial, registro, lo que conoce, el avatar a pie). No hay ninguna frontera entre los tres.
2. **El reloj del mundo lo mueve la nave.** En `Step`: `proper = real × timeScale_ × timeFocus_` y `seconds = proper × exteriorFactor_`; `simTime_ += seconds`. Si la nave va a 10 c, el universo entero corre `maxTimeFactor` veces más deprisa. Con dos naves eso no tiene sentido tal cual.
3. **El motor tiene un solo punto de vista.** `World::origin` (origen flotante) y `StreamWorld(camera)` cargan los sistemas alrededor de *una* posición, y `Engine::Advance` recibe *un* `simTime`. Dos naves en sistemas distintos no caben en un `World`.
4. **Las órdenes son llamadas directas.** El mando continuo va en `ShipControls`, pero todo lo demás (`SetDrive`, `Fire`, `Dock`, `DeployDrones`, `InstallModule`… unos 40 métodos que cambian el estado) lo llaman `main.cpp` (a través de `perform(HudAction)`) y los comandos de consola. No hay nada que se pueda enviar por un cable.
5. **Lo bueno**: `GameSession` no conoce la ventana, `epsilon_game_logic` ya es una biblioteca aparte que compila sin SDL (presets `x64-release`, `x64-asan`), las pruebas juegan partidas enteras sin GPU y `HudAction {tipo, entero}` ya es casi un formato de orden. El servidor sin ventana está medio hecho.

---

## 1. Miembros de `GameSession`: de quién es cada uno

Categorías:

- **Mundo**: uno por partida; lo guarda y lo manda el servidor.
- **Burbuja**: uno por zona simulada (ver [§2](#2-el-tiempo)). En la fase 3 solo hay una, así que equivale a «mundo».
- **Nave**: uno por nave.
- **Jugador**: uno por persona conectada.
- **Cliente**: solo presentación; no se sincroniza ni se guarda.

Columna «Sinc.»: **G** se guarda, **I** va en las instantáneas, **E** se manda como evento suelto, **D** se deriva (se recalcula en cada lado), **—** no viaja.

### Mundo y burbuja

| Miembro | De quién | Sinc. | Notas |
|---|---|---|---|
| `engine_` | Burbuja | — | Hoy una referencia única. Un `World` por burbuja en el servidor; el cliente conserva el suyo. |
| `rules_` | Mundo | — | Constante. Debe ser idéntico en todos: se compara una suma de `Balance` al conectar. |
| `seed_` | Mundo | G | |
| `mode_` | Mundo | G | Dificultad de la partida, igual para todos. |
| `stations_` | Mundo | G I | Falta un campo **dueño** en `Station` (decisión 4). `Produce(hours)` hoy corre con el reloj de la nave: pasa a producir por sello de tiempo. |
| `anomalies_` | Mundo | G I | `UpdateAnomalies` persigue y dispara a `ship_`: debe elegir blanco entre las naves cercanas. Solo se simulan dentro de `activeRange` de alguna nave. |
| `species_` | Mundo | G I | Alerta por sistema y rendición: ya usa instantes de `simTime_`; con relojes por burbuja pasa a sellos por sistema. |
| `projectiles_` | Burbuja | I | Tienen `team`; falta la **nave que disparó** (para `kills_`, `lastStrike_` y el fuego amigo). No se guardan, como hoy. |
| `impacts_` | Burbuja | E | Destellos de 0,6 s: un evento, no estado. |
| `lostPieces_` | Burbuja | E | Trozos de casco (de naves y de anomalías). Evento con semilla; cada cliente los anima. |
| `rng_` | Mundo | — | Hoy mezcla lo que decide (puntería, zona del impacto, defensa puntual, anomalías) con lo decorativo (giro de los trozos, tripulación a pie). Hay que partirlo en dos: el de simulación, solo en el servidor. |
| `rocks_` | Burbuja | D | Caché de rocas por semilla de familia: cada lado la regenera. |
| `pins_` | Burbuja | D | Cuerpos que no se descargan: la unión de los de todas las naves de la burbuja. |
| `simTime_` | Burbuja | G I | El reloj. Ver §2. |
| `timeScale_` | Mundo | G | Hoy lo cambia un comando de consola. En red, ajuste del servidor. |
| `timeFocus_` | Burbuja | I | Hoy lo calcula `UpdateFocus` mirando una nave. Ver §2. |
| `steps_` | Burbuja | — | Contador de pasos (cadencia de `UpdateContacts`). |
| `body_` | Mundo | — | Reglas del humanoide: constante. |
| `deck_`, `deckKind_` | Burbuja | D | Planta de la cubierta de la estación atracada: sale de la estación. |
| `crew_` | Burbuja | I (baja frec.) | Tripulantes de la estación, con `rng_`. Decorativos: se puede simular en cada cliente con una semilla común. |
| `checkpoint_` | Mundo | — | **Desaparece.** Guarda nave + estaciones + anomalías + reloj y vuelve el mundo atrás: imposible con más de una nave. Se sustituye por una regla de reaparición (decisión 5). |
| `saveWanted_`, `saveLost_` | Mundo | — | Avisos al bucle principal; pasan al servidor. |

### Nave

| Miembro | Sinc. | Notas |
|---|---|---|
| `ship_` (`ShipState`) | G I | `position` es relativa al origen del `World`: en el cable y en el archivo va la absoluta (como ya hace `absolutePosition`). `velocity` es relativa al cuerpo dominante (`frame_`). |
| `autopilot_`, `orbiting_`, `tracking_` | G I | Hoy solo se guarda `orbiting_`. |
| `orbitOffset_`, `orbitNormal_`, `orbitRate_` | G I | |
| `frame_` | D | Se recalcula en cada paso (`UpdateFrame`). |
| `routePhase_`, `routeDistance_`, `fuelWarned_` | I | Estado del piloto automático. |
| `dockPhase_`, `dockWanted_`, `undockGrace_`, `dockedAge_` | G I | Hoy solo se guarda «atracada sí/no». |
| `approach_`, `warning_` | D | Avisos para la interfaz; se pueden recalcular en el cliente o mandar. |
| `exteriorFactor_` | I | Dilatación de *esta* nave. Ver §2. |
| `nearestSystem_`, `inHeatZone_`, `radiators_`, `ramming_` | D / I | Derivados del sitio y del estado. |
| `phase_`, `wreckAge_`, `wreckPosition_`, `lossReason_` | I | Volando, estallando, a la deriva, perdida. Hoy es «la partida»; pasa a ser «esta nave». |
| `failedZone_`, `driftVelocity_` | I | |
| `hitStreak_`, `hitDamage_`, `hitShield_`, `directedLoss_` | — | Acumuladores del paso y de los avisos de impacto. Solo en el servidor. |
| `zoneHeat_` | I | Brillo de lo recién golpeado. |
| `hullHits_`, `nextHit_` | E | El cráter lo calcula `GameLayer`: basta el evento del golpe. |
| `lastWarpCharge_` | — | Auxiliar del calor. |
| `ramWarned_`, `overheatWarned_`, `gasWarned_`, `dronesFullWarned_` | — | «Ya se avisó»: en el servidor, junto a la nave. |
| `drones_`, `droneSite_`, `droneDeposit_`, `extractZ_`, `droneYield_` | I | No se guardan (al cargar están en sus bahías), como hoy. |
| `scanProgress_`, `lastScan_`, `echo_`, `echoTarget_`, `scansDone_` | I | El barrido es de la nave; lo que descubre va al conocimiento (abajo). |
| `exploring_` | I | La estructura que se está explorando. |
| `contacts_` | D / I | Lo que ven los sensores de esta nave. |
| `creative_` | I | Hoy es de la sesión, pero lo que hace es rellenar la nave. Pasa a ser una marca de la nave que solo puede poner un administrador (decisión 8). |

Aparte: la **clase de la nave** hoy es global (`rules_.ship.classId` y, según los comentarios de `Ship.h`, `CurrentShipDesign()`). Con naves distintas por jugador, el diseño tiene que colgar de cada nave. Es un cambio propio dentro de la fase 3a.

### Jugador

| Miembro | Sinc. | Notas |
|---|---|---|
| `dialogue_` (tutorial) | G | Sus condiciones miran la sesión entera (`Update(*this, …)`): hay que darle «mi nave + mi jugador». Decisión 8. |
| `analysisUsed_`, `cursorUsed_`, `collected_` | G | Marcas y contadores del tutorial. |
| `log_` | E | Hoy `Say()` escribe en una cola única. Cada mensaje necesita destinatario: un jugador, la tripulación de una nave o todos. Es el cambio más repartido por el código (hay `Say` por todas partes). |
| `known_`, `visited_`, `scanned_` | G I | Lo que se conoce. Por jugador o por equipo: decisión 3. Por defecto, **por equipo**. |
| `kills_` | G | Estadística. |
| `onFoot_`, `player_` | I | El avatar. Hoy «a pie» excluye «volando» para toda la sesión; con tripulación, uno puede pasear por la estación mientras otro mira el taller. |
| `lastStrike_`, `strikeAge_` | E | «Mi último impacto», para la cámara. |

### Cliente (no viaja)

| Miembro | Notas |
|---|---|
| `firstPerson_` | Vista a pie. |
| `cinematic_` | Preferencia de cámara. Hoy además frena el tiempo: ver §2.4. |

Ya son del cliente y están en `main.cpp`: el objetivo seleccionado (`target`, `contactIndex`), la cámara (`viewOrbit`, `viewDistance`), el atlas y la consola. Eso ayuda: cada jugador tendrá su propio objetivo sin tocar nada.

---

## 2. El tiempo

### 2.1 Qué hay hoy

Tres medidas de tiempo conviven en `Step`:

| Medida | Cálculo | Qué avanza con ella |
|---|---|---|
| **Real** | `realSeconds` | Giro del casco (`StepHeading`), atraque, barrido, andar a pie, edad de trozos y restos, el propio `timeFocus_`. Una maniobra dura lo mismo sea cual sea la escala. |
| **Propio** (a bordo) | `real × timeScale_ × timeFocus_` | Reactor, calor, `shipHours`, empuje del motor. |
| **Exterior** (mundo) | `propio × exteriorFactor_` | `simTime_`: órbitas, producción de estaciones, drones, anomalías, proyectiles, reparación, distancia recorrida. |

- `timeScale_` (60 por defecto): 1 s real = 1 min.
- `timeFocus_` (de 1 hasta `1/timeScale_`): baja en peligro, cerca de una estación, a pie o con la nave perdida.
- `exteriorFactor_` = `ExteriorRate(velocidad, pozo gravitatorio, maxTimeFactor)`: Lorentz por debajo de c, y el tope por encima. «Un viaje entre estrellas lleva días fuera y minutos a bordo».
- Modo cinematográfico: multiplica `realSeconds` por `cinematic.timeFactor` al principio del paso, así que frena **todo**, incluido lo que va en tiempo real.
- Pausa: `main.cpp` llama a `Step(0, …)` cuando la pantalla no es la de juego.

El ritmo del mundo va, por tanto, de ×1 (arrimándose a una escotilla) a ×60 × `maxTimeFactor` (en hiper). Un reloj único no puede servir a la vez a una nave que atraca y a otra que cruza la galaxia.

### 2.2 Opciones

| Modelo | Qué pasa | Veredicto |
|---|---|---|
| **A. Un reloj; manda la más rápida** | Quien va en hiper acelera el mundo de todos. A quien está en combate le llueven proyectiles a ×1000. | Solo vale si hay una nave (fase 3b): ahí es exactamente lo de hoy. |
| **B. Un reloj; manda la más lenta** | El que viaja no puede acelerar el mundo: su viaje entre estrellas dura `maxTimeFactor` veces más tiempo real. Horas. | No. |
| **C. Un reloj; la dilatación se convierte en velocidad** | El mundo no envejece más; la nave que dilata recorre `exteriorFactor_` veces más distancia por segundo de mundo. | Jugable y sencillo, pero la nave supera c en crucero «de mentira» y los viajes dejan de hacer producir a las estaciones. Rompe la regla de unidades reales. |
| **D. Un reloj por burbuja** | Cada zona con naves tiene su reloj. A solas, la nave lo mueve como hoy. Juntas, se ponen de acuerdo. | **Recomendado.** |

### 2.3 Reloj por burbuja (modelo elegido)

**Burbuja** = un sistema estelar (o un tramo de espacio abierto) donde hay al menos una nave. Cada burbuja tiene su `World` (su origen flotante, sus sistemas cargados), su reloj `t` y su `timeFocus`. Encaja con el motor sin retocarlo: `Engine::Advance` ya recibe el `simTime` en cada llamada.

Por qué se puede: casi nada del mundo necesita un «ahora» común.

- Las **órbitas** son analíticas (`UpdateOrbits(orbits, time, …)`): se evalúan en cualquier instante.
- Las **anomalías** solo se mueven cerca de una nave: viven en la burbuja de esa nave.
- Las **estaciones** producen `ritmo × horas`: se integra de golpe con un **sello** `producidoHasta` por estación.
- La **alerta** de la especie decae exponencialmente por sistema: igual, con sello por sistema.
- Los **choques** del motor ya se resuelven «al cargar» según `World::simTime`.

Reglas:

1. **Una nave sola en su burbuja** la mueve como hoy: `dt = real × timeScale × timeFocus × exteriorFactor`. Un jugador solo no nota ningún cambio. Es el caso que las pruebas actuales siguen cubriendo.
2. **Varias naves en una burbuja**: `dt_burbuja = real × timeScale × min(focus_i) × min(exterior_i)`.
   - El `min` del foco: si una está en peligro, todas van despacio. Es lo justo en combate.
   - El `min` del exterior: manda la que no dilata. La que va rápida recibe tiempo propio `dt_burbuja / exterior_i`: su reactor, su calor y su `shipHours` van más lentos (relatividad de verdad: los gemelos), pero **no** acelera el mundo de las demás. Dentro de un sistema, en crucero, eso alarga su trayecto por el factor de Lorentz (pequeño salvo muy cerca de c). En hiper sale de la burbuja en segundos y vuelve a la regla 1.
3. **Salir de una burbuja**: la nave se lleva una copia del reloj y abre la suya (espacio abierto).
4. **Entrar en una burbuja vacía** (un sistema sin nadie): `t = max(sello del sistema, reloj de la nave)`. El tiempo de un sistema nunca retrocede. Sus estaciones producen lo que les falte hasta `t`.
5. **Entrar en una burbuja ocupada**: la que llega **adopta el reloj de la que está**, aunque sea anterior al suyo. Sus «días de fuera» del viaje se quedan en el viaje; a bordo solo pasaron minutos, así que para la tripulación es coherente. La burbuja ocupada no da saltos: quien está dentro no ve los planetas moverse de golpe.
6. **Reloj de mundo** `T = max(t de todas las burbujas y sellos)`. No mueve nada: es la fecha que se enseña, la que se guarda y la que usan los sucesos globales (rendición y revancha de la especie, que hoy son instantes absolutos).
7. **Acciones a distancia** (el enlace `relayTier`, que vacía estaciones lejanas): la estación produce hasta el reloj de quien la consulta si es posterior a su sello; si no, se queda como está. Nunca se produce dos veces el mismo tramo.

Efecto visible que hay que aceptar: dos jugadores pueden tener fechas distintas hasta que se juntan, y al juntarse vale la del que estaba. No hay forma de viajar al pasado ni de duplicar producción, porque todo sello solo crece.

En la fase 3 (antes de la red) basta con la regla 2 y **una sola burbuja**; las burbujas múltiples son la fase 4.

### 2.4 Foco, cinemática, pausa y a pie

- **`timeFocus`** pasa a calcularse **por nave** (`UpdateFocus` ya mira solo lo que rodea a una) y la burbuja usa el mínimo.
- **A pie**: hoy pide tiempo real al mundo entero (`1/timeScale_`). El avatar ya se simula en segundos reales (`StepFoot`), así que en compañía no hace falta frenar a nadie: un jugador a pie **no vota** en el foco de la burbuja si hay otra nave volando en ella. A solas, como hoy.
- **Modo cinematográfico**: la cámara es del cliente y se queda. El freno del tiempo solo se aplica si **todas** las naves de la burbuja son de jugadores que lo tienen activado (a solas, siempre). Si no, toma sin cámara lenta. Decisión 10.
- **Pausa**: en red no existe; Esc abre el menú y el mundo sigue. En solitario (servidor dentro del propio juego, sin nadie más), como hoy. Con varios en la misma nave, se puede ofrecer «pausa por acuerdo».
- **`timeScale_`**: uno por servidor, no por jugador.

---

## 3. Plan de migración

Cada paso deja el proyecto **compilando y con las pruebas en verde** (`epsilon_game_tests`, `epsilon_tests`), y es un commit propio. Como casi todos tocan cabeceras: `--clean-first`.

### Fase 0 — Red de seguridad (sin cambiar el comportamiento) — **hecha el 13-11-2026**

| Paso | Qué | Cómo quedó |
|---|---|---|
| 0.1 | **Partida dorada**: una prueba que juega con `dt` fijo y órdenes fijas (viajar, minar, maniobrar, atracar, combatir) y compara `WriteSave(Snapshot())` con un texto guardado. | Grupo «Partida dorada» de `game_tests.cpp`, contra `tests/data/partida_dorada.txt`. Se generó **antes** de tocar la sesión y sigue pasando después. Además comprueba que dos partidas seguidas dan exactamente lo mismo. Se regenera con `--regenerar-dorada` (`docs/pruebas.md`). |
| 0.2 | **Órdenes como datos**: `ShipCommand { tipo, objetivo, value, index, amount }` y `GameSession::Apply`, que devuelve `CommandResult { ok, count, why }` y rechaza valores fuera de su enumeración. | `perform(HudAction)` y los comandos de consola que son órdenes del jugador pasan por `Apply`. Los métodos de siempre siguen ahí para las pruebas. |
| 0.3 | **Paso fijo opcional**: `GameSession::StepFixed`, con acumulador, a 1/60 s. Suma los giros que llegan entre dos pasos. | Opción `--fixed-step` en el juego. Sin interpolación todavía: por encima de 60 FPS hay fotogramas sin paso. |

Lo que **no** pasa por `Apply`, a propósito:

- Los **trucos de consola** (`saltar`, `regalo`, `nivel`, `aparecer`, `creativo`, `casco`, `viajar`, `tiempo`, `tutorial`, `dano`…) siguen tocando la sesión directamente. En red no serán órdenes: el texto del comando viajará al servidor y se ejecutará allí, solo para el administrador (decisión 8).
- Lo que es **del cliente**: `ToggleFootView` y todo lo de `HudInput` (atlas, taller, radar, ajustes).
- La **gestión de la partida**: `SetMode`, `Snapshot`, `Restore`, `Announce`/`Notice`. Pasan al servidor en la fase 5.

### Fase 1 — Separar el estado dentro de `GameSession` (una nave, un jugador) — **hecha el 13-11-2026**

Cómo quedó (el plan original sigue debajo):

- **1.1–1.3** Los miembros de `GameSession` viven ahora en cuatro estructuras de `Session.h`: `Vessel` (`vessel_`), `PlayerState` (`pilot_`), `WorldState` (`world_`) y `Bubble` (`bubble_`). En la sesión solo quedan el motor, las reglas y los avisos de guardado. `ApproachInfo`, `LostPiece`, `StrikeMark` y `HullHit` salieron de la clase (siguen valiendo como `GameSession::HullHit`). Cambio mecánico: la partida dorada dio idéntica.
- **1.2** `Say(texto, Audience)` con `Crew` (por defecto), `Player` y `Everyone`. Todos siguen yendo al único registro; solo están marcados `Announce`/`Notice` (jugador) y la rendición y la revancha de la especie (todos). **El resto de los `Say` están sin clasificar**: van como `Crew`.
- **1.4** `Step` son cinco tramos, no cuatro: `BeginBubble`, `StepVessel`, `AdvanceBubble`, `SettleVessel` y `FinishVessel`, con el trabajo de la nave y de la burbuja (drones, barrido, anomalías, combate) entre los dos últimos. Lo que una nave recuerda de un tramo a otro va en `VesselStep`. Sigue habiendo una salida temprana que con varias naves no valdrá: si la nave está perdida, anomalías y combate no avanzan.
- **1.5** `Bubble::rng` (simulación) y `Bubble::fx` (decoración: giro de los trozos sueltos, tripulación a pie). La dorada no cambió.
- **1.6** Sin `Checkpoint`. Al perder la nave el mundo no vuelve atrás; `Respawn()` la deja reparada y atracada en `Vessel::homeStation` (la última estación propia donde atracó) o en el punto de partida, con la pérdida de bodega del modo. Si el sistema de la estación no está cargado, aparece en él y atraca en cuanto lo está. Los módulos perdidos en combate no vuelven. El guardado lleva una línea nueva, `base <estación> <perdida>`: una partida guardada con la nave perdida reaparece al cargar. `SaveCheckpoint` solo avisa («Partida guardada») y pide guardar.
- **1.7** `StationNetwork::ProduceUntil` y `DecayAlertUntil`: sello `producedUntil` por estación y `decayedUntil` por sistema. Los sellos aún no se guardan (al cargar se ponen al reloj de la partida); entran en el formato de la fase 2. Pendiente para la fase 4: con saltos largos, la alerta decide «en calma o no» con el instante final, no a lo largo del tramo.

Cambios mecánicos, guiados por la tabla del §1. La clase sigue siendo una; solo se agrupan sus miembros.

| Paso | Qué |
|---|---|
| 1.1 | `struct Vessel` con todo lo de «Nave». `GameSession` tiene `Vessel vessel_`; `ship_` pasa a ser `vessel_.ship`. Por grupos, un commit cada uno: vuelo y órbita · atraque · daños · drones · barrido. |
| 1.2 | `struct PlayerState` con lo de «Jugador». `Say(texto)` pasa a `Say(destino, texto)` con tres destinos: jugador, tripulación, todos. |
| 1.3 | `struct WorldState` (estaciones, anomalías, especie, semilla, modo) y `struct Bubble` (motor, reloj, foco, proyectiles, destellos, trozos). |
| 1.4 | Partir `Step` en cuatro tramos, aún para una nave: `BeginBubble` (reloj, foco) → `StepVessel(v, mandos)` (mando, motor, atraque) → `AdvanceBubble` (recentrar, `Engine::Advance`, estaciones, anomalías, proyectiles) → `FinishVessel(v)` (choques, calor, reparación, contactos). El orden actual se conserva tal cual. |
| 1.5 | Partir `rng_` en simulación y decoración. *Cambia la partida dorada*: se regenera en este commit y solo en este. |
| 1.6 | Quitar `checkpoint_` como vuelta atrás del mundo: reaparición según la decisión 5. *Cambia el comportamiento*: pruebas nuevas. |
| 1.7 | Producción de estaciones y alerta por **sello de tiempo** en vez de por `hours` del paso. Con una nave da lo mismo (salvo redondeo): la partida dorada lo confirma. |

### Fase 2 — Guardado nuevo — **hecha el 13-11-2026**

Cómo quedó:

- Formato 13: carpeta con `mundo.sav` y `jugadores/<id>.sav`, cada uno con su cabecera y su suma. En el código, `SaveData { WorldSave world; PlayerSave player; }`, con las naves en `WorldSave::ships` (`ShipSave`, un bloque `nave <id>` por nave). `SaveGame`, `LoadGame` y `DeleteGame` trabajan con la carpeta; `WriteSave`/`ReadSave` dan y leen los dos documentos seguidos, que es lo que se mandará a un jugador que entra.
- Cargar un mundo sin archivo de ese jugador no es un error: entra como jugador nuevo, con la primera nave.
- Se guardan los sellos de tiempo de estaciones y alerta, y dos datos del jugador que antes se perdían al cargar (`analisis`, `bajas`).
- El identificador del jugador se crea al azar la primera vez y va en `ajustes.txt` (línea `jugador`). Con `--frames` es `local`.
- Fuera del formato: los ceros del yacimiento de las minas y el descarte de las minas retiradas.
- **No hecho**: lo que se conoce va en el archivo del jugador y no hay sección `equipo` (decisión 3 sin tomar); las estaciones no llevan dueño (decisión 4 sin tomar); `Restore` conserva sus ramas para partidas «anteriores al combate» y «anteriores a la fase F», que ya no pueden darse.

Formato del §5, versión 13, todavía con una nave y un jugador. Prueba de ida y vuelta: guardar, cargar y guardar da el mismo texto.

### Fase 3a — Dos naves locales, una entrada por nave — **hecha el 13-11-2026, incluido el paso 3a.6**

Cómo quedó:

- **Flota en la sesión.** `GameSession` guarda `fleet_` (naves) y `crews_` (un jugador por nave, en el mismo orden). No se pasó la nave como parámetro a cada método: hay una **nave en curso** (`vessel_`, `pilot_`), sobre la que trabaja todo el código, y `Use()`/`Using` la cambian. Fuera de `Step` la nave en curso es siempre la del **foco** (`SetFocus`), que es de la que hablan `Ship()`, `Apply()`, `Contacts()` y lo demás. Es menos explícito que un parámetro, pero dejó intactas las firmas de más de cien métodos y la partida dorada pasó sin regenerar. `ApplyTo(id, orden)` manda una orden a una nave concreta.
- **Paso.** `StepAll(real, mandos)` lleva un mando por nave; `Step` le da el suyo a la del foco. El reloj sigue la regla 2 del §2.3: `Vessel::timeFocus` y `exteriorFactor` por nave, y la burbuja usa el mínimo de cada uno. La nave que dilata más recibe menos tiempo propio. La cámara lenta de la cinemática solo se aplica si todos la están viendo.
- **El mundo las ve a todas.** Cada anomalía atiende a la nave en vuelo más cercana. Los proyectiles llevan `Projectile::ship` (quién disparó, o a quién apuntan): un proyectil enemigo puede dar a cualquier nave, y la baja, el botín y los avisos son de quien disparó. Sin fuego amigo. Dos naves chocan y se dañan las dos. `TargetRef::Kind::Ship` deja apuntar, seguir e ir con el piloto a otra nave, que sale siempre en los sensores.
- **Límite provisional (3a.7).** Con más de una nave, ninguna sale del sistema donde está la flota (a 1,5 veces su radio se la frena y se avisa). También la del foco, no solo la segunda: el motor carga un único sistema.
- **Guardado.** `mundo.sav` lleva todas las naves; `Snapshot()` da el jugador del foco y `SnapshotPlayer(i)` el de otra nave. Al cargar, el foco va a la nave del jugador que carga.
- **Juego.** Consola: `nave`, `nave nueva [km]`, `nave ver <id>`, `nave <id> <orden...>`. F2 pasa a llevar la siguiente nave. Las demás naves se dibujan.

Lo que falta o es provisional:

- **3a.6, clase y diseño por nave, hecho (decisión 12).** `Vessel::rules` (`ShipRules`: clase, variante, casco, radio, bodega, maniobra…) sustituye a `rules_.ship` en todo `Session.cpp`; `rules_.ship` queda como la clase de serie de la que parte cada nave nueva. `ShipState::design` es el diseño de esa nave; si es nulo, vale `CurrentShipDesign()` (el diseño por defecto), así que una nave sin diseño propio sigue al que se cargue. `DesignOf(ship)` lo resuelve y lo usan `Ship.cpp`, la sesión, `GameLayer`, `GameHud` y `main`; `GameSession::ShipSpec()` da las cifras de la nave en curso. `AddVessel(km, clase, variante)` y `nave nueva [km] [clase 0-9] [variante 0-2]` crean naves de otra clase (`RulesForClass`: casco por la tabla `ShipClass`, radio y medidas por la eslora). Sin clase, todas son la de serie, como pedía la decisión 12; la partida dorada no cambió. **Límites:** la clase no va en el guardado (al cargar, todas vuelven a la de serie; meterla pide subir el formato), y una clase distinta usa el modelo y el diseño de la fragata. Prueba: «Flota: clase por nave».
- Las otras naves se dibujan solo con el casco, y con las zonas dañadas de la nave del foco: sin sus módulos, estelas ni cráteres.
- El origen del mundo y lo que carga el motor siguen a la nave del foco.
- Con todas las naves perdidas, anomalías y combate se detienen, como antes con una.
- El modo realista con varias naves no está pensado: al perder la suya, la del foco reinicia la campaña entera.
- Los jugadores de las naves que no son del foco no se guardan solos: `main.cpp` solo escribe el del foco.

Una sola burbuja; reloj según la regla 2 del §2.3.

| Paso | Qué |
|---|---|
| 3a.1 | `std::vector<Vessel>` con identificador estable. `Step(real, mandos por nave)`. Los métodos públicos reciben la nave. Las consultas (`Ship()`, `Contacts()`, `Route()`…) también; `main.cpp`, `GameHud` y `GameLayer` piden «la mía». |
| 3a.2 | Segunda nave creada por consola (`nave nueva`), quieta. Se dibuja: `GameLayer` pinta todas las naves de la burbuja. Captura y a mirarla. |
| 3a.3 | El mundo las ve a las dos: las anomalías eligen blanco, `Hurt` recibe la nave, los proyectiles llevan dueño, las naves chocan entre sí (o no: decisión 7), `Resolve` admite `Kind::Ship` para apuntarse y seguirse. |
| 3a.4 | Reloj con dos naves: foco mínimo, exterior mínimo, tiempo propio por nave. Pruebas: una en combate y otra en crucero; una atracando y otra lejos. |
| 3a.5 | Segunda entrada: `--cmd "nave 2 motor crucero"` y órdenes con marca de fotograma (ya existen los `timedCommands`) para pruebas sin manos; en ventana, una tecla cambia qué nave se lleva y se mira. |
| 3a.6 | Diseño y clase **por nave** en lugar de globales. **Hecho.** |
| 3a.7 | Límite provisional: la segunda nave no puede salir del sistema de la primera (avisa y frena). Se quita en la fase 4. |

### Fase 3b — Dos entradas sobre la misma nave — **hecha el 13-11-2026, salvo el segundo mando físico**

Cómo quedó:

- **Jugadores y naves, por separado.** `players_` sustituye a la lista paralela a la flota: cada `PlayerState` dice en qué nave va (`vessel`) y qué puestos ocupa (`seats`). Una nave tiene siempre al menos un jugador. El foco pasa a ser un **jugador** (`SetFocusPlayer`); `SetFocus(nave)` lleva a quien la pilota.
- **Puestos (decisión 2c).** Tres: pilotaje, armas y escudos, ingeniería (`Seat`, y `SeatOf(orden)` dice cuál pide cada orden). Quien va solo los lleva todos; al entrar el segundo (`AddPlayer`), el primero se queda con el pilotaje y el otro con lo demás; un tercero se lleva uno de los dos del segundo; el cuarto entra sin puesto. `TakeSeat` solo ocupa un puesto libre; `LeaveSeat` lo suelta.
- **Órdenes con autor.** `ApplyAs(jugador, orden)` rechaza la que pide un puesto que no es suyo y se lo explica solo a él; `Apply` es el jugador del foco y `ApplyTo(nave, orden)` la da quien ocupe el puesto. Bajar a pie, subir, el tutorial y la cámara no piden puesto.
- **Mandos.** `StepPlayers(real, mandos)` lleva un mando por jugador. La nave obedece **solo** el de quien ocupa el pilotaje (sin piloto, a nadie), así que no hizo falta sumar giros de varios: el plan preveía mezclarlos para el reparto «todos pueden todo», que no es el elegido. Cada jugador a pie anda con su propio mando.
- **Mensajes.** `Say` reparte ya de verdad: `Crew` a todos los de la nave, `Player` solo a quien toca, `Everyone` a todos.
- **A pie.** Cada jugador baja y sube por su cuenta, a la misma cubierta. Mientras alguien de la tripulación está a pie, la nave no suelta amarras. Los compañeros que han bajado se dibujan.
- **Conocimiento.** Lo que averigua uno (sistemas, astros analizados) lo sabe toda la tripulación al acabar el paso.
- **Juego.** Consola: `jugador`, `jugador nuevo [nombre]`, `jugador ver <n>`, `jugador <n> <orden...>`, `puesto`, `puesto <pilotaje|armas|ingenieria>`, `puesto dejar <...>`. F3 pasa a ser el siguiente jugador.

Lo que falta o es provisional:

- **No hay segundo mando físico** (paso 3b.5): el segundo jugador solo se maneja por consola, con guiones o cambiando con F3. Un mando de juego de SDL3 queda pendiente.
- **La interfaz no enseña los puestos**: quién lleva qué solo se ve con `jugador` y en el mensaje de rechazo.
- **Los jugadores que entran con `AddPlayer` no se guardan**: al cargar hay un jugador por nave, con todos los puestos.
- Las bajas, el botín y la toma cinematográfica de un disparo son de quien habla por la nave (el piloto), no de quien apretó el gatillo.
- La cubierta a pie es una sola para toda la burbuja: dos tripulaciones a pie en estaciones distintas no caben todavía.
- Apuntar y alinear el casco (`Aim`, `Track`) son del pilotaje, porque giran la nave; las torretas y los escudos, del puesto de armas.

| Paso | Qué |
|---|---|
| 3b.1 | `struct Seat` (puesto): qué jugador, qué permisos. Una nave tiene N puestos. Decisión 2 sobre el reparto. |
| 3b.2 | **Mezcla de mandos.** `ShipControls` lleva incrementos de giro *respecto a la cámara de cada uno* (`view`): se convierten al marco de la nave **antes** de sumarlos. Giro: suma con tope. Mando de gases: gana el último que lo toca. Freno: cualquiera. |
| 3b.3 | Órdenes con autor: cada `ShipCommand` lleva su jugador y se filtra por puesto. Las que chocan (dos motores distintos en el mismo paso) se resuelven por orden de llegada, con aviso al otro. |
| 3b.4 | Lo que es de cada uno funciona por separado: objetivo, cámara, registro, tutorial y avatar a pie. `onFoot_` deja de bloquear la nave: es del jugador. |
| 3b.5 | Segunda entrada local para probar: un mando de juego (SDL3 ya lo trae) o un guion de órdenes. Captura con dos puestos activos. |

Con 3a y 3b terminadas, **toda la lógica multijugador existe y se prueba en un proceso**, sin red.

### Fase 4 — Varias burbujas — **hecha el 13-11-2026**

**Medido antes de empezar** (`epsilon_game_tests.exe --medir-motor`, galaxia del juego, 40 sistemas): un motor más cuesta unos 5 ms en crearse, unos 2 MB de memoria y 0,08 ms por paso. El riesgo R3 no existe: un `Engine` por burbuja es barato.

Cómo quedó:

- **Burbuja = motor + reloj + lo que vuela.** `GameSession::bubbles_`; cada `Bubble` tiene su `Engine` (su mundo cargado y su origen flotante), su reloj, su foco de tiempo, sus proyectiles y sus restos. Cada nave dice en cuál está (`Vessel::bubble`). `bubble_` y `engine_` son los de la nave en curso, igual que `vessel_` y `pilot_`. La primera burbuja usa el motor que recibe la sesión; las demás, uno que hace la **fábrica de motores** (`SetEngineFactory`).
- **Paso.** `StepPlayers` avanza cada burbuja por separado (`StepBubble`) y luego `Regroup` mueve las naves que han cambiado de sitio.
- **Salir (regla 3).** Con dos naves o más en un sistema, la que pasa de 1,5 radios se lleva una burbuja propia, con una copia del reloj. Su motor se coloca ya donde está la nave, para que tenga cargado lo de alrededor. Se reutilizan los motores de burbujas vacías.
- **Entrar (regla 5).** Una nave que va sola y entra (a 1,4 radios) en el sistema donde hay otra burbuja se une a ella y adopta su reloj, aunque sea anterior. Entra la que llega, no la que estaba (`Vessel::settled`).
- **Estaciones (reglas 4 y 7).** Cada estación produce con el reloj de la burbuja que tiene una nave en su sistema; las de sistemas vacíos, con el de cualquiera. El sello nunca retrocede, así que nada se produce dos veces.
- **Lo que no se comparte.** Las naves de burbujas distintas no se ven, no chocan ni se disparan, y no salen en los sensores de las otras.
- **Guardado.** Cada nave lleva la línea `reloj` con el de su burbuja; `tiempo` del mundo es el más adelantado (`WorldTime`). Al cargar, cada nave vuelve a su burbuja con su reloj.
- **Juego.** Se dibuja el motor de la burbuja de la nave que se lleva (`ViewEngine`); `nave` dice burbuja, sistema y fecha de cada una.
- **Sin fábrica de motores** (pruebas antiguas, herramientas) sigue el límite de la fase 3a.

Lo que falta o es provisional:

- **Dos naves no viajan juntas entre sistemas**: una burbuja con varias naves está atada a un sistema, y cada nave que sale se lleva la suya. En el vacío no se ven aunque vayan una al lado de otra.
- **El reloj de una nave no salta hacia delante** al entrar en un sistema vacío cuyas estaciones llevan un sello posterior (la regla 4 lo pedía): esas estaciones simplemente esperan a que el reloj las alcance.
- La alerta de la especie y la rendición usan el reloj de la burbuja que las toca; con relojes muy distintos, una rendición puede acabar antes para unos que para otros.
- Cargar sin fábrica de motores una partida con naves en sistemas distintos las junta a la fuerza en uno.
- Con N burbujas hay N motores avanzando en el mismo hilo.
- Cada burbuja tiene su propio generador aleatorio; las anomalías y los cascos perdidos siguen siendo del mundo.

`Bubble` con su propio `Engine`/`World`. Entrar, salir y fundir burbujas (reglas 3–5 del §2.3). Se quita el límite de 3a.7. Hay que medir antes cuánto ocupa un `World` (riesgo R3).

### Fase 5 — Protocolo dentro de un proceso — **hecha el 13-11-2026; en el juego, solo con `--loopback`**

Cómo quedó (`game/include/game/Net.h`, `game/src/Net.cpp`):

- **Servidor y cliente.** `GameServer` avanza la sesión de verdad a paso fijo de 1/60 s con los mandos que le llegan (`StepPlayers`) y manda a cada cliente lo que ve. `GameClient` convierte otra `GameSession` en una **réplica**: no simula; aplica las instantáneas. En una réplica, `Apply()` manda la orden y `Step()` manda el mando y solo mantiene cargado el mundo alrededor de la nave. Por eso la interfaz y el dibujo no han cambiado: leen la réplica como leían la sesión (riesgo R4 resuelto así).
- **Instantánea** (`SnapshotData`): el mundo (estaciones, anomalías, especie) y la burbuja del jugador (reloj, proyectiles, restos, naves y jugadores que hay en ella). Las naves de otras burbujas no viajan. Las posiciones van en el mundo del servidor, con su origen; la réplica las pasa al suyo.
- **Mensajes**: `Hello`, `Welcome`, `Refused`, `Controls`, `Command`, `Snapshot`. El saludo comprueba la versión del protocolo y la del guardado; `Welcome` lleva la semilla, y quien tiene otra galaxia no entra. Un jugador entra como anfitrión, como tripulante de la nave del anfitrión o con nave propia (`JoinAs`); quien vuelve con el mismo identificador recupera su sitio.
- **Transporte**: la interfaz `Transport` y su versión en memoria (`MakeMemoryLink`). El de red es la fase 6.
- **Un solo código para escribir y leer**: cada estructura tiene una función `Wire` que sirve para las dos cosas. Lo que se puede copiar byte a byte va tal cual, así que servidor y clientes tienen que ser el mismo ejecutable.
- **Probado**: la partida que lleva el servidor con los mandos y órdenes de un cliente es idéntica, número a número, a la misma partida jugada sin red; la réplica ve lo mismo que el servidor; tripulante, nave propia, burbujas distintas y los tres rechazos.

Lo que se aparta del plan o queda pendiente:

- ~~El juego en solitario no pasa por el servidor.~~ **Desde el menú de red (fase 7) sí**: servidor y cliente en memoria es el modo normal, y `--direct` el antiguo.
- **No se manda el guardado al entrar.** No hizo falta: la primera instantánea ya trae todo lo que el cliente ve.
- **La instantánea pesa unos 20 kB y va entera en cada paso**: a 60 por segundo es más de 1 MB/s por cliente. `GameServer::snapshotEvery` la espacia; partirla en rápida y lenta y mandar solo diferencias es la fase 8.
- **Sin interpolación ni predicción**: con una instantánea por paso no se nota; al espaciarlas, sí.
- **En loopback**: la consola actúa sobre la sesión del servidor, así que `nave ver`, F2 y F3 no cambian lo que ve el cliente; y no se comprueba que el `Balance` sea el mismo (es el mismo proceso).
- Las respuestas a las órdenes no vuelven al cliente: `Apply()` en una réplica dice siempre que sí, y si el servidor la rechaza se sabe por el registro.
- Un cliente que se desconecta deja a su jugador y su nave donde estaban (decisión 6, sin tomar).

Servidor y cliente separados por una interfaz `Transport`, con una implementación en memoria. El cliente ya **no** toca la sesión: manda `ShipControls` y `ShipCommand`, recibe instantáneas y eventos, y con ellos rellena una copia de solo lectura que alimentan `GameHud` y `GameLayer`. El juego en solitario pasa a ser «servidor + cliente en memoria». Pruebas: serializar y deserializar da lo mismo; una partida por el transporte en memoria da lo mismo que directa.

### Fase 6 — Red — **hecha el 13-11-2026**

Cómo quedó:

- **ENet 1.3.18**, con `"enet"` en `vcpkg.json`. Entró **sin mover la línea base** (riesgo R8 descartado). `game/src/NetEnet.cpp` da el mismo `Transport` que el de memoria: `ListenOn(puerto)` para el servidor y `ConnectTo(ip, puerto)` para el cliente. Canal 0 sin fiabilidad (mandos e instantáneas, estas en trozos) y canal 1 fiable (saludo y órdenes). Si se configura sin ENet, el juego compila igual y solo queda el transporte en memoria.
- **En el juego**: `--host [puerto]` abre la partida a la red (el anfitrión juega por el mismo camino que con `--loopback`, y el servidor no se para aunque él esté en pausa); `--connect ip[:puerto]` entra en la de otro, con nave propia (`--join ship`, por defecto) o de tripulante (`--join crew`); `--name` dice el nombre. Puerto por defecto, 27300.
- **Desconexión (decisión 6).** Cerrar la conexión es `GameSession::PlayerLeft`: el jugador deja sus puestos y, si no queda nadie conectado a bordo, la nave se para y **sale del mundo (`GamePhase::Away`) cuando lleva `logoutCalm` segundos reales sin combate**. Hasta entonces sigue ahí y se la puede destruir. Cuenta como combate recibir un impacto, disparar y tener un hostil armado a tiro. Una nave ausente no se mueve, no se ve y no se le puede disparar; va en `mundo.sav` (línea `ausente`). Volver con el mismo identificador es `PlayerBack`: la nave reaparece donde estaba.
- **Probado**: paquetes de 3 bytes y de 40 kB por los dos canales, una partida entera por ENet en esta máquina dentro de las pruebas, y **dos procesos del juego** (`--host` y `--connect`), con nave propia y de tripulante.

Lo que falta o es provisional:

- **Solo probado en una máquina** (127.0.0.1). Entre dos equipos de la LAN no se ha probado, y el cortafuegos de Windows preguntará la primera vez.
- ~~Una instantánea entera por paso (más de 1 MB/s por cliente).~~ Resuelto en la fase 8: unos 23 kB/s.
- **El cliente no guarda nada**: la partida es del servidor. Su jugador y los puestos de la tripulación siguen sin guardarse.
- **En el cliente, los trucos de consola no hacen nada** (cambian la réplica, que el servidor pisa). Las órdenes de consola (`motor`, `escudo`…) sí viajan.
- **El anfitrión no puede cambiar de vista** (`nave ver`, F2, F3) y **no hay forma de echar a nadie** ni contraseña.
- Si el servidor se cae, el cliente se queda con la última imagen: no hay aviso ni reconexión automática.
- No se comprueba que el `Balance` coincida; el saludo mira versión de protocolo, versión de guardado y semilla.

Biblioteca `epsilon_net` con ENet. `--servidor [puerto]` y `--conectar ip[:puerto]`. Saludo: versión, suma de `Balance`, semilla, identidad del jugador. Entrada tardía y desconexión limpia.

### Fase 7 — LAN y servidor dedicado — **hecha el 13-11-2026, salvo el menú**

Cómo quedó:

- **Descubrimiento en la red local.** Quien abre una partida contesta en el puerto 27301 a quien pregunta por difusión (`AnnounceOnLan`, `FindLanGames` en `NetEnet.cpp`). La pregunta lleva la versión del protocolo; la respuesta, puerto, jugadores, máximo, modo y nombre. La pregunta va también a 127.0.0.1, y una partida que contesta por los dos caminos cuenta una vez.
- **Servidor sin ventana**: `epsilon_server.exe` (`game/src/server_main.cpp`), que enlaza solo la lógica del juego y la red, sin SDL. Opciones `--world`, `--port`, `--name`, `--players`, `--mode`, `--seed`, `--no-tutorial`, `--save-every`, `--seconds`. Bucle a 60 pasos por segundo; guarda cuando la partida lo pide, cada cierto tiempo si hay alguien, cuando se va el último y al cerrar con Ctrl+C. Sin nadie dentro, el mundo no avanza.
- **Sin jugador propio.** En una partida nueva, la nave de salida espera fuera del mundo y es para el primero que entre. En una guardada (`GameSession::RestoreWorld`), todas las naves quedan fuera hasta que entra su jugador.
- **Cada jugador recupera lo suyo.** El servidor tiene la carpeta de la partida (`GameServer::SetSaveFolder`): al entrar alguien lee `jugadores/<id>.sav`, lo devuelve a su nave y le restaura lo que conocía y su tutorial; `GameServer::Save` escribe el mundo y el archivo de cada jugador que ha entrado. Vale también para `--host` en el juego, que ahora guarda a los invitados.
- **En el juego**: `--host` se anuncia en la red local y `--connect lan` busca partidas, las lista y entra en la primera.
- **Probado**: en las pruebas (anuncio y búsqueda, servidor que guarda y recupera a tres jugadores) y con `epsilon_server.exe` y el juego en dos procesos, en dos rondas: partida nueva, y la misma carpeta cargada otra vez.

**La consola (hecha después, el 13-11-2026):**

- **Los comandos viven en la biblioteca común**: `RegisterGameCommands` (`game/src/Console.cpp`, `game/Console.h`), que antes era `RegisterCommands` en `main.cpp`. Los registran el juego con ventana y `epsilon_server`; las pruebas los ejecutan sin GPU. El juego se comporta igual.
- **Solo el personal usa comandos.** `Rank`: jugador < moderador < administrador < dueño < desarrollador. `CanUseCommands` es verdadero desde moderador. Todo texto que llegue al registro por una persona pasa por `RunCommand(registro, categoría, línea)`, que rechaza al jugador sin ejecutar nada (ni trucos del juego ni comandos del motor). La categoría la da el servidor: `RankBook`, por `PlayerState::id`, en `rangos.txt` dentro de la carpeta de la partida (o `--ranks`). Quien no figura es jugador.
- **Entrada estándar de `epsilon_server`**: lo que se escribe en la terminal lo ejecuta el dueño (cualquier comando del juego). Además `ayuda`, `jugadores`, `rangos`, `guardar`, `salir`, `rango <id> <categoría>` (la guarda en `rangos.txt`) y `como <id> <orden…>` (la orden con la categoría de ese jugador). `--no-console` la desactiva.
- **En el juego con ventana**: la consola (F1) es de quien tiene la partida (dueño); dentro de la partida de otro (`--connect` o desde el menú) eres jugador y no se ejecuta nada. `--cmd` y `--cmd-at` son la línea de órdenes de quien lanza el juego y no pasan por el control.
- **Probado**: `TestConsole` (categorías, rechazo de cada orden sin efecto, las cuatro categorías de personal, lista de rangos con archivo) y a mano con `epsilon_server.exe` por la entrada estándar.

**El menú (hecho después, el 13-11-2026):**

- **El juego va siempre por servidor y cliente en memoria.** Lo que era `--loopback` es ahora lo normal, como preveía la fase 5; `--direct` deja el camino antiguo, sin servidor. Gracias a eso, abrir a la red es encender la escucha del servidor que ya está en marcha, y entrar en otra partida es cambiar la conexión del cliente.
- **Página «Partida en red»**, en el menú principal y en la pausa (`NetworkPanel` en `GameHud.cpp`, `MenuPage::Network`, `HudInput::net`): estado, abrir o cerrar la partida propia, buscar en la red local (en segundo plano) y entrar en una de las encontradas. Dentro de la de otro, «Volver a mi partida».
- **Entrar en la de otro con el juego en marcha**: el cliente cierra su conexión en memoria (su nave sale de su propio mundo, como en cualquier desconexión) y se conecta al otro servidor con nave propia. Si la partida propia no está abierta a la red, su mundo espera. Al volver, o si se pierde la conexión, el cliente se conecta otra vez al servidor de aquí y recupera su nave.
- **`nave ver`, `jugador ver`, F2 y F3 vuelven a funcionar**: cambian el foco de la partida de verdad, y la conexión de este proceso pasa a ver y mandar como ese jugador (`GameServer::Follow`).
- **Comando `red`** (`red abrir`, `red cerrar`, `red buscar`, `red entrar <n>`, `red volver`): lo mismo que los botones, para probar sin manos. Solo con `--cmd-at`, no con `--cmd`.
- **Probado**: capturas del menú y de la página; abrir la partida; y, con un `epsilon_server.exe` aparte, buscar, entrar, jugar y volver, todo con el juego ya arrancado.

Lo que falta:

- **Entrar de tripulante desde el menú**: el botón entra siempre con nave propia. De tripulante, solo con `--connect ... --join crew`.
- **Escribir una dirección en el menú**: solo se entra en lo que se encuentra en la red local; con dirección, `--connect`.
- Entrar bloquea el juego hasta tres segundos mientras espera al servidor.
- **Comandos de jugadores por la red**: la consola del servidor ya existe (ver abajo), pero un cliente aún no puede mandar texto de comando al servidor; cuando exista, tiene que pasar por `RunCommand` con la categoría que el servidor tenga anotada para ese `PlayerState::id`.
- **El cliente tiene que usar la misma semilla** que el servidor (la de por defecto, si no se pasa `--seed`); si no, se le rechaza por «otra galaxia».
- **Los puestos de la tripulación no se guardan**: al volver, cada uno ocupa los que estén libres.
- La difusión solo se ha probado en esta máquina; con varias tarjetas de red puede hacer falta preguntar por cada una.

Descubrimiento por difusión, lista de partidas en el menú, `epsilon_server.exe` sin ventana.

### Fase 8 — Pulido de red — **hecho el 13-11-2026: tráfico y caídas**

Cómo quedó:

- **Instantáneas por diferencias** (`SplitSnapshot`, `EncodeDelta`, `DecodeDelta` en `Net.cpp`). La instantánea se parte en trozos (cabecera, proyectiles, destellos, restos, cubierta, estaciones, anomalías, especie, cada nave, cada jugador). De cada trozo viaja nada si es idéntico al de la referencia, la diferencia byte a byte comprimida (ceros seguidos) si mide lo mismo, y entero si no. La **referencia es la última instantánea que el cliente confirmó** (`Ack`), así que perder paquetes no rompe nada; si el cliente no la tiene, la pide entera (`kNeedFull`). Cada lado guarda las últimas 64.
- **Medido**: una instantánea entera, 18,6 kB; por diferencias, unos 300 bytes. En vuelo, **398 bytes por paso: 23 kB/s por cliente** a 60 pasos por segundo, frente a más de 1 MB/s. Sigue yendo una por paso, así que no hace falta interpolar.
- **Probado con pérdida**: con una red que pierde uno de cada tres paquetes en los dos sentidos, la partida sigue, y en cuanto la red se limpia la réplica es idéntica al servidor.
- **Caídas**: una conexión que deja de contestar se da por perdida entre 3 y 8 segundos después (antes, 30). Si es el servidor el que cae, el cliente lo dice y sale, o vuelve a su partida si había entrado desde el menú.

Lo que el plan ponía en esta fase y no está hecho:

- **Interpolación y predicción del propio giro.** No hacen falta en LAN con una instantánea por paso. Para jugar por Internet, con menos instantáneas y más retardo, sí.
- **Reconexión automática**: tras una caída hay que volver a entrar a mano.
- **Zona de interés más fina**: se manda la burbuja entera del jugador, con todas sus anomalías, estén cerca o no.
- El formato sigue siendo «los bytes tal como están en memoria»: servidor y clientes tienen que ser el mismo ejecutable.

Interpolación, predicción del propio giro, instantáneas por diferencias, zona de interés, reconexión, registro de errores del servidor.

---

## 4. Red

### 4.1 Arquitectura: servidor con autoridad

**No** recomiendo simulación determinista sincronizada (cada uno simula y solo viajan los mandos). Motivos concretos de este código:

- `Step` usa `dt` variable y el `JobSystem` reparte trabajo en hilos.
- El motor carga y descarga sistemas según la cámara y la focal (`StreamWorld`): dos clientes con ventanas distintas no tienen el mismo `World`.
- Hay coma flotante por todas partes y distancias de 10¹⁰ km; una diferencia mínima se nota.
- Con burbujas, cada cliente solo tiene cargada la suya.

Así que: **el servidor simula; los clientes mandan intenciones y dibujan lo que reciben.** En LAN la latencia es de 1–5 ms; se puede empezar sin predicción ninguna.

### 4.2 Qué debe ser determinista

Solo lo que se **genera**, no lo que se simula. Con la misma semilla y la misma versión, todos deben obtener lo mismo de:

- la galaxia y cada sistema (`BuildGalaxy`, `GenerateWorldSystem`);
- las rocas de cada familia (`rocks_`, por semilla de familia);
- los 118 elementos de un astro (`ElementSharesOf`);
- la planta de una cubierta (`deck_`);
- las anomalías iniciales (`GenerateAnomalies`): estas las manda el servidor de todos modos.

Eso ya es así en una misma máquina; entre máquinas lo garantiza usar **el mismo ejecutable**. El saludo rechaza versiones o `Balance` distintos. Además, el servidor corre a **paso fijo de 60 Hz**, para que las pruebas se puedan repetir.

### 4.3 Qué viaja

**Cliente → servidor**

| Qué | Canal | Frecuencia |
|---|---|---|
| `ShipControls` de su puesto (giro ya en el marco de la nave, mando, freno; a pie: movimiento y mirada) con número de secuencia | no fiable | 60 Hz |
| `ShipCommand` (motor, ruta, disparo, atraque, drones, taller…) | fiable y ordenado | al ocurrir |

**Servidor → cliente**

| Qué | Canal | Frecuencia |
|---|---|---|
| **Instantánea de la burbuja**: reloj, foco, y por nave: posición absoluta, orientación, velocidades, motor, burbuja de curvatura, casco y zonas, escudos y calor, fase | no fiable | 20–30 Hz |
| Proyectiles y drones (posición, velocidad, dueño) | no fiable, en la misma instantánea | 20–30 Hz |
| Anomalías dentro de `activeRange` | no fiable | 10 Hz |
| **Estado lento** de mi nave: bodega, elementos, módulos, munición, niveles | fiable, solo al cambiar | — |
| **Mundo lento**: estaciones, alerta, sistemas conocidos y analizados | fiable, solo al cambiar | — |
| **Eventos**: mensajes del registro, impactos, golpes en el casco, trozos sueltos, pasos del tutorial, sonidos | fiable | al ocurrir |
| **Estado completo** (es el archivo de guardado, tal cual) | fiable | al entrar |

El cliente interpola las demás naves entre dos instantáneas. `contacts_`, `warning_`, `approach_`, `Route()` y `PredictedPath()` se pueden recalcular en el cliente con lo recibido, lo que ahorra ancho de banda; al principio es más simple mandarlos.

Al entrar, el estado completo **reutiliza `WriteSave`**: mismo código para guardar y para sincronizar, y una sola cosa que probar.

### 4.4 Transporte: ENet

| | ENet | SDL_net |
|---|---|---|
| Qué da | UDP con canales fiables y no fiables, orden, troceo de paquetes grandes, control de congestión, gestión de pares y desconexiones | Sockets TCP y UDP, resolución de nombres. Nada más. |
| Lo que habría que escribir | Serialización | Serialización **y** toda la fiabilidad sobre UDP, o resignarse a TCP (bloqueo de cabeza de línea con 60 mandos por segundo) |
| vcpkg | Puerto `enet` | La versión para SDL3 es reciente: habría que comprobar que hay puerto en la línea base fijada en `vcpkg.json` |
| Licencia | MIT | zlib |
| Depende de SDL | No: el servidor sin ventana no arrastra SDL | Sí |

**Recomendación: ENet.** Se añade `"enet"` a `vcpkg.json` y una biblioteca `epsilon_net` que solo enlazan el juego y el servidor. Antes de la fase 6 hay que comprobar con `vcpkg search enet` la versión que trae la línea base actual; cambiar de línea base recompila SDL3 e ImGui.

Canales: 0 mandos (no fiable, secuenciado) · 1 instantáneas (no fiable, secuenciado) · 2 órdenes y eventos (fiable, ordenado) · 3 estado completo y guardado (fiable, troceado).

### 4.5 LAN: descubrimiento por difusión

ENet no trae descubrimiento, pero expone sus sockets (`enet_socket_create`, opción de difusión):

1. El servidor escucha en un puerto UDP fijo de descubrimiento, distinto del de juego.
2. El cliente, al abrir «Partidas en red local», emite a `255.255.255.255` una pregunta con la marca `EPSILON-NEBULOSA` y la versión, cada segundo.
3. El servidor responde directamente al que pregunta: nombre de la partida, modo, jugadores/máximo, puerto de juego, versión.
4. El menú lista las respuestas y las caduca a los 3 s.

Detalles: el cortafuegos de Windows pedirá permiso la primera vez (aviso en pantalla); con varias tarjetas de red conviene emitir por cada interfaz; siempre queda «conectar por IP».

### 4.6 Servidor sin ventana

Nuevo ejecutable `epsilon_server`, que enlaza `epsilon_game_logic` + `epsilon_net` y **no** SDL. Se compila en todos los presets, también en `x64-release` y `x64-asan`, con lo que las pruebas de red corren sin GPU.

```
epsilon_server.exe --mundo mundo.sav --puerto 27300 --nombre "Nebulosa" --jugadores 4 --modo 2
```

- Bucle a 60 Hz con acumulador; duerme lo que sobra.
- Guarda cada N minutos y al cerrar (Ctrl+C), en la carpeta que se le diga, no en `%APPDATA%`.
- Sin jugadores, el mundo **se detiene**: con relojes por burbuja, sin naves no hay nada que mover.
- Consola por la entrada estándar con los comandos de `engine.Commands()` que ya existen — **hecha**, solo para el personal (ver fase 7).
- «Crear partida» desde el juego es el mismo servidor dentro del proceso, en un hilo, con el anfitrión conectado por el transporte en memoria.

Pendiente de comprobar: que `Engine` se construye sin recursos gráficos. Las pruebas ya lo hacen, así que debería.

---

## 5. Guardado nuevo: mundo + jugador

Mismo formato de texto de `SaveGame.cpp` (una línea por dato, `suma` FNV al final, escritura a archivo temporal y renombrado). `kSaveVersion = 13`; lo anterior se rechaza, como ya se hace.

### Carpeta de una partida

```
<partida>/
  mundo.sav              todo lo que existe aunque no haya nadie conectado
  jugadores/
    <id>.sav             uno por jugador
```

En solitario la carpeta va en `%APPDATA%\Epsilon\Nebulosa`; el servidor dedicado usa la que se le indique. El `<id>` es un identificador aleatorio que el cliente crea una vez y guarda en `ajustes.txt`.

### `mundo.sav`

| Sección | Contenido |
|---|---|
| Cabecera | `EPSILON-NEBULOSA-MUNDO 13`, semilla, modo, `timeScale`, reloj de mundo `T` |
| `sistema` (uno por sistema tocado) | sello de tiempo, alerta, último ataque |
| `rendicion` | como hoy |
| `estacion` | como hoy + **dueño** + sello `producidoHasta` |
| `anomalia`, `combate`, `clase`, `nivel`, `zonas_anomalia` | como hoy |
| `nave <id>` y sus líneas | Todo `ShipState` (las líneas actuales `posicion` … `piezas`, con el id delante), más: clase y diseño, dueño o tripulación, reloj propio si está en espacio abierto, órbita, atraque, fase |
| `equipo` | sistemas conocidos, visitados y astros analizados, si el conocimiento es compartido |

**Las naves van en el mundo**, no en el archivo del jugador: una nave ocupa un sitio, puede estar atracada en una estación de otro y, en cooperativo, es de varios. Lo que le pasa mientras su dueño no está es la decisión 6.

Se quitan los huecos que el formato arrastra por compatibilidad: los ceros del yacimiento de las minas, `kRetiredMiningKind` y las ramas «partida anterior a…» de `Restore`.

### `jugadores/<id>.sav`

| Línea | Contenido |
|---|---|
| Cabecera | `EPSILON-NEBULOSA-JUGADOR 13`, id, nombre |
| `nave` | id de la nave y puesto |
| `tutorial`, `cursor`, `recogidas`, `analisis` | progreso del tutorial |
| `bajas` | estadística |
| `sistemas`, `visitados`, `analizados` | solo si el conocimiento es por jugador |
| `a-pie` | estación y posición en la cubierta, si se desconectó a pie |

### En el código

`SaveData` se parte en `WorldSave` y `PlayerSave`; `Snapshot()`/`Restore()` pasan a `SnapshotWorld()`, `SnapshotPlayer(id)`, `RestoreWorld()`, `RestorePlayer()`. `WriteSave` del mundo es también el «estado completo» de la red (§4.3).

Sin `checkpoint_`, el guardado es siempre el estado **actual**. Hoy, con la nave recién perdida, se guarda el último punto de control; eso desaparece.

---

## 6. Riesgos y esfuerzo

### Riesgos principales

| # | Riesgo | Por qué | Cómo se acota |
|---|---|---|---|
| R1 | **El reloj** | Es la única parte sin solución obvia y afecta al equilibrio (producción, viajes). | Decidirlo antes de la fase 1.7. La fase 3a.4 lo prueba sin red. |
| R2 | **Tamaño del cambio en `Session.cpp`** | Más de 4000 líneas donde `ship_` aparece en casi todas las funciones. | Fase 1 por grupos pequeños, con la partida dorada vigilando. Es largo, no difícil. |
| R3 | **Un `World` por burbuja** | `BuildGalaxy` crea toda la galaxia en cada `World`. Con 4 burbujas, 4 galaxias en memoria. | Medir antes de la fase 4. Si pesa, compartir lo inmutable (`systems`, `sky` y `catalog` ya son `shared_ptr`). |
| R4 | **`GameHud` y `GameLayer` leen la sesión directamente** | En la fase 5 el cliente ya no tiene sesión. | Que la copia del cliente use las mismas estructuras (`Vessel`, `WorldState`) y las mismas consultas, rellenadas desde la red. |
| R5 | **`Say()` y el tutorial** | Mensajes sin destinatario por todo el código; el tutorial mira la sesión entera. | Fase 1.2. Tutorial solo en solitario al principio (decisión 8). |
| R6 | **Saltos al fundir burbujas** | Dos orígenes flotantes y dos relojes que se unen. | La regla 5 del §2.3 evita el salto para quien estaba. Fundir solo cuando la que llega baja de curvatura. |
| R7 | **Coordenadas en el cable** | Posiciones de hasta 10¹⁴ km con precisión de metros. | Mandar sistema + desplazamiento en `double`, nunca `float`. |
| R8 | **Línea base de vcpkg** | Añadir `enet` puede obligar a moverla y recompilar todo. | Comprobarlo al empezar la fase 6, no antes. |
| R9 | **Trampas** | Un cliente modificado. | Con servidor con autoridad solo puede mentir en sus mandos. Suficiente para LAN. |
| R10 | **Modo creativo y consola** | Hoy cualquiera puede `creativo si` o `saltar`. | Permiso de administrador en el servidor (decisión 8). |

### Esfuerzo

En **jornadas de trabajo conjunto** (una sesión larga, con compilación, pruebas y capturas). Son estimaciones gruesas; las fases 1 y 4 son las más inciertas.

| Fase | Jornadas | Riesgo |
|---|---|---|
| 0 — Partida dorada, órdenes como datos, paso fijo | 2–3 | Bajo |
| 1 — Separar nave, jugador, mundo; partir `Step`; sellos de tiempo | 5–8 | Medio (volumen) |
| 2 — Guardado nuevo | 2–3 | Bajo |
| 3a — Dos naves locales | 5–7 | Medio |
| 3b — Dos entradas en una nave | 3–4 | Medio (diseño de puestos) |
| 4 — Burbujas | 5–8 | **Alto** |
| 5 — Protocolo en memoria, cliente sin sesión | 6–9 | **Alto** |
| 6 — ENet, servidor y cliente | 3–4 | Medio |
| 7 — Difusión LAN, menú, servidor dedicado | 2–4 | Bajo |
| 8 — Pulido de red | 4–6 | Medio |
| **Total** | **37–56** | |

**Atajo**: si lo primero que se quiere jugar es el cooperativo en una nave, el orden 0 → 1 → 2 → 3b → 5 → 6 → 7 lo da en unas **23–35 jornadas**, sin las fases 3a y 4. Varias naves llegarían después sin rehacer nada, porque la fase 1 ya deja el estado separado.

---

## 7. Decisiones que necesito

Marcadas con ★ las que bloquean el arranque. En cada una, mi propuesta.

| # | Decisión | Opciones | Propongo | Bloquea |
|---|---|---|---|---|
| 1 ★ | **Modelo de tiempo** | A manda la rápida · C dilatación como velocidad · D reloj por burbuja | **Decidido (13-11-2026): D, reloj por burbuja** (§2.3) | 1.7, 3a.4, 4 |
| 2 ★ | **Reparto en cooperativo** | (a) puestos fijos: piloto, artillería, ingeniería/sensores · (b) todos pueden todo · (c) puestos, pero cualquiera puede ocupar uno libre | **Decidido (13-11-2026): (c)**; con dos jugadores, piloto + todo lo demás | 3b |
| 3 | **Conocimiento** (sistemas, astros analizados) | por jugador · por equipo | **Decidido (13-11-2026): por equipo; equipo = gremio (14-11-2026).** Lo que descubre uno lo sabe todo el gremio, y quien se va del gremio lo pierde. En solitario y LAN es automático mientras existan naves o estaciones | 1.2, 2 |
| 4 | **Estaciones y recursos** | comunes · de cada jugador, con permiso para los demás | **Decidido (13-11-2026), detalle en [§8](#8-estaciones-gremios-y-defensa)**: cada jugador tiene las suyas, que solo él edita; los de su equipo las usan y se mueven libres por dentro. Hay una estación de gremio que construyen, mejoran y usan todos. Producción a partes iguales. Las estaciones no desaparecen por balanceo: se defienden, y con su dueño ausente reciben 1/10 del daño. *Por cerrar: ver §8.* | 2, 3a |
| 5 ★ | **Perder la nave** sin vuelta atrás del mundo | (a) reaparece en la última estación propia donde atracó, con la penalización de bodega del modo · (b) nave nueva en el punto de inicio · (c) en modo realista, fuera de la partida | **Decidido (13-11-2026): (a)**; (c) para el modo realista. **Cambia con §8 (14-11-2026):** si la nave colapsa, la tripulación sale en una cápsula de escape a su estación individual; si no tiene, a la de gremio; si no, al lobby. Sin estaciones ni naves capitales, reaparece en el lobby. La muerte de la persona (§8, regla 16) manda al primer sistema con una nave inicial en todos los modos salvo el realista, donde se vuelve a una estación aliada o a la nave de un compañero | 1.6 |
| 6 | **Nave de un jugador desconectado** | desaparece · se queda donde está, vulnerable · solo se puede salir atracado y queda a salvo | **Decidido (13-11-2026): desaparece y vuelve donde estaba, pero no en combate**: se queda parada y vulnerable hasta llevar `WorldRules::logoutCalm` (15 s reales) sin recibir ni hacer fuego y sin un hostil a tiro; si la destruyen antes, está perdida. En cooperativo la nave sigue mientras quede alguien conectado | 2, 6 |
| 7 | **Entre naves de jugadores** | fuego amigo sí/no · choque entre naves sí/no · ¿habrá jugador contra jugador? | Sin fuego amigo, con choque, sin JcJ de momento. **Cambia con §8 (14-11-2026):** habrá JcJ regulado por la facción NPC y treguas entre gremios | 3a.3 |
| 8 | **Tutorial, creativo y consola** | tutorial por jugador en red o solo en solitario · quién puede usar la consola | Tutorial solo en solitario al principio; consola y creativo solo para el anfitrión | 1.2, 6 |
| 9 | **Tamaño** | jugadores por nave · naves por mundo · ¿Internet o solo LAN? | **Decidido (13-11-2026), modificado el 14-11-2026: LAN e Internet.** **LAN:** sin límites; puede haber más de 4 personas por nave y no hay límite de naves por jugador. **Internet:** de supercarguero en adelante, hasta 30 personas por nave (cifras por clase en §8). Las naves que un jugador puede almacenar dependen de los módulos de hangar de su estación (máximo 4), su nivel, la tecnología del hangar y el volumen de cada nave; las no desplegadas quedan sin cargar dentro del hangar. Tope de naves activas del servidor: 200 (para probar). Internet exige interpolación, predicción del giro y reconexión (Fase 8) y resolver el paso por el router (NAT), que ENet no hace: queda por elegir entre reenvío de puerto manual, servidor de relevo o perforación de NAT | 6, 7, 8 |
| 10 | **Cámara lenta del modo cinematográfico** en red | se pierde · solo si todos la tienen · siempre | Solo si todos los de la burbuja la tienen | 3a.4 |
| 11 | **Orden** | completo (3a antes de la red) · atajo cooperativo (§6) | Completo, como pides; el atajo existe si quieres jugar antes | — |
| 12 | **Naves distintas** | cada jugador elige clase al entrar · todos la misma al principio | Misma clase hasta la fase 3a.6 — **mecanismo hecho (3a.6)**: cada nave lleva su clase y diseño; por defecto todas la de serie. Falta la elección al entrar (fase 6) y guardar la clase | 3a.6 |

Con las decisiones 1, 2 y 5 se puede empezar la fase 0, que no depende de ninguna, y entrar en la fase 1 sin rehacer nada.

---

## 8. Estaciones, gremios y defensa

Estado: **diseño acordado entre el 13 y el 14-11-2026, sin código.**

**Dos modos.** Casi todo este apartado (lobby, facción NPC, balizas, control del espacio, fama, créditos, mercado, gremios y estación de gremio) es del **modo Internet**. En el **modo un jugador y en LAN** no hay lobby ni apoyo de una facción: el jugador está solo contra la nebulosa. Ahí **no hay límite de estaciones ni de naves creadas**, aunque no hay acceso a una estación de gremio. En LAN, dos jugadores pueden ir en la **misma nave** si ambos entran a ella estando en una estación. En solitario y LAN el **conocimiento se comparte automáticamente** mientras existan naves o estaciones. La **cápsula de escape** se mantiene también en solitario y LAN. En solitario, si se destruye la nave, el jugador vuelve a la **estación o nave más cercana**; si no hay ninguna y no es el modo realista, recibe una **nave inicial nueva** con la mitad del inventario máximo.

Punto de partida: `Station` (`Stations.h`) no tiene casco, vida ni defensas, así que hoy una estación no puede perderse. Hay dos tipos: la **central** (modular) y la **origami** o de anillo (nivel 1: un anillo de 1 km que fabrica con el material que se le entrega; todavía no es modular).

### Reglas acordadas

1. **Estaciones.** En el modo Internet, cada jugador tiene como mucho **una estación central y una origami** (límite 1 de cada tipo), y cada gremio **una** de gremio. En solitario y LAN no hay límite. Solo su dueño edita los módulos de las individuales. Los de su equipo pueden usarlas y moverse libremente por dentro, sin editar sus módulos. **No hay sistema de transporte**: quien quiera su estación cerca en los sistemas salvajes la mueve con él. Mover una estación necesita un **módulo especial de movimiento** y gasta mucha antimateria, **de 2 a 3 veces** lo que gastaría la nave. La **origami** puede **volver a plegarse y anclarse a la nave** del jugador, con tecnología 100 % humana.
2. **No desaparecen por balanceo.** Una estación no se quita del mundo por ajuste de reglas: solo la destruyen jugadores o enemigos. Por eso necesitan casco y defensas propias.
3. **Dueño ausente.** Mientras su dueño no está en la partida, la estación recibe **1/10 del daño**.
4. **Se defiende sola.** Con su artillería de defensa. Que un grupo fuerte pueda destruirla es desafortunado pero equilibrado.
5. **Escombros y reposición.** Al destruirse quedan sus escombros y no se pierde del todo, salvo que el jugador la deseche. Reponer la estación cuesta el **(100 % − control del espacio)** de los materiales totales de su coste.
6. **Facción NPC reguladora** (nueva): regula las interacciones entre usuarios y fija el **control del espacio**, un porcentaje por sistema.
   - **Al 100 %**, la facción ataca a los agresores enviando una flota.
   - **Por debajo**, el equipo atacante solo recibe el **(100 % − control)** del botín de la nave, y el jugador solo repone el **(100 % − control)** del coste de su estación.
   - **Cómo se reparte:** el sistema del lobby y el siguiente (los 2 primeros) están siempre al 100 %. Desde el segundo hay **balizas**, que aportan un 50 %. Cada baliza **desactivada** baja un **30 %** el control de los sistemas de alrededor, y así hasta el séptimo, que no tiene baliza. El efecto se **atenúa un 20 %** por cada sistema que se aleja de la baliza; la fórmula exacta sale del balanceo. **Las estaciones no bajan el control.**
   - **Producción:** cuanto más lejos del lobby está una estación, más rápido y más produce.
   - **Invasión:** cada día de vida real hay una invasión a cada baliza, y la defienden **naves NPC** de la facción. Solo se desactiva si los alienígenas hostiles **las vencen** y la inhabilitan atacándola; entonces el control de ese sistema baja **entre un 50 % y un 70 %**, según salga del balanceo. Si la baliza sigue **activa** tras la invasión no hay ataques; si está **inactiva**, las naves hostiles atacan las estaciones de ese sistema. El jugador puede **defender la baliza y repararla**; la recompensa por repararla la recibe en el lobby.
   - **Agresor:** quien hace que el **casco o el escudo** de otro jugador o de su estación baje un **5 %** en **un día de juego**. Se cuenta con el reloj del sistema del objetivo. Lo mismo vale con naves y estaciones NPC.
7. **Balizas.** Son de la especie alienígena hostil. Para un jugador, una baliza se puede:
   - **Recapturar:** da créditos para el lobby (dinero), fama positiva y la certeza de que su estación allí estará bien. Sube el control un 50 %.
   - **Destruir:** da materiales, antimateria y una **pieza easter egg** para fabricar un radar especial con una habilidad de ayuda. Es **muy difícil**: acude en su rescate la flota hostil y la de la facción NPC a la vez. El jugador pierde fama o adquiere fama negativa. La baliza queda desactivada y baja el control de alrededor (regla 6).
   - **Radar especial** (hecho con la pieza easter egg): manda una **señal de auxilio de baliza** que atrae una flota grande de la facción NPC del lobby. Que ataquen o no al jugador depende de su fama; **siempre** atacan a las naves alienígenas hostiles. Consume **toda la energía** de la nave o estación del jugador y tiene un **enfriamiento de 7 días de juego** por jugador, contado con el reloj de la nave que lleva el radar.
8. **Estación de gremio: una por equipo; equipo y gremio son lo mismo.** Muy superior y más grande que las demás; la construyen, modifican y usan todos los miembros. Arranca con **una pieza fabricada por cada miembro** en su estación de anillo. Coste total de las piezas: **el de un carguero**, dividido entre los miembros **presentes en ese momento**. La tecnología se comparte, pero el **paquete inicial** (planos y herramientas) para hacer las piezas es consumible. Mejorarla cuesta mucho más material.
   - Si un miembro se va, **su pieza se queda**. Si aún no está construida, se rehacen las piezas según los miembros que quedan.
   - **Tregua entre gremios:** usuarios de gremios distintos pueden elegir de común acuerdo una tregua que elimina el fuego amigo entre ellos.
9. **Gestión del gremio.** Lo lleva un líder votado. El equipo puede cambiarlo si 1/4 lo pide, como mucho una vez por semana de **tiempo real**, para evitar abusos.
10. **Producción repartida a partes iguales** entre los miembros, aporte cada uno lo que aporte. La parte de quien se va se reparte entre los que quedan.
11. **Artillería de defensa**: categoría nueva de módulos de estación, con armamento superior exclusivo para estaciones. Solo hay armas **ligeras y pesadas**, como en las naves (sin medianas). Cada nivel de estación da **5 huecos de arma ligera y 1 de pesada**: el nivel 1 tiene 5 y 1, y el nivel 30 (máximo) tiene 150 y 30. **Los huecos van vacíos**: las armas se fabrican en los **módulos** de la propia estación. **Las armas de estación son las de las naves, del doble de tamaño y con el triple de daño.**
12. **Escudos de estación.** Son modulares, como los de las naves. El principal consume **arena**; con más tecnología, gases para los plasmas, antimateria para los electromagnéticos, etc. Se regeneran mientras tengan con qué mantenerlos. Su consumo es un **múltiplo del daño que recibe el objeto atacado**, y el múltiplo se ajusta empíricamente.
13. **Subir de nivel una estación** (la de gremio y las individuales, igual). Los jugadores depositan en sus almacenes los materiales y herramientas de la mejora. Cuando hay suficiente, empieza un **proceso de construcción** durante el cual la estación es **vulnerable**. Para que no sea injusto, el usuario puede **pagar una cuota a la facción NPC**, que depende del nivel de la flotilla que la proteja mientras se termina de construir.
14. **Estación lobby.** Una ciudadela de enorme tamaño y defensas, en el sistema central. Recibe a las naves de los jugadores que acaban de entrar en el servidor. Ofrece: **reaparición**, **creación de gremios** y **comercio** sistemático y entre jugadores. Los precios sistemáticos se fijan empíricamente, balanceados con la minería.
15. **Recursos de inicio.** Solo si el jugador se queda **sin naves ni estaciones activas** puede reclamarlos: una **nave de inicio** con la **mitad del inventario máximo** de la nave y solo **dos módulos de minería**. En esa nave no importa que esos módulos sean pesados.
16. **Reaparición y cápsula de escape.** Si se pierden las estaciones y las naves capitales, el jugador reaparece en el lobby. Si una nave colapsa, los tripulantes salen en una **cápsula de escape** que los lleva a su estación individual; si no tienen, a la de gremio; y si no, a la del lobby. Si un grupo entero llevaba una sola nave, la prioridad es la **estación de gremio**, luego la estación amiga más cercana y luego el lobby.
   - **Bodega:** se pierde cuando la nave se destruye. El jugador recupera en el lobby una parte que depende del control del sistema; el resto queda en los **restos de la nave** en ese sistema, y el jugador u otra persona puede recogerlo.
   - **Muerte:** por ahora es solo la **nave destruida**; más adelante se añadirán interacciones a nivel de persona. La persona reaparece en el **primer sistema** (en Internet, el del **lobby**) con una **nave inicial** con las dos torretas de minería y la **estación origami**. Vale en **todos los modos excepto el realista**, donde el tripulante vuelve a una **estación aliada** o a la **nave de un compañero**.
   - **Resumen de reaparición:** 1) nave destruida → la tripulación sale en cápsula a su estación individual, luego a la de gremio y luego al lobby (en solitario, a la estación o nave más cercana); 2) sin naves ni estaciones activas, salvo en el modo realista → primer sistema con una nave inicial, la origami y la mitad del inventario máximo.
17. **Fama.** Se gana recapturando balizas y se pierde destruyéndolas. Fama positiva: descuentos y acceso a **skins y tecnologías exclusivas** de la facción NPC reguladora. Fama negativa, según lo grande que sea el número: **sobreprecios en el lobby** y, más grave, **ataques aleatorios** de las naves NPC de la facción del lobby, pero solo **fuera de los 2 primeros sistemas** alrededor del lobby.
18. **Créditos y mercado.** Todos los jugadores empiezan con una **pequeña cantidad de créditos**. Se gastan en **tecnología básica y materiales** que vende el lobby. También son la **economía entre jugadores**: un jugador pone su producto en el **tablero de economía** del lobby y otros compran esa reserva. No hay límites de precio. El lobby cobra un **impuesto** que depende del nivel de fama; no se considera adónde va. Las ofertas del lobby son **eternas** en la mayoría de los casos; las de los jugadores **desaparecen una semana (de tiempo real) después de publicarse**, y quien vende **deposita el material en el lobby**. Se venden **por unidades**. Si la oferta caduca, el jugador tiene que **recoger en el lobby** lo que no se vendió.
19. **Flota desplegada.** Un jugador puede desplegar a la vez **hasta dos naves de clase carguero o superior** y dar el mando de sus naves a **NPC de su propia tripulación**. Puede manejar cualquiera de ellas. Cada nave desplegada resta daño y resistencia a **todas** las naves del jugador: **20 %** por cada nave grande (carguero en adelante) y **(5 − nº de categorías por debajo del carguero) %** por las menores, es decir acorazado 4 %, destructor 3 %, crucero 2 %, fragata 1 %. Las **naves dron** son módulos y no cuentan. Las **corvetas** van aparte: se pueden desplegar **hasta 30** y su daño baja un **2 %** por cada corveta desplegada. Cada clase por debajo del carguero tiene un **tope de 30** naves, y el límite real es menor por la reducción de daño por numerosidad. Los topes y las penalizaciones cuentan **naves activas** (desplegadas), en todos los modos; **no hay límite de naves creadas**. Los porcentajes se suman y el daño no baja de 0 %: con una flota enorme hay que cuidar que no llegue a 0. Si destruyen la nave en la que va el jugador, pasa a la **más grande que tenga desplegada**. La única que **no pierde** daño por naves desplegadas es la **estación espacial**.
20. **Bodega y peso.** Cada nave tiene una **capacidad máxima de almacenamiento**. Hay **módulos de almacén** que la amplían, a costa del hueco de módulo que ocupan y de un mayor coste de mover la nave. Cuanto más peso lleva, **más cuesta moverla**, así que llenar la bodega al máximo se penaliza. En el modo Internet el sistema de bodega se **reescribe en lo que haga falta** para que se pueda perder **en fracciones** (regla 16).
21. **Tripulación NPC.** Los tripulantes son **aleatorios**, con estadísticas distintas y una categoría que sale de ellas. Las estadísticas son **Audacia, Tenacidad, Firmeza y Adaptabilidad** y afectan al **manejo**, la **resistencia**, el **daño** y la **regeneración de escudos** de la nave. El NPC al mando de cada nave es su **sub capitán** y da a la nave **mejoras del 1 % al 10 %**, según lo fuerte que sea en cada estadística. **Audacia** sube el daño, **Tenacidad** la resistencia, **Firmeza** la regeneración de escudos y **Adaptabilidad** el manejo. Cada nave tiene canónicamente **1000 tripulantes**, y solo **20** están capacitados para manejar una nave. Esos 20 son los de la **nave inicial**; se consiguen más con **módulos de las estaciones**: el **módulo de recepción de personal** recibe naves pequeñas de traslado y trae **10 personas capacitadas cada 30 días de juego**. Cada estación admite como máximo **20** de ese personal, y se puede **devolver personal a la Tierra**. Hace falta **un capacitado por nave desplegada**, así que este número limita la flota. Los capacitados que llegan cada 30 días tienen también estadísticas **aleatorias**, que siguen una **campana de Gauss** centrada en valores medios bajos: un tripulante con estadísticas muy altas es **extremadamente raro** y será **personal clave** para el jugador.
22. **Hangares y naves por jugador.** En **LAN** no hay límite de naves por jugador ni de personas por nave. En **Internet**, las naves que un jugador puede tener almacenadas dependen de los **módulos de hangar** de su estación (**máximo 4**), de su **nivel**, de la **tecnología del hangar** (su volumen de almacén) y del **volumen de cada nave**: cuanto mayor es la nave, menos caben. Las naves **no desplegadas** se quedan **sin cargar dentro de los hangares**, para que el servidor no colapse. Hay un **tope de naves activas por servidor**, que se prueba empíricamente (cifra inicial en la tabla). En Internet, de **supercarguero** en adelante, una nave admite **hasta 30 personas**, pero **solo las 4 primeras son operativas**: las demás no hacen nada en la nave, aunque cuentan para el bonus de la regla 23. Las estaciones **no empiezan con hangares**: se construyen como módulos. Los drones no los maneja ningún usuario: son sobre todo módulos de torreta.
23. **Más personas al mando, más fuerte.** Cuantas más personas hay dentro de una misma nave (operativas o no), más suben su daño y su resistencia, de forma **logarítmica**, para que sea justo. **El bonus no tiene tope propio**, ni en LAN ni en Internet: es cuestión de balanceo.

### Por cerrar

- **Cifras de balanceo:** la fórmula del control, la producción por distancia, la fama (escala y umbrales), el coste de mover estaciones y el de las invasiones se proponen como cifras iniciales para probar y se ajustan jugando.
- **Origami:** replegarla y anclarla a la nave cambia cómo se despliega hoy (una sola vez).
- **«Naves capitales»:** ¿la clase Capital o la nave principal del jugador?
- **Cápsula de escape:** dijiste «nave gremial», puse **estación de gremio**.
- **Bodega:** cuánto recupera en el lobby (propongo el porcentaje de control) y cómo encaja con la pérdida de bodega del modo (`cargoLoss`). Qué parte de `Inventory` y de las zonas de bodega interfiere con perderla en fracciones.
- **Idea:** una «semana de ofertas» del lobby como easter egg.
- **Hangares:** el volumen, los niveles y el tope de 4 módulos son cifras iniciales.
- **Flota:** con un capacitado por nave desplegada, la flota empieza limitada a 20, frente al tope de 30 por clase.
- **Modo realista sin aliados:** si se queda sin naves ni estaciones no recibe nave nueva; ¿la campaña acaba, como hoy con `permadeath`? La decisión 5 decía «fuera de la partida».
- **Cifras de flotilla y de nivel:** ver propuesta abajo; se ajustan jugando.
- **Nivel de estación:** no confundirlo con los 6 niveles de las estaciones alienígenas ni con el «nivel 1» de la origami.
- **Coste de una nave:** hoy `Balance.h` no define cuánto cuesta una nave en materiales (las armas sí tienen masa). Hay que implementarlo, con un coste por clase y modelo (ligera, normal y pesada), y decidirlo **empíricamente**. La pieza del gremio se mide con el carguero.

### Propuesta de cifras (para ajustar, no decidido)

Como no hay un coste de nave en el código, todo va en múltiplos de **C** (el coste de un carguero normal). Cuando haya masas por clase, C se convierte en toneladas y el resto sale solo.

| Concepto | Propuesta |
|---|---|
| Subir una estación individual del nivel n−1 al n | `0,05 × C × n^1,5`: nivel 2 = 0,14 C, nivel 10 = 1,6 C, nivel 30 = 8,2 C. Del 1 al 30 suma unas 100 C |
| Estación de gremio | ×4 respecto a la individual (muy superior, mucho más material) |
| Composición del coste | 85 % materiales a granel (baratos) y 15 % elementos raros (lo escaso, como acordado en `diseno-combate.md`) |
| Planos y herramientas | Un paquete consumible cada 5 niveles |
| Cuota de flotilla | Un porcentaje del coste de ese nivel: flotilla 1 = 5 %, 2 = 10 %, 3 = 20 %, 4 = 40 % |
| Flotilla | Nivel 1: corvetas. 2: fragatas. 3: cruceros. 4: destructores. La cantidad de naves y su modelo se ajustan empíricamente y llevan un grado de aleatoriedad |
| Ventana vulnerable | Duración fija de 15 min reales por mejora |

Para ajustarlas con datos, conviene medir cuánto tarda un jugador en reunir una C con sus drones y partir de ahí (objetivo propuesto: nivel 5 en unas horas, nivel 30 en cientos).

### Cifras iniciales de balanceo (para probar)

Puntos de partida razonados, no decididos. Anclas del código: depósito de antimateria de la nave 120 kt para «unas veinte burbujas» (6 kt cada una), minería de 10 kt/h por dron, escala de tiempo 60 (1 min real = 1 h de juego), y las clases y el casco de `Balance.h`.

**Control del espacio.** Anillo 0 es el sistema del lobby y anillo 1 el siguiente: siempre 100 %. Balizas desde el anillo 2 hasta el 6 (el 7 no tiene).

| Anillo | 2 | 3 | 4 | 5 | 6 | 7 | 8 o más (salvaje) |
|---|---|---|---|---|---|---|---|
| Control base sin balizas | 70 | 55 | 40 | 25 | 15 | 10 | 0 |

- `control = recorte(base + Σ balizas activas 50·a(d) − Σ balizas desactivadas 30·a(d), 0, 100)`, con `a(d) = 1 − 0,2·d` (d = sistemas de distancia a la baliza; con d ≥ 5 no afecta).
- Prueba rápida: en el anillo 2 una baliza propia activa da 100 y desactivada da 40, una bajada de 60, dentro del 50–70 % pedido.
- Invasión: una por baliza y día real.

**Economía y producción**

| Concepto | Cifra inicial |
|---|---|
| Producción de una estación según su anillo `k` (ritmo y capacidad) | ×(1 + 0,2·k): ×1,4 en el anillo 2, ×2,4 en el 7 y más allá |
| Créditos iniciales | 1000 |
| Material a granel en el lobby | compra del jugador 1 cr/kt, venta al lobby 0,5 cr/kt |
| Elementos raros | 50 a 500 cr/kt según escasez |
| Tecnología básica del lobby | 500 a 5000 cr |
| Recapturar una baliza | +500 cr y +50 de fama |
| Destruir una baliza | −100 de fama |
| Impuesto del mercado | 5 % base; −1 punto por cada 200 de fama positiva (mínimo 2 %); +1 punto por cada 100 de fama negativa (máximo 15 %) |
| Bodega recuperada en el lobby al perder la nave | `control` % de la bodega; el resto, en los restos |
| Coste de mover según la carga | ×(1 + (carga ÷ capacidad)²): vacía ×1, a la mitad ×1,25, llena ×2 |
| Capacidad de bodega | 3000 kt × casco relativo de la clase (fragata ligera 3000, carguero normal 90 000) |
| Módulo de almacén | +25 % de la capacidad base de la nave, ocupa un hueco de módulo y suma un 10 % al coste de mover |
| Penalización de flota | 20 % por nave grande; 4 %, 3 %, 2 % y 1 % de acorazado a fragata; corvetas aparte: 2 % cada una, máximo 30 (regla 19) |
| Oferta de un jugador | caduca a los 7 días de tiempo real |
| Volumen de una nave (UF = una fragata ligera) | casco relativo^0,8: corveta 0,2; fragata 1 a 1,3; crucero 3,2; destructor 4,7; acorazado 11; carguero 15; supercarguero 40; capital 96; capital suprema 600 |
| Volumen de un módulo de hangar | 10 UF × nivel^1,5: nivel 1 = 10, nivel 5 = 112, nivel 10 = 316 (nivel máximo 10) |
| Tecnología de hangar | +50 % de volumen por nivel de tecnología, hasta 5 |
| Módulos de hangar por estación | máximo 4 |
| Naves activas por servidor (Internet) | 200, para probar |
| Personas por nave en Internet | corveta 2, fragata 4, crucero 6, destructor 8, acorazado 12, carguero 20, supercarguero o más 30 |
| Mejora de un sub capitán | por estadística, 1 % + 9 % × (valor ÷ 100), con el valor de la campana de Gauss (fila siguiente). Categoría por la media de las cuatro: Recluta < 30, Veterano 30–50, Élite 50–65, Leyenda > 65 |
| Estadística de un tripulante | campana de Gauss, media 40 y desviación 18, entre 0 y 100: ≥ 60 el 13 %, ≥ 70 el 4,8 %, ≥ 80 el 1,3 %, ≥ 90 el 0,27 % |
| Bonus por personas a bordo | daño y resistencia ×(1 + 0,25 · ln n), con n todas las personas dentro de la nave: 2 = +17 %, 4 = +35 %, 10 = +58 %, 30 = +85 % |

**Fama** (escala −1000 a +1000)

| Fama | Efecto |
|---|---|
| ≥ 100 | 5 % de descuento en el lobby |
| ≥ 300 | 10 % de descuento y skins |
| ≥ 600 | 20 % de descuento y tecnologías exclusivas de la facción |
| ≤ −100 | sobreprecio del 10 % |
| ≤ −300 | sobreprecio del 25 % |
| ≤ −500 | ataques aleatorios de las naves NPC fuera de los 2 primeros sistemas |

**Estaciones y combate**

| Concepto | Cifra inicial |
|---|---|
| Casco y escudo de una estación de nivel n | 30·n veces los de la fragata ligera: nivel 1 = 30, nivel 30 = 900 |
| Daño con el dueño ausente | 1/10 |
| Consumo del escudo por unidad de daño absorbida | arena ×2, gases ×1, antimateria ×0,5 (más tecnología, menos material) |
| Mover una estación | 15 kt de antimateria por salto equivalente a una burbuja de la nave (2,5 × 6 kt) |
| Flotilla de la facción | nivel 1: 3 corvetas; 2: 3 fragatas; 3: 4 cruceros; 4: 4 destructores; cantidad ±25 % al azar |
| Ataque a estaciones con la baliza inactiva | 2 corvetas por estación y día de invasión |
| Cuota de flotilla | ver propuesta de arriba (5 %, 10 %, 20 %, 40 % del coste del nivel) |

Cómo ajustarlas: medir los créditos y kt que gana un jugador por hora real, y mover el precio del granel hasta que lo inicial tarde unos 30 minutos en gastarse.

### Qué cambia en el código

- `Station`: casco, escudo, dueño, equipo o gremio y nivel (hasta 30). Las estaciones guardan hoy `site`, módulos y producción, no vida. Tope de una por jugador y una por gremio.
- `ModuleKind`: categoría de artillería de defensa (ligera y pesada), con huecos por nivel; módulo de fabricación de armas de estación; módulo de movimiento; escudos modulares con su munición (arena, gases, antimateria); la origami pasa a ser modular.
- Origami que se repliega y se ancla a la nave; límite de una central y una origami por jugador.
- Radar especial: señal de auxilio, flota NPC, gasto de toda la energía y enfriamiento de 7 días de juego.
- Escombros: estado nuevo de la estación; el jugador puede repararla o desecharla.
- Facción NPC reguladora y nivel de control del espacio por sistema, con balizas (recaptura, destrucción, propagación) y pérdida diaria por invasiones.
- Fama y créditos (moneda del lobby), tablero de economía con impuesto por fama y la pieza easter egg del radar.
- Restos de nave con la bodega que no se recupera en el lobby, recogibles por cualquiera.
- Varias naves desplegadas con mando NPC y penalización de flota; paso del jugador a la nave más grande al perder la suya.
- Capacidad de bodega por clase, módulos de almacén y coste de mover según la carga; la bodega se reescribe para perderse en fracciones.
- Separar el modo Internet (lobby, facción, balizas, fama, mercado, gremios) del solitario y LAN (sin nada de eso, sin límite de estaciones).
- Tripulantes NPC aleatorios con estadísticas (Audacia, Tenacidad, Firmeza, Adaptabilidad) y mejoras por nave; penalización propia de las corvetas (máximo 30) y tope de 30 por clase bajo el carguero.
- Plantilla de 1000 tripulantes por nave, con 20 capacitados, y módulo de recepción de personal que renueva el personal emergente cada 30 días de juego.
- Reaparición de la persona en el primer sistema con nave inicial y estación origami.
- LAN: subir a la misma nave desde una estación, además de entrar de tripulante.
- Módulos de hangar (máximo 4 por estación), volumen de cada nave y naves no desplegadas guardadas sin cargar; tope de naves activas por servidor y de personas por nave en Internet.
- Gremio: líder, votos, cambio semanal en tiempo real, treguas entre gremios y reparto igual de la producción.
- Botín y reposición escalados por `100 % − control`; flota de la facción al 100 %.
- Construcción de mejora: estado vulnerable y cuota a la facción por flotilla.
- Armas de estación: las de nave al doble de tamaño y ×3 de daño (`Balance.h`), fabricadas en módulos de la estación.
- Estación lobby (ciudadela): reaparición, gremios, comercio, recursos de inicio.
- Cápsula de escape al colapsar una nave y reaparición en el lobby.
- Coste en materiales por clase y variante de nave (tabla nueva en `Balance.h`).
- Combate: anomalías y especies hostiles pueden atacar estaciones; el daño se escala por presencia del dueño (cifra en `Balance.h`).
- Guardado (§5): sección `gremio`, dueño y casco en `estacion`, control por sistema, fama y créditos; el formato 13 cambia.
- Red: casco y estado de las estaciones van en la instantánea del mundo.

## 9. Diseño técnico de estaciones

Estado: **escrito el 14-11-2026, sin código.** Cubre las estaciones, su defensa, el control del espacio, las balizas y los gremios del §8. Quedan fuera, para un §10: tripulación NPC (regla 21), flota desplegada (19), hangares (22), bodega por clase (20), mercado y lobby (14, 18) y radar especial (7). Las cifras salen de las tablas del §8 y viven en `Balance.h`.

**Idea guía:** fuera del modo Internet el control del espacio vale **0 en todas partes**. Así las fórmulas «(100 % − control)» funcionan igual en los tres modos (se paga y se saquea el 100 %) y no hacen falta dos caminos de código.

### 9.1 Estructuras nuevas

**Cambios en lo que ya existe**

| Dónde | Campo nuevo | Para qué |
|---|---|---|
| `WorldRules` (`Balance.h`) | `internet` (bool, por defecto falso) | La bandera del modo Internet (9.3). Se fija al crear el mundo y se guarda |
| `StationRules` (`Balance.h`) | casco y escudo por nivel (30·n fragatas ligeras), factor de dueño ausente (0,1), huecos por nivel (5 ligeras y 1 pesada), consumo de escudo por munición (arena 2, gases 1, antimateria 0,5), coste de mover (×2,5 la burbuja), armas de estación (×2 de tamaño, ×3 de daño), coste de nivel (0,05·C·n^1,5) y una **C provisional** en kt | Cifras iniciales para probar |
| `StationKind` | `Guild = 3`, `Lobby = 4` | Estación de gremio y ciudadela. Los números siguen siendo los del guardado |
| `ModuleKind` | a partir de 5: `Armory` (fabrica armas de estación), `Mover` (módulo de movimiento), `ShieldSand`, `ShieldGas`, `ShieldAntimatter` | `Fits` decide en qué estaciones cabe cada uno |
| `Station` | `level` (1 a 30), `state`, `hull`, `shield`, `owner` (id de jugador, texto), `guild` (0 = ninguno), `buildUntil` (reloj en s, fin de la mejora), `mounts` (armas montadas), `shieldCooldown` (no se guarda) | Vida, dueño y nivel. El máximo de casco y escudo se calcula del nivel y los módulos, no se guarda |
| `Projectile` (`Combat.h`) | `station` (id de la estación que lo disparó; 0 = ninguna), `targetStation` (-1 = ninguna) | Que una estación dispare y que se le pueda apuntar |
| `Projectile::team` | se amplía: 3 = facción reguladora; en Internet, 100 + id de gremio para los jugadores | Fuego amigo por gremio y treguas. Una función `Hostile(a, b, mundo)` decide en un solo sitio |
| `PlayerSave` | `credits`, `fame` | Solo cambian en Internet; en los demás modos quedan en su valor inicial |

**Estructuras nuevas**

- **`StationState`** (enumeración): `Active`, `Upgrading` (mejora en curso: vulnerable), `Debris` (escombros: casco 0, sigue en la lista), `Folded` (origami plegada y anclada a una nave; sin `site`, va con la nave).
- **`DefenseMount`**: `weapon` (`WeaponKind`), `tier`, `heavy` (bool). No se guardan la recarga ni el blanco. Cuántos caben se calcula del nivel, no se guarda.
- **`AggressionMark`**: `attacker` (id de jugador o de anomalía), `target` (estación o nave), `day` (día de juego del reloj del sistema del objetivo), `lost` (fracción de casco o escudo quitada ese día). Llega a 0,05 → agresor. Las marcas de días pasados se borran.
- **`Beacon`**: `system`, `ring` (anillo desde el lobby), `active` (bool), `integrity` (0 a 1), `lastInvasion` (día real del servidor), `capturedBy` (id de jugador, informativo).
- **`Guild`**: `id`, `name`, `leader`, `members` (ids de jugador), `truces` (ids de gremio), `station` (id; -1 sin construir), `pieces` (lista de `GuildPiece`), `leaderChangedAt` (fecha real, UTC), `recallVotes` (ids que piden cambiar de líder), `starterUsed` (paquete inicial consumido).
- **`GuildPiece`**: `member`, `cost` (`Inventory`, su parte de C), `built` (bool).
- **`Wreck`** (restos): `id`, `site`, `contents` (`Inventory` y antimateria), `fromStation` o `fromShip`, `createdAt` (reloj). Cualquiera puede recogerlos.
- **Control del espacio**: un vector de porcentajes por sistema, **derivado** de los anillos y las balizas cada vez que cambia una baliza. No se guarda ni viaja por la red: cada lado lo recalcula.

### 9.2 Guardado 14 e instantánea de red

**Formato 14** (`kSaveVersion` 13 → 14; la 13 se rechaza, como hoy). Regla nueva: **una línea que vale su valor por defecto no se escribe**, para que una partida en solitario sin estaciones dañadas ni gremios cambie lo mínimo.

| Documento | Entra | No entra |
|---|---|---|
| Mundo, cabecera | `internet` | el control (derivado) |
| Mundo, cada `estacion` | `level`, `state`, `hull`, `shield`, `owner`, `guild`, `buildUntil`, una línea por arma montada | máximos, recargas, blancos, `deployAge`, `launch` |
| Mundo, bloques nuevos | `baliza`, `gremio` (con sus piezas, votos y treguas), `restos`, `agresion` (solo las del día en curso) | — |
| Jugador | `creditos`, `fama` | — |

**Instantánea** (`SnapshotData` y `SplitSnapshot`). `kProtocolVersion` sube en cada paso que cambia su forma.

- Trozo de **estaciones**: los mismos campos guardados, más el escudo y el casco máximos ya calculados (el cliente no tiene que conocer las reglas de módulos para pintar la barra).
- Trozos nuevos: **balizas** (uno), **gremios** (uno por gremio), **restos** (uno). Cambian poco, así que por diferencias casi nunca viajan.
- Trozo de cada **jugador**: créditos y fama.
- Los proyectiles de estación van en el trozo de proyectiles con sus dos campos nuevos.
- No viajan: el control (se recalcula), las marcas de agresión (solo las usa el servidor) ni las recargas.

### 9.3 Qué reglas del §8 dependen de `WorldRules::internet`

| Regla | Con la bandera (Internet) | Sin ella (solitario y LAN) |
|---|---|---|
| 1 | Una central y una origami por jugador, una de gremio por gremio; solo el dueño edita módulos | Sin límite; cualquiera de la partida edita |
| 6, 7 | Facción, control, balizas, invasiones, agresor con flota | Control 0 en todo el mapa; sin balizas ni facción |
| 8, 9, 10 | Gremios, estación de gremio, líder, votos, reparto, treguas | No existen |
| 13 | Proceso de construcción **y** cuota a la facción | Solo el proceso de construcción (vulnerable igual) |
| 14, 15, 18 | Lobby, recursos de inicio en el lobby, créditos y mercado | Nave inicial en el primer sistema (regla 16) |
| 16 | Cápsula: estación propia → de gremio → lobby | Cápsula a la estación o nave más cercana |
| 17 | Fama | No existe |
| 20 (final), 22 | Bodega en fracciones; tope de naves por hangar | — |

**Sin bandera, en todos los modos:** 2 (solo se pierden por daño), 3 (dueño ausente: en solitario nunca se da), 4, 5 (con control 0 se repone el 100 %), 11, 12, 13 (sin cuota), 19, 21 y 23.

### 9.4 Pasos de implementación

Cada paso compila, pasa `epsilon_game_tests` y `epsilon_tests`, y deja el juego jugable. Los que tocan cabeceras se compilan con `--clean-first`. Columna «Hecho»: se marca al terminar.

| Paso | Hecho | Qué | Ficheros | Pruebas |
|---|---|---|---|---|
| P1 | [ ] | Cifras: `WorldRules::internet`, ampliación de `StationRules`, C provisional. Sin cambio de comportamiento | `Balance.h`, `game_tests.cpp` | Nivel 30 → 150 y 30 huecos; casco del nivel 1 = 30 fragatas ligeras; control 0 sin bandera. Partida dorada igual |
| P2 | [x] | `StationState` y campos nuevos de `Station`; formato 14; trozo de estaciones de la instantánea | `Stations.h/.cpp`, `SaveGame.h/.cpp`, `Session.h` (`SnapshotData`), `Net.cpp`, `game_tests.cpp` | Ida y vuelta del guardado y de la instantánea; una 13 se rechaza. **La partida dorada cambia**: la versión pasa a 14 y las estaciones ganan `level` y `hull` (valores por defecto); nada más |
| P3 | [ ] | Daño a estaciones: impacto de proyectil, escudo antes que casco, ×0,1 con el dueño ausente, casco 0 → `Debris` | `Combat.h/.cpp`, `Stations.cpp`, `Session.cpp` | Un proyectil baja escudo y luego casco; ausente recibe 1/10; destruida queda en la lista como escombros |
| P4 | [ ] | Escudos modulares: tres módulos, regeneración que gasta `stored` | `Stations.h/.cpp`, `Balance.h` | Absorber X gasta 2X de arena; sin arena no se regenera; más módulos, más escudo máximo |
| P5 | [ ] | Artillería: `DefenseMount`, `Armory` fabrica (coste ×2 de masa), disparo automático con `Solve` a lo hostil en alcance | `Stations.h/.cpp`, `Combat.cpp`, `Session.cpp`, `GameHud.cpp` | El nivel 1 rechaza la 6.ª ligera y la 2.ª pesada; dispara a un hostil y no a un amigo; daño ×3. Captura del disparo |
| P6 | [ ] | Subir de nivel: depósito en almacenes, `Upgrading` hasta `buildUntil` por el reloj del sistema, máximos nuevos | `Stations.h/.cpp`, `Session.cpp`, `main.cpp` (comando `estacion nivel`) | Con material incompleto no empieza; completo empieza, es dañable y termina al sello; no retrocede con otro reloj |
| P7 | [ ] | Escombros: reparar por (100 % − control) del coste o desechar (deja `Wreck`) | `Stations.cpp`, `Session.cpp`, `SaveGame.cpp` | Con control 0 cuesta el 100 %; con control 60, el 40 %; desechar crea restos recogibles |
| P8 | [ ] | Límites y permisos por modo: 1+1 por jugador en Internet; editar módulos solo el dueño | `Stations.h/.cpp`, `Session.cpp`, `main.cpp` (opción `--internet`) | Internet rechaza la 2.ª central; solitario acepta 5; otro jugador usa pero no edita |
| P9 | [ ] | Origami plegable (`Folded`, va con la nave) y `Mover` con antimateria ×2,5 | `Stations.cpp`, `Session.cpp`, `Ship.cpp`, `SaveGame.cpp` | Plegar y desplegar conserva módulos e inventario; mover sin `Mover` falla; gasto = 2,5 × burbuja |
| P10 | [ ] | Control y agresor: `Beacon`, control derivado, `AggressionMark` con el día del sistema del objetivo, botín escalado; trozo de balizas | `Stations.h` o `Faction.h/.cpp` nuevo, `Session.cpp`, `SaveGame.cpp`, `Net.cpp` | Tabla del anillo 2: 100 con baliza activa, 40 desactivada; agresor justo al 5 %; botín ×(1 − control) |
| P11 | [ ] | Facción e invasiones: invasión diaria por baliza (reloj real inyectable), flota NPC al 100 %, ataque a estaciones con la baliza inactiva; recapturar y destruir; créditos y fama | `Faction.cpp`, `Anomalies.cpp`, `SaveGame.cpp`, `Session.cpp` | Con un reloj falso: un día → una invasión; baliza perdida baja el control; recapturar da +500 cr y +50 de fama |
| P12 | [ ] | Gremios: `Guild`, estación de gremio por piezas (C entre presentes), líder y votos semanales, treguas, reparto igual; trozos de gremio | `Guild.h/.cpp` nuevo, `Stations.cpp`, `Combat.cpp` (`Hostile`), `SaveGame.cpp`, `Net.cpp`, `main.cpp` | Piezas se rehacen si alguien se va antes de construir; 1/4 de votos cambia el líder una vez por semana real; tregua quita el fuego amigo; reparto igual |
| P13 | [ ] | Interfaz y consola: barras de casco y escudo, estado, nivel, control del sistema, panel de gremio; comandos de prueba en `docs/pruebas.md` | `GameHud.cpp`, `main.cpp`, `docs/pruebas.md`, `game/README.md` | Prueba de texto del HUD donde la haya; capturas de una estación dañada, en mejora y en escombros |

**P2, hecho el 14-11-2026** (con parte de P3 y una defensa provisional de P5): `Station` gana `level`, `state`, `hull`, `shield` (fracciones del máximo, que sale del nivel) y `owner`; `kSaveVersion` 14 (línea `defensa`, solo si no es el valor por defecto) y `kProtocolVersion` 2. Además: `StationNetwork::Hurt` (escudo, casco, ×0,1 con el dueño ausente, escombros que quedan en la lista); los proyectiles hostiles alcanzan estaciones (`StepProjectiles`); los hostiles armados las atacan y cada estación dispara con `nivel × turretsPerLevel` torretas de serie (×3 de daño) hasta que P5 monte armas por huecos. El combate de estaciones usa su propio generador (`Bubble::stationRng`). **Partida dorada:** además de la versión y la línea `defensa`, cambia un impacto en la nave, porque un hostil dispara ahora a la central junto a la que está atracada y un proyectil cruza la nave (probado: con ese disparo desactivado, la dorada es idéntica). Sin HUD todavía (P13).

### 9.5 Riesgos

- **Partida dorada.** Cambia en P2 por la versión. Con la regla de no escribir valores por defecto, P3 a P12 no deberían tocarla; si la tocan, es un fallo o hay que explicarlo.
- **Procesador.** Una estación de nivel 30 tiene 180 armas; con `Solve` por arma y paso, unas pocas estaciones bastan para hundir los FPS en este equipo, que ya va limitado por la CPU. Mitigación: las armas iguales disparan en **baterías** con una sola solución, y el blanco se recalcula cada 0,5 s. Medir con `--no-vsync` en P5.
- **Sistemas sin cargar.** Hoy las estaciones de un sistema descargado solo producen por sello. Daño, invasiones y ataques a estaciones lejanas no pueden simularse proyectil a proyectil: hará falta una **resolución resumida** (daño por día según la flota), que P11 debe dejar acotada.
- **Tres relojes.** Reloj del sistema (producción, mejora, día del agresor), reloj de la nave (enfriamientos) y tiempo real (invasiones, votos, ofertas). Mezclarlos es el error más probable: cada campo dice en su comentario qué reloj usa, y el tiempo real se inyecta para poder probarlo.
- **Equipos.** Ampliar `Projectile::team` toca la puntería de anomalías y especies. `Hostile()` centraliza la decisión; las pruebas de combate actuales tienen que seguir pasando sin cambios.
- **Dueño como texto.** En solitario el dueño es `"local"`; si el jugador cambia de id en `ajustes.txt`, sus estaciones dejan de ser suyas. Sin bandera Internet, la propiedad no debe restringir nada.
- **La C provisional.** Todo el coste de niveles y gremios cuelga de una cifra que aún no existe en el código; hasta tener coste por clase de nave, C es una constante a ajustar jugando.
- **Ciudadela.** La estación lobby no puede caer: lleva una marca de indestructible para que la regla 2 no la alcance por accidente.
- **Ninja y cabeceras.** P2, P5, P10 y P12 tocan cabeceras muy incluidas: `--clean-first` siempre.
