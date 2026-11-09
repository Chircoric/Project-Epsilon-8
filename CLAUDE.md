# Epsilon-Z

Motor gráfico propio (C++20, SDL3 + SDL_GPU sobre Direct3D 12, Dear ImGui como lienzo) y el juego **Nebulosa**, de la línea Epsilon (Epsilon-0 a Epsilon-Z, sobre la Nebulosa Naranja). Windows, Visual Studio 18, CMake + Ninja, dependencias con vcpkg. Repositorio: `Chircoric/Project-Epsilon-8`, rama `main`.

## Reglas

- Habla con el usuario en **español**, y escribe en español los textos del juego, los comentarios y la documentación.
- **Unidades reales**: distancias en km, energía en EJ/ZJ/YJ, potencia en W, masa en t/kt/Mt (`game/include/game/Units.h`). Las cifras de juego viven en `game/include/game/Balance.h`.
- **Solo fuentes y recursos cuya licencia permita incluirlos en un juego** (OFL o similar), con su licencia al lado. Las licencias de escritorio no valen.
- No subas compilados ni partidas: `out/`, `.vs/`, `*.sav` y `*.blend1` están en `.gitignore`.
- Tras un cambio: compila, pasa las pruebas y, si se ve en pantalla, saca una captura y mírala antes de darlo por hecho.

## Dónde está cada cosa

| Carpeta | Qué hay |
|---|---|
| `engine/` | El motor: astrofísica, galaxia, render (`SdlGpuBackend`), comandos, funciones |
| `game/` | El juego: `Session` (reglas), `Ship`, `Stations`, `Anomalies`, combate, defensa, `GameHud` (interfaz), `main.cpp` (opciones y comandos de consola) |
| `game/assets/` | Fuentes (Audiowide, Rubik Glitch, Hangul) y modelos glTF |
| `tests/` | `engine_tests.cpp` y `game_tests.cpp` |
| `docs/` | Capturas, `astrofisica.md` y **`pruebas.md`**: todas las opciones, comandos y recetas de prueba |

Cómo funciona el juego, con cifras: `game/README.md`. El motor: `README.md`.

## Compilar y probar

Desde `EpsilonEngine`, con el entorno de Visual Studio (`"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"`):

```
cmake --preset x64-sdl-release
cmake --build out\build\x64-sdl-release
out\build\x64-sdl-release\epsilon_game_tests.exe
out\build\x64-sdl-release\epsilon_tests.exe
```

- Presets: `x64-debug`, `x64-release`, `x64-asan` (sin ventana) y `x64-sdl-debug`, `x64-sdl-release` (con el juego).
- Las pruebas necesitan `out\vcpkg_installed\x64-windows\bin` en el `PATH`. Terminan con `N comprobaciones, 0 fallos`.
- **Tras tocar una cabecera, añade `--clean-first`**: Ninja a veces deja un `.obj` viejo y el fallo parece de lógica.
- **Si el juego está abierto, el `.exe` está bloqueado**: renómbralo (`epsilon_game_oldN.exe`) antes de enlazar y avisa al usuario.

## Ver el juego sin manos

Desde `out\build\x64-sdl-release` (busca las fuentes en `fonts\`):

```
epsilon_game.exe --frames 200 --mode 2 --no-tutorial --save prueba.sav --screenshot a.png --cmd revelar --cmd "saltar planeta 30000"
```

Detalle en `docs/pruebas.md`. Trampas conocidas:

- El motor de curvatura se llama **`hibrida`**: `motor curvatura` deja la nave en impulso sin avisar.
- `hibrida` e `hiper` empiezan bloqueados: en una prueba, pon antes `--cmd "creativo si"`.
- Para medir FPS, siempre `--no-vsync`. En este equipo (Xeon de 14 núcleos, RX 6600) el límite es el procesador, no la tarjeta.
- Con `--frames` el juego no lee ni escribe la partida ni `ajustes.txt` de `%APPDATA%\Epsilon\Nebulosa`.
- La pausa no se puede capturar: no hay forma de pulsar Esc desde la línea de órdenes.

## Herramientas de este equipo

- **git** no está en el `PATH`: usa `"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe"`. El remoto ya lleva el usuario `Chircoric`. Un `push` que pida iniciar sesión hay que lanzarlo en la terminal del usuario, porque desde aquí no se puede abrir la ventana.
- **Python** solo está el de Ansys, sin numpy ni Pillow: `"C:\Program Files\Ansys Inc\v261\Scade One Student\commonfiles\CPython\3_10\winx64\Release\python\python.exe"`.
- En PowerShell, un script largo con `//` puede ser rechazado: escríbelo en un archivo `.ps1` y ejecútalo.

## Epsilon 0

El juego original (Allegro 5, `MENUMANAGER.cpp` y compañía) está en la carpeta superior, fuera de este repositorio. Su historial y sus versiones `.rar` están en el mismo repositorio de GitHub. Sirve de referencia de estilo: el menú de Epsilon-Z sigue su estructura.
