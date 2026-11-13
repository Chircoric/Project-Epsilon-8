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

### Fase 3a — Dos naves locales, una entrada por nave — **hecha el 13-11-2026, salvo el paso 3a.6**

Cómo quedó:

- **Flota en la sesión.** `GameSession` guarda `fleet_` (naves) y `crews_` (un jugador por nave, en el mismo orden). No se pasó la nave como parámetro a cada método: hay una **nave en curso** (`vessel_`, `pilot_`), sobre la que trabaja todo el código, y `Use()`/`Using` la cambian. Fuera de `Step` la nave en curso es siempre la del **foco** (`SetFocus`), que es de la que hablan `Ship()`, `Apply()`, `Contacts()` y lo demás. Es menos explícito que un parámetro, pero dejó intactas las firmas de más de cien métodos y la partida dorada pasó sin regenerar. `ApplyTo(id, orden)` manda una orden a una nave concreta.
- **Paso.** `StepAll(real, mandos)` lleva un mando por nave; `Step` le da el suyo a la del foco. El reloj sigue la regla 2 del §2.3: `Vessel::timeFocus` y `exteriorFactor` por nave, y la burbuja usa el mínimo de cada uno. La nave que dilata más recibe menos tiempo propio. La cámara lenta de la cinemática solo se aplica si todos la están viendo.
- **El mundo las ve a todas.** Cada anomalía atiende a la nave en vuelo más cercana. Los proyectiles llevan `Projectile::ship` (quién disparó, o a quién apuntan): un proyectil enemigo puede dar a cualquier nave, y la baja, el botín y los avisos son de quien disparó. Sin fuego amigo. Dos naves chocan y se dañan las dos. `TargetRef::Kind::Ship` deja apuntar, seguir e ir con el piloto a otra nave, que sale siempre en los sensores.
- **Límite provisional (3a.7).** Con más de una nave, ninguna sale del sistema donde está la flota (a 1,5 veces su radio se la frena y se avisa). También la del foco, no solo la segunda: el motor carga un único sistema.
- **Guardado.** `mundo.sav` lleva todas las naves; `Snapshot()` da el jugador del foco y `SnapshotPlayer(i)` el de otra nave. Al cargar, el foco va a la nave del jugador que carga.
- **Juego.** Consola: `nave`, `nave nueva [km]`, `nave ver <id>`, `nave <id> <orden...>`. F2 pasa a llevar la siguiente nave. Las demás naves se dibujan.

Lo que falta o es provisional:

- **3a.6, clase y diseño por nave, no está hecho.** `CurrentShipDesign()` y `rules_.ship` siguen siendo globales: todas las naves son de la misma clase.
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
| 3a.6 | Diseño y clase **por nave** en lugar de globales. |
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

### Fase 5 — Protocolo dentro de un proceso

Servidor y cliente separados por una interfaz `Transport`, con una implementación en memoria. El cliente ya **no** toca la sesión: manda `ShipControls` y `ShipCommand`, recibe instantáneas y eventos, y con ellos rellena una copia de solo lectura que alimentan `GameHud` y `GameLayer`. El juego en solitario pasa a ser «servidor + cliente en memoria». Pruebas: serializar y deserializar da lo mismo; una partida por el transporte en memoria da lo mismo que directa.

### Fase 6 — Red

Biblioteca `epsilon_net` con ENet. `--servidor [puerto]` y `--conectar ip[:puerto]`. Saludo: versión, suma de `Balance`, semilla, identidad del jugador. Entrada tardía y desconexión limpia.

### Fase 7 — LAN y servidor dedicado

Descubrimiento por difusión, lista de partidas en el menú, `epsilon_server.exe` sin ventana.

### Fase 8 — Pulido de red

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
- Consola por la entrada estándar con los comandos de `engine.Commands()` que ya existen.
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
| 3 | **Conocimiento** (sistemas, astros analizados) | por jugador · por equipo | Por equipo | 1.2, 2 |
| 4 | **Estaciones y recursos** | comunes · de cada jugador, con permiso para los demás | Comunes en cooperativo; con dueño cuando haya varias naves | 2, 3a |
| 5 ★ | **Perder la nave** sin vuelta atrás del mundo | (a) reaparece en la última estación propia donde atracó, con la penalización de bodega del modo · (b) nave nueva en el punto de inicio · (c) en modo realista, fuera de la partida | **Decidido (13-11-2026): (a)**; (c) para el modo realista | 1.6 |
| 6 | **Nave de un jugador desconectado** | desaparece · se queda donde está, vulnerable · solo se puede salir atracado y queda a salvo | Desaparece (y vuelve donde estaba); en cooperativo la nave sigue mientras quede alguien | 2, 6 |
| 7 | **Entre naves de jugadores** | fuego amigo sí/no · choque entre naves sí/no · ¿habrá jugador contra jugador? | Sin fuego amigo, con choque, sin JcJ de momento | 3a.3 |
| 8 | **Tutorial, creativo y consola** | tutorial por jugador en red o solo en solitario · quién puede usar la consola | Tutorial solo en solitario al principio; consola y creativo solo para el anfitrión | 1.2, 6 |
| 9 | **Tamaño** | jugadores por nave · naves por mundo · ¿Internet o solo LAN? | 4 por nave, 8 naves, solo LAN y por IP (Internet pide atravesar el router: otro proyecto) | 6, 7 |
| 10 | **Cámara lenta del modo cinematográfico** en red | se pierde · solo si todos la tienen · siempre | Solo si todos los de la burbuja la tienen | 3a.4 |
| 11 | **Orden** | completo (3a antes de la red) · atajo cooperativo (§6) | Completo, como pides; el atajo existe si quieres jugar antes | — |
| 12 | **Naves distintas** | cada jugador elige clase al entrar · todos la misma al principio | Misma clase hasta la fase 3a.6 | 3a.6 |

Con las decisiones 1, 2 y 5 se puede empezar la fase 0, que no depende de ninguna, y entrar en la fase 1 sin rehacer nada.
