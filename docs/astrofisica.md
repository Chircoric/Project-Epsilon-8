# Datos astrofísicos del simulador

Este documento recoge las cifras que usa [Astro.cpp](../engine/src/Astro.cpp), de dónde salen y cuáles son una decisión de diseño.

Unidades del motor: kilómetros, segundos y radianes. 1 UA = 149 597 870,7 km, 1 año luz = 9,461 × 10¹² km y 1 radio solar = 695 700 km.

## Tamaños de las estrellas

Cada estrella se sortea dentro de su clase. Un mismo valor coloca masa, radio y temperatura en su rango, porque en la secuencia principal las tres crecen juntas. Después el radio varía hasta un ±15 % y la temperatura hasta un ±5 %. La luminosidad no se sortea: sale de la ley de Stefan-Boltzmann.

| Clase | Ejemplo de uso | Masa (M☉) | Radio (R☉) | Radio en km | Temperatura (K) | Frecuencia real |
|---|---|---:|---:|---:|---:|---:|
| O | Azul muy caliente | 16–60 | 6,6–13,4 | 4,6–9,3 millones | 33 000–45 000 | 0,00003 % |
| B | Azul | 2,1–16 | 1,8–6,6 | 1,3–4,6 millones | 10 000–33 000 | 0,12 % |
| A | Blanca | 1,4–2,1 | 1,4–1,8 | 1,0–1,3 millones | 7300–10 000 | 0,61 % |
| F | Blanco amarillenta | 1,04–1,4 | 1,15–1,4 | 800 mil–1,0 millones | 6000–7300 | 3,0 % |
| G | Como el Sol | 0,8–1,04 | 0,96–1,15 | 668–800 mil | 5300–6000 | 7,6 % |
| K | Naranja | 0,45–0,8 | 0,7–0,96 | 487–668 mil | 3900–5300 | 12 % |
| M | Enana roja | 0,08–0,45 | 0,1–0,7 | 70–487 mil | 2300–3900 | 76 % |
| Gigante roja | Sol envejecido | 0,8–3 | 10–100 | 7–70 millones | 3500–5000 | Estimación: 0,5 % |
| Supergigante roja | Betelgeuse: unos 764 R☉ | 8–25 | 200–1500 | 139 millones–1040 millones | 3400–4200 | Estimación: muy rara |
| Supergigante azul | | 15–40 | 20–80 | 14–56 millones | 12 000–30 000 | Estimación: muy rara |
| Enana blanca | Tamaño cercano a la Tierra | 0,5–1,1 | 0,015–0,0075 | 10 400–5200 | 6000–40 000 | Estimación: 6 % |
| Estrella de neutrones | | 1,2–2,0 | 1,6–2,0 × 10⁻⁵ | 11–14 | 100 000–1 000 000 | Estimación: 0,5 % |

Las clases O a M vienen de la clasificación espectral de Harvard, y sus radios se contrastaron con la tabla de Pecaut y Mamajek. Las filas evolucionadas proceden de fuentes de menor calidad o de varias medidas distintas: tómalas como órdenes de magnitud. Los rangos de masa y temperatura de esas filas, los periodos de giro y sus frecuencias son estimaciones mías, adecuadas para un juego. No son valores medidos.

La opción `--real-frequencies` usa la columna de frecuencia. Por defecto se usa una mezcla más variada, porque en 60 sistemas reales casi todo serían enanas rojas y no aparecería ninguna supergigante.

## Órbitas

La trayectoria sale de la ecuación de Kepler. Es una función del tiempo, no una integración paso a paso.

```text
n = sqrt(mu / a³)
M(t) = M0 + n * t
M = E - e * sin(E)
r(t) = a * (cos(E) - e) * P + b * sin(E) * Q
b = a * sqrt(1 - e²)
giro(t) = fase + velocidad * t
```

`P` y `Q` son vectores unitarios del plano de la órbita, calculados a partir de la inclinación, el nodo ascendente y el argumento del periastro.

