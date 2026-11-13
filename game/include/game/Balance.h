// Balance.h - Todas las cifras del juego, en un solo sitio.
//
// Nada de lo que hay aqui esta decidido todavia: son valores provisionales para que las
// mecanicas funcionen. Las mejoras y las condiciones se ajustaran cambiando este archivo, sin
// tocar la logica. Unidades: km, km/s y horas simuladas; la energia en EJ y la masa en kt (Units.h).
#pragma once

namespace game {

// Escala de combate (docs/diseno-combate.md, fase A): los cascos, los escudos de las naves hostiles y el
// dano de las armas van multiplicados por esto respecto a las cifras de antes. Un solo numero, para
// que el casco del jugador y el de los hostiles escalen siempre igual.
inline constexpr double kCombatScale = 20.0;

// La referencia de las clases de nave (ShipClass): la fragata ligera, que es la nave del jugador, vale 1.
inline constexpr double kReferenceHull = 1000.0 * kCombatScale;
inline constexpr double kReferenceShield = 120.0 * kCombatScale;

// 1 kt de antimateria y 1 kt de materia corriente dan 2 kt de masa convertidos en energia: 1,8e23 J.
inline constexpr double kAnnihilationEnergy = 1.8e5;  // EJ por kt de antimateria

struct DriveRules {
    double impulseSpeed = 200.0;      // km/s
    // Los motores de iones propulsan con gas de la bodega. Sin gas solo queda el campo residual:
    // la velocidad maxima baja a esta parte. Con gas, cada segundo a fondo gasta ionGas kt.
    double ionNoGas = 0.5;
    double ionGas = 0.0005;           // kt por segundo a bordo con el mando a fondo
    double cruiseFraction = 0.99;     // tope del crucero, sin curvar el espacio: nunca alcanza c
    double warpFactor = 10.0;         // veces c: tope de la curvatura humana
    double hyperBase = 50.0;          // veces c, con el hiper recien montado: donde el silicio deja de bastar
    double hyperCap = 250.0;          // veces c: tope de la tecnologia recuperada de las ruinas
    int hyperEmitters = 2;            // emisores de energia negativa que pide el hiper; el primero da la hibrida
    double hyperGrowth = 1.0;         // cuanto mejora con cada emisor de mas: 50, 100, 150... c

    // Coste de la curvatura. Formar la burbuja hibrida cuesta 10^24 J. Con la metrica del hiper,
    // entre 10^16 y 10^13 J.
    double formationEnergy = 1.0e6;       // EJ: burbuja hibrida
    double hybridFormationHigh = 1.0e-2;  // EJ: burbuja del hiper, recien montado
    double hybridFormationLow = 1.0e-5;   // EJ: y al llegar al tope
    // Mantenerla gasta cada segundo la formacion dividida por esto: mas cuanto mas rapido y mas
    // tiempo lleve estable.
    double maintenanceRatioLow = 1.0e4;
    double maintenanceRatioHigh = 1.0e6;
    double maintenanceSettle = 7200.0;    // segundos de fuera: lo que tarda en estabilizarse
    double maintenanceFloor = 1.0e-9;     // EJ/s: 1 GW, lo minimo que sostiene una burbuja hibrida

    double impulseAccel = 0.5;        // km/s2 a fondo: la nave acelera, no salta a una velocidad
    double drag = 1.85e-4;            // por segundo: la nebulosa frena poco a poco, unos 90 s reales
    double response = 240.0;          // segundos simulados para que la burbuja cambie de velocidad
    double warpBrake = 20.0;          // segundos simulados para frenar la burbuja: frena mucho mas de lo que acelera
    double reverseThrust = 0.5;       // el motor hacia atras empuja esta parte
    double warpCharge = 240.0;        // segundos simulados que tarda en formarse la curvatura

