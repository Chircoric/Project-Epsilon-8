# Nave Epsilon (nave inicial)

- `nave_epsilon.blend`: el archivo fuente. El texto interno `nave_lib` guarda las funciones de geometría.
- `nave_epsilon_v1.blend`: la primera versión, con un anillo fino. Usa texturas antiguas de `texturas/` que la nave actual ya no usa.
- La versión que carga el juego está en [game/assets/modelos](../../../game/assets/modelos): `nave.gltf`, `nave.bin` y `texturas/`, con solo las texturas que usa. Al cambiar el modelo, se exporta de nuevo ahí con **glTF Separate (.gltf + .bin + texturas)**, la carpeta de texturas `texturas` y "Incluir: selección" con la colección `Nave_Epsilon`.
- Escala real, en metros: 1591 m de proa a popa (contando el mástil del deflector) y anillos de 690 m de diámetro. El origen queda a media eslora, sobre el eje. La proa mira a +Y en Blender, que es -Z en glTF y +Z en el motor.

## Zonas del casco

El juego reparte las piezas en sus 15 zonas por el principio del nombre ([game/README.md](../../../game/README.md#tu-nave-en-gltf)):

| Zona | Piezas |
|---|---|
| Proa | `proa_escudo` (losetas ablativas del borde de ataque), `proa_emisor`, `proa_emisor_bulbo` |
| Puente | `puente_platillo`, `puente_cupula`, `puente_ventanas`, `puente_ventanas_platillo`, `puente_franja`, `puente_cupula_inferior`, `puente_hangar_*`, `puente_luz_babor`, `puente_luz_estribor`, `puente_montaje_torreta_1..4` |
| Bodegas 1 a 6 | `bodega_N`, `bodega_N_herrajes`, `bodega_N_blindaje`: seis tanques alrededor del núcleo, cada 60° |
| Anillo de curvatura | Todo lo que empieza por `rotor1_anillo_` o `rotor2_anillo_`: banda, carenado, luces, habitáculos, radios, cubo y balizas |
| Góndolas | `gondola_babor_*` y `gondola_estribor_*`: motor, tobera y escape de los dos motores laterales |
| Radiadores | `radiador_babor*` y `radiador_estribor*`: panel, mástil y costillas, a popa |
| Espina | `espina_central`, `espina_cuello*`, `espina_cojinete_*`, `espina_torre_*`, `espina_capsula_*`, `espina_baliza_*`, `espina_ventanales` |
| Núcleo | `nucleo_modulo_popa`, `nucleo_blindaje_popa`, `nucleo_motor_principal`, `nucleo_tobera_principal`, `nucleo_escape_principal`, `nucleo_rcs_*`, `nucleo_antena_*`, `nucleo_montaje_torreta_5..6` |

Cuando una zona periférica llega a 0, sus piezas se sueltan juntas: la proa pierde el escudo y el emisor, una bodega se va con su tanque, y el anillo se lleva los dos rotores.

## Anillos giratorios

- Las piezas `rotor1_…` y `rotor2_…` giran sobre el eje de la nave, en sentidos contrarios para que el casco no reciba par. Van a la velocidad que da 1 g en la cara interior, una vuelta cada ~36 s.
- Lo hace el juego (`GameLayer`) con el prefijo del nombre. El glTF lleva además las animaciones `giro_anillo_1` y `giro_anillo_2` para otros visores, pero el juego no las usa.
- Los cojinetes (`espina_cojinete_*`) son fijos.
- Cada anillo lleva 6 pares de radios en V, de celosía de sección triangular, y un anillo de tensión hexagonal.

## Torretas

Las bases vacías son `puente_montaje_torreta_1..4` y `nucleo_montaje_torreta_5..6`. Encima de cada una hay un nodo vacío `hp_torreta_N`, con su +Y local en glTF hacia fuera del casco y los extras `tipo = "torreta"` y `ranura = N`. El juego los usa como **ranuras exteriores de módulos** (torretas, radiadores, sensores, blindaje): cualquier nodo vacío cuyo nombre empiece por `hp_` es una ranura, y el módulo se monta en la pieza que tiene debajo ([game/README.md](../../../game/README.md#módulos)).

| Ranura | Sitio | Hacia |
|---|---|---|
| 1, 2 | Platillo, a babor y estribor | arriba |
| 3, 4 | Platillo, a babor y estribor | abajo |
| 5, 6 | Módulo de popa | arriba, abajo |

## Texturas

Son PBR: `*_color` (sRGB), `*_orm` (R oclusión, G rugosidad, B metal) y `*_normal` (tipo OpenGL), y se repiten, así que necesitan el sampler en repetición.

- Las piezas que brillan (luces, ventanas, escapes, emisor, hangar) llevan la emisión en el material. El juego la lee como brillo propio y la apaga con el daño de su zona.
- El juego de momento solo usa `*_color`. Los mapas de relieve y rugosidad están listos para cuando el shader de mallas los use.
