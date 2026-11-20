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