    double impulseBurn = 2.0;         // energia por hora, a fondo
    double cruiseBurn = 120.0;        // energia por hora, a fondo
    double reactorRate = 2000.0;      // energia por hora que el reactor saca de la antimateria
    double emergencyFraction = 0.10;  // sin energia, aun se mueve a esta parte del impulso
    double idleRecharge = 8.0;        // energia por hora con la nave parada: evita quedarse atrapado
    double idleAntimatter = 0.5;      // kt por hora con la nave parada: lo que sintetiza del gas de la nebulosa
};

// Clases de nave (docs/diseno-combate.md, apartado 3); la tabla kShipClasses esta mas abajo.
enum class ShipClassId : int { Drone, Corvette, Frigate, Cruiser, Destroyer, Battleship, Freighter, Superfreighter, Capital, SupremeCapital, Count };
enum class ShipVariant : int { Light, Normal, Heavy, Count };

struct ShipRules {
    // La clase de la nave del jugador: una fragata ligera. Decide cuantos huecos tiene y que armas admite.
    ShipClassId classId = ShipClassId::Frigate;
    ShipVariant variant = ShipVariant::Light;
    double energy = 1000.0;
    double antimatterTank = 120.0;    // kt: unas veinte burbujas
    double startAntimatter = 30.0;    // kt: unas cinco
    double hull = kReferenceHull;     // fragata ligera: casco de referencia (1 en la tabla ShipClass)
    double radius = 0.6;             // km: lo que cuenta para un impacto
    int weaponTier = 1;               // nivel de las cuatro armas de salida
    int computerTier = 1;             // nivel del computador de punteria
    double cargo = 3000.0;
    double startSilicate = 60.0;      // kt: municion del escudo de arena para empezar
    // Propulsores de maniobra: rendijas de iones a los lados que gastan gas de la bodega. Sin gas
    // solo gira el giroscopio, mas despacio.
    double startGas = 40.0;           // kt: unos dos mil giros
    double rcsGas = 0.05;             // kt de gas por cada rad/s de giro que se gana o se pierde
    double gyroOnly = 0.35;           // parte del giro que queda sin gas
    int heatShieldTier = 1;           // decide a que estrellas se puede entrar
    double turnRate = 0.20;           // rad/s reales: es una nave enorme, gira despacio
    double turnAccel = 0.14;          // rad/s2 reales: y le cuesta empezar y parar
    double driftTime = 6.0;           // s reales: tarda en seguir su nuevo rumbo; derrapa
    double handlingBonus = 0.5;       // cuanto mejora cada nivel de maniobra
    double aimLead = 0.65;            // radianes: lo mas que se puede apuntar por delante del casco
    double clearance = 1.15;          // radios: el piloto automatico no baja de esta altura
    double shatterSpeed = 1.0;        // km/s contra un cuerpo: a partir de aqui, la nave se deshace
    double heatReach = 4.0;           // radios de una estrella: mas cerca, el calor quema el casco
    double heatBurn = 0.11 * kCombatScale;  // casco por segundo a una vez la intensidad limite
    double sensorRange = 8.0e7;       // km
    double identifyRange = 6.0e6;     // km: mas lejos, solo una senal sin identificar
    double interactRange = 5.0e4;     // km para recoger de una estacion o explorar
    double repairPerHour = 60.0 * kCombatScale;  // en orbita de una estacion de energia propia
    double bow = 0.55;                // km del centro de la nave a la punta de la proa
    double beam = 0.2;                // km: medio ancho del casco, para rozar estructuras
    double dockCorridor = 1.5;        // km: el haz trae la nave hasta aqui, frente a la escotilla, y entra recta
    double dockTime = 2.5;            // s reales: cada tanto se acerca un factor e
    // A menos de esto de una estacion, el tiempo corre como el real y mandan los propulsores finos.
    double maneuverRange = 25.0;      // km
    double maneuverTime = 15.0;       // s: acercandose, no mas rapido que la distancia entre esto
    double maneuverMinimum = 0.02;    // km/s: 20 m/s junto al casco
    double maneuverLeave = 12.0;      // s: alejandose o rodeandola
    double maneuverLeaveMinimum = 0.12;  // km/s
    // Cerca de una estacion: la velocidad respecto a ella no pasa de la distancia dividida por esto,
    // en segundos reales (el tiempo se frena al acercarse).
    double approachTime = 12.0;       // s reales, acercandose, a menos de 3000 km
    double recedeTime = 6.0;          // s reales, alejandose: salir cuesta menos que llegar
    double dockZone = 80.0;           // km: zona de atraque
    double driftDamping = 2.0;        // s reales: sin mando, la deriva junto a una estacion cae un factor e
};

struct FootRules {
    double gravity = 9.81;            // m/s2 en las cubiertas: gravedad artificial
    double boardRange = 3.5;          // m a la esclusa para volver a la nave
    int crew = 3;                     // personas en cada cubierta
};

struct StationRules {
    int freePower = 1;                // por debajo de este numero, desplegar no cuesta nada
    double powerMetal = 300.0, powerSilicate = 200.0;
    double deployRange = 4.0e4;       // km al borde de la orbita
    int moduleSlots = 4;
    int moduleMaxLevel = 5;
    double moduleBaseCost = 100.0;    // metal; cada nivel multiplica por moduleCostGrowth
    double moduleCostGrowth = 1.8;

    double orbitRate = 0.005;         // radianes por hora; debe ser alcanzable a impulso

    double acceleratorEfficiency = 0.1;  // parte de la energia captada que acaba siendo antimateria
    double antimatterStorage = 60.0;     // kt
    double matterStorage = 300.0;
    double collectorBonus = 0.5;
    double capacitorBonus = 0.75;
    double scoopBonus = 0.75;