| Parámetro | Cómo se elige | Referencia o motivo |
|---|---|---|
| Excentricidad de planetas | Beta(0,867; 3,03) llevada a la mediana e(N) = 0,584 N^−1,20, con tope de 0,25; gigantes × 0,6 | Kipping (2013) para la forma; Limbach y Turner (2015) para N planetas |
| Inclinación de planetas | Rayleigh con σ = 8°/N, entre 1° y 3° | Decisión de diseño: más planetas, más planos |
| Separación entre órbitas | Paso = (espacio disponible)^(1/(N − 1)), entre 1,3 y 3,2, con ±15 %, y al menos 10 radios de Hill mutuos | Decisión de diseño sobre un criterio habitual de estabilidad |
| Órbitas que no se cruzan | Apoastro interior menor que periastro exterior, con un 2 % de margen | Se comprueba en `CheckStability` |
| Borde interior | El mayor de 3 radios estelares, 0,05 √L UA y 0,005 UA | Decisión de diseño: evaporación del polvo |
| Lunas | Fuera de 2 límites de Roche y dentro de 0,4 radios de Hill | Decisión de diseño conservadora |
| Cometas | e = 0,6–0,97, con planos más inclinados | Son los únicos que pueden cruzar órbitas |

## Estrellas múltiples

Una pareja recorre dos elipses alrededor de su centro de masas, en lados opuestos y con el mismo periodo:

```text
n² = G (m1 + m2) / a³
a1 = a * m2 / (m1 + m2)     a2 = a * m1 / (m1 + m2)
mu1 = n² a1³                mu2 = n² a2³
```

Ese `mu` no es el de ninguna masa real. Es el valor que hace que `KeplerOrbit` dé el periodo correcto a cada elipse, y así una estrella se calcula igual que un planeta.

| Dato | Valor | Referencia o motivo |
|---|---|---|
| Fracción con compañera | O 90 %, B 80 %, A 70 %, F 50 %, G 45 %, K 40 %, M 27 %, resto 30 % | Órdenes de magnitud conocidos, escritos de memoria y no cotejados |
| Reparto entre múltiples | Binaria 75 %, triple 18 %, cuádruple 7 % | Decisión de diseño, con más múltiples de las reales |
| Masa de la compañera | Uniforme entre 0,1 y 1 veces la principal, mínimo 0,08 masas solares | Razón de masas plana |
| Separación | Cerrada 0,04–1,2 UA; abierta 25–250 UA | Decisión de diseño: el rango real llega a miles de UA, pero no cabría en pantalla |
| Excentricidad | Cerrada Rayleigh σ = 0,06, tope 0,2; abierta 0–0,55 | Las mareas circularizan las parejas cerradas |
| Sin contacto | Periastro de al menos 3,5 veces la suma de los tamaños | Decisión de diseño; `CheckStability` exige 2 |
| Pareja dentro de una pareja | Semieje exterior > 2,8/(1 − e) [(1 + q)(1 + e)/√(1 − e)]^0,4 × semieje interior, sin el término de inclinación | Mardling y Aarseth (2001), con un 10 % de margen |
| Planetas | Pareja de menos de 3 UA: alrededor de las dos. Más separada: alrededor de cada una | Decisión de diseño |

Zona estable para planetas, con μ = m2/(m1 + m2) y e la excentricidad de la pareja (Holman y Wiegert, 1999):

```text
Alrededor de una estrella, por dentro de:
a/ab = 0,464 − 0,380 μ − 0,631 e + 0,586 μe + 0,150 e² − 0,198 μe²

Alrededor de la pareja, por fuera de:
a/ab = 1,60 + 5,10 e − 2,22 e² + 4,12 μ − 4,27 eμ − 5,09 μ² + 4,61 e²μ²
```

Estos coeficientes se escribieron de memoria y no se pudieron cotejar con el artículo. Las pruebas comprueban que dan 2,39 y 0,274 para una pareja igual y circular, y unos 2,9 para Kepler-16. El ajuste vale para 0,1 ≤ μ ≤ 0,9 y e ≤ 0,7, así que `μ` y `e` se limitan a ese rango. Se le añade un 5 % de margen alrededor de la pareja.

Lo que orbita una pareja la ve como una sola fuente con la masa y la luminosidad sumadas. De ahí salen su zona habitable y su línea de hielo. Es una aproximación: la órbita real tiene pequeñas oscilaciones que aquí no existen, y la temperatura de un planeta no varía al moverse las estrellas.

