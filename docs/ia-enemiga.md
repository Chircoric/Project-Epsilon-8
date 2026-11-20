# IA enemiga: reglas de diseño y técnicas

Estado: **propuesta, sin código**. Escrito el 2026-11-20 tras una revisión de riesgo de patentes. Esto no es asesoría legal: si el juego se vende, un abogado de propiedad intelectual debe revisar la versión final de la IA.

Planes que motivan este documento: combate en tierra y por abordaje, y enemigos que «recuerdan» al jugador (fama) y responden a su armamento (counter).

---

## 1. Reglas de diseño (restricción legal)

Warner Bros. tiene la patente **US 10.926.179** («Nemesis characters, nemesis forts, social vendettas and followers in computer games»), concedida en 2021 y vigente hasta ~2035. Según la prensa especializada, la reivindicación 1 es un método que detecta una interacción entre el avatar del jugador y un personaje no jugador y cambia parámetros de **otro** personaje no jugador a partir de esa detección. **No he leído el texto completo de las reivindicaciones**: hay que consultarlo en el registro de la USPTO antes de decidir casos dudosos.

Por eso, la IA de Nebulosa sigue estas reglas:

1. **La memoria es de la raza**, no de individuos. No hay enemigos con nombre, rasgos generados ni historial propio que cambien por lo que pasó en un encuentro.
2. **La fama es un valor por raza y por región** (niveles discretos), como las reputaciones de Fallout, Skyrim o Elite Dangerous. Ya existe algo parecido en `SpeciesState` (alerta por sistema).
3. **El counter se calcula sobre estadísticas agregadas**, no como reacción a un duelo. La raza guarda un histograma del armamento que el jugador **usó en total** (por tipo de daño), con desvanecimiento en el tiempo, y lo aplica como **doctrina** a las naves que **aparezcan después** (equipamiento, escudos, composición de flota).
4. **Ninguna reacción inmediata de «venganza»**: destruir una nave no cambia en el acto a otra concreta. El cambio llega por el histograma y por el reparto de nuevas flotas, con retraso (horas simuladas).
5. **Sin ascensos ni jerarquía individual**: un enemigo no sube de rango por derrotarte ni por huir tú. Las categorías son **clases fijas** de la tabla `kShipClasses`.
6. **Sin seguidores ni guardaespaldas personalizados** de un enemigo concreto.
7. Los jefes con nombre son **fijos y escritos a mano**; sus rasgos no evolucionan según tus encuentros con ellos.

Riesgo residual: la regla 3 está cerca de la reivindicación 1 si se implementa como «matar una nave ⇒ cambian los parámetros de otras». Hay que mantenerla como **agregado en el tiempo y aplicado en la generación**, no como evento. Conviene que un abogado lo revise.

---

## 2. Técnicas de IA de combate (ligeras, escalables, impredecibles)

Resumen de lo investigado (fuentes en el chat; la parte de costes y escalado es conocimiento general, no medición):

| Técnica | Coste | Impredecible | Control | Veredicto |
|---|---|---|---|---|
| Máquina de estados | Mínimo | No | Total | Sirve para el nivel más bajo (esquivar, orbitar) |
| Árbol de comportamiento | Bajo | Poco (rígido) | Alto | Útil como armazón y para jefes |
| **Utility AI** (puntuaciones y curvas) | Bajo | **Alto** | Medio | **Núcleo de la decisión táctica** |
| GOAP (planificación) | Alto | Medio | Medio | Solo para decisiones estratégicas poco frecuentes |

### Arquitectura propuesta: tres niveles

1. **Estrategia de la raza** (muy poco frecuente, minutos u horas simuladas). Lee el histograma de armamento del jugador, la fama y la alerta por región y decide la **doctrina**: qué escudos y armas llevan las próximas flotas y qué clases mandan. Es una tabla pequeña, no una búsqueda.
2. **Táctica de la nave** (unos pocos hercios, fuera del bucle de física). **Utility AI**: cada acción (acercarse, mantener distancia, flanquear, disparar torretas, lanzar misil, huir, abordar) puntúa con curvas de respuesta sobre pocas entradas: distancia, casco propio, escudo del blanco, tipo de daño que le hace más, aliados cerca. Se elige con **softmax o top-k aleatorio**, no con el máximo, para que dos situaciones casi iguales no den siempre la misma respuesta.
3. **Control de bajo nivel** (cada fotograma, ya existe: el computador de puntería y la balística de `Combat.h`). Sin decisiones.

### Cómo se consigue la impredecibilidad sin mucho coste

