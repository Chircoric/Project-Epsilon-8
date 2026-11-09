// Balance.h - Todas las cifras del juego, en un solo sitio.
//
// Nada de lo que hay aqui esta decidido todavia: son valores provisionales para que las
// mecanicas funcionen. Las mejoras y las condiciones se ajustaran cambiando este archivo, sin
// tocar la logica. Unidades: km, km/s y horas simuladas; la energia en EJ y la masa en kt (Units.h).
#pragma once

namespace game {

// 1 kt de antimateria y 1 kt de materia corriente dan 2 kt de masa convertidos en energia: 1,8e23 J.
inline constexpr double kAnnihilationEnergy = 1.8e5;  // EJ por kt de antimateria

struct DriveRules {
    double impulseSpeed = 200.0;      // km/s
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

struct ShipRules {
    double energy = 1000.0;
    double antimatterTank = 120.0;    // kt: unas veinte burbujas
    double startAntimatter = 30.0;    // kt: unas cinco
    double hull = 1000.0;
    double radius = 0.6;              // km: lo que cuenta para un impacto
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
    double heatBurn = 0.11;           // casco por segundo a una vez la intensidad limite
    double sensorRange = 8.0e7;       // km
    double identifyRange = 6.0e6;     // km: mas lejos, solo una senal sin identificar
    double interactRange = 5.0e4;     // km para recoger de una estacion o explorar
    double repairPerHour = 60.0;      // en orbita de una estacion de energia propia
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
    int freeMining = 3;               // por debajo de este numero, desplegar no cuesta nada
    int freePower = 1;
    double miningMetal = 150.0, miningSilicate = 80.0;
    double powerMetal = 300.0, powerSilicate = 200.0;
    double deployRange = 4.0e4;       // km al borde de la orbita
    int moduleSlots = 4;
    int moduleMaxLevel = 5;
    double moduleBaseCost = 100.0;    // metal; cada nivel multiplica por moduleCostGrowth
    double moduleCostGrowth = 1.8;

    double miningRate = 30.0;         // unidades por hora con composicion pura
    double orbitRate = 0.005;         // radianes por hora; debe ser alcanzable a impulso
    double miningStorage = 900.0;
    double drillBonus = 0.5;          // por nivel de modulo
    double hopperBonus = 0.75;

