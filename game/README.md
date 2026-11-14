# Nebulosa

Una nave enorme atrapada en una nebulosa tan densa que apenas se ve a unos cientos de miles de kilómetros. Hay naves y estaciones alienígenas, hostiles y neutrales, y estructuras abandonadas muy avanzadas repartidas por el mapa.

Las mecánicas funcionan, pero **todas las cifras son provisionales**: las mejoras y las condiciones están por decidir.

![Curvatura](../docs/juego_curvatura.png)

## Compilar y jugar

En Visual Studio, elige la configuración `x64 SDL Release` y el objetivo `epsilon_game.exe`. Si ya está compilado:

```bash
out\build\x64-sdl-release\epsilon_game.exe
```

| Entrada | Acción |
|---|---|
| Ratón, W A S D | Apuntar el rumbo: la nave lo sigue con retraso. W y S cabecean, A y D giñan |
| Q, E | Girar sobre el eje de la nave |
| R, F | Subir o bajar el mando: cuánto empuja el motor. F por debajo de cero da marcha atrás |
| Espacio | Frenar, también en burbuja, y mantener la posición contra la gravedad |
| T o botón central | **Rueda de opciones** |
| M | **Mapa**: sistemas, astros y modelo en 3D |
| G | Disparar al objetivo todo lo que esté listo |
| B | El rumbo sigue la solución de tiro del cañón espinal, hasta volver a pulsar o mover el rumbo |
| K | Cambiar de escudo: arena, plasma, electromagnético y apagado |
| 1, 2 | Impulso, crucero |
| 3, 4 | Híbrida, con el primer emisor de energía negativa; hiper, con el segundo |
| Tab | Siguiente objetivo de los sensores |
| C | **Activar o desactivar el cursor en la interfaz** |
| H | Mostrar u ocultar las teclas |
| Enter | Avanzar el diálogo |
| Botón derecho | Arrastrar la cámara alrededor de la nave, en cualquier sentido |
| Rueda | Acercar o alejar la cámara |
| V | Devolver la cámara detrás de la nave |
| Esc | Pausa |
| F1 | Consola de comandos; escribe `ayuda`. Ctrl+V pega, Ctrl+C copia y Ctrl+X corta |

Las teclas de vuelo también funcionan con el cursor activo; solo el ratón deja de mover el rumbo. Q gira a la izquierda y E a la derecha.

### A pie

| Entrada | Acción |
|---|---|
| W A S D | Andar |
| Ratón | Mirar |
| Mayús | Correr |
| Espacio | Saltar |
| V | Primera o tercera persona |
| E | Volver a la nave, junto a la esclusa |

### Rueda de opciones

T abre la rueda y un clic elige; también se puede mantener el botón central, mover el ratón y soltar. La nave sigue volando, sin mandos, mientras está abierta. Las opciones están siempre en el mismo sitio, y lo que no sirve ahora se ve apagado: Escáner, Ir o Parar, Órbita, Atracar o Soltar, A pie, Usar, **Drones** (o Recoger drones), Central, **Estación** (suelta la origami), Mejorar, Análisis, Mapa, Disparar, **Trayectoria** (o Quitar ruta) y Taller.

**Trayectoria** dibuja en verde lo que va a recorrer la nave en los próximos 30 segundos reales con el mando y el giro que lleva: una copia de la nave vuela sola, girando hacia donde apuntas y con la gravedad de ahí (junto a una estación, en su marco y con sus topes). Lleva una marca cada 5 segundos y «+30 s» al final.

### Atraque

Cada estación tiene una **escotilla** con luz verde: la central, encima del depósito, lejos de los colectores; la origami, en la esclusa del nudo 1, por fuera del anillo. En la origami la nave atraca paralela al eje del anillo. Con una estación propia como objetivo, **Atracar** en la rueda la lleva hasta ella, con la ruta si está lejos. A unos 50 000 km engancha un **haz de atraque** que primero la rodea, sin atravesarla, hasta ponerla delante de la escotilla, y luego la mete en línea recta con la proa por delante: no hay rebotes ni forma de pasarse. Atracada, la nave pone su compuerta frente a la escotilla, con la proa de lado, y un ducto las une. En la maqueta la compuerta está a estribor; en la nave glTF es la bahía de atraque bajo el puente, así que la nave se tumba hasta que su panza mira a la escotilla (el nodo vacío `atraque` del modelo dice dónde está y hacia dónde mira). La nave sigue a la escotilla aunque la estación gire u orbite; se repara el casco y la partida se guarda así. Al dar mando, o con Soltar, sale; recién soltada, esa estación no choca hasta que la nave se aparta.

Sin atracar también se puede arrimar a mano hasta la escotilla. A menos de 3000 km de una estación propia, el tiempo se frena y la velocidad respecto a ella se recorta según la distancia, contada en segundos reales: acercarse va a la distancia entre 12 s y alejarse, entre 6 s. A menos de **25 km** el tiempo corre como el real y mandan los **propulsores finos**: acercarse va a la distancia entre 15 s (20 m/s como poco) y rodearla o alejarse, entre 12 s (120 m/s como poco). La nave cuenta como lo que es, 1,2 km de proa a popa y 400 m de ancho, así que puede pasar junto al casco; rozarlo a esas velocidades apenas abolla. El centro de la pantalla dice en qué zona estás y la velocidad máxima. Sin mando, unos estabilizadores frenan la deriva y la nave se queda al lado.

En esos 3000 km la nave vuela **en el marco de la estación**: la estación la lleva consigo mientras orbita, su planeta o su estrella no tiran de ella y la velocidad que se ve es respecto a la estación. Al soltar amarras se queda donde estaba, y con el mando se mueve a su alrededor sin que la órbita la deje atrás.

### A pie

Atracada, **A pie** en la rueda baja a la cubierta de la estación: un hangar de 60 por 40 m con galería, escalera, columnas y carga. El de la central tiene el núcleo de antimateria. Hay tres tripulantes que van de un sitio a otro. El tiempo corre a tiempo real, no a 60 veces. La gravedad es artificial, de 1 g. Se vuelve con E junto a la esclusa, la puerta azul.

Las personas usan un motor propio, [Humanoid.h](../engine/include/epsilon/Humanoid.h), en metros y con Y hacia arriba, dentro de un recinto que el juego coloca en el mundo. Así sus centímetros no se pierden aunque estén a muchas unidades astronómicas del origen. El cuerpo mide 1,8 m por 0,6, anda a 1,6 m/s, corre a 4,5, salta medio metro, sube solo escalones de 40 cm y no atraviesa nada. Con gravedad 0 flota, para cuando haga falta salir al vacío. La cubierta es una lista de cajas, [OnFoot.h](include/game/OnFoot.h): las mismas sirven para chocar y para dibujarse, así lo que se ve es lo que se pisa.

### Consola

Cada comando deja su respuesta en el registro de la pantalla. Pegar varias líneas las ejecuta una a una; la última, sin salto de línea, queda escrita para revisarla y confirmar con Enter.

## Menú de inicio

![Menú](../docs/juego_menu.png)

Una consola de navegación con la misma estructura que la de Epsilon 0: el nombre arriba a la izquierda y los botones a la derecha, dentro del marco holográfico de esquinas. Alrededor hay una carta de sector con barrido de radar y la ruta hacia un destino, el archivo del sector N-42 que se descifra línea a línea y una columna de códigos que corre. Es decoración: no revela nada de la partida.

| Botón | Qué hace |
|---|---|
| Continuar misión | Sigue la partida guardada. También con Enter |
| Nueva misión | Elige uno de los cinco modos. También con las teclas 1 a 5 |
| Configuración | Calidad gráfica, pantalla completa y sincronización vertical |
| Créditos | Quién hizo el juego y de quién son las letras |
| Abortar | Sale del juego. También con Esc |

Esc vuelve atrás desde cualquier página. La configuración también se abre desde la pausa.

### Calidad gráfica

![Configuración](../docs/juego_configuracion.png)

| Calidad | Escena | Bloom | Grano y aberración | Detalle de superficies | Casco dañado |
|---|---:|---|---|---:|---|
| Baja | 50 % | No | No | 5 capas | No se ve (las piezas perdidas sí desaparecen) |
| Media | 75 % | Sí | No | 7 capas | Quemaduras |
| Alta | 100 % | Sí | Sí | 10 capas | Quemaduras y grietas al rojo |
| Ultra | 100 % | Sí | Sí | 12 capas, más nítidas | Además, daño en 3D: cráteres hundidos que perforan, vigas y chapas que asoman, escombros, chispas y gas |

Lo que más alivia a una tarjeta modesta es la escena: en Baja se pinta a la mitad de resolución, la cuarta parte de los píxeles, y luego se amplía. La interfaz va siempre a resolución completa, así que el texto sigue nítido. La configuración se guarda en `%APPDATA%\Epsilon\Nebulosa\ajustes.txt` y vale para todas las partidas. Los perfiles están en [Settings.cpp](src/Settings.cpp).

**Gráficos**, dentro de Configuración (en el menú y en la pausa), deja cambiar cada cosa por separado: resolución de la escena (50, 67, 75 o 100 %), detalle de superficies, luz y nebulosa, daño del casco, bloom, grano y aberración, y las sombras de la nave. Elegir una calidad las pone todas de golpe; tocar una después deja la calidad como «Personalizada».

## Modos de juego

