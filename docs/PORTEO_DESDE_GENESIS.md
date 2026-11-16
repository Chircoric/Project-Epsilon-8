# Porteo desde Engineer Genesis a Epsilon-Z

Archivo de traspaso. Está escrito para que una conversación abierta **dentro de `EpsilonEngine/`** lo lea y sepa qué traer de Engineer Genesis, de dónde y en qué orden, sin que el usuario tenga que explicarlo otra vez. Redactado el 15-11-2026 tras revisar los dos proyectos (no se modificó ningún archivo en esa revisión).

**Léelo entero antes de tocar nada. Si algo de aquí contradice el código, manda el código: dilo y corrige este archivo.**

## 1. Contexto

- **Epsilon-Z** (esta carpeta, `main`, commit `babcdfe`): el motor y el juego *Nebulosa*. Combate, galaxia procedural, nebulosas, naves. **Epsilon-Z conserva todo eso.** No lo quites.
- **Engineer Genesis**: simulador de ingenieros de estaciones espaciales en el Sistema Solar real. Se importó **una sola vez** de Epsilon en ese mismo commit `babcdfe` (`docs/ORIGEN_EPSILON.md` de Genesis). Por eso toda diferencia entre los dos códigos es trabajo hecho en Genesis desde entonces, y se puede ver con `diff`.
- Genesis ganó una interfaz que al usuario le gustó (se perdió el concurso, pero la UI gustó). **Quiere esa interfaz en Epsilon-Z con otro color, menos chillón**, y traer otras cosas de Genesis donde sean compatibles.
- Las reglas de ambientación de Genesis (sin nebulosas, sin combate, Sistema Solar real) **no se aplican aquí**. Al revés: no hay que contagiar a Epsilon-Z con lo de Genesis que sea específico de estaciones y EVA salvo que el usuario lo pida.

## 2. Dónde está Genesis