## Planetas

| Dato | Fórmula |
|---|---|
| Temperatura | T = 278,5 K × (L × (1 − albedo))^¼ × √(1 UA / a); la Tierra sale 255 K |
| Zona habitable | 0,99–1,70 √L UA, de Kopparapu y colaboradores |
| Línea de hielo | 2,7 √L UA; aproximación habitual, no confirmada en la búsqueda |
| Radio rocoso | R = M^0,279 hasta 2,04 masas terrestres, de Chen y Kipping |
| Radio neptuniano | R ∝ M^0,589 a partir de esa masa, de Chen y Kipping |
| Radio de Hill | r = a × (1 − e) × ∛(m / 3M) |
| Límite de Roche rígido | d = R × ∛(2 ρ / ρ₂) |

Acoplamiento por marea: un cuerpo cercano gira a la misma velocidad con la que orbita, así que muestra siempre la misma cara a su centro. El simulador acopla todas las lunas, los Júpiter calientes y los planetas rocosos a menos de 0,4 ∛M UA. Con excentricidad mayor de 0,1 usa la resonancia 3:2, como Mercurio. El límite de 0,4 es una aproximación mía.

El tipo de planeta depende de su temperatura y de si está dentro de la línea de hielo. Los planetas gigantes solo se forman fuera de ella, salvo los Júpiter calientes, que llegan ahí por migración.

## Materiales

`x` es la distancia a la estrella dividida por la línea de hielo. La composición depende de ella, así que la lectura de los materiales indica la zona donde se formó un planeta.

| Dato | Regla | Origen |
|---|---|---|
| Hierro en planetas rocosos | 0,75 − 1,15 x, entre 0,15 y 0,60, ± 0,08 | Decisión de diseño; imita a Mercurio y Marte |
| Agua en planetas rocosos | Crece desde x = 0,3 | Decisión de diseño |
| Densidad | Mezcla por volumen: hierro 7,87; silicatos 3,3; agua 1,0; hielos 0,9 g/cm³ | Densidades sin comprimir |
| Radio rocoso | Chen y Kipping × ∛(4,05 g/cm³ / densidad) | Ajuste propio: la masa manda y la composición corrige |
| Temperatura con atmósfera | T × (1 + 0,63 p)^¼, con p en bares | Modelo gris ajustado para dar 288 K en la Tierra |
| Achatamiento | 0,97 q en sólidos y 0,73 q en gigantes; q = ω² R³ / GM | Ajuste propio a Tierra, Júpiter y Saturno |
| Océano | 10–90 %, según el agua total | Decisión de diseño |
| Vida | Probabilidad de 0,6 en planetas terrestres | Decisión de diseño, solo visual |

Valores reales de achatamiento: Tierra 0,00335, Júpiter 0,06487 y Saturno cerca de 0,098.

Color de los gigantes según las clases de Sudarsky:

| Temperatura | Nubes | Color |
|---|---|---|
| Menos de 150 K | Amoníaco | Crema y marrón |
| 150–350 K | Agua | Blanco |
| 350–900 K | Pocas nubes | Azul |
| 900–1400 K | Metales alcalinos | Muy oscuro |
| Más de 1400 K | Silicatos | Rojo anaranjado |

Los límites son aproximados y proceden de fuentes secundarias. No se consultaron los artículos originales de Sudarsky.

## Agujeros negros

Las longitudes se miden en radios de Schwarzschild, rs = 2GM/c². Son 2,95 km por masa solar. El giro es a/M, de 0 a menos de 1.

| Magnitud | Schwarzschild | Kerr |
|---|---|---|
| Horizonte | 1 rs | ½ (1 + √(1 − a²)) rs |
| Órbita de fotones | 1,5 rs | Depende del sentido: 2M [1 + cos(⅔ arccos(∓a))] |
| Última órbita estable | 3 rs | Fórmula de Bardeen, Press y Teukolsky (1972); 1,16 rs con a/M = 0,9 |
| Sombra, radio | √27/2 rs = 2,598 rs | Se calcula con las órbitas de fotones ecuatoriales (Bardeen, 1973) |