    // Estacion origami: viaja plegada en las bodegas y se despliega dentro de un sistema.
    double shipyardDeploySpeed = 2.0;  // km/s: hay que estar casi parado respecto a lo que domina aqui
    double shipyardSlide = 1.6;        // km que se aparta hacia popa, ya abierta, antes de sacar los brazos
    double shipyardGoal = 1500.0;      // kt de material: la referencia de la barra, lo que pedira la primera nave minera
};

// Drones mineros: salen de sus bahias (modulo exterior) a un planeta o un campo de asteroides, minan
// hasta llenar la bodega, vuelven, descargan en la nave y repiten hasta que se les recoge.
struct DroneRules {
    int perBay = 2;               // drones por bahia
    double speed = 12.0;          // km/s simulados: un dron ionico ligero
    double hold = 20.0;           // kt de mineral por viaje
    double rate = 18.0;           // kt por hora simulada, con riqueza 1
    double range = 4.0e4;         // km al borde del astro para mandarlos
    double leash = 2.0e5;         // km: si la nave se aleja mas del sitio, vuelven
    double recovery = 0.6;        // extraccion selectiva: fraccion del elemento que se recupera del mineral
};

// Lo que se dispara. Cada familia hace un tipo de dano distinto, para que los escudos de la
// etapa siguiente tengan donde elegir.
// Explosivo y nuclear son los de los misiles humanos (Armament.h); van al final para no cambiar los numeros.
enum class DamageType : int { Kinetic, Photonic, Antimatter, Thermal, Explosive, Nuclear, Count };
inline constexpr int kDamageTypeCount = static_cast<int>(DamageType::Count);
enum class WeaponKind : int { Turret, Spinal, Missile, Drone, Count };

struct WeaponRules {
    const char* name;
    DamageType type;
    double damage;       // casco por impacto, con el nivel 1
    double speed;        // km/s; en los guiados, la de crucero
    double range;        // km
    double cooldown;     // segundos simulados entre disparos
    double energy;       // EJ por disparo
    double antimatter;   // kt por disparo
    double metal;        // kt por disparo
    double proximity;    // km: a esta distancia del blanco ya cuenta como impacto
    double life;         // segundos simulados que vuela antes de perderse
    double turn;         // rad/s: lo que giran los guiados; 0 si no lo son
    int count;           // proyectiles por disparo
};

struct CombatRules {
    // Torreta: cadencia y alcance corto, sirve de defensa. Canon espinal: fijo al eje del casco,
    // un haz de luz que viaja a c y se curva junto a las estrellas. Misil: cabeza de antimateria
    // guiada, lenta y con mucho alcance. Drones: un enjambre barato que quema el casco.
    // El dano va por kCombatScale (x20): lo usan igual el jugador y los alienigenas.
    WeaponRules weapons[4] = {
        {"Torreta", DamageType::Kinetic, 20.0 * kCombatScale, 4000.0, 1.5e5, 3.0, 0.4, 0.0, 0.0, 10.0, 60.0, 0.0, 1},
        {"Canón espinal", DamageType::Photonic, 260.0 * kCombatScale, 299792.458, 5.0e5, 40.0, 60.0, 0.0, 0.0, 40.0, 5.0, 0.0, 1},
        {"Misil", DamageType::Antimatter, 180.0 * kCombatScale, 1500.0, 1.2e6, 10.0, 0.05, 0.2, 0.0, 30.0, 900.0, 0.05, 1},
        {"Drones", DamageType::Thermal, 18.0 * kCombatScale, 900.0, 6.0e5, 25.0, 0.2, 0.0, 1.0, 4.0, 600.0, 0.12, 6},
    };
    double tierDamage = 0.4;        // cada nivel de arma suma esta parte del dano base
    double tierCooldown = 0.1;      // y recarga mas deprisa
    double computerError = 1.5e-4;  // rad de error de puntería con el computador de nivel 1
    double computerErrorPower = 1.5;  // el error baja con el nivel elevado a esto
    int bodiesPerTier = 2;          // astros que el computador cuenta al calcular la trayectoria
    int maxTier = 5;
    double gravityScale = 1.0;      // 1 = la gravedad real: junto a un planeta o una estrella se nota
    double defenseRate = 0.25;      // probabilidad por segundo, por nivel, de que una torreta derribe un guiado
    double defenseRange = 8.0e4;    // km
    double jitter = 0.25;           // variacion del tiempo de recarga de los alienigenas
};

// Escudos. Arena, plasma, electromagnetico y amor son campos: solo uno encendido a la vez. Los
// conos ablativos son placas en la proa, sin energia: van siempre puestos y se suman al campo.
enum class ShieldKind : int { Dust, Plasma, EM, Ablative, Love, Count };

struct ShieldRules {
    const char* name;
    double absorb[kDamageTypeCount];   // parte que para de cada tipo de dano: cinetico, fotonico, antimateria, termico, explosivo, nuclear
    double capacity;    // dano que aguanta lleno, con el nivel 1
    double regen;       // por segundo a bordo, encendido; los conos no se regeneran solos
    double energy;      // por hora, encendido
    double heat;        // calor por segundo encendido; negativo, lo absorbe
};

struct DefenseRules {
    ShieldRules shields[5] = {
        // Arena: buena contra haces y calor; los proyectiles rapidos la atraviesan. Gasta silicato.
        // Capacidad y regeneracion van por kCombatScale, igual que el dano y el casco.
        {"Arena magnética", {0.15, 0.90, 0.40, 0.85, 0.20, 0.40},350.0 * kCombatScale, 3.0 * kCombatScale, 20.0, -0.15},
        // Plasma: vaporiza lo fisico, pero la nave brilla y se la ve desde muy lejos.
        {"Plasma", {0.95, 0.30, 0.30, 0.10, 0.80, 0.30},300.0 * kCombatScale, 2.5 * kCombatScale, 60.0, 0.15},
        // Electromagnetico: para la antimateria y ciega los sensores enemigos; nulo contra lo cinetico.
        {"Electromagnético", {0.00, 0.55, 0.95, 0.50, 0.10, 0.60},400.0 * kCombatScale, 3.5 * kCombatScale, 900.0, 0.03},
        // Conos ablativos: placas de titanio y aerogel en la proa. Pesan y se gastan con el gas.
        {"Conos ablativos", {0.60, 0.30, 0.30, 0.50, 0.55, 0.40},500.0 * kCombatScale, 0.0, 0.0, 0.0},
        // Campo de Amor: de los Unicornios. Para todo y lo devuelve.
        {"Campo de Amor", {1.0, 1.0, 1.0, 1.0, 1.0, 1.0},1.0e9, 1.0e9, 0.0, 0.0},
    };
    double tierCapacity = 0.5;     // cada nivel suma esta parte de la capacidad y de la regeneracion
    // Lo que se gasta por punto de dano baja con la escala: cada impacto cuesta lo de siempre.
    double dustPerDamage = 0.05 / kCombatScale;   // silicato por punto de dano parado
    double dustUpkeep = 2.0;       // silicato por hora, encendido
    double plasmaSight = 3.0;      // con plasma, los hostiles ven la nave desde tantas veces mas lejos
    double plasmaRock = 0.5;       // km: los asteroides menores se vaporizan al chocar
    double emJamming = 3.0;        // el error de punteria enemigo se multiplica por esto
    double ablativeMass = 0.3;     // con las placas enteras, la maniobra se divide por 1 + esto
    double ablativeWear = 0.002 * kCombatScale;   // placas por segundo, por densidad de nebulosa y por fraccion de c
    double ramPressure = 0.4 * kCombatScale;      // casco por segundo real en curvatura a 10 c, con densidad 1
    double loveHeal = 0.5;         // parte del dano que el Amor vuelve casco; el resto, energia
    double plateRepair = 120.0 * kCombatScale;    // placas por hora en una estacion propia
    double plateMetal = 0.25 / kCombatScale;      // metal por placa reparada
};

// Calor. 100 es el limite: por encima se apagan escudos y armas y el casco sufre.
enum class CoolantKind : int { Water, Ice, Nitrogen, Radiator, Count };

struct ThermalRules {
    double limit = 100.0;
    double maximum = 150.0;
    double weaponHeat[4] = {1.0, 25.0, 4.0, 6.0};  // por disparo: torreta, espinal, misil, drones
    double formHeat = 30.0;        // formar una burbuja de curvatura
    double warpHeat = 0.04;        // por segundo a bordo, con la burbuja a 10 c; sube con la raiz de la velocidad
    double absorbHeat = 0.05 / kCombatScale;      // por punto de dano que para un escudo (baja con la escala)
    double overheatBurn = 0.02 * kCombatScale;    // casco por segundo y por punto por encima del limite
    double starReach = 12.0;       // radios de estrella: mas cerca, los radiadores pierden eficacia
    double starHeat = 0.3;         // calor por segundo que entra en la zona de calor de una estrella
    double coolStart = 0.4;        // los refrigerantes se gastan por encima de esta parte del limite
    double tierFlow = 0.5;         // cada nivel suma esta parte al caudal
    // Agua: barata, poca capacidad. Se rellena al fundir hielo y en las estaciones.
    double waterHeat = 4.0, waterFlow = 1.5, waterTank = 60.0;
    // Hielo de la bodega: se funde en la nave, y el agua que deja va al deposito.
    double iceHeat = 6.0, iceFlow = 1.0;
    // Nitrogeno liquido: mucha capacidad; se evapora solo y se licua del gas con energia.
    double nitrogenHeat = 10.0, nitrogenFlow = 4.0, nitrogenTank = 40.0;
    double nitrogenBoil = 2.0e-5;  // parte del deposito que se evapora por segundo
    double nitrogenMake = 20.0;    // unidades por hora, con gas en la bodega
    double nitrogenEnergy = 8.0;   // energia por unidad licuada
    // Radiadores infrarrojos: sin consumo, lentos, inutiles junto a una estrella.
    double radiatorRate = 0.35;    // calor por segundo con el nivel 1
    double stationRefill = 60.0;   // agua por hora en una estacion propia, del hielo de la bodega
};

// Clases de nave (docs/diseno-combate.md, apartado 3). Cada clase tiene tres variantes. Las cifras son
// propuestas: se ajustan aqui, en la tabla kShipClasses, sin tocar la logica.
//   hull y shield: veces el casco y el escudo de la fragata ligera, que es la nave del jugador
//                  (kReferenceHull y kReferenceShield; la fragata ligera vale 1).
//   length:        eslora en km. Los modelos de dron, corveta y fragata (fase E) se hacen a esta medida.
//   speed:         velocidad maxima en km/s.
//   weaponSlots:   huecos de armas. heavyWeapons: si admite armas pesadas.
inline constexpr int kShipClassCount = static_cast<int>(ShipClassId::Count);
inline constexpr int kShipVariantCount = static_cast<int>(ShipVariant::Count);

struct ShipClass {
    double length;       // km
    double hull;         // veces el casco de la fragata ligera
    double shield;       // veces su escudo
    double speed;        // km/s
    int weaponSlots;
    bool heavyWeapons;
};

inline constexpr const char* kShipClassNames[kShipClassCount] = {
    "Dron", "Corveta", "Fragata", "Crucero", "Destructor", "Acorazado", "Carguero", "Supercarguero", "Capital", "Capital suprema"};
inline constexpr const char* kShipVariantNames[kShipVariantCount] = {"ligera", "normal", "pesada"};

// {eslora, casco, escudo, velocidad, huecos, armas pesadas} por clase y variante (ligera, normal, pesada).
inline constexpr ShipClass kShipClasses[kShipClassCount][kShipVariantCount] = {
    /* Dron */           {{0.027, 0.007, 0.007, 600.0, 1, false},   {0.034, 0.01, 0.01, 500.0, 1, false},    {0.045, 0.015, 0.015, 420.0, 1, false}},
    /* Corveta */        {{0.25, 0.105, 0.105, 450.0, 2, false},    {0.30, 0.15, 0.15, 400.0, 2, false},     {0.40, 0.225, 0.25, 340.0, 3, false}},
    /* Fragata */        {{1.2, 1.0, 1.0, 300.0, 6, false},         {1.5, 1.4, 1.4, 260.0, 7, false},        {1.9, 2.1, 2.2, 220.0, 9, true}},
    /* Crucero */        {{2.2, 3.0, 3.0, 240.0, 10, false},        {2.8, 4.3, 4.5, 210.0, 12, false},       {3.6, 6.5, 7.0, 180.0, 16, true}},
    /* Destructor */     {{3.0, 5.0, 5.0, 230.0, 14, false},        {3.8, 7.0, 7.5, 200.0, 18, false},       {5.0, 10.5, 11.0, 170.0, 24, true}},
    /* Acorazado */      {{5.0, 14.0, 15.0, 180.0, 25, false},      {6.5, 20.0, 22.0, 150.0, 32, true},      {8.5, 30.0, 34.0, 120.0, 42, true}},
    /* Carguero */       {{6.0, 20.0, 10.0, 150.0, 20, false},      {8.0, 30.0, 15.0, 120.0, 26, true},      {11.0, 45.0, 24.0, 100.0, 34, true}},
    /* Supercarguero */  {{15.0, 70.0, 60.0, 100.0, 70, false},     {20.0, 100.0, 90.0, 80.0, 100, true},    {28.0, 150.0, 140.0, 65.0, 130, true}},
    /* Capital */        {{30.0, 200.0, 250.0, 80.0, 250, false},   {40.0, 300.0, 400.0, 65.0, 350, true},   {55.0, 450.0, 600.0, 50.0, 450, true}},
    /* Capital suprema */{{90.0, 2000.0, 2500.0, 60.0, 450, false}, {120.0, 3000.0, 4000.0, 50.0, 600, true}, {170.0, 4500.0, 6500.0, 40.0, 800, true}},
};

inline constexpr const ShipClass& ShipClassOf(ShipClassId id, ShipVariant variant) {
    return kShipClasses[static_cast<int>(id)][static_cast<int>(variant)];
}

// Armamento humano (docs/diseno-combate.md, fase B). Ver Armament.h para las reglas de montaje.
// Cada tipo tiene variante ligera y pesada; la pesada solo la montan las clases con heavyWeapons.
//   mass:     kt de metal a granel (masa real de la pieza); lo escaso son los elementos de la tabla.
//   elements: kg de cada elemento que pide montarla (Z = numero atomico).
//   Misiles:  capacity misiles por bahia; cada misil cuesta missileEnergy y missileMass de metal, mas sus elementos.
enum class ArmType : int { Coilgun, RailSpinal, Rail360, MissileBay, NuclearMissile, Count };
inline constexpr int kArmTypeCount = static_cast<int>(ArmType::Count);
inline constexpr int kHumanWeaponCount = 14;

struct ElementCost {
    int z;
    double kg;
};

struct HumanWeaponSpec {
    const char* name;
    const char* description;
    ArmType type;
    bool heavy;
    int capacity;            // misiles que lleva; 0 si no dispara misiles
    DamageType damageType;
    double damage;           // casco por impacto
    double speed;            // km/s; en los misiles, la de crucero
    double range;            // km
    double cooldown;         // segundos simulados entre disparos
    double energy;           // EJ por disparo
    double heat;             // calor por disparo
    double proximity;        // km
    double life;             // segundos simulados de vuelo
    double turn;             // rad/s; 0 si no es guiado
    double aimError;         // multiplica el error del computador de punteria
    double mass;             // kt de metal para montarla
    ElementCost elements[4];  // kg que pide montarla; z = 0 no cuenta
    ElementCost missile[3];   // kg que pide cada misil; z = 0 no cuenta
    double missileEnergy;    // EJ por misil fabricado
    double missileMass;      // kt de metal por misil
};

struct ArmamentRules {
    // Orden fijo: coilgun, railgun espinal, railgun 360, bahias de 10, 25 y 50 (ligeras y luego pesadas), misil nuclear.
    // Cada par es {ligera, pesada}; el orden es el de ShipModuleKind::CoilgunLight en adelante.
    HumanWeaponSpec weapons[kHumanWeaponCount] = {
        {"Coilgun ligera", "Débil pero de cadencia alta: bobinas de niobio aceleran un proyectil metálico.", ArmType::Coilgun, false, 0, DamageType::Kinetic,
         150.0, 3000.0, 1.2e5, 0.8, 0.2, 1.2, 8.0, 60.0, 0.0, 1.0, 0.2, {{74, 2000.0}, {75, 150.0}, {27, 1500.0}, {41, 800.0}}, {}, 0.0, 0.0},
        {"Coilgun pesada", "La coilgun de las naves grandes: mismo ritmo, golpe mucho mayor.", ArmType::Coilgun, true, 0, DamageType::Kinetic,
         600.0, 3500.0, 1.6e5, 0.8, 0.6, 3.0, 10.0, 60.0, 0.0, 1.0, 0.6, {{74, 6000.0}, {75, 450.0}, {27, 4500.0}, {41, 2400.0}}, {}, 0.0, 0.0},
        {"Railgun espinal ligera", "Fija al eje del casco: hay que girar la nave entera hacia el blanco. Buena de lejos, mala de cerca.", ArmType::RailSpinal, false, 0, DamageType::Kinetic,
         4000.0, 30000.0, 6.0e5, 12.0, 25.0, 20.0, 25.0, 40.0, 0.0, 0.5, 0.8, {{74, 6000.0}, {75, 500.0}, {27, 4000.0}, {41, 3000.0}}, {}, 0.0, 0.0},
        {"Railgun espinal pesada", "La más potente: solo en cascos pesados, y solo una por nave.", ArmType::RailSpinal, true, 0, DamageType::Kinetic,
         12000.0, 40000.0, 9.0e5, 18.0, 80.0, 50.0, 30.0, 40.0, 0.0, 0.5, 1.7, {{74, 12600.0}, {75, 1050.0}, {27, 8400.0}, {41, 6300.0}}, {}, 0.0, 0.0},
        {"Railgun 360 ligera", "En torreta móvil: apunta a cualquier lado, pero es peor en todo que la espinal.", ArmType::Rail360, false, 0, DamageType::Kinetic,
         1400.0, 15000.0, 3.0e5, 7.0, 10.0, 10.0, 15.0, 50.0, 0.0, 1.5, 0.5, {{74, 3500.0}, {75, 300.0}, {27, 2500.0}, {41, 1800.0}}, {}, 0.0, 0.0},
        {"Railgun 360 pesada", "Torreta móvil de las naves grandes.", ArmType::Rail360, true, 0, DamageType::Kinetic,
         4000.0, 18000.0, 4.0e5, 10.0, 30.0, 25.0, 20.0, 50.0, 0.0, 1.5, 1.2, {{74, 8400.0}, {75, 720.0}, {27, 6000.0}, {41, 4300.0}}, {}, 0.0, 0.0},
        {"Bahía de misiles ligera (10)", "Diez misiles explosivos guiados. Se derriban con defensa puntual.", ArmType::MissileBay, false, 10, DamageType::Explosive,
         2000.0, 1200.0, 1.0e6, 5.0, 0.02, 3.0, 25.0, 800.0, 0.06, 1.0, 0.25, {}, {{78, 20.0}, {46, 15.0}, {77, 5.0}}, 2.0, 0.004},
        {"Bahía de misiles ligera (25)", "Veinticinco misiles explosivos guiados.", ArmType::MissileBay, false, 25, DamageType::Explosive,
         2000.0, 1200.0, 1.0e6, 5.0, 0.02, 3.0, 25.0, 800.0, 0.06, 1.0, 0.45, {}, {{78, 20.0}, {46, 15.0}, {77, 5.0}}, 2.0, 0.004},
        {"Bahía de misiles ligera (50)", "Cincuenta misiles explosivos guiados.", ArmType::MissileBay, false, 50, DamageType::Explosive,
         2000.0, 1200.0, 1.0e6, 5.0, 0.02, 3.0, 25.0, 800.0, 0.06, 1.0, 0.8, {}, {{78, 20.0}, {46, 15.0}, {77, 5.0}}, 2.0, 0.004},
        {"Bahía de misiles pesada (10)", "Diez misiles pesados, con triple carga explosiva.", ArmType::MissileBay, true, 10, DamageType::Explosive,
         6000.0, 1000.0, 1.2e6, 8.0, 0.05, 8.0, 30.0, 900.0, 0.05, 1.0, 0.4, {}, {{78, 60.0}, {46, 45.0}, {77, 15.0}}, 6.0, 0.012},
        {"Bahía de misiles pesada (25)", "Veinticinco misiles pesados.", ArmType::MissileBay, true, 25, DamageType::Explosive,
         6000.0, 1000.0, 1.2e6, 8.0, 0.05, 8.0, 30.0, 900.0, 0.05, 1.0, 0.7, {}, {{78, 60.0}, {46, 45.0}, {77, 15.0}}, 6.0, 0.012},
        {"Bahía de misiles pesada (50)", "Cincuenta misiles pesados.", ArmType::MissileBay, true, 50, DamageType::Explosive,
         6000.0, 1000.0, 1.2e6, 8.0, 0.05, 8.0, 30.0, 900.0, 0.05, 1.0, 1.2, {}, {{78, 60.0}, {46, 45.0}, {77, 15.0}}, 6.0, 0.012},
        {"Misil nuclear ligero", "Un solo misil nuclear por torreta, de un solo uso y muy caro.", ArmType::NuclearMissile, false, 1, DamageType::Nuclear,
         12000.0, 1000.0, 1.2e6, 20.0, 0.05, 6.0, 60.0, 900.0, 0.04, 1.0, 0.2, {}, {{92, 60.0}, {90, 20.0}, {0, 0.0}}, 40.0, 0.006},
        {"Misil nuclear pesado", "Cabeza nuclear de gran rendimiento para naves pesadas.", ArmType::NuclearMissile, true, 1, DamageType::Nuclear,
         40000.0, 1000.0, 1.2e6, 20.0, 0.1, 12.0, 80.0, 900.0, 0.04, 1.0, 0.5, {}, {{92, 150.0}, {90, 50.0}, {0, 0.0}}, 100.0, 0.015},
    };
    // El dano de esta tabla ya esta en la escala de combate (kCombatScale = 20).

