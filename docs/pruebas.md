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
| `epsilon_game_tests.exe` | El juego: motores, antimateria, estaciones, combate, escudos, calor, partidas guardadas, configuración, rutas, tutorial… |

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

## 3. Opciones del ejecutable del juego

`epsilon_game.exe` se ejecuta **desde su propia carpeta**, porque busca las fuentes en `fonts\`.

| Opción | Qué hace |
|---|---|
| `--frames N` | Termina tras N fotogramas e imprime `Fotogramas`, `Presentados` y `Media: X FPS`. Es lo que hace que una prueba sea repetible |
| `--screenshot a.png` | Guarda el último fotograma. Requiere `--frames` |
| `--size W H` | Tamaño de la ventana. Por defecto, 1600 × 900 |
| `--no-vsync` | Sin sincronización vertical. **Imprescindible para medir FPS** |
| `--mode N` | Salta el menú: 0 Pacífico, 1 Fácil, 2 Normal, 3 Difícil, 4 Realista |
| `--no-tutorial` | Sin diálogos |
| `--seed N` | Otra galaxia. Por defecto, 20261105. Con la de por defecto no hay mundos terrestres; usa `--seed 1` |
| `--cmd "texto"` | Ejecuta un comando de la consola al empezar. Se puede repetir; van en orden |
| `--cursor` | Empieza con el cursor activo |
| `--click F X Y` | Clic simulado en el fotograma F, en esos píxeles. Se puede repetir |
| `--wheel` | Empieza con la rueda de opciones abierta |
| `--atlas` | Empieza con el mapa de sistemas abierto |
| `--atlas-astro` | El mapa, en la vista de un astro |
| `--atlas-tabla` | El mapa, en la tabla periódica |
| `--analysis` | Empieza con la ventana de análisis abierta |
| `--save a.sav` | Usa ese archivo de partida |
| `--load` | Carga la partida al empezar, sin pasar por el menú |
| `--quality N` | Calidad gráfica: 0 Baja, 1 Media, 2 Alta, 3 Ultra |
| `--page N` | Página del menú: 0 principal, 1 nueva partida, 2 configuración, 3 créditos |
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
| `ir` | Ruta asistida al objetivo |
| `orbitar` | Circulariza alrededor de lo que domina aquí |
| `girar <derecha> <abajo>` | Gira la nave de golpe, en grados |
| `vista <grados> [km]` | Coloca la cámara alrededor de la nave |

### Motores y tiempo

| Comando | Qué hace |
|---|---|
| `motor <modo>` | `impulso`, `crucero`, `hibrida` o `hiper`. Los dos últimos hay que desbloquearlos: en una prueba, pon antes `creativo si`, o responde «No disponible». **Ojo: `curvatura` ya no existe y se convierte en impulso, sin avisar** |
| `mando <n>` | Mando de -100 a 100 |
| `tiempo <n>` | Segundos simulados por segundo real. Por defecto, 60 |

### La nave

| Comando | Qué hace |
|---|---|
| `casco <n>` | Fija el casco |
| `dano <zona> <casco>` | Golpea una zona del casco. Sin nada, lista las zonas |
| `blindaje <n>`, `maniobra <n>`, `computador <n>` | Fija ese nivel |
| `escaner` | Lanza un barrido. Con un número, fija su nivel |
| `nivel <parte> <n>` | Fija un nivel: `escaner`, `blindaje`, `maniobra`, `computador`, `armas`, `enlace`, `arena`, `plasma`, `em`, `conos`, `amor`, `agua`, `hielo`, `nitrogeno`, `radiadores` |
| `emisores <n>` | Fija los emisores de energía negativa |
| `regalo [n]` | Suma emisores (2 por defecto), 2000 kt de metal y 800 kt de silicatos |
| `escudo [tipo]` | `arena`, `plasma`, `em`, `amor` o `no`. Sin nada, el siguiente |
| `calor [n]` | Muestra el calor; con un número, lo fija. 100 es el límite |
| `refrigerante <tipo> <si\|no>` | `agua`, `hielo`, `nitrogeno` o `radiadores` |
| `creativo [si\|no]` | Invulnerable, todo sin fin, todos los motores y todos los sistemas conocidos |
| `seguir` | Continúa tras perder la nave |

### Astros, estaciones y alienígenas

| Comando | Qué hace |
|---|---|
| `revelar` | Da por analizado todo el sistema actual, sin barrido |
| `analisis` | Marca como usado el análisis del objetivo, para el tutorial |
| `mina`, `central` | Despliega una mina o una central en el objetivo |
| `usar` | Recoge de una estación o explora una estructura |
| `atracar [ya]` | Atraca en la estación objetivo. Con `ya`, sin esperar al haz |
| `soltar` | Suelta las amarras |
| `apie`, `abordo`, `vistapie` | Bajar a la cubierta, volver a la nave, cambiar de cámara a pie |
| `aparecer <tipo> [km] [n]` | Crea delante de la nave: `hostil`, `neutral`, `bastion`, `estacion` o `estructura`, a 2000 km por defecto, hasta 20. El último queda como objetivo |
| `destruir` | Destruye la anomalía objetivo |
| `disparar [arma]` | `torreta`, `espinal`, `misil` o `drones`. Sin nada, todo lo que esté listo |
| `apuntar [arma]` | Gira el rumbo a la solución de tiro |
| `alinear` | El rumbo sigue la solución del cañón espinal |

### Interfaz

| Comando | Qué hace |
|---|---|
| `trayectoria` | Muestra u oculta la línea de la ruta |
| `radar <0..10>` | Alcance del radar |

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

**Menú:**

```bash
epsilon_game.exe --frames 300 --page 2 --save prueba.sav --screenshot configuracion.png
```

**Tutorial con su objetivo en pantalla.** Cada línea de diálogo necesita dos clics: uno la completa y otro pasa a la siguiente. El cuadro del diálogo está hacia (800, 570) a 1600 × 900:

```bash
epsilon_game.exe --frames 260 --mode 2 --cursor --save prueba.sav --screenshot tutorial.png --click 60 800 570 --click 80 800 570 --click 100 800 570 --click 120 800 570 --click 140 800 570 --click 160 800 570
```

### Consejos

- **Usa siempre `--save` con un archivo de prueba** cuando captures: así no se mezcla con tu partida.
- **Elige por tipo, no por nombre:** `objetivo planeta` sigue funcionando aunque cambie la galaxia.
- **Deja pasar fotogramas** después de un salto. Un sistema tarda unos fotogramas en cargarse: con menos de 60, la captura puede salir vacía.
- **La captura usa el último fotograma**, así que los efectos que tardan, como la burbuja de curvatura (unos 4 segundos reales), necesitan bastantes fotogramas.
- **No se puede pulsar Esc** desde la línea de órdenes, así que la pausa no se puede capturar.