| Qué | Ruta |
|---|---|
| Clon con la guía de modelos y los modelos exportados | `C:\Users\Admin\Desktop\Paralelo NASA SPACE APPS\engineer-genesis-22` |
| Archivos `.blend` de los tripulantes y sus scripts | `C:\Users\Admin\Desktop\Paralelo NASA SPACE APPS\engineer_genesis\personajes` (scripts en `scripts\`: `eg_render.py`, `eg_personas.py`, `eg_export.py`) |
| Capturas de referencia de la interfaz | `engineer-genesis-22\docs\capturas\idioma_en_hud_a_pie.png`, `idioma_es_ajustes.png`, `idioma_fr_tras_clic.png` |

**Cuidado con la rama.** `engineer-genesis-22` está en `tarea/cohete-real`, cuyo último commit (`e49da4d`) es "EN CURSO… no compila aún". Los archivos de interfaz son idénticos a los del commit `f44f3e3` (merge en `tarea/graficos`, sí compila), así que **se pueden leer de ahí sin riesgo**, pero no copies a ciegas nada más de ese árbol. Para consultar un archivo en un commit concreto:

```
git -C "C:\Users\Admin\Desktop\Paralelo NASA SPACE APPS\engineer-genesis-22" show f44f3e3:game/src/GameHud.cpp
```

(El `git.exe` no está en el PATH: ver `CLAUDE.md` de esta carpeta.)

## 3. Reglas que siguen valiendo aquí

Las de `CLAUDE.md` de esta carpeta: español, unidades reales, solo recursos propios bajo CC BY-SA 4.0 y código AGPL-3.0-only (ver `LICENCIAS.md`), no subir `out/`, `.vs/`, `*.sav`, `*.blend1`. Compilar con `vcvars64.bat`, `--clean-first` tras tocar un header, renombrar el `.exe` si está abierto. **Un cambio está terminado cuando compila, pasa `epsilon_game_tests` y `epsilon_tests` con `0 fallos` y, si se ve, se sacó una captura y se miró.**

Trabaja en una **rama nueva**, por ejemplo `tarea/interfaz-genesis`. `main` estaba limpio al escribir esto.

Dependencias: las dos vcpkg.json son idénticas (misma línea base `fe856ed9…`; SDL3, cgltf, enet, imgui con sdlgpu3). **No hace falta cambiar la línea base ni recompilar vcpkg.**

## 4. Qué se trae, por fases (cada una compila y se prueba sola)

### Fase 0 — Línea base
1. Rama nueva, compilar `x64-sdl-release`, pasar los dos ejecutables de pruebas.
2. Capturas **antes** con la receta de `CLAUDE.md` (`--frames … --screenshot antes_menu.png`, `antes_juego.png`) para comparar al final.

### Fase 1 — Color de la interfaz (la más pequeña y la que más se nota)
Es solo el principio de `game/src/GameHud.cpp`. En Epsilon-Z:

```cpp
ImU32 Rgba(int r, int g, int b, float a = 1.0f) { return IM_COL32(r, g, b, static_cast<int>(a * 255.0f)); }
```

En Genesis (`GameHud.cpp`, líneas ~30–99) esa línea pasó a ser un **filtro de tono**: `Themed(r, g, b)` convierte cada color por su matiz (ámbar → blanco tenue, amarillo → blanco, fondos marrones → velo gris translúcido, cian → blanco frío) y devuelve cuánto multiplicar la opacidad; `Rgba` lo llama. **El resto del HUD no se toca**: todas las constantes (`kAmber`, `kYellow`, `kPanel`…) pasan por `Rgba`, así que se recoloca toda la interfaz cambiando una función. Rojo (peligro), naranja vivo (lo pulsado), verde y violeta se dejan como estaban.

Por qué no basta copiarlo tal cual: Genesis lo hizo blanco, y sus propios comentarios avisan de que **sobre la nebulosa el blanco se pierde** (roja, amarilla y gris), que es justo el escenario de Epsilon-Z. Por eso `kSignal` es cian (`70,232,255`) con trazo de tinta debajo. El "menos chillón" de aquí debe respetar eso.

**Hacer:**
1. Copiar `Themed()` y el `Rgba` nuevo, pero **sacar los números a una sola estructura** (`struct HudTheme { float hue, sat, valueScale, fade… }`) para poder cambiar el color en un solo sitio.
2. Paleta propuesta (el usuario aún **no la ha confirmado**; pregúntale si no te lo han dicho ya):

| Rol (en `GameHud.cpp`) | Hoy en Epsilon-Z | Propuesta |
|---|---|---|
| Líneas y marcos (`kAmber*`) | ámbar `255,170,40` | gris azulado frío, con poca opacidad |
| Lo activo (`kYellow`, `kOwn`) | amarillo `255,228,96` | ámbar apagado (≈ `214,170,92`) |
| Fondos (`kPanel*`) | marrón tostado | velo pizarra oscuro, más transparente |
| Señales sobre el espacio (`kSignal*`) | cian vivo | cian suavizado, conservando el trazo `kInk` |
| Peligro, verde, violeta | sin cambios | sin cambios |

3. Genesis también añadió `kQuietText/Muted/Line/Faint` (el menú, "visto a través del cristal") y `kHalo` (contorno del texto). Traer `kHalo`; los `kQuiet*` solo si se restyla el menú (Fase 4).
4. Captura **después** y compararla con la de Fase 0 y con las de Genesis. Comprobar legibilidad sobre la nebulosa roja, sobre una estrella y sobre una superficie clara.

### Fase 2 — Cristal del casco (opcional según el usuario)
- `game/shaders/visor.hlsl` (110 líneas): borde de cristal que desenfoca, curva, separa colores y empaña hacia las esquinas, con luz roja de alarma. Es un efecto de pantalla completa del mismo contrato que `warp.hlsl` y `reticle.hlsl`, que Epsilon-Z ya tiene.
- Cableado (en Genesis): `CMakeLists.txt` línea ~337 (`epsilon_add_shader(... visor.hlsl ps_6_0 PSEffect gPsVisor ${SHADER_OUT}/ps_visor.h)` y `ps_visor.h` en `add_executable` y en `OBJECT_DEPENDS`) y `main.cpp` ~615 (`AddPostEffect("visor", gPsVisor, …)`) y ~1734 (`SetPostEffectParams` + `SetPostEffect(visorEffect, inSuit, 1.0f)`).
- **Decisión que no es mía:** en Genesis se activa con el traje puesto (`inSuit`). Epsilon-Z no tiene traje EVA. Preguntar si se quiere solo al ir a pie, siempre muy suave, o no traerlo.

### Fase 3 — Antialiasing y gradación (motor)
- `engine/shaders/post.hlsl`: Genesis añadió FXAA (`Antialiased`) y una gradación suave (`Grade`: curva S y algo de saturación). **Es un cambio de motor y se ve en todo el juego**, no solo en la interfaz.
- Aplicar el diff de `post.hlsl` (ver comando en §7). Comprobar FPS con `--no-vsync` antes y después: aquí el límite es el procesador, pero el FXAA añade muestras por píxel.
- `engine/src/Features.cpp` y `Features.h` también difieren, pero **no los copies**: Genesis quitó `Combat` y `ProceduralGalaxy`, y Epsilon-Z los necesita.

### Fase 4 — Menú y textos del casco (opcional)
- Estilo `kQuiet*` del menú. Va aparte de la Fase 1 porque cambia el carácter del menú, no solo el color.
- **Textos por clave (ES/EN/FR).** El HUD de Genesis llama a `Tr()`/`Tf()` 125 veces (`genesis/Text.h`, tablas en `data/textos/*.tsv`); el de Epsilon-Z, cero veces. Dos caminos: (a) dejar los textos en español como están, o (b) traer `genesis/Text.h` + `data/textos` y migrar. **No migres a ciegas**: es un cambio grande y el usuario no lo pidió todavía.

### Fase 5 — Modelos de personas
- **Aviso de licencia (16-11-2026):** Epsilon-Z solo admite modelos, texturas y sonidos propios, bajo CC BY-SA 4.0 (ver `LICENCIAS.md`). Si algún modelo de personas de Genesis es de terceros (CC BY o cualquier otra licencia), **no se trae**. Antes de copiar nada, leer `CREDITOS-modelos.txt` de Genesis y traer solo lo que sea obra propia; el resto hay que hacerlo de nuevo.
- `Models.cpp` es **idéntico** en los dos motores, y Epsilon-Z ya tiene `LoadGltfModel` (`GameModels.cpp:370`). Los `.gltf` de tripulantes cargan sin tocar el cargador.
- Traer de `engineer-genesis-22\game\assets\modelos\`: `<id>.gltf/.bin` y sus texturas de `texturas\` (`ingeniera`, `kwame`, `mei`, `erik`, `amara`, `arjun`, `nino`; versiones `_eva` solo si hay traje) y **`CREDITOS-modelos.txt`**. Los modelos CC BY exigen conservar el crédito; copia solo las líneas de lo que traigas.
- Formato exacto que pide el motor: `docs/GUIA_MODELOS_PERSONAS.md` de Genesis (piezas rígidas con nombres fijos, 14 empties de articulación `art_*`, +Y arriba, un material por pieza, texturas `nombre_color/normal/orm.png`). Se puede leer `engine/include/epsilon/Humanoid.h` y `OnFoot.cpp` de Epsilon-Z para ver cómo se pintan hoy las personas.
- **Aún falta código:** `GameModels.cpp` no carga personas ni `GameLayer.cpp` (línea ~1535, lambda `person`) las usa con un modelo de Genesis. Mirar cómo lo hace `GameModels.cpp`/`GameLayer.cpp` de Genesis y adaptarlo.
- **No traer** `iss`, `estacion_central`, `estacion_scifi`, `chatarra` (≈ 233 MB, y son de la ambientación de Genesis). Epsilon ya dejó fuera sus modelos grandes. Si el usuario los quiere, avisar del tamaño primero.
- Nuevos personajes: los `.blend` y scripts de §2. Se pueden seguir exportando a `game/assets/modelos/` de aquí.

### Fase 6 — Red
Estado real, comprobado:

| | Epsilon-Z | Genesis |
|---|---|---|
| Red que usa el juego | ENet, LAN con descubrimiento (UDP 27301), servidor sin ventana, protocolo `kProtocolVersion = 2`, **conectada al juego** | `genesis_net` (biblioteca nueva, ~1 600 líneas) **no está conectada a su juego con ventana**: el juego sigue con el `Net.cpp` heredado de Epsilon (idéntico al de aquí) |
| Extras | no comprueba `Balance` | contraseña, huella de contenido (`data/`), motivos de rechazo por clave, `--internet` (1×, 20 instantáneas/s en vez de 60), diferencias probadas con pérdida y desorden |
| NAT del router | sin resolver | sin resolver |

- **No copiar `genesis_net` entera**: está hecha sobre `Authority`/`Replica` de la sesión de Genesis y obligaría a reescribir `Session`.
- Lo que sí se puede portar, **una cosa por tarea**, al `game/src/Net.cpp`/`NetEnet.cpp` de aquí: (1) contraseña en el saludo, (2) comprobar `Balance` o una huella (hoy el saludo solo mira protocolo, guardado y semilla), (3) ritmo de instantáneas reducido en modo Internet, (4) motivos de rechazo. Si se tocan las estructuras de `Net.h`, **subir `kProtocolVersion`**: el protocolo copia estructuras tal cual, así que servidor y cliente deben ser el mismo ejecutable.
- Para Internet de verdad falta el NAT (reenvío de puertos manual, relevo o perforación). `docs/multijugador.md` §9 ya lo deja pendiente; decidirlo con el usuario antes de prometerlo.
- Probar: `epsilon_game_tests` cubre el protocolo; añadir una prueba por cada rechazo nuevo.

## 5. Lo que NO se debe hacer

- No copiar `GameHud.cpp`, `GameHud.h`, `Session.*`, `OnFoot.*` ni `GameLayer.*` de Genesis enteros: `GameHud.h` incluye `genesis/MissionRunner.h` y la `Session.h` de Genesis, que aquí no existen; son ~1 088 líneas distintas en 125 bloques, casi todo traje, misiones y textos por clave.
- No quitar combate, galaxia, nebulosas ni los modelos `alien_*`, `torretas`, `dron`, `bahia_drones`.
- No cambiar el espacio de nombres `eps`, la línea base de vcpkg ni las fuentes (Audiowide, Rubik Glitch, Hangul son idénticas en los dos).
- No subir al repositorio audios de `ui/` ni modelos pesados sin avisar del tamaño.

## 6. Orden recomendado y qué preguntar al usuario

1. Fase 0 + Fase 1 (una tarde; es lo que el usuario pidió primero).
2. Preguntas que **solo el usuario puede contestar**: la paleta definitiva (§4 Fase 1), si el cristal del casco se quiere y cuándo (Fase 2), si se migra a textos por clave (Fase 4), y qué hacer con el NAT (Fase 6).
3. Fases 3 y 5 después; Fase 6 al final, una mejora por vez.

## 7. Comandos útiles

Ver qué cambió Genesis en un archivo concreto (ignorando saltos de línea):

```
diff --strip-trailing-cr "C:\Users\Admin\Desktop\PROYECTO EPLSILON-0\EpsilonEngine\game\src\GameHud.cpp" "C:\Users\Admin\Desktop\Paralelo NASA SPACE APPS\engineer-genesis-22\game\src\GameHud.cpp"
```

Lo mismo para `engine\shaders\post.hlsl`, `game\shaders\visor.hlsl` (solo existe en Genesis), `game\src\GameModels.cpp`. En PowerShell se puede usar `git diff --no-index` con el `git.exe` de Visual Studio.

Capturas sin manos (de `CLAUDE.md` de esta carpeta, desde `out\build\x64-sdl-release`):

```
epsilon_game.exe --frames 200 --mode 2 --no-tutorial --save prueba.sav --screenshot despues.png --cmd revelar
```

## 8. Estado de este porteo

Nada portado todavía. Al terminar cada fase, apunta aquí la fecha, el commit y lo que se dejó sin hacer.

| Fase | Estado | Commit |
|---|---|---|
| 0 Línea base | pendiente | |
| 1 Color | pendiente | |
| 2 Cristal del casco | pendiente (sin decidir) | |
| 3 FXAA y gradación | pendiente | |
| 4 Menú / textos | pendiente (sin decidir) | |
| 5 Modelos de personas | pendiente | |
| 6 Red | pendiente | |