La clase estelar va de 5 a 40 masas solares. Una de cada dos no rota, y el resto gira con a/M entre 0,3 y 0,98. La galaxia añade uno supermasivo de 4,3 millones de soles, como Sagitario A*, cuyo rs mide unos 12,7 millones de km.

### Rayos de luz

El shader integra por píxel, en radios de Schwarzschild:

```text
a = -1.5 h² x / r⁵            (h = |x × v| se conserva)
```

Esta aceleración da exactamente la forma de una órbita de fotones alrededor de Schwarzschild. La integración es numérica, con paso proporcional a r: la sombra sale un 1,3 % menor que la exacta. La desviación a 30 rs coincide con los dos primeros términos de la serie, 2 rs/b + (15π/16)(rs/b)², con un 0,3 % de error.

Para Kerr se añade un término del tipo `v × B`, con un campo de dipolo proporcional al giro. **No es una geodésica de Kerr:** una exacta por píxel exigiría integrar un sistema mucho más caro. La constante se calibró para que la anchura de la sombra coincida con la exacta. Con a/M = 0,9, la anchura tiene un error del 1,4 %, pero el desplazamiento se queda en 0,60 rs frente a 1,00 rs. Por encima de 0,9 el método se vuelve inestable y su efecto se limita.

### Disco de acreción

- **Temperatura:** T⁴ ∝ (1 − √(rin/r)) / r³, la forma de un disco delgado de Shakura y Sunyaev (1973). Empieza en la última órbita estable y termina a 14 rs.
- **Giro:** cada anillo gira a la velocidad de Kepler. Los grumos se estiran en espiral.
- **Corrimiento:** combina el Doppler relativista del gas con el corrimiento gravitacional. El brillo sigue el cubo de ese factor.
- **Modelo visual:** el color es una rampa que imita un cuerpo negro. No hay transferencia radiativa, ni opacidad física, ni chorros.

El disco no tiene escala real: un disco real de un agujero estelar emitiría casi todo en rayos X. El brillo y el color están elegidos para verse.

## Asteroides

| Tipo | Composición | Dónde abunda |
|---|---|---|
| C | Carbono; oscuro | Más allá de la línea de hielo |
| S | Silicatos; claro | Parte interior |
| M | Hierro y níquel | Un 8 % en todas partes |

- **Órbita:** la excentricidad sigue una Rayleigh de σ = 0,09 hasta 0,3, y la inclinación una Rayleigh de σ = 6° hasta 25°.
- **Lugar:** el hueco entre planetas más grande, si uno es al menos 1,9 veces más lejano que el otro. Otro cinturón va de 1,4 a 2,0 veces la órbita del último planeta.
- **Tamaños:** dependen del perfil, con N(>r) ∝ r^−k y k entre 1,3 y 2,6. Son leyes de potencias elegidas para el aspecto, no ajustes al cinturón real.

| Perfil | Familias | Rocas por familia | Radio de familia | Tamaño | k |
|---|---:|---:|---:|---|---:|
| Disperso | 480 | 280 | 260 km | 0,2–8 km | 2,1 |
| Regular | 1200 | 800 | 220 km | 0,12–14 km | 1,9 |
| Denso | 2400 | 1760 | 160 km | 0,1–10 km | 2,0 |
| Escombros | 1680 | 2400 | 140 km | 0,03–2,5 km | 2,6 |
| Gigantes | 840 | 200 | 340 km | 2–90 km | 1,3 |

La densidad **no es real**. En el cinturón real, las rocas grandes están separadas del orden de un millón de kilómetros. Aquí, una familia regular reúne 800 rocas en un radio de unos 220 km. Tampoco se calculan las perturbaciones.

## Choques

La energía específica compara lo que aporta el choque con el total de masa:

```text
Q = ½ μ v² / (m1 + m2)        μ = m1 m2 / (m1 + m2)
```

El umbral es la energía que dispersa la mitad de la masa, con el ajuste de Benz y Asphaug (1999) para basalto a 3 km/s:

```text
Q* = 3,5e7 R^−0,38 + 0,3 ρ R^1,36     [erg/g, R en cm, ρ en g/cm³]
```

Su término de resistencia domina en lo pequeño y el de gravedad en lo grande, con el mínimo en cuerpos de cientos de metros. Los coeficientes se escribieron de memoria, y las pruebas solo comprueban su forma. Se usa `R` del cuerpo que resultaría de juntar los dos.

