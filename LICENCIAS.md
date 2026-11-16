# Licencias

Copyright (C) 2026 Chircoric.

Epsilon-Z (el motor y el juego Nebulosa) usa dos licencias, según lo que sea cada cosa.

| Qué | Licencia | Texto |
|---|---|---|
| **Código**: `engine/`, `game/src`, `game/include`, `tests/`, `demo/`, `tools/`, shaders `.hlsl`, `CMakeLists.txt`, `cmake/` y los scripts `construir_*.py` | **GNU AGPL v3** (`AGPL-3.0-only`) | [LICENSE](LICENSE) |
| **Recursos propios**: modelos (`.gltf`, `.bin`, `.blend`), texturas, sprites, sonidos y capturas, más la documentación de `docs/` | **CC BY-SA 4.0** (`CC-BY-SA-4.0`) | [LICENSES/CC-BY-SA-4.0.txt](LICENSES/CC-BY-SA-4.0.txt) |
| **Fuentes tipográficas** de `game/assets/fonts/` (Audiowide, Rubik Glitch, Hangul) | **SIL OFL 1.1**, de sus autores; cada una con su `OFL-*.txt` | `game/assets/fonts/` |

Cada archivo de código lleva su cabecera `SPDX-License-Identifier`.

## Recursos propios (CC BY-SA 4.0)

Todos los modelos, texturas y sonidos son obra original, hecha para el juego:

- **Modelos**: hechos en Blender y exportados a glTF. Los fuentes `.blend` y los scripts que los construyen están en `assets/modelos/`.
- **Texturas PBR** (`color`, `normal` y `orm`): generadas por código con numpy desde el texto `nave_texturas` del `.blend` de la nave, y las de `franja_aviso`, `compuerta_carga` y `montaje_torreta` con scripts propios. No hay texturas de bancos externos (ambientCG, Poly Haven y similares).
- **Sonidos**: sintetizados con `tools/sfx_ui.py`, sin muestras de terceros. Ver `game/assets/audio/ui/LICENCIA-audio.txt`.
- **Sprites de planetas** de `assets/modelos/*.png`: generados con `tools/generar_modelos.py`.

Para reutilizarlos hay que dar crédito, indicar los cambios y compartir lo derivado con la misma licencia. Crédito sugerido:

> Nebulosa (Epsilon-Z), de Chircoric, bajo CC BY-SA 4.0: <https://github.com/Chircoric/Project-Epsilon-8>

**Antes de añadir un recurso nuevo**: solo si es propio. No valen texturas, modelos ni sonidos de terceros aunque sean CC0 o de uso libre, salvo que su licencia sea compatible con CC BY-SA 4.0 y se anote aquí con su autor.

## Librerías de terceros

Se instalan con vcpkg y no se incluyen en el repositorio. Todas permiten usarlas en un programa AGPL.

| Librería | Licencia |
|---|---|
| SDL3 | zlib |
| Dear ImGui | MIT |
| cgltf | MIT |
| ENet | MIT |

## Si lo modificas y lo pones en línea

La AGPL (sección 13) obliga a que, si ejecutas una versión modificada como servicio de red (por ejemplo, el servidor de partidas de `game/src/server_main.cpp`), ofrezcas el código fuente de tu versión a quienes se conecten. Si solo lo usas o lo distribuyes sin modificar, basta con conservar los avisos.

## Sin licencia

`Evidencia de originalidad de documentos.pdf` es un documento personal, no un recurso del juego, y no está cubierto por ninguna de estas licencias.

## Historial

Hasta el 16 de noviembre de 2026 el repositorio no tenía licencia. Ese día se purgaron del historial los `.rar` de Epsilon 0, `musica_fondo.OGG` y un sonido derivado de Pixabay, y se pasó todo a las licencias de arriba.