| Modo | Daño | Al perder la nave |
|---|---:|---|
| Pacífico | Ninguno | Nada te ataca |
| Fácil | ×0,5 | Reapareces sin perder carga |
| Normal | ×1 | Reapareces y pierdes un cuarto de la bodega |
| Difícil | ×1,6 | Reapareces y pierdes la mitad de la bodega |
| Realista | ×1,6 | Lo pierdes todo y empiezas de cero |

Al perder la nave **el mundo no vuelve atrás**: estaciones, anomalías, reloj y lo descubierto siguen como estaban. La nave reaparece reparada y atracada en la última estación propia donde atracó, o en el punto de partida si no ha atracado en ninguna. Conserva niveles, emisores y lo que lleva montado, salvo los módulos que perdió en combate; de la bodega pierde lo que diga el modo. Es así porque el mismo mundo lo compartirán varias naves (`docs/multijugador.md`).

### Partidas guardadas

Una partida es una carpeta, `%APPDATA%\Epsilon\Nebulosa\partida\`, con dos clases de archivo de texto (formato 13, `game/include/game/SaveGame.h`):

- `mundo.sav`: lo que existe aunque no haya nadie jugando. Reloj, estaciones, anomalías, la especie y las naves.
- `jugadores/<id>.sav`: lo de cada jugador. Qué nave lleva, el tutorial y lo que conoce (sistemas y astros analizados). El `<id>` se crea al azar la primera vez y se guarda en `ajustes.txt`.

Las partidas anteriores, de un solo archivo `partida.sav`, ya no se cargan: el menú lo dice y hay que empezar una misión nueva.

La partida se guarda sola al desplegar una estación, al recoger de ella, al atracar, al vaciar una estructura, al localizar algo con el escáner y al salir. También hay un botón en la pausa. El menú ofrece seguir la partida guardada; una partida nueva la sustituye la primera vez que se guarda. En Realista, perder la nave borra el archivo.

El archivo está en `%APPDATA%\Epsilon\Nebulosa\partida.sav`. Es texto, una línea por dato, con el nombre del dato delante:

- Se puede abrir y leer.
- Los números vuelven exactamente iguales al cargar.
- La última línea es una suma de control: un archivo cortado o tocado a mano se rechaza en lugar de cargarse a medias, así que editarlo a mano no sirve.
- Se escribe en un archivo temporal y luego sustituye al bueno. Un corte de luz al guardar no destruye la partida anterior.
- **Solo se carga la versión actual del formato (11).** Una partida de una versión anterior no se migra: se rechaza, y el menú dice por qué («partida de la versión 10, anterior a la actual…») y que hay que empezar una misión nueva, que la sustituye.

Si hubiera que ocultar los datos, bastaría con cambiar [SaveGame.cpp](src/SaveGame.cpp): el resto del juego no conoce el formato.

Cuando el casco llega a cero, la nave se desmantela: las piezas se sueltan, estallan a lo largo del casco y cede el reactor.

![Destrucción](../docs/juego_destruccion.png)

## Unidades

Las cifras son unidades reales: la energía va de **petajulios a yottajulios** (PJ, EJ, ZJ, YJ) y la potencia de **gigavatios a exavatios** (GW, TW, PW, EW); la masa, en **toneladas, kilotoneladas y megatoneladas** (t, kt, Mt). Las conversiones están en [Units.h](include/game/Units.h).

La nave guarda 1 ZJ de energía, 120 kt de antimateria y 3 Mt de carga. Una central junto a una estrella como el Sol capta unos 750 EW, dos millonésimas de su luz: un enjambre de espejos de unos 28 000 km de lado a esa distancia. Cada dron minero da unas decenas de kt por hora.

## Mecánicas

### La nebulosa

La niebla oculta lo opaco según la distancia, y su espesor cambia de una zona a otra. Va del rojo al gris pasando por el naranja y el amarillo: el color cambia por zonas al viajar y también de un lado a otro del cielo.

Es una nube de verdad alrededor de la nave (`nebula.hlsl`, `GasAt`): masas de gas que brillan con filamentos, calles de polvo gris que las cortan y huecos por los que se ve la nebulosa lejana. Cada masa tiene su color de la paleta, rojo, naranja, ocre o gris. Las formas están en el espacio, en baldosas de 12 veces la visibilidad (4,8 millones de km), así que se mueven al viajar. La luz depende de las estrellas cercanas: cae con el cuadrado de la distancia (plena a 1 UA de un sol, un cuarto a 5 UA, casi nada a 20 UA), y el borde de cada masa vuelto hacia la estrella se enciende. Lejos de los soles la nube es rojiza y oscura; junto a ellos brilla en naranja y amarillo. Los pasos por rayo dependen de la calidad: 12, 18, 26 o 34.

Dentro de la nebulosa el lado de noche de los planetas no queda negro: recibe la luz de la nube, de su color. En Ultra, los planetas llevan además dispersión atmosférica (el suelo se lava hacia el borde y la luz del sol se enrojece al atravesar el aire), suelo rugoso (Oren-Nayar) y mar con Fresnel y brillo de microfacetas.

Los planetas con atmósfera muestran aire iluminado sobre el borde, del color de su aire. Se apaga hacia el espacio y en el lado de noche, y su grueso cambia de un punto a otro y con el tiempo.

Las estrellas dispersan su luz en la niebla, con un brillo suave que no depende de que estén en pantalla, así que nada salta al girar la cámara. De lejos son un resplandor. Su radiación barre la niebla alrededor, y de cerca se ve la superficie: granos de plasma, filamentos, manchas y zonas activas, con la corona sobre el borde del disco.

![Plasma](../docs/juego_plasma.png)

### Luz sobre la nave

La nave proyecta sombras sobre sí misma: cada fotograma se dibuja vista desde su estrella en un mapa de sombras de 2048 × 2048, con penumbra suave (doce muestras en disco). La luz del sol se mide con la raíz del flujo: a 1 UA es plena y las sombras son casi negras; a 5 UA aún se nota; a 20 UA casi todo es la luz de la nebulosa. Un planeta o una luna entre la nave y su estrella la eclipsa, con penumbra. Las chapas iluminadas devuelven luz a lo que tienen alrededor (rebote), así que la sombra no es negra sino del color del sol, más tenue. La pintura blanca del casco es cerámica mate, como la de la NASA (rugosidad alrededor de 0,75, reparto de Oren-Nayar), y la luz ambiente de la nebulosa es más fuerte en la cara que mira a la estrella.
Lejos de todo, la niebla reparte una luz suave de su mismo color. Cerca de una estrella o de un agujero negro manda una sola fuente: el lado iluminado brilla y las sombras son casi negras. Un planeta cercano devuelve la luz con su propio color, igual que una segunda estrella. Vale para la nave, las estaciones y los alienígenas. Lo que no se ve aparece en los sensores: primero como una señal sin identificar y, al acercarse, con su nombre y su actitud.

### Manejo y física

Es una nave de 1,2 km y se maneja como tal. El ratón apunta adónde quieres ir, y el casco gira despacio hacia allí, frenando para no pasarse. Un giro de 57° tarda unos 6 segundos. El nivel de maniobra mejora el giro, el derrape y el empuje; `maniobra 3` lo prueba.

- **Empuje e inercia.** El mando no pide una velocidad: dice cuánto empuja el motor. Al soltarlo la nave sigue por inercia, y la nebulosa la frena poco a poco, a la mitad en cerca de un minuto real.
- **Frenar.** Espacio frena con los propulsores y luego mantiene la nave quieta contra la gravedad. Al empezar está así. En curvatura, la burbuja cae en menos de un segundo.
- **Marcha atrás.** Con F por debajo de cero el motor empuja hacia atrás, a la mitad de fuerza. Sale con el motor normal, también en burbuja.
- **Gravedad.** Lo que no tiene empuje cae. Cada cuerpo domina una zona a su alrededor (su esfera de influencia), y dentro de ella la nave vuela respecto a él: una nave quieta junto a un planeta viaja con el planeta, y un cúmulo de asteroides la lleva con él. Ya no hay que perseguir nada.
- **Órbitas.** Si llevas la velocidad adecuada, orbitas de verdad, sin motor. Órbita, en la rueda, circulariza la órbita a la altura actual, gastando energía. Una órbita así no decae ni gasta, y el barrido funciona en ella. Cerca de un planeta la niebla no frena; alrededor de una estrella, una órbita libre sí decae.
- **Curvatura.** Dentro de la burbuja no hay gravedad ni fricción. La burbuja se suma a la velocidad que llevabas, y al apagarla esa velocidad sigue ahí.

En el espacio no hay arriba ni abajo: la nave y la cámara pueden dar la vuelta completa en cualquier eje. Lo apuntado no se adelanta más de unos 37° al casco, para que no se salga de la pantalla. La cinta de rumbo sigue siendo una referencia del plano del sistema; el radar va en los ejes de la nave, con delante al fondo.

El punto de mira se dibuja con el color inverso de lo que tiene detrás, así que se ve sobre una estrella igual que sobre el vacío. Si el rumbo o lo apuntado se salen de la pantalla, o quedan detrás de la cámara, su marca se queda en el borde como una flecha hacia donde están, y la línea entre los dos sigue dibujada. En el centro de la pantalla hay además un **anillo de giro** fijo: la flecha amarilla dice hacia dónde gira la proa ahora mismo (más larga cuanto más deprisa, con los grados por segundo), el triángulo hueco verde hacia dónde va a girar (lo apuntado), y el arco violeta de fuera, hacia dónde rueda. La línea azul es la trayectoria que sigues por inercia o en órbita; la amarilla, la ruta y su órbita de llegada. Sus puntos se aprietan junto a la nave, donde está la cámara, y se separan a lo lejos: así la curva se ve suave sin dibujar miles de tramos.

### Ruta asistida

Con un objetivo, Ir, en la rueda, calcula y vuela la ruta: orienta la nave, viaja en crucero, forma una burbuja si va a otra estrella y hay antimateria para ello, frena y la deja en órbita circular, o junto a una estación. Rodea lo que se cruce, a 1,15 radios de un planeta y a 4 de una estrella, y reduce para poder trazar la curva. El lateral muestra la fase, la distancia y el tiempo que queda. Llegar a una central al otro lado de su estrella lleva unos 80 segundos reales, con el tiempo a 600.

La ruta no corre cerca de una masa: su velocidad depende de lo lejos que esté de cualquier cuerpo, y frena con margen para no pasarse nunca. Se probaron 18 rutas por tres galaxias, sin un rasguño.

### Choques y calor

- Contra un planeta, una luna o un asteroide por encima de **1 km/s**, la nave **se hace añicos**. Por debajo, abolla el casco según el cuadrado de la velocidad.
- Las **naves, bastiones, estructuras y estaciones propias también son sólidos**, con las mismas reglas de velocidad. Embestir una nave o un bastión le hace el mismo daño que recibe tu casco, y lo vuelve hostil. Atracando, la estación deja pasar a la nave. Con las estaciones propias cuenta la eslora entera de la nave, de proa a popa, y la esfera real de la estación (0,45 km la central). La origami es un anillo hueco de 470 m de radio y 45 m de grueso: cuenta la distancia al tubo, así que se puede pasar por dentro.
- Las **rocas sueltas del anillo** también son sólidas (ver Cinturones).
- Tocar una estrella la **vaporiza**. Dentro de **4 radios**, el calor quema el casco con la cuarta potencia de la distancia, y a 1,5 radios la evapora en unos segundos; cada nivel de blindaje de más acorta esa zona. La órbita de trabajo de una estación está mucho más lejos, a 8 radios.
- Un paso largo no se salta nada: se comprueba todo el tramo recorrido, también en curvatura.
- Hasta 30 segundos antes salta la alarma de colisión o de calor letal.
- En modo Pacífico nada de esto destruye la nave: resbala por la superficie.

### Casco por zonas

El casco tiene 15 zonas, cada una con su integridad: proa, puente, las seis bodegas, anillo de curvatura, góndolas y radiadores de babor y estribor, espina y núcleo ([HullZones.h](include/game/HullZones.h)). El panel **Estructura**, abajo a la derecha, las enseña vistas desde arriba: verde entera, amarilla tocada, roja a punto de caer, gris perdida; parpadea la que acaba de recibir.

- **Un proyectil da donde lo lleva su trayectoria.** Su tramo recorrido, visto desde la nave (que también se mueve), se sigue a través de las formas de las zonas, y la primera que toca recibe el golpe (`ProjectileImpact`). Un disparo de frente apuntado a una góndola da en la góndola, no en la proa. Uno que va al núcleo por estribor da antes en la góndola de estribor, y uno que pasa por el hueco del anillo sigue hasta lo que haya detrás. Si pasa rozando o estalla por proximidad, cuenta la zona más cercana a su paso. El cráter de Ultra se abre en ese mismo punto, sobre la superficie real del modelo.
- Los choques y las rocas, que no tienen trayectoria propia, caen en la zona del lado que da al cuerpo: casi siempre la más cercana, a veces una vecina. Una zona aguanta de lleno el **30 %** del casco entero.
- El daño sin dirección (calor de una estrella, presión de ariete, sobrecalentamiento) se reparte entre todas.
- Una zona periférica que llega a 0 **se suelta**: sale despedida a unas decenas de m/s, girando, al rojo los primeros segundos, y se pierde. Pasa igual en todas las calidades.
- **El daño avanza hacia dentro.** Lo que sobra de un golpe, y todo lo que entra por el hueco de una zona ya perdida, pasa (al 70 %) a la zona de detrás: proa → puente → espina → núcleo; bodegas y anillo → espina; góndolas y radiadores → núcleo.
- En una central, o atracada en cualquier estación propia, las zonas se reparan al ritmo del casco. Una pieza perdida **solo se rehace atracada**. Una zona reparada del todo pierde sus cráteres.
- Las zonas se guardan con la partida; una partida anterior empieza con el casco entero. Los cráteres no se guardan.

**Lo que hace cada zona.** Dañada, su sistema rinde menos; perdida, deja de existir:

| Zona | Sistema | Efecto |
|---|---|---|
| Góndolas | Empuje | Cada una da la mitad: con una se acelera la mitad; sin ninguna quedan los propulsores de maniobra (8 %) |
| Anillo de curvatura | Crucero, híbrida e hiper | Dañado llega menos lejos y tarda más en formar la burbuja; por debajo del 25 % no hay curvatura, solo impulso |
| Radiadores | Refrigeración | Rotos, sueltan menos calor (un 25 % sin ninguno) |
| Proa | Conos ablativos | Las placas van en ella: dañada caben menos; sin proa, ninguna |
| Bodegas | Carga | Al perder una se pierde su parte de cada material |
| Puente | Puntería | La torreta falla más cuanto peor está (hasta 4 veces) |
| Espina | Maniobra | Gira y acelera peor (hasta el 40 %) |
| Núcleo | Reactor | Saca menos energía de la antimateria (hasta el 25 %) |

**Zonas rojas.** El puente, la espina y el núcleo no se sueltan: si llegan a 0, la nave está perdida aunque le quede casco. Un golpe directo en una zona roja hace **1,5 veces** más daño.

- **Puente** a 0: la nave se queda sin mando y **se apaga**.
- **Espina** a 0: la energía ya no llega a ningún sistema y **se apaga**.
- **Núcleo** a 0: pierde la contención y **estalla** («FALLO DE CONTENCIÓN»).

Al apagarse empieza una cinemática de 14 s: franjas negras, la alarma con la zona que ha caído, las luces de la nave parpadean y mueren, la nave se queda a la deriva girando despacio, y un parte se escribe letra a letra (sin mando, sin potencia, escudos caídos, soporte vital) hasta la baliza de socorro, que destella en rojo sin que nadie responda. La cámara se aparta despacio y todo acaba en negro, con la pantalla final «NAVE APAGADA». En creativo y en Pacífico nada apaga la nave.

**Cómo se ve, según la calidad.** El daño lo dibuja el propio shader de mallas ([mesh.hlsl](../engine/shaders/mesh.hlsl)). En Baja la instancia no lleva daño y el shader se lo salta, así que no cuesta nada. En Media, manchas de quemadura que crecen con el daño. En Alta, además grietas y bordes al rojo que se enfrían en unos segundos. Las luces de una zona dañada se apagan.

**En Ultra, el daño es 3D.** Solo con esta calidad se calcula; en las demás ni se mira. Cada golpe que llega al casco sin que lo paren los escudos queda anotado (`GameSession::HullHits`):

- Un rayo lo clava en la superficie real del modelo, desde el lado por el que llegó. Vale igual para la maqueta que para un `.glb`.
- Abre un **cráter** de 10 m a ~120 m según la parte del casco que se lleve. Los vértices se hunden y el borde se levanta, y la normal sigue la pendiente para que la luz lo marque. Alrededor queda una quemadura en rayos con brasas. Si el golpe es fuerte (más de ~2,5 % del casco) **perfora**: el agujero tiene el labio fundido, y dentro se ve el interior a oscuras con el resplandor del metal. Los choques abollan más ancho y no perforan.
- Del cráter **asoman vigas en I** dobladas y **chapas arrancadas** abiertas como pétalos, al rojo al principio.
- El golpe **arroja escombros**: astillas, chapas, trozos de viga y roca, que salen despedidos girando y se quedan atrás. También salta una lluvia de **chispas**, y por un agujero escapa un chorro de **gas**.
- Cada pieza lleva hasta cuatro cráteres, los más grandes. Se recuerdan los últimos 48 golpes.

Para que la abolladura se vea, las caras grandes de la nave se parten hasta que ninguna arista pase de 12 m (`SubdivideLongEdges`).

### Tu nave en glTF

El juego carga la nave de `modelos/nave.glb` (o `.gltf`), junto al ejecutable, o del archivo que se le pase con `--nave archivo.glb`. Si no hay ninguno, o no se puede leer, usa la maqueta hecha por código y lo avisa en la consola. La nave del juego está en [assets/modelos](assets/modelos), y la compilación la copia junto al ejecutable. Su archivo de Blender y sus notas están en [../assets/modelos/nave_epsilon](../assets/modelos/nave_epsilon/LEEME.md).

- **Escala y ejes:** 1 unidad = 1 m. +Y hacia arriba y la proa hacia −Z. En Blender: proa hacia +Y, arriba +Z, y la escala aplicada. El origen debe quedar a media eslora, sobre el eje.
- **Las medidas salen del modelo:** las zonas toman la forma de sus piezas (`BuildZoneLayout`: bolas de unos 30 m que las cubren), así que un proyectil da en la pieza que de verdad está en su camino. La eslora y el ancho para los choques, la cámara y el panel **Estructura**, que dibuja la planta de cada pieza, también salen de él.
- **Una pieza por zona:** cada nodo con malla es una pieza. Si se separa la nave en piezas, cada una se daña, se apaga y se suelta por su cuenta. Una nave de una sola malla también se abolla y se agujerea, pero no puede perder piezas.
- **El nombre del nodo dice su zona:** `proa`, `puente`, `bodega_1` a `bodega_6`, `anillo`, `gondola_babor`, `gondola_estribor`, `radiador_babor`, `radiador_estribor`, `espina` y `nucleo`. También vale en inglés (`bow`, `bridge`, `hold`, `ring`, `nacelle`, `engine`, `radiator`, `spine`, `core`). Sin un nombre conocido, la pieza va a la zona que tenga más cerca. Sin «babor» ni «estribor», el lado se saca de la posición.
  - Se mira por orden: `nucleo`, `puente` o `ventanas`, `proa`, `anillo` o `brazo`, `gondola`, `tobera`, `escape` o `motor`, `radiador`, `espina` y `bodega`. Cuenta la primera palabra que aparezca: `radiador_brazo` iría al anillo, y `espina_ventanas`, al puente.
  - En `bodega_N` cuenta el número más bajo del 1 al 6 que haya en el nombre: no conviene poner otras cifras.
  - Con varias piezas por zona basta con empezar el nombre por la zona: `bodega_3_blindaje`, `gondola_babor_tobera`, `nucleo_motor_principal`.
- **Anillos que giran:** una pieza cuyo nombre empieza por `rotor1_`, `rotor2_`… gira alrededor del eje de la nave. Los impares giran en un sentido y los pares en el otro, a la velocidad que da 1 g en la cara interior del anillo. Un anillo dañado gira más despacio, uno suelto sigue girando por inercia y, con la nave apagada, se van parando. Los cráteres giran con su anillo.
- **Color, textura y brillo:** se usa el color base del material. Una textura en un archivo aparte se carga; una incrustada en el `.glb` todavía no. La emisión del material, con su intensidad (`KHR_materials_emissive_strength`, que Blender exporta solo), da el brillo propio de la pieza (luces y ventanas), y se apaga con el daño de su zona como el de la maqueta.
- **Lámparas que alumbran:** según el nombre de la pieza, el juego la trata como lámpara (`GameLayer::LampOfPart`):
  - las «baliza» dan un destello doble blanco cada 1,6 s;
  - «luz_babor» y «luz_estribor» laten en rojo y verde;
  - los focos cuyo nombre acaba en «_luces» (salvo las franjas del anillo) alumbran fijos.

  Cada una ilumina el casco que tiene cerca, hasta 65–90 m, con una luz puntual del motor (`FramePacket::pointLights`, hasta 64 por fotograma; si hay más, cuentan las más cercanas a la cámara; sombreadas en `mesh.hlsl`), y las balizas y las de navegación llevan un halo. Lo que lleve «soporte», «mastil» o «base» en el nombre no es lámpara.

### Módulos

La nave se arma por módulos ([ShipModules.h](include/game/ShipModules.h)). Todo se cambia en el **Taller** (rueda > Taller, o `taller` en la consola), atracada en una estación propia. Montar cuesta metal y silicato de la bodega, y lo que se quita devuelve la mitad. En creativo el taller abre en cualquier sitio y no cuesta nada.

- **Ranuras exteriores:** son los nodos vacíos `hp_…` del `.glb` (la nave trae `hp_torreta_1` a `hp_torreta_6`); la maqueta tiene seis en el puente y el núcleo. Lo que se monta en ellas **se ve** sobre el casco, apuntando hacia fuera:

  | Módulo | Efecto | Coste |
  |---|---|---|
  | Torreta | Un arma más, con su recarga, que dispara desde su ranura | 260 metal + 60 silicato |
  | Aleta de radiador | +25 % de refrigeración | 180 + 40 |
  | Antena de sensores | +30 % de alcance del escáner | 150 + 90 |
  | Placa de blindaje | Su zona aguanta un 40 % más | 320 |
  | Bahía de drones | Dos drones mineros (ver Drones mineros). La nave sale con dos, en `hp_torreta_5` y `hp_torreta_6` | 300 + 120 |

- **Bahías interiores:** no se ven. Hay dos por bodega, tres en la espina, dos en el núcleo, una en el puente y una en la proa:

  | Módulo | Efecto | Coste |
  |---|---|---|
  | Almacén | +1 Mt de bodega | 200 + 20 |
  | Tanque de antimateria | +40 kt de antimateria | 240 + 120 |
  | Refuerzo estructural | Su zona aguanta un 30 % más | 280 + 30 |

- **Motor de impulso:** una sección que se cambia entera. Cada modelo sube la velocidad máxima y la aceleración del impulso, gasta más energía y cambia el color del escape:

  | Motor | Velocidad | Aceleración | Gasto | Coste (menos la mitad del montado) |
  |---|---|---|---|---|
  | Iónico de serie | x1 | x1 | x1 | — |
  | Iónico de alto flujo | x1,5 | x1,3 | x1,4 | 400 + 120 |
  | Plasma pulsado | x2,2 | x1,7 | x2 | 900 + 300 |
  | Fusión magnética | x3 | x2,2 | x3 | 1800 + 700 |

- **La bodega depende de las bodegas:** cada una de las seis aporta un sexto de la capacidad de serie; si se pierde, esa parte también.
- **Un módulo se pierde con su sitio:** si cae su zona o, en las exteriores, se arranca la pieza en la que va montado.
- **Daño pieza a pieza:** cada pieza del modelo tiene su propia integridad. Un impacto con trayectoria daña la pieza más cercana de su zona, medida sobre puntos de su superficie real. Lo pequeño (una antena, un escape, un mástil) se rompe antes que la zona entera. A 0 **se arranca sola** y se aleja girando, aunque su zona aguante. Las piezas se reparan con la zona, y una arrancada solo se rehace atracada.
- **Mejoras visibles:** las mejoras de siempre añaden piezas al casco, posadas con un rayo sobre la superficie real. Cada nivel de escáner añade una antena en la espina, cada nivel de computador una cúpula en el puente, cada nivel de maniobra cuatro propulsores a los lados de la proa y del núcleo, y cada nivel de blindaje un escudo térmico abombado en la punta de la proa.
- Módulos, motor e integridad de las piezas se guardan con la partida. Las torretas de los módulos salen de los módulos al cargar.

### Tiempo

El jugador vive el tiempo de a bordo. Fuera pasa más deprisa, y los mundos, las estaciones y los alienígenas van a ese ritmo. El reloj de velocidad muestra `EXTERIOR x` cuando se nota, y el panel da el día exterior y el de la nave.

- **Velocidad.** Es el factor de Lorentz real: a 0,99 c, fuera pasan 7 veces más. Por eso cruzar un sistema lleva segundos a bordo.
- **Gravedad.** La fórmula es la real, `1 / sqrt(1 - rs / r)`, y junto a un agujero negro se usa tal cual. Junto a una estrella el efecto real sería de millonésimas, así que allí se multiplica por 100 000 para que se note: **ese número es decisión de juego**, en [Balance.h](include/game/Balance.h). Junto a un planeta sigue siendo casi nulo.
- Por encima de la luz no hay fórmula, así que el tiempo exterior va al máximo, 60 veces. Cruzar 0,1 años luz a 10 c lleva 3,6 días fuera y hora y media a bordo, unos 90 segundos reales. **Es decisión de juego**: sin ella, ese viaje duraría hora y media de partida.

Además, en los momentos críticos el tiempo se frena, para que dé tiempo a reaccionar, y el lateral lo avisa:

| Momento | Escala |
|---|---:|
| Normal | 60 |
| Junto a una estación, a menos de 3000 km | 18 |
| Combate o alarma de colisión | 9 |
| Explosión de la nave | 1,2 |

### Motores

| Motor | Tope | Coste | Condición |
|---|---|---|---|
| Impulso | 200 km/s | 0,6 PW | Hidrógeno ionizado acelerado. Funciona siempre; sin energía, a 20 km/s |
| Crucero | 0,99 c | 33 PW a fondo | Curvatura positiva suave, sin burbuja. Nunca alcanza la luz. A bordo, el tiempo va más lento: a tope, unas 7 veces |
| Híbrida | 10 c | Formar: 1 YJ. Mantener: de 100 a 1 EW | El emisor de energía negativa de la estructura del sistema inicial |
| Hiper | De 50 a 250 c | Formar: de 10 PJ a 10 TJ. Mantener: de 1 TW a 1 GW | Un segundo emisor, de una estructura de otro sistema |

**Híbrida e Hiper no aparecen** ni en el panel ni en las teclas hasta tener su tecnología. Cada estructura abandonada guarda un **emisor de energía negativa**: con él la burbuja se cierra sin colapsar. El de la estructura del sistema inicial da la Híbrida; hasta entonces no se puede salir de él, y la ruta asistida va en crucero. Con un segundo emisor, de una estructura de otro sistema, se monta el Hiper.

Los emisores **no se gastan**: van montados en el motor. Cada emisor más allá del segundo afina el hiper: 50 c con dos, 100 c con tres, 150 c con cinco, hasta 250 c. La materia oscura ya no existe en el juego. Con el motor apagado, también en órbita, la nave recupera 2,2 PW de energía y sintetiza 0,5 kt de antimateria por hora del gas de la nebulosa, así que nunca te quedas atrapado del todo.

### Antimateria

La nave ya no se recarga con energía solar. La central usa la luz de su estrella para mover aceleradores de partículas, y el 10 % de esa energía acaba convertida en antimateria. Una estrella como el Sol da unas 3 kt por hora, y la central guarda 60 kt hasta que vengas a recogerlas con Usar, en la rueda.

- **Rendimiento.** 1 kt de antimateria y 1 kt de materia corriente dan 180 ZJ al aniquilarse. Formar una burbuja humana pide 5,6 kt, y el depósito guarda 120 kt.
- **Formar.** Se paga mientras se forma, en unos 4 segundos reales. Sin antimateria para terminarla, no empieza.
- **Mantener.** Cada segundo gasta la formación dividida por un número que va de 10 000 a 1 000 000: la mitad depende de lo cerca que vaya de su tope, y la otra mitad de lo que lleva estable, con unas 2 horas exteriores de escala. Cruzar 0,1 años luz gasta en mantenerla menos que en formarla.
- **Apagar.** Al soltar el mando la burbuja se deshace, y la parte perdida se vuelve a pagar. Si se acaba la antimateria, colapsa.
- **Lo demás.** El impulso, el crucero y el escáner gastan energía, y el reactor la repone con antimateria: 1 ZJ cuesta 5,6 t.

Las cifras salen de [Warp Technology & Alien Factions Specification]: 10 c, 50 c, 250 c, y la formación y el mantenimiento de la métrica híbrida. Sin energía negativa la formación sube a 10²⁴ J. Las facciones y la velocidad de 300 c aún no están en el juego; el plan de trabajo está en las notas del proyecto. El combate, los escudos y el calor ya sí.

### Cómo se ve la curvatura

Al formarse la burbuja, el espacio se pliega delante de la nave y la distorsión crece hasta ocupar toda la pantalla, como al entrar en un agujero negro. Es la misma lente del motor, sin disco: dobla de verdad lo que hay detrás, y una estrella al frente forma un anillo de luz. Al cerrarse, las estrellas se convierten en líneas. La nave va dentro de la burbuja, así que se dibuja después y no le afecta.

![Lente](../docs/juego_lente.png)

Ya en marcha, se usa la aberración relativista real: cerca de c, todo el cielo se comprime hacia delante, así que en el campo de visión cabe casi la esfera entera. Por eso se ve la luz azul al frente, el arcoíris alrededor, el rojo hacia los bordes y el negro detrás. Los colores concretos son una representación, porque la luz real saldría del espectro visible.

### Mapa

M abre el menú de sistemas, que se puede elegir también en la rueda. A la izquierda, la lista por secciones: **Visitados** y **Localizados**. El sistema elegido se abre y enseña sus estrellas, sus planetas con las lunas debajo, los campos de asteroides, las anomalías vistas y las estaciones propias. Lo que no se ha analizado aparece como `Sin analizar`. A la derecha, los datos de lo elegido, y un botón que lo fija como objetivo. Vale para sistemas lejanos: los datos analizados se conservan aunque el sistema no esté en memoria.

En el centro hay un modelo en 3D. Se gira arrastrando con el ratón y se acerca con la rueda:

- **Sistema.** Las órbitas reales, a escala real y con su inclinación y excentricidad. El planeta elegido es el centro del zoom, y sus lunas aparecen al acercarse. Un clic en un astro lo elige. También se marcan las anomalías vistas y la nave.
- **Astro.** La esfera real, con su superficie, su giro y su atmósfera, y las órbitas de sus lunas al alejarse. Una estrella lleva su corona con el mismo cálculo que en el juego, un poco atenuada para que el disco se siga viendo: un halo cálido que se apaga sin borde, serpentinas tenues y el filo rojizo de la cromosfera. Solo está disponible la superficie real en el sistema donde estás; en otro se ve un globo de su color.

La rueda del ratón acerca cambiando el enfoque, no moviendo la cámara: así las órbitas no se deforman al pasar entre ellas. Arrastrar gira en el sentido de la mano, como la cámara de la nave.

**Elementos.** Planetas, lunas, asteroides y estrellas tienen su composición por elementos, en porcentaje de masa: hidrógeno, helio, carbono, nitrógeno, oxígeno, magnesio, aluminio, silicio, azufre, calcio, hierro y níquel. Sale de sus materiales con recetas reales: un núcleo de hierro y níquel, rocas como el manto terrestre, agua, hielos de metano y amoníaco, y gas como el del Sol. Las estrellas usan su hidrógeno, su helio y los metales en proporción solar; los campos de asteroides, lo que sacarían los drones. Se ve en los datos del mapa y del análisis.

**Tabla periódica.** El botón **Tabla periódica** del mapa, o **Ver los 118 elementos** en los datos, enseña el porcentaje en masa de todos los átomos de lo elegido, del hidrógeno al oganesón, en su sitio de la tabla. El color de cada casilla dice cuánto hay en escala logarítmica, y al pasar el ratón sale el valor exacto, en ppm o ppb si es una traza. Los doce de arriba salen de las recetas. El resto sale de las abundancias de las condritas CI y del Sol (Lodders, 2003), repartidas según a qué se une cada elemento: los siderófilos, como el platino o el oro, van con el hierro; los calcófilos, como el cobre o el plomo, con el azufre; los litófilos, como las tierras raras o el uranio, con la roca; los volátiles, con el agua y los hielos; y los gases nobles, con el helio. Los hijos radiactivos del uranio, como el radio o el polonio, salen del equilibrio secular. Cada astro se aparta un poco de la media según su semilla, y a veces tiene una veta de un elemento escaso. Del 95 en adelante no hay nada: solo se fabrican. Servirá para diseñar tecnología y piezas.

**Superficies.** El color de la roca sale de esos elementos: el hierro la oscurece, el magnesio, el aluminio y el calcio la aclaran, el azufre la amarillea y el carbono la ennegrece. Los mundos rocosos tienen mares de basalto oscuro, con menos cráteres, y cráteres en cuatro escalas, más cuanto menos aire tienen. Cada cráter tiene un perfil real: un cuenco hondo, con fondo plano y pico central si es grande, un borde levantado y un manto de eyecta que se apaga hacia fuera. Hay muchos más pequeños que grandes, ninguno es un círculo perfecto, los viejos están gastados y los nuevos, más claros, se montan encima y borran lo que había. El relieve tuerce la luz, así que se ven sus sombras, y sin aire el polvo devuelve la luz como la Luna: el disco no se oscurece hacia el borde y la sombra corta en seco. Además de cráteres, la roca y el hielo tienen colinas, cordilleras de crestas afiladas y fracturas largas; en los mundos helados, como Encélado, las fracturas abundan y se tiñen de azul donde aflora hielo limpio. Los mundos con agua tienen cordilleras con relieve, costas turquesa, vegetación que se apaga hacia los polos, desiertos en las latitudes secas y el reflejo del sol en el mar. Sus **nubes** tienen volumen: frentes estirados por el viento, ciclones que las enroscan al revés en cada hemisferio, jirones y cúmulos de cerca; la luz modela sus cimas y cada nube proyecta su sombra sobre el suelo, desplazada lejos del sol. Los gigantes tienen bandas de anchura desigual con estrías, remolinos en los bordes entre bandas, óvalos blancos y una gran tormenta que tuerce las bandas a su alrededor. En el mapa la luz llega de lado, para que se vea el relieve. El borde de un planeta se funde con su atmósfera, sin la línea oscura de un píxel que quedaba entre ambos.

**Atmósfera.** Los datos de un planeta dicen si tiene aire, lo denso que es y de qué está hecho: dióxido de carbono en un desierto, nitrógeno y oxígeno en un mundo con vida, vapor de roca en uno de lava, hidrógeno y helio en un gigante. El brillo del borde sigue una escala logarítmica, de 0,1 milibares a 1 bar, así que un aire fino como el de Marte también se ve. Un planeta achatado por su giro tiene la atmósfera ceñida a su forma, también el resplandor que deja en la niebla, que antes era un círculo y dejaba un hueco sobre los polos. En los gigantes el aire visible es una bruma fina sobre las nubes.

### Escáner

![Escáner](../docs/juego_escaner.png)

Los sensores normales ven lo que emite. Las estructuras abandonadas llevan eones apagadas: para ellas hace falta el barrido de microondas y ultravioleta.

El radar pasivo tiene dos niveles:

| Nivel | Qué nota | Alcance (niebla media) |
|---|---|---|
| Contacto | Naves, estructuras, anomalías y cualquier astro | 8·10⁷ km; no crece con el escáner, y lo frío sigue sin aparecer hasta un barrido |
| Eco de masa | Estrellas, planetas de más de 10²² kg y cinturones, como «señal sin identificar» (anillo en el radar y en la lista) | 2·10⁹ km (unas 13 UA) con el escáner 1; cada nivel suma un 80 % y cada antena un 30 %. Una estrella de un sistema sin localizar, a 10 veces eso |

La niebla acorta o alarga los dos por igual (de ×0,56 a ×2,2). En el radar, la leyenda «N MASAS» da cuántos ecos hay y a qué distancia llegan. Caben 24 señales; si hay más, se quedan por relevancia y no por distancia: primero naves y anomalías, después las estrellas y estaciones del sistema en que se está, sus astros conocidos, sus ecos de masa y, al final, lo de otros sistemas. Las cifras están en `ShipRules` de `Balance.h`.

![Radar pasivo con escáner 1](../docs/radar_1e11.png)
![Radar pasivo con escáner 5](../docs/radar_1e11_escaner5.png)

Tampoco se conoce otro sistema hasta que un barrido lo localiza o se llega a él. Al empezar solo aparece el tuyo.

**Sin barrido no se sabe nada de un astro**: es una señal sin identificar, sin recursos ni datos, y no se puede desplegar en él. El barrido analiza todo lo que hay a unas 40 UA en este sistema. Análisis, en la rueda, abre sus datos:

![Análisis](../docs/juego_analisis.png)

- La esfera real del astro, girando.
- Un holograma del sistema con todas las órbitas, en escala logarítmica y con el astro resaltado.
- Sus datos, composición y órbita.
- Lo que sacarían los drones o daría una central.

- La nave debe estar **quieta o en órbita**. Si enciende el motor, el barrido se pierde.
- Gasta **160 EJ** y dura 30 segundos.
- Encuentra anomalías a **6 UA** con el nivel 1, y localiza sistemas a unos **0,17 años luz**. Todo llega más lejos con cada nivel.
- Lo mejor escondido pide más nivel: el barrido avisa de una señal débil.

**Eco frío.** Lo frío que queda fuera del alcance pero dentro del **radio de eco** (4 veces el alcance: unas **24 UA** con el nivel 1; crece con el nivel y con las antenas de sensores) no se revela, pero el barrido da su **rumbo** (en grados sobre el plano del sistema, con su elevación) y su **distancia en UA**, y lo marca en pantalla con un círculo violeta, o con una flecha junto al anillo de giro si queda fuera de la vista. Con el nivel 1 el rumbo puede errar hasta 25° y la distancia un 30 %; cada nivel de más reduce los dos errores a la mitad. Si hay varios, señala el más cercano. La pista se borra al revelar lo que señalaba o al salir del sistema, y no se guarda: basta con barrer otra vez.

- **La firma manda:** algo que pide más nivel que el del escáner no da rumbo; el barrido solo avisa de una «señal débil más allá del alcance» y del nivel que pide.
- **El tutorial la garantiza:** mientras está en el paso de la estructura, la pista es exacta y llega aunque el eco no alcance (sin saltarse la firma). Fuera de ese paso, la regla normal.

**La primera estructura** del sistema inicial está a entre **1,5 y 4 UA de su estrella** (`firstDerelictMinAu` y `firstDerelictMaxAu` en [Balance.h](include/game/Balance.h)): un barrido desde la órbita de la estrella, adonde el tutorial lleva para la central, la encuentra con el nivel 1. Desde la salida, a unas 10 a 25 UA, suele quedar dentro del eco. La posición sale de la semilla y gasta los mismos números que antes, así que el resto del reparto no cambia. `escaner 3` prueba otro nivel desde la consola, y `sondeo` dice a cuánto está cada anomalía fría y qué pista hay.

### Combate

La nave sale con una arma de cada familia. Cada una hace un tipo de daño distinto, para que los escudos de la etapa siguiente tengan donde elegir:

| Arma | Daño | Alcance | Recarga | Coste | Cómo funciona |
|---|---|---|---|---|---|
| Torreta | Cinético, 20 | 150 000 km | 3 s | 0,4 EJ | Proyectil a 4000 km/s. Derriba misiles y drones que se acerquen |
| Cañón espinal | Fotónico, 260 | 500 000 km | 40 s | 60 EJ | Haz de luz a c, **fijo al eje del casco**: hay que girar hasta la solución antes de disparar |
| Misil | Antimateria, 180 | 1 200 000 km | 10 s | 0,2 kt de antimateria | Guiado, lento (hasta 1500 km/s) y con mucho alcance; gira a 0,05 rad/s |
| Drones | Térmico, 18 ×6 | 600 000 km | 25 s | 1 kt de metal | Enjambre guiado de seis; si no los derriban, queman el casco |

Los tiempos son simulados: en combate el reloj se frena, y unos 10 segundos reales son unos 90 simulados. Cada nivel de arma suma un 40 % de daño y acorta un 10 % la recarga.

**En pantalla.** Al fijar una nave o un bastión, el panel de objetivo muestra su casco y una fila por arma: LISTA, los segundos de recarga, o lo que falta (LEJOS, SIN METAL, GIRA 3,2°). Con el cursor activo, cada fila dispara esa arma. El rombo verde marca adónde debe mirar el casco para el cañón espinal. Cada proyectil cercano lleva una marca, roja si viene hacia ti, y un aviso central cuenta el fuego entrante y su distancia. Las estelas salen del arma y cada tipo de daño tiene su color: amarillo cinético, azul fotónico, magenta antimateria y naranja térmico.

**Computador de puntería.** El blanco está a cientos de miles de km y mide menos de 1, así que no se apunta a mano. El computador resuelve la intercepción (velocidad del tirador, del blanco y tiempo de vuelo) y la corrige con un método de tiro: simula el vuelo con la gravedad que conoce y desplaza el punto de mira hasta que acierta. El **nivel del computador** decide cuántos astros cuenta (1, 3, 5, 7 o 9), cuántas correcciones hace y cuánto se equivoca al final: el error baja con el nivel elevado a 1,5. Lo que no cuenta, desvía el tiro. Las armas guiadas no necesitan puntería; el cañón espinal tampoco se equivoca, pero no gira.

**Gravedad y luz.** Los proyectiles siguen la gravedad real de los planetas y estrellas del sistema; los haces de luz se desvían el doble, como en la relatividad. Junto a una estrella, un tiro recto falla por cientos de km. Los proyectiles que fallan **siguen su camino**: caen a un planeta, orbitan o vuelven a cruzar el sistema, y pueden dar a quien disparó o a un tercero. Un astro que se cruza los detiene.

**Impactos.** Un proyectil cuenta como impacto a cierta distancia del blanco (de 4 a 40 km, según el arma). El daño recibido pasa por un único cauce, `Hurt`, que aplica el multiplicador del modo. En Pacífico nada dispara, pero se puede cazar. Las naves destruidas sueltan metal y los bastiones, más.

Los enemigos usan las mismas reglas: una nave hostil dispara torretas con un computador de nivel 1, y un bastión lanza misiles. Disparar a una nave neutral la vuelve hostil. Las armas valen contra cualquier bando, no solo contra el jugador: es la base para que luego peleen entre sí.

### Escudos

Cinco defensas con ventajas cruzadas, del documento de especificación. Cuatro son **campos**, y solo uno va encendido a la vez: K los recorre. Los **conos ablativos** son placas en la proa, sin energía, y van siempre puestos.

| Defensa | Para de cada impacto (cinético, fotónico, antimateria, térmico) | Aguanta | Coste e inconveniente |
|---|---|---:|---|
| Arena magnética | 15 %, 90 %, 40 %, 85 % | 350 | 2 kt de silicato por hora y un poco por cada golpe parado; sin silicato no funciona. Absorbe calor |
| Plasma | 95 %, 30 %, 30 %, 10 % | 300 | Calienta la nave, y los hostiles la ven desde **tres veces más lejos**. Vaporiza los asteroides de menos de 500 m al chocar |
| Electromagnético | 0 %, 55 %, 95 %, 50 % | 400 | Gasta 900 EJ por hora mientras está encendido, casi la mitad de lo que da el reactor. **Triplica el error de puntería enemigo**, incluido el del haz |
| Conos ablativos | 60 %, 30 %, 30 %, 50 %, solo lo que llega **por delante** | 500 | Pesan: con las placas enteras la nave maniobra un 23 % peor. El gas de la nebulosa las gasta con la velocidad |
| Campo de Amor | Todo | Sin límite | Devuelve al atacante el daño que no es luz, y lo convierte en casco y energía. Solo de los Unicornios; hoy, con `creativo` |

Un impacto pasa primero por el campo y lo que queda, si viene por delante, por los conos; el resto llega al casco. Los campos se recargan solos, despacio; las placas se reponen en una estación propia, con metal. El registro dice cuánto pararon los escudos en cada ráfaga.

**Presión de ariete.** En curvatura por encima de la luz, el gas de la nebulosa golpea la proa. Los conos la aguantan gastando placas; sin ellos, se lleva casco: unos 24 por minuto real a 10 c con la niebla normal, y más cuanto más rápido y más espesa.

**Escudos alienígenas.** Las naves llevan arena, plasma o electromagnético, según cada una; los bastiones, electromagnético. El panel de objetivo enseña su escudo y, en cada arma, qué parte lo atraviesa: contra el plasma conviene la luz o la antimateria, y contra el electromagnético, la torreta.

### Calor

El calor sube al disparar (el cañón espinal mucho más que la torreta), al parar golpes con un escudo, con el plasma encendido, al formar una burbuja y mientras dura, según la velocidad, y en la zona de calor de una estrella. **Por encima del 100 %** los escudos y las armas dejan de funcionar, el casco se funde poco a poco y el tiempo se frena para dar tiempo a salir.

| Sistema | Quita | Se gasta | Notas |
|---|---|---|---|
| Radiadores infrarrojos | 0,35 por segundo | Nada | Siempre funcionan, pero junto a una estrella apenas sirven: por debajo de 12 radios pierden eficacia con el cuadrado de la distancia |
| Agua | Hasta 1,5 por segundo | 60 de depósito | La más barata. Se rellena al fundir hielo y en las estaciones propias, con hielo de la bodega |
| Hielo | Hasta 1 por segundo | Hielo de la bodega | Se funde en la nave, y el agua que deja va al depósito |
| Nitrógeno líquido | Hasta 4 por segundo | 40 de depósito | El que más quita. Se evapora solo, despacio, y con la nave fría se licua del gas de la bodega con energía |

Los que se gastan solo entran por encima del 40 %, primero los baratos. El panel de la nave muestra el escudo encendido, las placas de proa y el calor, con el agua, el nitrógeno y la eficacia de los radiadores. Todas las cifras están en [Balance.h](include/game/Balance.h), en `DefenseRules` y `ThermalRules`.

### Estaciones

| Estación | Dónde | Gratis | Después |
|---|---|---|---|
| Origami | En cualquier punto de un sistema, fuera de órbita y casi parada | Va a bordo desde el principio; hay una sola | — |
| Central | Estrellas y agujeros negros | Solo la primera | 300 kt de metal y 200 kt de silicatos |

**Estación origami (nivel 1).** Viaja plegada alrededor de las bodegas, entre los dos anillos de curvatura. La sujeta un **andamio provisional**:
- dos anillos de celosía alrededor de los tanques;
- abrazaderas en la espina, con radios que pasan entre los tanques;
- un puntal recto hasta cada nudo.

Al soltarla (**Estación** en la rueda, o `estacion` en la consola):
1. Los puntales se pliegan contra el anillo del andamio durante 1 s.
2. Se abre alrededor de la nave durante 9 s: es una corona en zigzag de 16 módulos que se aplana en un anillo de 1026 m.
3. Se aparta 1,6 km hacia popa durante 12 s, sin llevarse la nave.
4. Saca sus ocho brazos durante 6 s: cuatro con impresoras 3D y cuatro con láseres. A la vez, el andamio se suelta: sale despedido por sectores hacia fuera, dando vueltas, y se desecha a unos 2,5 km (`GameLayer::ScaffoldMotion`).

Después:
- Se queda quieta junto al planeta que domina ese punto o, si no hay ninguno, en el sistema.
- Tiene ocho esclusas como la de la nave. La nave atraca en la del nudo 1, paralela al eje del anillo para que los anillos no choquen.
- **Usar** (o ENTREGAR) le da todo el metal y el silicato de la bodega, salvo los 60 kt de silicato que son la munición del escudo de arena. Ese material servirá para construir naves.
- No produce ni es modular hasta que exista la primera nave minera.
- Para chocar cuenta como un anillo hueco: se puede pasar por dentro.

Las cifras están en `StationRules` ([Balance.h](include/game/Balance.h)). El modelo y su animación son `modelos/estacion.gltf`; sus notas, en [assets/modelos/estacion_origami](../assets/modelos/estacion_origami/LEEME.md).

Una central produce sola, aunque estés en otro sistema, y guarda lo producido. **Hay que ir en persona a recogerlo**; `relayTier` deja preparado el enlace a distancia para más adelante. En órbita de una central propia, además, se repara el casco. Tiene 4 huecos, y cada módulo (colector, condensador y cuchara) llega al nivel 5. Al desplegarla, las piezas salen de la nave por un haz y se montan una tras otra durante 6 segundos.

La estación de minería ya no existe: la minería la hacen los drones. Las partidas viejas que tenían una la pierden al cargarse; el resto se conserva.

### Drones mineros

La nave sale con dos **bahías de drones**, módulos exteriores montados en `hp_torreta_5` y `hp_torreta_6`, en el módulo de popa. Cada bahía lleva dos drones de 34 m, posados encima. Una bahía nueva se monta en el taller como cualquier módulo exterior: cuesta 300 kt de metal y 120 de silicato.

- Con un planeta o un campo **analizado** como objetivo, a menos de 40 000 km de su borde y con el impulso, **Drones** (en la rueda o en el panel del objetivo) los manda allí.
- Cada dron hace este ciclo:
  1. Vuela a 12 km/s simulados hasta la superficie, del lado de la nave, o hasta el cúmulo.
  2. Mina hasta llenar su bodega de 20 kt, a 18 kt por hora con riqueza 1.
  3. Vuelve y descarga en la nave según la composición del sitio.
  4. Repite.
- Con la bodega llena esperan posados con el mineral.
- **Drones** otra vez, o `recoger-drones`, los hace volver.
- Vuelven solos si la nave se aleja más de 200 000 km del sitio. Si la nave enciende otro motor que el impulso, se repliegan de golpe y descargan lo que traigan.
- Una bahía perdida se lleva sus drones. Los drones no se guardan: al cargar una partida están todos posados y vacíos.

Fuera se ven por el brillo de sus motores; minando, la broca chispea. Las cifras están en `DroneRules` ([Balance.h](include/game/Balance.h)). Los modelos son `modelos/dron.gltf` y `modelos/bahia_drones.gltf`; sus notas, en [assets/modelos/drones](../assets/modelos/drones/LEEME.md).

### Cinturones

Dentro de la nebulosa solo se ve a unos cientos de miles de km, y las familias de asteroides de un cinturón quedan a millones de km unas de otras: antes, dentro del cinturón solo se veía un cúmulo suelto. Ahora el anillo existe alrededor de la nave, esté donde esté dentro de él (`BeltField`, en [Asteroids.h](../engine/include/epsilon/Asteroids.h)):

- **Polvo:** puntos que forman la banda hasta 220 000 km, más densos en el plano y en el centro del anillo.
- **Rocas sueltas:** de 0,1 a unos pocos km, con su malla, hasta 15 000 km; una cada 2000 km más o menos en un cinturón normal. Son sólidas, como las de las familias.
- **Familias:** más grandes, de 250 a 610 km de radio según el perfil, con cinco veces más rocas (unas 4000 en uno normal), casi igual de apretadas que antes. Sus órbitas son menos excéntricas (0,05 de media y 0,15 como mucho, antes 0,09 y 0,3), así que se quedan dentro del anillo: antes la más cercana a la estrella solía quedar muy por dentro del borde, donde no había nada más.
- Dentro de la nebulosa el fondo es claro, así que el polvo y las rocas lejanas se ven como motas oscuras, más grandes cuanto más cerca.

El anillo se divide en celdas que giran con la velocidad orbital de su radio, y cada celda saca sus rocas de su semilla: se mueven en órbitas circulares y alrededor de un punto aparecen siempre las mismas, sin guardar nada. Sigue siendo mucho más apretado que un cinturón real, en el que entre roca y roca hay cerca de un millón de km: así hay algo que ver y que esquivar.

### Estrellas

Lo que da cada una depende de su luminosidad, y entrar en su órbita depende del blindaje, que empieza en nivel 1:

| Estrella | Blindaje | Potencia | Materia |
|---|---:|---:|---|
| M, K | 0 | Baja | Poca |
| G, F | 1 | Media | Poca |
| A, enana blanca | 2 | Alta | Normal |
| Gigante roja | 2 | Según su luz | ×4 |
| B, supergigante roja | 3 | Muy alta | Supergigante ×8 |
| O, estrella de neutrones, supergigante azul | 4 | Máxima o 139 PW | Supergigante ×6 |
| Agujero negro de Schwarzschild | 4 | Unos 110 PW | 20 kt/h |
| Agujero negro de Kerr | 5 | Hasta 3 veces más, según su giro | 20 kt/h |

Sin blindaje no se puede desplegar, y entrar en esa zona quema el casco.

### Alienígenas y estructuras

![Bastión](../docs/juego_bastion.png)

Las neutrales patrullan y no hacen nada, hasta que alguien les dispara. Las naves hostiles te persiguen a 300 km/s si te acercas a 3 millones de km, se quedan a unos 80 000 km y disparan torretas: se escapa con la curvatura. Los bastiones no se mueven y lanzan misiles a lo que entra en su alcance. Todas se pueden destruir (ver Combate); las estructuras abandonadas no.

Cada estructura guarda un emisor de energía negativa. Sacarlo lleva 2 horas simuladas, unos 2 minutos reales, con la nave quieta a su lado; la bodega muestra los que llevas.

![Estructura](../docs/juego_estructura.png)

## Tutorial

![Tutorial](../docs/juego_tutorial.png)

El primer sistema enseña lo básico con diálogos de la IA de a bordo, escritos letra a letra y con una sola idea por línea. Son 13 pasos: mando, frenar, cursor, escáner, análisis, drones mineros, estación origami (opcional: Enter la guarda para otro sitio), central, recoger, estructura, explorarla, burbuja Híbrida y despedida. Cada paso espera a que lo hagas, y el juego no se detiene mientras tanto.

Mientras el objetivo está en pantalla, la interfaz **señala dónde se hace**: un marco cian que late y una flecha hacia la tecla, el panel o el botón. Una opción de la rueda se ilumina dentro de la rueda abierta; con la rueda cerrada se señala la T. Una ventana plegada se señala por su cabecera, con cómo desplegarla. Lo que no está a la vista sale en una etiqueta bajo el objetivo. Bajo el objetivo va también **lo que aún falta**, con cifras: «Nave quieta: no, va a 33 km/s (Espacio)», «Energía insuficiente: faltan 40 EJ», «Distancia: 10.17 UA, hace falta menos de 151461 km (Ir)» o, en la Híbrida, el coste de la burbuja junto a la antimateria que hay. En el paso de la estructura, la pista del escáner sale en el radar como un anillo violeta. Al cumplir un objetivo suena la confirmación y destella el borde de la pantalla; **REPETIR** (o Retroceso) vuelve a decir las líneas del paso.

Lo que se señala es un `Highlight` de [Tutorial.h](include/game/Tutorial.h); `DialogueStep::guide` lo afina y dice qué falta. `Dialogue` sirve para cualquier otra conversación: basta con darle otra lista de pasos. Las partidas de antes del tutorial nuevo (versión 11) ya no se cargan.

## Interfaz

Naranja y amarilla, con [Audiowide](assets/fonts/OFL-Audiowide.txt) para el texto y los títulos, y [Rubik Glitch](assets/fonts/OFL-RubikGlitch.txt) para todo lo alienígena: nombres de naves, bastiones y estructuras, y el aviso de fuego entrante. El título del menú, Epsilon-Z, va en [Hangul](assets/fonts/OFL-Hangul.txt), de Sébastien Hayez: se escribe `3PSiLgn-Z` para que se vea «3psilon-z», estirado y en cursiva. Las tres tienen licencia OFL, que permite incluirlas en el juego; sus licencias van junto a ellas. Tiene cinta de rumbo, reloj de velocidad con la marca de la luz, radar 3D, panel de sensores, objetivo y bodega, punto de mira con el rumbo real y el apuntado, anillo de giro, y una barra con las teclas que sirven en cada momento. El punto de mira va aparte, en [reticle.hlsl](shaders/reticle.hlsl), porque necesita leer la imagen.

El **radar 3D** va siempre abajo, en el centro, encima de las teclas. Es un disco visto en perspectiva, en los ejes de la nave (delante es el fondo del disco), con **cinco capas de altura**: el plano de la nave en el medio, y dos por encima y dos por debajo, cada una de 36° de elevación. Cada señal va en su capa, con un tallo hasta el plano: triángulo hacia arriba si está encima, hacia abajo si está debajo, rombo si está a su altura; hueca si está más lejos que el alcance. La distancia va en escala logarítmica, cuatro décadas hasta el alcance, que se cambia con − y + (de 10 km a 10¹³ km). Con el cursor (C), al pasar por una señal sale su nombre y su distancia, y un clic la fija como objetivo.

El panel **Estructura**, abajo a la derecha, es el casco visto desde arriba, zona a zona, con las zonas rojas recuadradas en rojo y lo que queda de empuje, curvatura, radiadores, maniobra y puntería (ver Casco por zonas).

Con C, el ratón pasa a manejar la interfaz: se puede elegir motor, tocar un contacto de la lista o del radar, lanzar el escáner y dar órdenes al objetivo. La nave mantiene su rumbo mientras tanto.

## Probar rápido

| Comando | Efecto |
|---|---|
| `objetivo planeta` | Fija el objetivo. También: `campo`, `estrella`, `estacion`, `estructura`, `hostil`, `neutral`, `bastion`, `refugio` |
| `saltar estructura 40` | Lleva la nave a 40 km del objetivo |
| `ir`, `central`, `usar`, `escaner` | Lo mismo que esas opciones de la rueda |
| `drones`, `recoger-drones` | Manda los drones mineros al objetivo (planeta o campo) o los hace volver |
| `estacion` | Suelta la estación origami donde está la nave (fuera de órbita y casi parada) |
| `motor crucero`, `mando 100` | Cambia de motor y acelera |
| `regalo 2` | Da 2 emisores de energía negativa y recursos |
| `creativo [si\|no]` | Modo creativo: nave invulnerable, energía, antimateria y materiales sin fin, todos los motores y niveles (también los escudos, el Campo de Amor incluido), todos los sistemas localizados y analizados. Los choques siguen frenando la nave, pero sin daño. El calor sigue contando, para probarlo. No se guarda |
| `aparecer hostil 2000 3` | Crea delante de la nave, a esa distancia y en esa cantidad: `hostil`, `neutral`, `bastion`, `estacion` o `estructura` |
| `buscar terrestre [n]` | Lleva la nave junto al primer astro (o el n-ésimo) de una clase: `terrestre`, `hielo`, `lava`, `roca`, `desierto`, `gaseoso`, `helado`, `caliente`, `luna` |
| `viajar 12` | Lleva la nave a la estrella del sistema 12 |
| `emisores 5`, `nivel armas 4` | Fija los emisores o un nivel: `escaner`, `blindaje`, `maniobra`, `computador`, `armas`, `enlace` |
| `destruir` | Destruye la anomalía objetivo |
| `escudo plasma` | Enciende un campo: `arena`, `plasma`, `em`, `amor` (si lo tienes) o `no`. Sin argumento, el siguiente, como K |
| `nivel conos 3`, `nivel nitrogeno 2` | Nivel de un escudo (`arena`, `plasma`, `em`, `conos`, `amor`) o de un refrigerante (`agua`, `hielo`, `nitrogeno`, `radiadores`); 0 lo quita |
| `calor 120` | Fija el calor; 100 es el límite |
| `refrigerante hielo no` | Enciende o apaga un refrigerante |
| `blindaje 5`, `maniobra 3`, `escaner 3` | Cambia un nivel |
| `casco 0` | Destruye la nave |
| `tiempo 600` | Acelera el tiempo |
| `vista 150 4` | Coloca la cámara: grados y km |
| `girar 0 130` | Gira la nave de golpe: grados a la derecha y hacia abajo |
| `orbitar` | Lo mismo que Órbita en la rueda |
| `disparar [torreta\|espinal\|misil\|drones]` | Dispara una familia, o todo lo listo, al objetivo |
| `apuntar [arma]` | Gira el rumbo a la solución de tiro una vez; por defecto, la del cañón espinal |
| `alinear` | Lo mismo que B |
| `computador 3` | Cambia el nivel del computador de puntería |
| `atracar`, `atracar ya`, `soltar` | Atraca en la estación objetivo; con `ya`, sin esperar al haz |
| `apie`, `abordo`, `vistapie` | Baja a la cubierta, vuelve desde cualquier punto, cambia la vista |
| `astro 5` | Fija como objetivo el astro número 5 de este sistema |
| `revelar` | Analiza todo el sistema, sin escanear |
| `trayectoria` | Muestra u oculta la línea de la ruta que lleva la nave, como en la rueda |
| `radar 3` | Alcance del radar, de 0 (10 km) a 10 (10¹³ km) |
| `dano`, `dano 9 310`, `dano 8 60 1 0 0` | Lista las zonas del casco con su integridad. Con zona y cantidad, golpea esa zona como un impacto sin escudos (el casco entero son 1000); con `x y z`, el golpe llega desde esa dirección del modelo. `dano 1 400` apaga la nave por el puente |
| `taller`, `modulo`, `modulo 0 1`, `impulsor 3` | Abre el taller. `modulo` lista ranuras y tipos; `modulo <ranura> <tipo>` monta (0 quita); `impulsor <1..4>` cambia el motor de impulso. Atracada, o en cualquier sitio en creativo |
| `disparo 0 0 1 -150 0 -380 60` | Un proyectil sin escudos que llega desde la dirección `dx dy dz` y pasa por el punto `px py pz` (m, en el modelo), con ese daño. Dice en qué zona da |

`--mode 0` a `--mode 4` salta el menú, y `--no-tutorial` quita los diálogos. `--page 0` a `--page 3` abre una página del menú, y `--quality 0` a `--quality 3` fuerza una calidad gráfica. `--save a.sav` usa otro archivo de partida y `--load` lo carga sin pasar por el menú; con `--frames` no se guarda nada salvo que se indique `--save`. `--nave a.glb` usa ese modelo para la nave.

## Cómo está hecho

| Archivo | Responsabilidad |
|---|---|
| [Balance.h](include/game/Balance.h) | **Todas las cifras** y los modos de juego |
| [Economy.h](include/game/Economy.h) | Recursos, yacimientos y lo que da cada estrella |
| [Ship.h](include/game/Ship.h) | Motores, manejo, energía y casco |
| [Stations.h](include/game/Stations.h) | La origami, las centrales, sus módulos y las estaciones gratis |
| [Anomalies.h](include/game/Anomalies.h) | Alienígenas y estructuras, con casco y arma |
| [Combat.h](include/game/Combat.h) | Proyectiles, balística con gravedad y computador de puntería |
| [Defense.h](include/game/Defense.h) | Escudos, calor y refrigeración |
| [OnFoot.h](include/game/OnFoot.h) | Cubiertas y tripulantes, a pie |
| [Humanoid.h](../engine/include/epsilon/Humanoid.h) | Motor de personas: andar, saltar y chocar, en metros |
| [Tutorial.h](include/game/Tutorial.h) | Diálogos y objetivos |
| [Session.h](include/game/Session.h) | Une todo en una partida, sin ventana ni GPU |
| [SaveGame.h](include/game/SaveGame.h) | Formato del archivo de partida |
| [GameModels.h](include/game/GameModels.h) | Modelos provisionales |
| [GameLayer.h](include/game/GameLayer.h) | Modelos, nubes, polvo, destrucción y lente |
| [warp.hlsl](shaders/warp.hlsl) | Lo que se ve dentro de la curvatura |
| [GameHud.h](include/game/GameHud.h) | Interfaz |
| [main.cpp](src/main.cpp) | Ventana, teclas y comandos |

La lógica no conoce SDL ni la GPU, así que [tests/game_tests.cpp](../tests/game_tests.cpp) juega partidas enteras sin ventana.

Los modelos son maquetas hechas por código. Para cambiar uno por el tuyo, carga un `.glb` con `LoadGltf` y `BuildModel`, y sustitúyelo en `GameAssets`. Las piezas `mod_1` a `mod_4` son las que aparecen al instalar módulos.

## Falta por decidir

1. **Mejoras de la nave:** blindaje, sensores, bodega, maniobra y escáner aún no se suben jugando.
2. **Coste de los módulos:** hoy solo piden metal. Falta dar uso al hielo, al gas y a la materia estelar.
3. **Alienígenas:** comercio con neutrales y si atacan a tus estaciones. Las armas, los escudos y los refrigerantes ya existen, pero sus niveles aún no se suben jugando (hoy, `nivel`): llegarán con los componentes de las facciones.
4. **Objetivo del juego.**