El mayor resto sigue la ley universal de Leinhardt y Stewart (2012), también escrita de memoria:

```text
m_resto / m_total = 1 − 0,5 Q/Q*                si Q/Q* < 1,8
m_resto / m_total = 0,1 (Q / (1,8 Q*))^−1,5     si no
```

| Resto | Resultado |
|---|---|
| ≥ 90 %, proyectil menor del 2 % del objetivo | Cráter |
| ≥ 90 % | Fusión |
| 10–90 % | Rotura parcial |
| < 10 % | Destrucción total |

La trayectoria de `Schedule` resuelve el problema de Lambert con variables universales (Curtis), por bisección y sin vueltas completas. Parte de la posición real del atacante y llega a la que tendrá el objetivo. Busca un tiempo de vuelo cerca de una transferencia de Hohmann y descarta órbitas hiperbólicas, con e > 0,97 o que pasen demasiado cerca de la estrella.

Simplificaciones:

- Basalto para todo: no hay ajuste propio para hielo ni para gas, así que un gigante gaseoso se trata como una roca poco densa.
- El ángulo de impacto no cuenta.
- Las explosiones son una representación: duran segundos reales y su tamaño depende del radio, no de la energía.
- Los fragmentos no orbitan. El campo de escombros aparece a mitad de la explosión, ya repartido a ±6 % de la órbita, cuando en realidad tardaría muchas vueltas en formarse.
- La detección compara como mucho 8 puntos por paso. Con el tiempo muy acelerado puede saltarse un roce. Los choques programados y generados no se pierden, porque se comprueban por su hora.
- Solo se detectan choques de cometas y asteroides contra planetas. Asteroide contra asteroide y luna contra luna se pueden programar, pero no ocurren solos.
- Las órbitas de los planetas no cambian tras un choque, y la masa nueva no altera las órbitas vecinas.

## Nebulosas

Los colores imitan las líneas de emisión más conocidas: el rojo del hidrógeno y el verde azulado del oxígeno. Densidad, forma y brillo son artísticos, y la nube no se ilumina con las estrellas que contiene. La central mide un 30 % del radio de la galaxia, mucho más que una nebulosa real entre estrellas tan próximas.

## Referencia: Sistema Solar

Datos de la NASA, que las pruebas usan para validar la tercera ley de Kepler.

| Planeta | Distancia (millones de km) | Periodo (días) | Excentricidad | Inclinación | Oblicuidad | Giro (horas) |
|---|---:|---:|---:|---:|---:|---:|
| Mercurio | 57,9 | 88,0 | 0,206 | 7,0° | 0,034° | 1407,6 |
| Venus | 108,2 | 224,7 | 0,007 | 3,4° | 177,4° | −5832,5 |
| Tierra | 149,6 | 365,2 | 0,017 | 0,0° | 23,4° | 23,9 |
| Marte | 228,0 | 687,0 | 0,094 | 1,8° | 25,2° | 24,6 |
| Júpiter | 778,5 | 4331 | 0,049 | 1,3° | 3,1° | 9,9 |
| Saturno | 1432,0 | 10 747 | 0,052 | 2,5° | 26,7° | 10,7 |
| Urano | 2867,0 | 30 589 | 0,047 | 0,8° | 97,8° | −17,2 |
| Neptuno | 4515,0 | 59 800 | 0,010 | 1,8° | 28,3° | 16,1 |

La Luna está a 384 400 km de la Tierra, con e = 0,055 y un periodo de 27,3 días.

## Qué simplifica el modelo

- Cada cuerpo sigue una elipse fija alrededor de su centro: los planetas no se perturban entre sí y las órbitas no precesan.
- Una estrella sola no se mueve alrededor del centro de masas que forma con sus planetas. Las estrellas múltiples sí se mueven, pero los planetas no las perturban.
- Los cometas no tienen cola.
- No hay eclipses: una estrella o un planeta no proyecta sombra sobre otro.
- La estabilidad se garantiza con criterios simples. No se comprobó con una integración de millones de años.
- Los materiales son un modelo visual. No hay química ni estructura interna, y las densidades no incluyen la compresión.
- El brillo de un planeta no depende de su distancia a la estrella: el shader usa la dirección de la luz, no su intensidad.
- El origen flotante mantiene la cámara cerca de cero, así que la distancia a la que esté una estrella ya no causa temblor. A 30 UA de su estrella, una posición en km tiene una precisión de 1 mm. Por eso la escala humana usa burbujas en metros.