- **Estrategia mixta** (como piedra-papel-tijera): contra el armamento dominante del jugador, la doctrina no pone siempre el mismo counter, sino una **mezcla** (por ejemplo 60/25/15 %). Un counter fijo se aprende y se evita; una mezcla, no.
- **Ruido acotado en las puntuaciones** (±10-15 %) y una **«paciencia» por nave** sorteada al aparecer (semilla de su `id`, nunca cambia): no genera historial individual, solo variedad.
- **Calidad de decisión variable**: la nave elige a veces la segunda mejor acción. Es el equivalente de la «racionalidad limitada» y evita el aspecto de máquina perfecta.
- **Rutas de aproximación y tiempos** con jitter, para que dos encuentros iguales no empiecen igual.
- **Contrapunto al counter**: la doctrina también tiene una probabilidad pequeña de **innovar** (probar algo que el jugador no ha visto), para que no sea siempre una reacción al pasado.

### Cómo se mantiene ligera y escalable

- **Presupuesto por fotograma**: se actualizan N naves por fotograma, repartidas con un contador (escalonado), no todas a la vez. Los hercios de decisión dependen de la distancia al jugador.
- **Nivel de detalle de IA**: lejos del jugador la flota se simula como una **unidad estadística** (posición, casco total, intención); solo al acercarse se «materializa» en naves con Utility AI. Encaja con las distancias de Nebulosa (cientos de miles de km de visibilidad).
- **Poca memoria por raza, no por nave**: el histograma de armamento es un vector con un número por `DamageType` (media móvil exponencial). Cabe en `SaveGame` con un par de líneas más.
- **Determinismo**: la semilla de cada decisión sale de `id` de nave y del paso de reloj, no de un generador global. Es necesario para el multijugador (`docs/multijugador.md`) y para las pruebas (`game_tests.cpp`).
- **Datos, no código**: curvas, pesos y doctrinas viven en `Balance.h`, como el resto de cifras.

### Combate en tierra y abordaje

La misma Utility AI sirve a pie, con otro conjunto de acciones (cubrirse, avanzar, flanquear, lanzar granada, retirarse). La doctrina de raza decide qué tropas desembarcan (ligeras o pesadas), no su «memoria» de ti.

---

## 3. Qué falta

- Leer el texto completo de las reivindicaciones de la patente '179 y comparar con la regla 3.
- Definir el histograma de armamento y su ritmo de desvanecimiento (propuesta: media móvil con vida media de 24 h simuladas, como la alerta).
- Decidir las acciones y las curvas de la Utility AI y escribir pruebas deterministas en `game_tests.cpp`.
- Conectar la doctrina con las flotas que envían las estaciones de nivel 4 o más (fase F: «la alerta aún no tiene efecto en el combate»).

---

## 4. Puntería balística y maniobra táctica

### Lo que ya existe (revisado el 2026-11-20)

- **La puntería balística ya es simétrica.** `Solve()` ([Combat.cpp](../game/src/Combat.cpp)) calcula la intercepción con la velocidad del blanco y la del tirador, simula el vuelo con la gravedad que conoce y corrige la desviación. La usan el jugador (`Session.cpp:3081`), las naves hostiles (`FireAlien`), los hostiles contra estaciones (`FireAtStation`) y las torretas de estación (`StationVolley`).
- Cada tirador falla según el nivel de su computador (`AimError`, `Scatter`), y el escudo electromagnético lo confunde. Los haces viajan a c y los guiados corrigen su rumbo.
- **Límite conocido:** `Solve()` supone que el blanco mantiene **velocidad constante**. Es el fallo útil que explota la maniobra evasiva: quien cambia de rumbo después de disparar el otro, hace fallar el cálculo.
- **El movimiento hostil es muy simple** (`StepAnomaly`, [Anomalies.cpp](../game/src/Anomalies.cpp)): va en línea recta a `0,8 × alcance` y ahí se queda quieto, con la velocidad suavizada (constante de 4 s). No esquiva y no mantiene formación; sin separación, todas acaban en el mismo punto.

### Evasión: girar bruscamente

- Cada nave hostil tiene un **temporizador de esquiva** con intervalo aleatorio (semilla de su `id`, como el resto) y un impulso lateral, perpendicular a la línea de tiro, de dirección sorteada.
- El intervalo se ajusta al **tiempo de vuelo** de los proyectiles que le apuntan (`FiringSolution::flightTime`): lo que se esquiva es el cálculo de intercepción, así que conviene cambiar de velocidad **a mitad del vuelo**, no al azar.
- La agilidad (empuje lateral y cadencia de giros) sale de la **clase de nave** (`kShipClasses`): los drones y las corvetas giran mucho, las fragatas poco, la artillería casi nada.
- Contra un haz (`Spinal`, a c) no sirve esquivar: se usa **cobertura** (escudo, interponer otra nave o un astro).
- Se aplica **también al jugador** de forma natural: la física ya existe, y es el contrapeso de que los hostiles apunten bien. Opcional: un indicador de «solución de tiro» enemiga.

