# Pruebas de Epsilon-Z

Todo lo que existe para probar el juego sin jugarlo a mano: compilar, las pruebas automáticas, las opciones del ejecutable y los comandos de la consola. Las órdenes de ejemplo se ejecutan desde `EpsilonEngine`, salvo que se diga otra cosa.

## 1. Compilar

| Preset | Qué compila | Para qué |
|---|---|---|
| `x64-debug` | Motor y lógica del juego, sin ventana | Pruebas con comprobaciones de depuración |
| `x64-release` | Lo mismo, optimizado | Pruebas rápidas |
| `x64-asan` | Lo mismo, con AddressSanitizer | Errores de memoria |
| `x64-sdl-debug` | Todo, con ventana y GPU | El juego en depuración |
| `x64-sdl-release` | Todo, optimizado | El juego de verdad y las capturas |

```bash
cmake --preset x64-sdl-release
```

```bash
cmake --build --preset x64-sdl-release
```

- Hay que compilar desde una consola de Visual Studio, o tras llamar a `vcvars64.bat`.
- **Tras tocar una cabecera `.h`**, añade `--clean-first`: Ninja a veces deja un `.obj` sin recompilar, y el fallo parece de lógica.
- **Si el juego está abierto**, el `.exe` está bloqueado y el enlazado falla. Ciérralo o renombra el `.exe` antes de compilar.

## 2. Pruebas automáticas

| Ejecutable | Qué prueba |
|---|---|
| `epsilon_tests.exe` | El motor: astrofísica, generación, órbitas, colisiones, cámara, comandos… |
| `epsilon_game_tests.exe` | El juego: motores, antimateria, estaciones, combate, escudos, calor, partidas guardadas (y el rechazo de las de otra versión), configuración, rutas, tutorial (cada paso señala algo que existe y el tutorial entero avanza cumpliendo sus condiciones, con su guía y sus motivos), escáner y eco frío (la primera estructura se encuentra desde la órbita de la estrella en seis semillas)… |