    double acceleratorEfficiency = 0.1;  // parte de la energia captada que acaba siendo antimateria
    double antimatterStorage = 60.0;     // kt
    double matterStorage = 300.0;
    double collectorBonus = 0.5;
    double capacitorBonus = 0.75;
    double scoopBonus = 0.75;
};

// Lo que se dispara. Cada familia hace un tipo de dano distinto, para que los escudos de la
// etapa siguiente tengan donde elegir.
enum class DamageType : int { Kinetic, Photonic, Antimatter, Thermal, Count };
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
    WeaponRules weapons[4] = {
        {"Torreta", DamageType::Kinetic, 20.0, 4000.0, 1.5e5, 3.0, 0.4, 0.0, 0.0, 10.0, 60.0, 0.0, 1},
        {"Canón espinal", DamageType::Photonic, 260.0, 299792.458, 5.0e5, 40.0, 60.0, 0.0, 0.0, 40.0, 5.0, 0.0, 1},
        {"Misil", DamageType::Antimatter, 180.0, 1500.0, 1.2e6, 10.0, 0.05, 0.2, 0.0, 30.0, 900.0, 0.05, 1},
        {"Drones", DamageType::Thermal, 18.0, 900.0, 6.0e5, 25.0, 0.2, 0.0, 1.0, 4.0, 600.0, 0.12, 6},
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
    double absorb[4];   // parte que para de cada tipo de dano: cinetico, fotonico, antimateria, termico
    double capacity;    // dano que aguanta lleno, con el nivel 1
    double regen;       // por segundo a bordo, encendido; los conos no se regeneran solos
    double energy;      // por hora, encendido
    double heat;        // calor por segundo encendido; negativo, lo absorbe
};

struct DefenseRules {
    ShieldRules shields[5] = {
        // Arena: buena contra haces y calor; los proyectiles rapidos la atraviesan. Gasta silicato.
        {"Arena magnética", {0.15, 0.90, 0.40, 0.85}, 350.0, 3.0, 20.0, -0.15},
        // Plasma: vaporiza lo fisico, pero la nave brilla y se la ve desde muy lejos.
        {"Plasma", {0.95, 0.30, 0.30, 0.10}, 300.0, 2.5, 60.0, 0.15},
        // Electromagnetico: para la antimateria y ciega los sensores enemigos; nulo contra lo cinetico.
        {"Electromagnético", {0.00, 0.55, 0.95, 0.50}, 400.0, 3.5, 900.0, 0.03},
        // Conos ablativos: placas de titanio y aerogel en la proa. Pesan y se gastan con el gas.
        {"Conos ablativos", {0.60, 0.30, 0.30, 0.50}, 500.0, 0.0, 0.0, 0.0},
        // Campo de Amor: de los Unicornios. Para todo y lo devuelve.
        {"Campo de Amor", {1.0, 1.0, 1.0, 1.0}, 1.0e9, 1.0e9, 0.0, 0.0},
    };
    double tierCapacity = 0.5;     // cada nivel suma esta parte de la capacidad y de la regeneracion
    double dustPerDamage = 0.05;   // silicato por punto de dano parado
    double dustUpkeep = 2.0;       // silicato por hora, encendido
    double plasmaSight = 3.0;      // con plasma, los hostiles ven la nave desde tantas veces mas lejos
    double plasmaRock = 0.5;       // km: los asteroides menores se vaporizan al chocar
    double emJamming = 3.0;        // el error de punteria enemigo se multiplica por esto
    double ablativeMass = 0.3;     // con las placas enteras, la maniobra se divide por 1 + esto
    double ablativeWear = 0.002;   // placas por segundo, por densidad de nebulosa y por fraccion de c
    double ramPressure = 0.4;      // casco por segundo real en curvatura a 10 c, con densidad 1
    double loveHeal = 0.5;         // parte del dano que el Amor vuelve casco; el resto, energia
    double plateRepair = 120.0;    // placas por hora en una estacion propia
    double plateMetal = 0.25;      // metal por placa reparada
};

// Calor. 100 es el limite: por encima se apagan escudos y armas y el casco sufre.
enum class CoolantKind : int { Water, Ice, Nitrogen, Radiator, Count };

struct ThermalRules {
    double limit = 100.0;
    double maximum = 150.0;
    double weaponHeat[4] = {1.0, 25.0, 4.0, 6.0};  // por disparo: torreta, espinal, misil, drones
    double formHeat = 30.0;        // formar una burbuja de curvatura
    double warpHeat = 0.04;        // por segundo a bordo, con la burbuja a 10 c; sube con la raiz de la velocidad
    double absorbHeat = 0.05;      // por punto de dano que para un escudo
    double overheatBurn = 0.02;    // casco por segundo y por punto por encima del limite
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

struct AnomalyRules {
    double hostileSightRange = 3.0e6;   // km
    double hostileWeaponRange = 1.0e5;  // km: distancia a la que se queda para disparar
    double shipHull = 300.0;
    double stationHull = 3500.0;
    double shipShield = 120.0;          // escudo de una nave alienigena: arena, plasma o EM
    double stationShield = 900.0;       // y de una estacion, siempre electromagnetico
    double shieldRegen = 0.01;          // parte de su escudo que recuperan por segundo
    double shipLoot = 12.0;             // kt de metal que sueltan al destruirse
    double stationLoot = 90.0;
    double hostileSpeed = 300.0;        // km/s; por debajo de la curvatura, para poder huir
    double patrolSpeed = 40.0;
    double exploreHours = 2.0;          // lo que tarda sacar el emisor de una estructura
    double activeRange = 4.0e9;         // km: mas lejos no se calcula su movimiento
};

struct ScannerRules {
    double hours = 0.5;            // lo que dura un barrido
    double energy = 160.0;         // lo que gasta
    double range = 9.0e8;          // km con el nivel 1, unas 6 UA
    double rangePerTier = 0.8;     // cada nivel suma esta parte de lo anterior
    double systemRange = 1.6e12;   // km a los que encuentra otros sistemas, unos 0,17 anos luz
    double bodyRange = 6.0e9;      // km a los que analiza astros de este sistema, unas 40 UA
    int startTier = 1;
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
};

struct Balance {
    DriveRules drive;
    ShipRules ship;
    FootRules foot;
    StationRules stations;
    AnomalyRules anomalies;
    CombatRules combat;
    DefenseRules defense;
    ThermalRules thermal;
    ScannerRules scanner;
    WorldRules world;
};

// Modos de juego. Cambian lo que cuesta equivocarse, no las mecanicas.
enum class GameModeId : int { Peaceful, Easy, Normal, Hard, Realistic, Count };

struct GameMode {
    const char* name;
    const char* description;
    bool hostilesAttack;
    double damage;        // multiplica el dano recibido
    double cargoLoss;     // parte de la bodega que se pierde al volver a un punto de control
    bool permadeath;      // true = se pierde todo y se empieza de cero
};

inline const GameMode& ModeInfo(GameModeId id) {
    static const GameMode modes[] = {
        {"Pac\u00EDfico", "Nada te ataca ni te da\u00F1a. Solo explorar y construir.", false, 0.0, 0.0, false},
        {"F\u00E1cil", "Da\u00F1o reducido. Vuelves al punto de control sin perder carga.", true, 0.5, 0.0, false},
        {"Normal", "Vuelves al punto de control y pierdes un cuarto de la bodega.", true, 1.0, 0.25, false},
        {"Dif\u00EDcil", "Da\u00F1o aumentado. Pierdes la mitad de la bodega.", true, 1.6, 0.5, false},
        {"Realista", "Una sola nave. Si la pierdes, lo pierdes todo.", true, 1.6, 1.0, true},
    };
    return modes[static_cast<int>(id)];
}

}  // namespace game