### Formaciones: amontonarse alrededor de la artillería

- **Roles fijos por clase**, nunca por historial: *Artillería* (lenta, mucho alcance, se queda atrás, apunta con todo el casco), *Pantalla* (escolta que se interpone), *Hostigador* (dron o corveta que esquiva y flanquea). Es una tabla en `Balance.h`.
- La pantalla usa **reglas de bandada** (Reynolds, 1986, ampliamente publicadas): separación, cohesión y alineación, más una **fuerza de ancla** hacia su nave de artillería.
- Cada escolta recibe un **hueco** en una cáscara esférica alrededor de la artillería (puntos de Fibonacci, calculados a partir del número de escoltas), con radio que crece con su eslora. Así «se amontonan» sin chocar y cubren los ángulos hacia el jugador, no solo su lado.
- Si cae la artillería, la pantalla pasa a hostigador: cambia el rol, no su memoria.
- Todo es **O(n) por flota** con vecinos cercanos por celdas. Se actualiza a baja frecuencia y se desactiva cuando la flota está lejos (simulación estadística, §2).

### Implementación sugerida (por fases)

1. `StepAnomaly`: añadir el temporizador de esquiva y la separación entre naves. Sin roles todavía.
2. Roles y ancla a la artillería, con pruebas deterministas en `game_tests.cpp`: misma semilla, misma formación.
3. Medir con `--cmd aparecer` y capturas (`docs/pruebas.md`): porcentaje de aciertos con y sin esquiva, por nivel de computador, para ajustar las cifras en `Balance.h`.
4. Opcional: que `Solve()` admita aceleración del blanco, para que un computador de nivel alto contrarreste una esquiva lenta.

### Patentes

Cálculo de tiro con adelanto, evasión y reglas de bandada son técnicas muy antiguas y de dominio público académico. No hay relación con la patente de §1, siempre que los roles y las maniobras sean **fijos por clase** y no se alteren por lo que ocurrió con un jugador.

---

## 5. Módulo de interferencia de comunicaciones (decidido el 2026-11-20)

Contrapeso del aprendizaje de la raza: el jugador sacrifica torretas para retrasar la adaptación enemiga a su armamento.

- **Módulo exterior**: ocupa una de las ranuras `hp_…` y compite con torretas, radiadores y antenas.
- **Detectable**: una interferencia activa la perciben los sensores enemigos y sube la alerta de la región. Se adapta menos, pero llegan antes.
- **Solo retraso**: alarga el intervalo con que la raza cambia de estrategia. No degrada su observación del jugador (sin ruido).
- Rendimientos decrecientes (propuesta): `multiplicador_de_época = 1 + (MAX − 1)·(1 − r^n)`, con `MAX = 8` y `r = 0,6` (un módulo ≈ ×3,8, dos ≈ ×5,5, tres ≈ ×6,5).
- Coste en energía continua y elementos de comunicaciones (Ga, In, Te), a decidir. Distinto del escudo EM, que estropea la puntería enemiga y no su estrategia.
- **Módulos de sigilo: exclusivos de las naves de categoría prowler (sigilosa)**, cuyo sistema aún está por diseñar (`diseno-combate.md`, apartado 3). No se montan en otras clases.
- Cifras en `Balance.h`; todas son de partida.

## 6. Tablero de estrategias (propuesta)

Registro de nodos con puntuación por condiciones, elegido por flota al llegar y no por individuo:

- `StrategyNode`: id, nombre, `fit(FleetProfile, PlayerProfile)` (0 = no aplica), lista de órdenes sencillas (esquivar, orbitar, anclar a la artillería, cubrirse, salva), enfriamiento y `next` (grafo de transiciones por fase).
- `StrategyBoard::Choose` elige por softmax con semilla de flota y época; `Advance` sigue por `next`.
- Se añaden estrategias con una fila nueva. La lógica vive en `game/` (`Strategy.h/.cpp`); `main.cpp` solo aporta un comando de consola (`estrategia <nombre>`) para forzar un nodo en pruebas.
- Requiere un id de grupo `Anomaly::fleet`, guardado en `SaveGame`.
- El perfil del jugador es una **foto del armamento montado** al llegar la flota, no un historial de combate.
- Aprendizaje opcional: un bandido por época ajusta un multiplicador de peso por nodo y raza. El módulo de §5 alarga esa época.

Estrategias candidatas: saturación de defensa puntual, señuelos, sondeo de escudo, fuego concentrado por zona, supresión, cobertura planetaria, pinza, orbitar, ataque desde la estrella, kiting hacia un bastión, escalonado por alcance y llamada de refuerzos.