Están en `out\build\<preset>\`. Terminan con una línea como `12108 comprobaciones, 0 fallos`, y devuelven 0 si todo pasa y 1 si algo falla. Cada fallo se imprime como `FALLO archivo:línea: condición`. Antes de cada grupo escriben su nombre entre corchetes, como `[ Motores ]`, así que se ve dónde se colgó si algo se cuelga.

Necesitan las DLL de vcpkg en el `PATH`:

```bash
set PATH=%PATH%;%CD%\out\vcpkg_installed\x64-windows\bin
```

```bash
out\build\x64-sdl-release\epsilon_game_tests.exe
```

También se lanzan los dos con CTest:

```bash
ctest --preset x64-release
```

No hay forma de lanzar un solo grupo: siempre se ejecutan todos. El juego tarda unos 2 minutos y el motor algo menos.

**Partida dorada.** El primer grupo juega una partida corta a paso fijo (viajar, minar, maniobrar, desplegar una central, atracar y combatir) y compara su guardado final con `tests/data/partida_dorada.txt` (es un guardado, pero con extensión `.txt` porque los `.sav` no se suben), número a número con una tolerancia relativa de 10⁻⁶. Sirve para reorganizar el código sin cambiar el comportamiento (ver `docs/multijugador.md`). Si falla, imprime las primeras líneas distintas y deja la partida de ahora en `tests/data/partida_dorada.ahora.txt` (que no se sube). Si el cambio es querido, se regenera (solo ejecuta ese grupo, unos 15 segundos) y se sube el archivo nuevo:

```bash
out\build\x64-sdl-release\epsilon_game_tests.exe --regenerar-dorada
```

Para comprobar solo la dorada, sin el resto de grupos (lo rápido tras reorganizar código):

```bash
out\build\x64-sdl-release\epsilon_game_tests.exe --solo-dorada
```

## 3. Opciones del ejecutable del juego

`epsilon_game.exe` se ejecuta **desde su propia carpeta**, porque busca las fuentes en `fonts\`.

| Opción | Qué hace |
|---|---|
| `--frames N` | Termina tras N fotogramas e imprime `Fotogramas`, `Presentados` y `Media: X FPS`. Es lo que hace que una prueba sea repetible |
| `--screenshot a.png` | Guarda el último fotograma. Requiere `--frames` |
| `--size W H` | Tamaño de la ventana. Por defecto, 1600 × 900 |
| `--no-vsync` | Sin sincronización vertical. **Imprescindible para medir FPS** |
| `--fixed-step` | La partida avanza a pasos fijos de 1/60 s (`GameSession::StepFixed`), como lo hará el servidor. Por encima de 60 FPS hay fotogramas sin paso: aún no se interpola |
| `--mode N` | Salta el menú: 0 Pacífico, 1 Fácil, 2 Normal, 3 Difícil, 4 Realista |
| `--no-tutorial` | Sin diálogos |
| `--seed N` | Otra galaxia. Por defecto, 20261105. Con la de por defecto no hay mundos terrestres; usa `--seed 1` |
| `--cmd "texto"` | Ejecuta un comando de la consola al empezar. Se puede repetir; van en orden |
| `--cmd-at F "texto"` | Ejecuta ese comando en el fotograma F (se puede repetir). Con `--frames`, el juego también anota en la salida cada cambio de paso del tutorial y cada mensaje nuevo del registro, con el tiempo real: `[f5617, 40.3 s reales] ...`. Con `--load`, los `--cmd` corren **después** de cargar la partida |
| `--cursor` | Empieza con el cursor activo |
| `--click F X Y` | Clic simulado en el fotograma F, en esos píxeles. Se puede repetir |
| `--wheel` | Empieza con la rueda de opciones abierta |
| `--atlas` | Empieza con el mapa de sistemas abierto |
| `--atlas-astro` | El mapa, en la vista de un astro |
| `--atlas-tabla` | El mapa, en la tabla periódica |
| `--analysis` | Empieza con la ventana de análisis abierta |
| `--save a.sav` | Usa esa **carpeta** de partida: dentro van `mundo.sav` y `jugadores/<id>.sav`. Si queda un archivo `a.sav` de antes de la versión 13 con ese nombre, bórralo: no se puede crear la carpeta y el juego avisa de que no pudo guardar |
| `--load` | Carga la partida al empezar, sin pasar por el menú |
| `--quality N` | Calidad gráfica: 0 Baja, 1 Media, 2 Alta, 3 Ultra |
| `--page N` | Página del menú: 0 principal, 1 nueva partida, 2 configuración, 3 créditos, 4 gráficos, 5 elección de interfaz |
| `--interfaz minimalista\|experta` | Modo de la interfaz. Sin esta opción y con `--frames`, el menú abre la elección de interfaz (aún no hay ninguna elegida) y la partida se ve en modo experto |
| `--tamano N` | Tamaño de la interfaz y de la letra: 100, 115 (de serie), 130 o 150 %. Crece solo hasta lo que cabe en la ventana |

Comandos de consola de la interfaz: `interfaz minimalista|experta [tamaño]` y `plegar N`, que pliega o despliega una ventana (0 nave, 1 bodega, 2 sensores, 3 objetivo, 4 radar, 5 estructura, 6 soles; en el modo minimalista, 7 nave, 8 señales, 9 objetivo, 10 detalles). En la partida, con el cursor (C), pulsar la cabecera de una ventana hace lo mismo.

```
epsilon_game.exe --frames 120 --mode 2 --no-tutorial --interfaz minimalista --save prueba.sav --screenshot mini.png --cmd revelar --cmd "plegar 10"
```
| `--debug` | Activa la capa de depuración de Direct3D 12 |

**Con `--frames` el juego no toca tus datos:** no lee ni escribe la partida de `%APPDATA%` (salvo que pases `--save`) ni `ajustes.txt`. Así una prueba no depende de lo que hayas jugado.

## 4. Comandos de la consola

Se escriben con F1 dentro del juego, o se pasan con `--cmd`. Cada uno deja su respuesta en el registro de la pantalla, y en la salida de la consola si viene de `--cmd`. `ayuda` los lista todos.

### Moverse y elegir

| Comando | Qué hace |
|---|---|
| `objetivo <tipo>` | Fija como objetivo lo más cercano de ese tipo: `planeta`, `campo`, `estrella`, `estacion`, `estructura`, `hostil`, `neutral`, `bastion`, `refugio`. No depende de cómo se llame cada cosa |
| `astro <n>` | Fija el astro número n de este sistema |
| `saltar <tipo> [km]` | Teletransporta la nave junto a lo más cercano de ese tipo, a esa distancia o a la de llegada |
| `viajar <sistema>` | Teletransporta a la estrella de otro sistema y lo marca como conocido |
| `buscar <clase> [n]` | Teletransporta junto al primer astro de una clase en toda la galaxia: `terrestre`, `hielo`, `lava`, `roca`, `desierto`, `gaseoso`, `helado`, `caliente`, `luna`. Con n, el n-ésimo |
| `eclipse [km]` | Pone la nave en la sombra del planeta objetivo, de espaldas al sol, y dice a qué distancia quedan los dos. Para ver el eclipse: `--cmd "objetivo planeta" --cmd "eclipse 250000" --cmd "vista 0 6"`. Ojo: con menos km que el radio del planeta, la nave queda dentro |
| `ir` | Ruta asistida al objetivo |
| `orbitar` | Circulariza alrededor de lo que domina aquí |
| `girar <derecha> <abajo>` | Gira la nave de golpe, en grados |
| `vista <grados> [km] [elevación]` | Coloca la cámara alrededor de la nave. La elevación va en grados: con un valor positivo mira desde abajo, por ejemplo `vista 160 1.15 55` enseña la panza y la bahía de atraque |

### Motores y tiempo

| Comando | Qué hace |
|---|---|
| `motor <modo>` | `impulso`, `crucero`, `hibrida` o `hiper`. Los dos últimos hay que desbloquearlos: en una prueba, pon antes `creativo si`, o responde «No disponible». **Ojo: `curvatura` ya no existe y se convierte en impulso, sin avisar** |
| `mando <n>` | Mando de -100 a 100 |
| `frenar` | Como pulsar Espacio: mando a cero y la nave quieta |
| `tiempo <n>` | Segundos simulados por segundo real. Por defecto, 60 |

### La nave

| Comando | Qué hace |
|---|---|
| `casco <n>` | Fija el casco |
| `dano <zona> <casco>` | Golpea una zona del casco. Sin nada, lista las zonas |
| `blindaje <n>`, `maniobra <n>`, `computador <n>` | Fija ese nivel |
| `escaner` | Lanza un barrido. Con un número, fija su nivel |
| `sondeo` | Radio del sistema, alcance y radio de eco del escáner, distancia en UA de la nave y de la estrella a cada anomalía fría del sistema (con su firma y si está revelada), y la pista del último barrido. Al empezar todavía no hay pista: los `--cmd` corren antes del primer barrido |
| `nivel <parte> <n>` | Fija un nivel: `escaner`, `blindaje`, `maniobra`, `computador`, `armas`, `enlace`, `arena`, `plasma`, `em`, `conos`, `amor`, `agua`, `hielo`, `nitrogeno`, `radiadores` |
| `emisores <n>` | Fija los emisores de energía negativa |
| `regalo [n]` | Suma emisores (2 por defecto), 2000 kt de metal y 800 kt de silicatos |
| `escudo [tipo]` | `arena`, `plasma`, `em`, `amor` o `no`. Sin nada, el siguiente |
| `calor [n]` | Muestra el calor; con un número, lo fija. 100 es el límite |
| `refrigerante <tipo> <si\|no>` | `agua`, `hielo`, `nitrogeno` o `radiadores` |
| `creativo [si\|no]` | Invulnerable, todo sin fin, todos los motores y todos los sistemas conocidos |
| `seguir` | Continúa tras perder la nave: reaparece atracada en la última estación propia donde atracó (o en el punto de partida). Hay que esperar a la pantalla final, unos 7 s reales tras `casco 0`; con `--cmd-at` |
| `modulo [ranura tipo]` | Monta un módulo (atracada o en creativo). Sin nada, lista tipos y ranuras. Las armas humanas son los tipos 10 a 23 y **no salen en el panel del taller**; piden plano, elementos y que la clase de la nave las admita (la fragata ligera no monta pesadas) |
| `mover <de> <a>` | Cambia una bahía de drones de serie a otro hueco exterior vacío. No se pueden sacar de la nave |
| `plano [id]` | Fabrica el plano de un arma humana (0 a 9); solo atracada en la origami o en creativo. Sin nada, lista los planos |
| `misiles <ranura> [n]` | Fabrica misiles para una bahía (atracada): gasta energía, metal y Pt/Pd/Ir (nuclear: U/Th). Sin número, hasta llenarla |
| `puntual [n]` | Monta n puntos de defensa puntual (30 en la fragata ligera). Las plataformas son el módulo 9 |
| `nave` | Lista las naves de la partida; la del asterisco es la que llevas |
| `nave nueva [km]` | Otra nave a tu costado (20 km si no se dice; nunca a menos de cuatro radios), parada, con su propio jugador. Queda como objetivo. Mientras haya más de una, ninguna puede salir del sistema |
| `nave ver <id>` | Pasas a llevar esa nave: la cámara, la interfaz y las órdenes son las suyas. En el juego, **F2** pasa a la siguiente |
| `nave <id> <orden...>` | Ejecuta cualquier comando con esa nave: `nave 2 motor crucero`, `nave 2 mando 40`, `nave 2 ir`. El objetivo es uno solo para toda la consola |
| `jugador` | Lista los jugadores con su nave, sus puestos y si van a pie; el del asterisco eres tú |
| `jugador nuevo [nombre]` | Otro jugador en tu nave. Con dos, el primero se queda con el pilotaje y el segundo con armas, escudos e ingeniería |
| `jugador ver <n>` | Pasas a ser ese jugador: su nave, sus puestos, su registro, su tutorial. En el juego, **F3** pasa al siguiente |
| `jugador <n> <orden...>` | Ejecuta cualquier comando como ese jugador: `jugador 2 escudo em`, `jugador 2 apie`. Si la orden pide un puesto que no lleva, se rechaza y se le dice por qué |
| `puesto` | Tus puestos. `puesto <pilotaje\|armas\|ingenieria>` ocupa uno libre; `puesto dejar <...>` lo suelta. Sin nadie en el pilotaje, la nave no obedece ningún mando |

Receta (dos naves a la vista): `--cmd "nave nueva 2.5" --cmd "plegar 0" --cmd "plegar 1" --cmd "plegar 2" --cmd "plegar 3"` deja la segunda a la derecha de la pantalla. Para verla moverse por su cuenta: `--cmd "nave 2 mando 40" --cmd-at 300 "nave 2 frenar"`.

Receta (armamento humano sin ir a ninguna estación): `--cmd creativo --cmd "plano 0" --cmd "modulo 2 10"` monta una coilgun ligera; `--cmd "modulo 3 16" --cmd "misiles 3"` una bahía de 10 llena. `FireAll` (el botón de fuego) dispara también estas armas.

### Astros, estaciones y alienígenas

| Comando | Qué hace |
|---|---|
| `revelar` | Da por analizado todo el sistema actual, sin barrido |
| `analisis` | Marca como usado el análisis del objetivo, para el tutorial |
| `central` | Despliega una central en el objetivo. |
| `drones`, `recoger-drones` | Manda los drones mineros al objetivo (un planeta o un campo analizado, a menos de 40 000 km) o los hace volver |
| `estacion` | Suelta la estación origami donde está la nave: dentro de un sistema, fuera de órbita, con el impulso y a menos de 2 km/s |
| `usar` | Recoge de una estación, entrega material a la origami o explora una estructura |
| `atracar [ya]` | Atraca en la estación objetivo. Con `ya`, sin esperar al haz |
| `soltar` | Suelta las amarras |
| `apie`, `abordo`, `vistapie` | Bajar a la cubierta, volver a la nave, cambiar de cámara a pie |
| `aparecer <tipo> [km] [n]` | Crea delante de la nave: `hostil`, `neutral`, `bastion`, `bastion1`…`bastion5` (con ese nivel), `estacion`, `estructura`, `capital` (capital suprema) o `coraza` (planeta coraza, a km de su superficie), a 2000 km por defecto, hasta 20. El último queda como objetivo |
| `alerta [sistema]` | Alerta de la especie en el sistema actual (o en otro), su región, si la flota está rendida y cuántas revanchas van |
| `destruir` | Destruye la anomalía objetivo |
| `disparar [arma]` | `torreta`, `espinal`, `misil` o `drones`. Sin nada, todo lo que esté listo |
| `apuntar [arma]` | Gira el rumbo a la solución de tiro |
| `alinear` | El rumbo sigue la solución del cañón espinal |
| `danar_enemigo [zona %]` | Golpe del jugador, sin escudos, en una zona del alienígena objetivo (o del más cercano): el % es de su casco entero. Sin nada, lista sus zonas con sus nombres (`cuchilla de babor`, `ojo`...) |
| `cinematica [si\|no]` | Modo cinemática, también con la **N**: tras cada impacto tuyo en un blanco, la cámara lo rodea mirando al golpe durante 6 s reales, con el tiempo a ×0,25 |

Receta (planeta coraza): `--frames 120 --cmd "creativo si" --cmd "aparecer coraza 150000" --cmd alerta` lo pone delante; dispara y avisa al entrar en su rango de percepción.

Receta (enemigo dañado, en cinemática): `--frames 90 --cmd "creativo si" --cmd "aparecer hostil 3" --cmd "cinematica si" --cmd "danar_enemigo 13 10" --cmd "danar_enemigo 9 34"` daña el cuerpo y arranca la cuchilla de babor. La toma dura 6 s reales: con más de unos 400 fotogramas ya ha terminado y vuelve la cámara de persecución.

### Interfaz

| Comando | Qué hace |
|---|---|
| `trayectoria` | Muestra u oculta la línea de la ruta |
| `radar <0..10>` | Alcance del radar |
| `tutorial [paso] [leer]` | Salta a ese paso del tutorial con su objetivo, su guía y sus motivos ya en pantalla. Con `leer`, empieza por sus líneas. Sin nada, dice en qué paso está. Si el paso ya está cumplido, pasa al siguiente en el primer fotograma |

Pasos del tutorial: 0 mando (R), 1 frenar (Espacio), 2 cursor (C), 3 escáner, 4 análisis, 5 drones, 6 estación origami (opcional), 7 central, 8 recoger, 9 estructura (`estructura`, con la pista exacta), 10 explorarla, 11 burbuja Híbrida, 12 despedida.

### Del motor

| Comando | Qué hace |
|---|---|
| `estado` | Qué funciones están activas |
| `activar <función>`, `desactivar <función>`, `alternar <función>` | Funciones: `cielo`, `nebulosa`, `asteroides`, `agujeros`, `modelos`, `orbitas`, `rutas`, `hud`, `bloom`, `vineta`, `aberracion`, `grano`, `streaming`, `descarte`, `colisiones`, o `todo` |
| `fijar [variable] [valor]` | Lista o cambia una variable: `bloom.fuerza`, `bloom.umbral`, `exposicion`, `vineta.fuerza`, `aberracion.fuerza`, `grano.fuerza`, `detalle`, `colision.rocas` |
| `impactos` | Lista los choques pendientes y los últimos |
| `cargados` | Cuenta lo que hay en memoria |
| `ayuda` | Lista los comandos |

Alias en inglés: `on`, `off`, `toggle`, `set`, `help`, `status`.

## 5. Recetas fiables

Desde `out\build\x64-sdl-release`, con las DLL en el `PATH`.

**Rendimiento.** Siempre `--no-vsync`, sin tutorial, la misma semilla, varias repeticiones y la media:

```bash
epsilon_game.exe --frames 500 --no-vsync --mode 2 --no-tutorial --cmd "creativo si" --cmd "motor hibrida" --cmd "mando 100" --cmd "tiempo 300"
```

Comprueba en la salida que `motor hibrida` respondió «Híbrida». Si dice «No disponible», la prueba midió otra cosa.

**Rendimiento por calidad.** La misma orden con `--quality 0` a `--quality 3`. En un equipo donde el límite es el procesador, las cuatro dan lo mismo.

**Rendimiento con menos núcleos.** Limita el juego a unos pocos núcleos: `55` son 4 núcleos sin hyperthreading en un Xeon de 28 hilos.

```bash
start "" /B /WAIT /AFFINITY 55 epsilon_game.exe --frames 500 --no-vsync --mode 2 --no-tutorial
```

**Captura junto a un planeta:**

```bash
epsilon_game.exe --frames 200 --mode 2 --no-tutorial --save prueba.sav --screenshot planeta.png --cmd revelar --cmd "objetivo planeta" --cmd "saltar planeta 30000"
```

**Captura de un mundo concreto:**

```bash
epsilon_game.exe --frames 200 --seed 1 --mode 2 --no-tutorial --save prueba.sav --screenshot terrestre.png --cmd creativo --cmd "buscar terrestre" --atlas-astro
```

**Combate:**

```bash
epsilon_game.exe --frames 300 --mode 2 --no-tutorial --save prueba.sav --screenshot combate.png --cmd "aparecer hostil 3000" --cmd "disparar"
```

**Estación origami.** El despliegue va en segundos reales: unos 90 fotogramas por segundo en este equipo, con vsync a 60. A 330 fotogramas se está abriendo alrededor de la nave, a 1000 se aparta a popa y a 1800 ya ha sacado los brazos. Con `atracar ya` la nave atraca en su esclusa:

```bash
epsilon_game.exe --frames 330 --mode 2 --no-tutorial --quality 3 --save prueba.sav --cmd estacion --cmd "vista 150 2.2 25" --screenshot abriendo.png
```

```bash
epsilon_game.exe --frames 2000 --mode 2 --no-tutorial --quality 3 --save prueba.sav --cmd estacion --cmd "objetivo estacion" --cmd "atracar ya" --cmd "vista 100 3.2 35" --screenshot atracada.png
```

**Drones mineros.** El panel de la bodega dice cuántos están fuera. A 60 veces el tiempo vuelan a 720 km/s reales, así que de lejos solo se ve el brillo de sus motores:

```bash
epsilon_game.exe --frames 420 --mode 2 --no-tutorial --save prueba.sav --cmd revelar --cmd "objetivo planeta" --cmd "saltar planeta 30000" --cmd drones --screenshot drones.png
```

**Menú:**

```bash
epsilon_game.exe --frames 300 --page 2 --save prueba.sav --screenshot configuracion.png
```

Sin `--interfaz experta` (o `minimalista`), un menú con `--frames` se queda en la elección de interfaz, porque no lee `ajustes.txt`.

**Escáner y eco frío.** El barrido dura 30 segundos **reales**: con `--no-vsync` este equipo va a unos 180 fotogramas por segundo, así que hacen falta unos 6500 fotogramas. El resultado sale en las líneas `Registro:` del final. Desde la salida, la estructura queda fuera del alcance pero dentro del eco: el barrido da la pista («Eco frío a ~15.1 UA (±8.0), rumbo 142°…») y la marca en pantalla:

```bash
epsilon_game.exe --frames 6500 --no-vsync --mode 2 --no-tutorial --save prueba.sav --screenshot eco.png --cmd sondeo --cmd escaner
```

Desde la órbita de la estrella la encuentra («1 anomalías nuevas»):

```bash
epsilon_game.exe --frames 6500 --no-vsync --mode 2 --no-tutorial --save prueba.sav --screenshot estructura.png --cmd "saltar estrella" --cmd escaner
```

**Partida de una versión anterior.** El juego la rechaza y el menú lo dice en rojo bajo los botones. La versión actual es la 12 (el tutorial reorganizado). Para probarlo, copia una partida y cambia la cabecera a `EPSILON-NEBULOSA 11` recalculando la suma (FNV-1a de 64 bits de todo lo anterior a la línea `suma`); luego:

```bash
epsilon_game.exe --frames 150 --interfaz experta --save viejo.sav --screenshot menu_viejo.png
```

**Tutorial con su guía en pantalla.** `tutorial N` deja el paso N con su objetivo, el marco y la flecha hacia lo que hay que usar y los motivos por los que aún no avanza. Conviene `--mode 0` (pacífico) y probar también a 1280 × 720, la resolución más pequeña:

```bash
epsilon_game.exe --frames 200 --mode 0 --interfaz experta --save prueba.sav --screenshot frenar.png --cmd "tutorial 3" --cmd "mando 50"
```

Escáner con la nave en marcha: señala Espacio y dice «Mando a cero: no» y «Nave quieta: no, va a 33 km/s». Otras recetas:

| Qué se ve | Opciones |
|---|---|
| La opción Escáner, iluminada en la rueda | `--wheel --cmd "tutorial 3"` |
| La T con «T, y en la rueda: Ir» y la distancia que falta | `--cmd revelar --cmd "tutorial 5" --cmd "objetivo planeta"` |
| La burbuja Híbrida: su coste junto a la antimateria que hay | `--cmd "emisores 1" --cmd "tutorial 11"` |
| «OBJETIVO CUMPLIDO» y el destello (a los 60 fotogramas; dura unos 2,6 s) | `--frames 60 --cursor --cmd "tutorial 2"` |
| El modo minimalista, en el paso de la central | `--interfaz minimalista --cmd revelar --cmd "tutorial 7" --cmd "objetivo estrella"` |
| La pista de la estructura, marcada en el radar | `--frames 6500 --no-vsync --cmd "tutorial 9" --cmd escaner` |

**Tutorial entero sin modo creativo.** Una sola ejecución, con `--cmd-at` (cada paso nuevo empieza leyendo sus líneas: `tutorial N` las salta). Ritmo medido, a ×60, a ~140 FPS: R, 1 s; frenar, 8 s; barrido, 30 s; Ir al planeta, 45 s; Ir a la estrella, 60 s; Usar en la central, 20 s; barrido de la estructura, 30 s; Ir a la estructura, 50 s; explorarla, 120 s. El eco exacto del paso 9 sale en el primer barrido desde la órbita de la estrella. Un `escaner` mientras la ruta asistida sigue en curso falla: «El barrido exige la nave quieta». Para ver la Híbrida junto a la estructura: `--frames 2600 --cmd "tutorial 11" --cmd "emisores 1" --cmd "aparecer estructura 400000" --cmd ir`.

Para ver las líneas del diálogo, `tutorial N leer`. Cada línea necesita dos clics con `--cursor`: uno la completa y otro pasa a la siguiente. El cuadro está hacia (800, 570) a 1600 × 900.

### Consejos

- **Usa siempre `--save` con un archivo de prueba** cuando captures: así no se mezcla con tu partida.
- **Elige por tipo, no por nombre:** `objetivo planeta` sigue funcionando aunque cambie la galaxia.
- **Deja pasar fotogramas** después de un salto. Un sistema tarda unos fotogramas en cargarse: con menos de 60, la captura puede salir vacía.
- **La captura usa el último fotograma**, así que los efectos que tardan, como la burbuja de curvatura (unos 4 segundos reales), necesitan bastantes fotogramas.
- **No se puede pulsar Esc** desde la línea de órdenes, así que la pausa no se puede capturar.