    // Railgun espinal: no la admite cualquier clase (los cargueros no tienen eje util) y solo cabe una.
    bool spinalClass[kShipClassCount] = {false, false, true, true, true, true, false, false, true, true};
    int spinalMax = 1;

    // Defensa puntual: puntos repartidos por la nave (tantos por hueco de arma de la clase) y plataformas
    // montables en huecos de torreta clasica. Probabilidad por segundo de derribar un guiado cercano.
    int pointsPerSlot = 5;             // fragata ligera: 6 huecos x 5 = 30 puntos
    double pointRate = 0.04;           // por punto
    double platformRate = 0.6;         // por plataforma
    double pointMass = 0.02;           // kt de metal por punto
    double platformMass = 0.15;        // kt de metal por plataforma

    // Planos: se fabrican en la estacion origami. Energia (EJ) por tipo, triple si es pesada; el metal es un decimo.
    double blueprintEnergy[kArmTypeCount] = {40.0, 120.0, 90.0, 80.0, 300.0};
    double blueprintHeavy = 3.0;
    double blueprintMetal = 0.1;       // kt de metal por EJ de energia

    double refund = 0.5;               // parte que se recupera al desmontar
    int starterBays = 2;               // bahias de drones de serie: se mueven, no se sacan
};

// Estaciones alienigenas (docs/diseno-combate.md, fase F): seis niveles. El 6 es el planeta coraza.
inline constexpr int kStationLevels = 6;
inline constexpr const char* kStationLevelNames[kStationLevels] = {
    "Drones de defensa", "Defensa industrial ligera", "Urbana", "Hangares militares", "Defensa suprema", "Planeta coraza"};
inline constexpr int kHangarLevel = 4;       // destruirla extiende la alerta
inline constexpr int kArmorPlanetLevel = 6;

struct AnomalyRules {
    double hostileSightRange = 3.0e6;   // km
    double hostileWeaponRange = 1.0e5;  // km: distancia a la que se queda para disparar
    // El casco y el escudo de una nave alienigena salen de su clase (ShipClass) por estas referencias.
    double referenceHull = kReferenceHull;      // casco de la fragata ligera, el mismo que el del jugador
    double referenceShield = kReferenceShield;  // y su escudo: arena, plasma o EM, segun la nave
    // Vida de cada nivel de estacion, en veces el casco de la fragata ligera. El nivel 2 (x30) esta
    // decidido; los demas son propuesta.
    double stationLevelHull[kStationLevels] = {10.0, 30.0, 100.0, 300.0, 1500.0, 30000.0};
    // Reparto de niveles 1 a 5 entre las estaciones generadas (propuesta): por su id, sin numeros aleatorios.
    double stationLevelShare[kStationLevels - 1] = {0.35, 0.30, 0.20, 0.10, 0.05};
    double stationShield = 900.0 * kCombatScale;  // el de una estacion, siempre electromagnetico
    // Planeta coraza: del tamano de Jupiter, inmovil y muy raro (propuesta salvo el radio).
    double armorPlanetRadius = 70000.0;   // km
    double armorPlanetSight = 2.0e6;      // km desde su superficie: dentro, te ve y dispara
    double armorPlanetChance = 0.33;      // parte de las galaxias que tienen uno
    int armorPlanetSignature = 4;         // nivel de escaner que lo encuentra
    double shieldRegen = 0.01;          // parte de su escudo que recuperan por segundo
    double shipLoot = 12.0;             // kt de metal que sueltan al destruirse
    double stationLoot = 90.0;
    double hostileSpeed = 300.0;        // km/s; por debajo de la curvatura, para poder huir
    double patrolSpeed = 40.0;
    double exploreHours = 2.0;          // lo que tarda sacar el emisor de una estructura
    double activeRange = 4.0e9;         // km: mas lejos no se calcula su movimiento
    // La primera estructura del sistema inicial, a esta distancia de su estrella, en UA. El tutorial
    // lleva a la orbita de la estrella para la central, y un barrido desde alli con el nivel 1 alcanza
    // 6 UA (ScannerRules::range): 4 UA deja un tercio de margen para el radio de la orbita y para una
    // estrella doble. Mas cerca de 1,5 UA quedaria pegada a la estrella y se encontraria sin buscarla.
    double firstDerelictMinAu = 1.5;
    double firstDerelictMaxAu = 4.0;
};

// Alerta de la especie (fase F): se guarda por sistema y sube por regiones. Cifras en propuesta, salvo
// los grupos de 4 o 10, los 20 sistemas del hangar y el +50 % de la revancha.
struct AlertRules {
    int regionSize = 4;                 // sistemas por region (4 o 10)
    int hangarSpread = 20;              // un hangar militar destruido la lleva a los 20 sistemas mas cercanos
    double destroyRaise[kStationLevels] = {0.02, 0.08, 0.2, 0.4, 0.8, 1.0};  // por nivel de estacion destruida
    double shipRaise = 0.01;            // por nave destruida
    double attackRaise = 0.01;          // por cada casco de fragata ligera de dano que hace el jugador
    double decayPerHour = 0.02;         // lo que baja por hora simulada (exponencial)
    double calmDecayPerHour = 0.1;      // y si lleva calmHours sin atacar alli
    double calmHours = 24.0;
    double surrenderHours = 6.0;        // horas simuladas de rendicion temporal
    double revengeFactor = 1.5;         // la siguiente vez, +50 % en casco, escudo, dano y velocidad
};

struct ScannerRules {
    double seconds = 30.0;         // segundos reales que dura un barrido, sin depender de la aceleracion del tiempo
    double energy = 160.0;         // lo que gasta
    double range = 9.0e8;          // km con el nivel 1, unas 6 UA
    double rangePerTier = 0.8;     // cada nivel suma esta parte de lo anterior
    double systemRange = 1.6e12;   // km a los que encuentra otros sistemas, unos 0,17 anos luz
    double bodyRange = 6.0e9;      // km a los que analiza astros de este sistema, unas 40 UA
    int startTier = 1;
    // Eco frio: mas alla del alcance, el barrido oye algo frio sin resolverlo y da su rumbo y su
    // distancia aproximados. El radio sale del alcance, asi que crece con el nivel y las antenas: con
    // el nivel 1, unas 24 UA, mas de la mitad del radio de un sistema inicial (35 a 80 UA).
    double echoFactor = 4.0;          // radio de eco, en veces el alcance
    double echoBearingError = 25.0;   // grados que puede errar el rumbo con el nivel 1
    double echoDistanceError = 0.3;   // parte que puede errar la distancia con el nivel 1
    double echoErrorPerTier = 0.5;    // cada nivel de mas multiplica los dos errores por esto
};

struct WorldRules {
    double timeScale = 60.0;      // segundos simulados por segundo real
    double fogDistance = 4.0e5;   // km
    double starSpacing = 0.02;    // de 0,06 a 0,16 anos luz entre estrellas
    int systems = 40;
    double gravityBoost = 1.0e5;  // la dilatacion real junto a una estrella no se notaria; se exagera
    double maxTimeFactor = 60.0;  // lo mas que puede adelantarse el exterior
    double wreckTime = 0.02;      // el tiempo se frena al perder la nave
    double dangerTime = 0.15;     // y en combate o con una colision encima
    double stationTime = 0.3;     // y junto a una estacion, para maniobrar
    double stationNear = 3000.0;  // km
    // Una nave cuyo jugador se desconecta no sale del mundo hasta llevar este tiempo real sin combate:
    // sin recibir ni hacer fuego y sin un hostil a tiro. Mientras, sigue ahi, parada y vulnerable.
    double logoutCalm = 15.0;     // s reales
};

// Modo cinematica (propuesta): tras cada impacto del jugador en un blanco, la camara lo rodea
// mirando al punto del golpe y el tiempo va mas despacio.
struct CinematicRules {
    double timeFactor = 0.25;  // el tiempo, a esta parte de lo normal
    double seconds = 6.0;      // segundos reales que dura cada toma
    double orbitRate = 0.35;   // rad por segundo real que gira la camara alrededor del blanco
    double distance = 2.2;     // la camara, a estas veces el tamano del blanco
    double minimum = 0.25;     // km: y nunca mas cerca que esto
};

struct Balance {
    DriveRules drive;
    ShipRules ship;
    FootRules foot;
    StationRules stations;
    DroneRules drones;
    ArmamentRules armament;
    AnomalyRules anomalies;
    AlertRules alert;
    CombatRules combat;
    DefenseRules defense;
    ThermalRules thermal;
    ScannerRules scanner;
    WorldRules world;
    CinematicRules cinematic;
};

// Modos de juego. Cambian lo que cuesta equivocarse, no las mecanicas.
enum class GameModeId : int { Peaceful, Easy, Normal, Hard, Realistic, Count };

struct GameMode {
    const char* name;
    const char* description;
    bool hostilesAttack;
    double damage;        // multiplica el dano recibido
    double cargoLoss;     // parte de la bodega que se pierde al reaparecer tras perder la nave
    bool permadeath;      // true = se pierde todo y se empieza de cero
};

inline const GameMode& ModeInfo(GameModeId id) {
    static const GameMode modes[] = {
        {"Pac\u00EDfico", "Nada te ataca ni te da\u00F1a. Solo explorar y construir.", false, 0.0, 0.0, false},
        {"F\u00E1cil", "Da\u00F1o reducido. Reapareces en tu \u00FAltima estaci\u00F3n sin perder carga.", true, 0.5, 0.0, false},
        {"Normal", "Reapareces en tu \u00FAltima estaci\u00F3n y pierdes un cuarto de la bodega.", true, 1.0, 0.25, false},
        {"Dif\u00EDcil", "Da\u00F1o aumentado. Pierdes la mitad de la bodega.", true, 1.6, 0.5, false},
        {"Realista", "Una sola nave. Si la pierdes, lo pierdes todo.", true, 1.6, 1.0, true},
    };
    return modes[static_cast<int>(id)];
}

}  // namespace game