## Fuentes

- [Clasificación estelar (tabla de Harvard)](https://en.wikipedia.org/wiki/Stellar_classification)
- [Pecaut y Mamajek: secuencia de enanas](https://www.pas.rochester.edu/~emamajek/EEM_dwarf_UBVIJHK_colors_Teff.txt)
- [NASA: hoja de datos planetarios](https://nssdc.gsfc.nasa.gov/planetary/factsheet/index.html)
- [Chen y Kipping (2017): relación masa-radio](https://ar5iv.labs.arxiv.org/html/1603.08614)
- [Kipping (2013): distribución de excentricidades](https://arxiv.org/pdf/1306.4982.pdf)
- [He y colaboradores (2020): separación en radios de Hill](https://arxiv.org/pdf/2007.14473)
- [Kopparapu y colaboradores (2013): zona habitable](https://alphaxiv.org/abs/1301.6674)
- [Miller y colaboradores (2021): radio de PSR J0740+6620](https://arxiv.org/pdf/2105.06979)
- [Teoría y datos sobre estrellas de neutrones](https://arxiv.org/pdf/2303.17021)
- [Lista de estrellas más grandes](https://ipfs.ssi.eecc.de/ipns/en.wikipedia-on-ipfs.org/wiki/List_of_largest_stars)
- [Radio de Betelgeuse](https://www.sci.news/astronomy/betelgeuse-size-distance-08957.html)
- [Tamaños de estrellas](https://www.daviddarling.info/encyclopedia/S/starssizes.html)
- [Clasificación de Sudarsky](https://EN.wikipedia.com/wiki/Sudarsky%27s_gas_giant_classification)
- [NASA: datos de Júpiter](https://nssdc.gsfc.nasa.gov/planetary/factsheet/jupiterfact.html)
- [Tanner Helland: de temperatura a color RGB](https://tannerhelland.com/2012/09/18/convert-temperature-rgb-algorithm-code.html)
- [Limbach y Turner (2015): excentricidad y número de planetas](https://pmc.ncbi.nlm.nih.gov/articles/PMC4291657)
- [Zinzi y Turrini (2017): revisión de esa relación](https://ar5iv.labs.arxiv.org/html/1709.00003)
- [Mardling y Aarseth, resumido en Toonen y colaboradores (2020)](https://arxiv.org/pdf/2004.07848)
- [Criterios de estabilidad en sistemas triples](https://arxiv.org/pdf/1408.5431)
- [Quarles y colaboradores: alcance de Holman y Wiegert](https://arxiv.org/pdf/2010.05585)
- [Jutzi (2015): modelos de choques de asteroides](https://arxiv.org/pdf/1502.01844)

Referencias clásicas de las fórmulas de agujeros negros, citadas de memoria y no releídas para este proyecto. Sus valores se comprobaron con las pruebas del motor, contra los resultados conocidos:

- Bardeen, Press y Teukolsky (1972), *Rotating Black Holes*: última órbita estable.
- Bardeen (1973), *Timelike and Null Geodesics in the Kerr Metric*: órbitas de fotones y sombra.
- Shakura y Sunyaev (1973), *Black Holes in Binary Systems*: disco delgado.
- Epstein y Shapiro (1980): término de segundo orden de la desviación de la luz.
- Holman y Wiegert (1999), *Long-Term Stability of Planets in Binary Systems*: zonas estables.
- Benz y Asphaug (1999), *Catastrophic Disruptions Revisited*: umbral de ruptura.
- Leinhardt y Stewart (2012), *Collisions between Gravity-dominated Bodies*: mayor resto.
- Curtis, *Orbital Mechanics for Engineering Students*: problema de Lambert.
- Möller y Trumbore (1997): intersección de rayo y triángulo.
- Jimenez (2014), *Next Generation Post Processing in Call of Duty: Advanced Warfare*: filtro del bloom.
