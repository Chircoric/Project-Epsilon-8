// Session.h - Una partida: une la nave, las estaciones, las anomalias y el universo.
//
// No conoce la ventana. Recibe lo que hace el jugador y deja listo lo que hay que mostrar, asi
// que una prueba puede jugar una partida entera sin GPU.
#pragma once

#include <deque>
#include <map>
#include <set>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "epsilon/Engine.h"
#include "game/Anomalies.h"
#include "game/Balance.h"
#include "game/Combat.h"
#include "game/Defense.h"
#include "game/Economy.h"
#include "game/OnFoot.h"
#include "game/Ship.h"
#include "game/SaveGame.h"
#include "game/Stations.h"
#include "game/Tutorial.h"

namespace game {

// Algo a lo que se puede apuntar. Usa indices estables: sobrevive a descargar un sistema.
struct TargetRef {
    enum class Kind : int { None, Star, Planet, Belt, Station, Anomaly } kind = Kind::None;
    int system = 0;
    int index = -1;  // estrella: StarAt; planeta: bodies; campo: belts; el resto: su id
    bool operator==(const TargetRef&) const = default;
};

struct TargetInfo {
    eps::Vec3d position;  // km
    eps::Vec3d velocity;  // km/s
    double radius = 0;    // km
    double arrival = 0;   // km del centro donde se considera que se ha llegado
    std::string name;
    std::string detail;
};

// Lo que muestran los sensores.
struct Contact {
    TargetRef target;
    std::string name;
    std::string detail;
    eps::Vec3d position;
    double distance = 0;
    bool identified = false;
    Stance stance = Stance::None;
};

// Un dron minero (Balance.h, DroneRules). Sale de su bahia, vuela al sitio, mina hasta llenarse,
// vuelve, descarga en la bodega de la nave y repite mientras tenga sitio asignado.
enum class DroneState : int { Docked, Outbound, Mining, Returning };

struct MiningDrone {
    int slot = -1;        // ranura de su bahia (ShipDesign::slots)
    int index = 0;        // puesto en la bahia: 0 o 1
    DroneState state = DroneState::Docked;
    eps::Vec3d position;  // km, en el mundo, cuando esta fuera
    eps::Vec3d heading{0, 0, 1};  // hacia donde mira, en el mundo
    double load = 0;      // kt de mineral a bordo
    int element = 0;      // extraccion selectiva: numero atomico de lo que saca de ese mineral; 0 = a granel
    double yield = 0;     // fraccion del mineral que es ese elemento una vez recuperado (fraccion x recuperacion)
};

// Una fila del menu de sistemas. depth 1 es una luna.
struct AtlasRow {
    TargetRef target;
    int depth = 0;
    std::string name;
    std::string detail;
    bool known = false;
};

enum class DockPhase : int { None, Approaching, Docked };

// A pie: lo que se manda en este paso.
struct FootControls {
    eps::HumanoidInput move;
    double yaw = 0, pitch = 0;  // radianes que gira la cabeza
};

struct ShipControls {
    FootControls foot;
    float turnYaw = 0, turnPitch = 0;  // radianes de apuntado
    float turnRoll = 0;                // radianes de alabeo: positivo, a la derecha
    eps::Quatd view;                   // camara respecto a la nave: el raton gira sobre sus ejes
    double throttleChange = 0;         // por segundo real
    bool manualBrake = false;
};

// Una orden del jugador a su nave, como dato: lo que la interfaz y la consola le mandan a la sesion
// (GameSession::Apply). Es lo que en multijugador viaja del cliente al servidor (docs/multijugador.md),
// asi que solo lleva numeros y un objetivo. El mando continuo (giro, gases) va aparte, en ShipControls.
enum class CommandType : int {
    SetDrive,         // value: DriveMode
    SetThrottle,      // amount: de -1 a 1
    HoldPosition,     // mando a cero y la nave quieta
    Autopilot,        // target
    CancelAutopilot,
    Orbit,            // circulariza alrededor de lo que domina aqui
    DeployPower,      // target: la estrella
    DeployShipyard,
    Interact,         // target
    UpgradeStation,   // value: id de la estacion; index: ModuleKind
    DeployDrones,     // target
    RecallDrones,
    ExtractElement,   // value: numero atomico; 0 = a granel
    StartScan,
    CancelScan,
    Fire,             // value: WeaponKind; target
    FireAll,          // target; el resultado lleva en count cuantas armas dispararon
    FireModule,       // value: ranura; target
    Aim,              // value: WeaponKind; target
    Track,            // target
    StopTracking,
    SetShield,        // value: ShieldKind
    ShieldOn,         // value: 0 apaga, 1 enciende
    CycleShield,      // el resultado lleva en count el campo que queda
    Coolant,          // value: CoolantKind; index: 0 apaga, 1 enciende
    Dock,             // target; value: 1 = sin esperar al haz (pruebas)
    Undock,
    Disembark,
    Board,            // value: 1 = desde cualquier punto de la cubierta
    InstallModule,    // index: ranura; value: ShipModuleKind
    MoveModule,       // value: ranura de origen; index: ranura de destino
    ImpulseEngine,    // value: motor
    Blueprint,        // value: plano
    Missiles,         // value: ranura; index: cantidad (0 = hasta llenar); count: los fabricados
    PointDefense,     // value: puntos
    Continue,         // sigue tras perder la nave
    NextDialogue,
    ReplayTip,
    AnalysisUsed,
    CursorUsed,
    Cinematic,        // value: 0 o 1
    Count
};

struct ShipCommand {
    CommandType type = CommandType::Count;
    TargetRef target;
    int value = 0, index = 0;
    double amount = 0;
};

inline ShipCommand Order(CommandType type, int value = 0, int index = 0) { return {type, {}, value, index, 0.0}; }
inline ShipCommand Order(CommandType type, const TargetRef& target, int value = 0) { return {type, target, value, 0, 0.0}; }

// Lo que responde la sesion a una orden. why solo se rellena en las ordenes del taller.
struct CommandResult {
    bool ok = false;
    int count = 0;
    std::string why;
    explicit operator bool() const { return ok; }
};

// Adrift: una zona roja ha apagado la nave. Va a la deriva, a oscuras, hasta la pantalla final.
enum class GamePhase : int { Flying, Exploding, GameOver, Adrift };

enum class RoutePhase : int { None, Aligning, Cruising, Rounding, Braking };

struct RouteReport {
    RoutePhase phase = RoutePhase::None;
    std::string target;
    double seconds = 0;   // reales hasta llegar, con la velocidad de ahora
    double distance = 0;  // km
};

struct OrbitReport {
    bool valid = false;
    bool locked = false;       // orbita asistida: no decae
    bool escaping = false;     // no esta ligada: se marcha
    std::string body;
    double periapsis = 0, apoapsis = 0;  // km de altura
    double period = 0;         // segundos simulados
};

struct CollisionWarning {
    std::string name;
    double seconds = 0;  // reales
    bool heat = false;
};

// Lo que manda en este punto: el cuerpo con cuya gravedad se vuela, y con el que se viaja.
struct LocalFrame {
    int body = -1;        // indice en World::bodies
    int belt = -1, family = -1;  // o un cumulo de asteroides
    double mu = 0;        // km3/s2
    double radius = 0;
    double reach = 0;     // km: hasta donde domina
    eps::Vec3d position, velocity;
};

// Lo que el ultimo barrido oyo de algo frio que no alcanzo a resolver: un rumbo y una distancia
// aproximados, mejores cuanto mas nivel tiene el escaner. En el paso de la estructura del tutorial
// es exacto y llega aunque el eco no alcance.
struct EchoHint {
    bool active = false;     // hay rumbo y distancia
    bool exact = false;      // sin error (lo garantiza el tutorial)
    int system = -1;
    eps::Vec3d offset;       // km: el punto estimado, desde el centro del sistema
    double distance = 0;     // km estimados desde donde se hizo el barrido
    double bearing = 0;      // grados, de 0 a 360, en el plano del sistema (desde +X hacia +Z)
    double elevation = 0;    // grados sobre ese plano
    double error = 0;        // km: radio de la zona donde puede estar
    int weak = 0;            // ecos de firma superior al nivel: solo dan "senal debil", sin rumbo
    int needTier = 0;        // el nivel que pide el mas fuerte de ellos
};

// Orientacion de una camara que mira hacia forward (no hace falta que sea unitaria), sin alabeo:
// su +Z va hacia forward y su -Y queda lo mas cerca posible del arriba del mundo (-Y).
eps::Quatd CameraLooking(const eps::Vec3d& forward);

// La estacion mas cercana a menos de 3000 km, y la velocidad maxima que permite ahora.
struct ApproachInfo {
    std::string station;
    double distance = 0, top = 0;  // km, km/s
    bool zone = false;             // dentro de la zona de atraque
};

// Una zona del casco que se ha soltado: se aleja girando y se pierde.
struct LostPiece {
    int zone = 0;
    int piece = -1;           // >= 0: solo esa pieza del modelo; -1: la zona entera
    int model = -1;           // >= 0: es de una anomalia, con esa maqueta (AlienModel); -1: de la nave
    eps::Vec3d pivot;         // m, en el modelo: el centro de lo que se ha soltado
    eps::Vec3d absolute;      // km, sin restar el origen del mundo
    eps::Vec3d velocity;      // km/s
    eps::Quatd orientation;   // la de la nave al soltarse, mas lo que ha girado
    eps::Vec3d spinAxis{0, 1, 0};
    double spin = 0;          // rad/s
    double age = 0;           // segundos reales
};

// El ultimo impacto del jugador en una anomalia: lo sigue la camara del modo cinematica.
struct StrikeMark {
    int serial = 0;        // crece con cada impacto: asi se sabe que hay uno nuevo
    int anomaly = -1;      // id de la anomalia
    int zone = -1;
    eps::Vec3d point;      // m, en el modelo de la anomalia
    // Donde estaba el blanco la ultima vez que se vio: la toma sigue aunque acabe de estallar.
    eps::Vec3d target;     // km, sin restar el origen del mundo
    double yaw = 0;
    double size = 0.3;     // km: media diagonal de su modelo
};

// Un golpe que llego al casco sin que lo pararan los escudos. La sesion solo lo anota: el crater,
// los escombros y las vigas los calcula GameLayer, y solo con la calidad maxima.
struct HullHit {
    int id = 0;
    int zone = 0;
    eps::Vec3d from{0, 0, 1};  // unitaria, en el modelo: de donde llego
    double intensity = 0;      // parte del casco entero que se llevo
    bool blunt = false;        // un choque: abolla mas ancho y no perfora
    bool located = false;      // se sabe donde dio: la trayectoria del proyectil
    eps::Vec3d point;          // m, en el modelo, si located
    double time = 0;           // reloj de la burbuja en el golpe
    double age = 0;            // segundos reales desde el golpe
    eps::Vec3d absolute;       // km: centro de la nave entonces, sin restar el origen del mundo
    eps::Vec3d velocity;       // km/s de la nave entonces
    eps::Quatd orientation;    // de la nave entonces
};

// ---- El estado de una partida, repartido segun de quien es (docs/multijugador.md, apartado 1) ----
// Hoy hay una nave, un jugador y una burbuja; estan separados para que deje de ser asi.

// Una nave: lo que lleva, por donde va y lo que le pasa.
struct Vessel {
    int id = 1;  // estable: es como la nombran el guardado y, en red, las ordenes
    ShipState ship;

    // Vuelo, ruta y orbita.
    std::optional<TargetRef> autopilot, orbiting, tracking;
    eps::Vec3d orbitOffset;
    eps::Vec3d orbitNormal{0, -1, 0};
    double orbitRate = 0;  // rad/s simulados; 0 = se queda quieta junto a su objetivo
    LocalFrame frame;
    RoutePhase routePhase = RoutePhase::None;
    double routeDistance = 0;
    bool fuelWarned = false;  // ya se aviso de que esta ruta no puede formar burbuja
    std::optional<CollisionWarning> warning;
    double exteriorFactor = 1.0;  // cuantas veces mas rapido pasa el tiempo fuera que a bordo
    int nearestSystem = 0;
    bool inHeatZone = false;
    bool creative = false;

    // Atraque.
    DockPhase dockPhase = DockPhase::None;
    int dockWanted = -1;       // estacion a la que atracar al acabar la ruta
    int undockGrace = -1;      // estacion recien soltada: no choca hasta que la nave salga de ella
    double dockedAge = 0;      // s reales atracada: el ducto se despliega en los primeros
    std::optional<ApproachInfo> approach;
    int homeStation = -1;      // la ultima estacion propia donde atraco: ahi reaparece si se pierde
    bool respawnPending = false;  // ha reaparecido en el sistema de su estacion y aun no ha podido atracar

    // Danos, calor y perdida.
    int hitStreak = 0;
    double hitDamage = 0, hitShield = 0;
    double directedLoss = 0;     // casco perdido en este paso por golpes con direccion
    HullZones zoneHeat{};
    std::vector<HullHit> hullHits;
    int nextHit = 1;
    int failedZone = -1;
    eps::Vec3d driftVelocity;    // km/s reales de la nave apagada, para la cinematica
    double lastWarpCharge = 0;   // para calentar mientras se forma la burbuja
    double radiators = 1.0;
    bool ramming = false, ramWarned = false, overheatWarned = false;
    bool gasWarned = false;      // ya se aviso de que no queda gas para maniobrar
    GamePhase phase = GamePhase::Flying;
    double wreckAge = 0;
    eps::Vec3d wreckPosition;
    std::string lossReason;

    // Drones mineros.
    std::vector<MiningDrone> drones;
    std::optional<TargetRef> droneSite;
    Deposit droneDeposit;
    bool dronesFullWarned = false;
    int extractZ = 0;           // elemento elegido para la extraccion selectiva; 0 = a granel
    double droneYield = 0;      // fraccion x recuperacion del elegido en el sitio de los drones

    // Sensores, barrido y exploracion.
    std::vector<Contact> contacts;
    double scanProgress = -1.0;
    std::string lastScan;
    EchoHint echo;
    int echoTarget = -1;        // la anomalia de la pista: al revelarse, la pista se borra
    int scansDone = 0;          // cambia el error de la pista de un barrido a otro
    int exploring = -1;
};

// Un jugador: lo que sabe, lo que le han dicho y su cuerpo cuando va a pie.
struct PlayerState {
    std::string id = "local";    // su archivo en la partida: jugadores/<id>.sav
    std::string name = "Piloto";
    Dialogue dialogue;
    bool analysisUsed = false, cursorUsed = false;
    int collected = 0;          // veces que se ha recogido de una estacion
    std::deque<std::string> log;
    std::vector<char> known;    // uno por sistema
    std::vector<char> visited;  // donde la nave ha estado de verdad
    std::set<std::uint64_t> scanned;
    int kills = 0;
    bool onFoot = false, firstPerson = false;
    eps::HumanoidState avatar;
    StrikeMark lastStrike;
    double strikeAge = 1e9;     // s reales desde su ultimo impacto en una anomalia
    bool cinematic = false;
};

// Lo que existe aunque no haya nadie mirando.
struct WorldState {
    WorldState(const Balance& rules, std::uint32_t s, GameModeId m) : seed(s), mode(m), stations(rules.stations), timeScale(rules.world.timeScale) {}
    std::uint32_t seed;
    GameModeId mode;
    StationNetwork stations;
    std::vector<Anomaly> anomalies;
    SpeciesState species;
    double timeScale = 60.0;
};

// Una zona simulada: su reloj y lo que vuela o flota en ella.
struct Bubble {
    explicit Bubble(std::uint32_t seed) : rng(seed), fx(seed ^ 0x9E3779B9u) {}
    double simTime = 0;
    double timeFocus = 1.0;
    int steps = 0;
    std::vector<Projectile> projectiles;
    std::vector<ImpactFlash> impacts;
    std::vector<LostPiece> lostPieces;
    std::map<std::uint32_t, std::vector<eps::AsteroidRock>> rocks;
    std::vector<int> pins;
    // Dos generadores: rng decide lo que cuenta (punteria, zona del impacto, defensa puntual, anomalias)
    // y solo lo usara el servidor; fx es para lo que solo se ve (como giran los trozos, por donde pasea
    // la tripulacion) y cada cliente tendra el suyo. Gastar de fx no cambia la partida.
    std::mt19937 rng;
    std::mt19937 fx;
    // La cubierta de la estacion donde hay alguien a pie.
    DeckPlan deck;
    StationKind deckKind = StationKind::Power;
    std::vector<Person> crew;
};

class GameSession {
public:
    GameSession(eps::Engine& engine, const Balance& rules, std::uint32_t seed = 20261105, GameModeId mode = GameModeId::Normal);

    // Avanza la partida. Llama a Engine::Advance: el juego no debe llamarlo por su cuenta.
    void Step(double realSeconds, const ShipControls& controls, float focalPixels = 800.0f);

    // Como Step, pero a paso fijo: acumula el tiempo real y da los pasos enteros de kFixedStep que
    // quepan (ninguno si aun no llega). Asi la partida no depende de los fotogramas por segundo, y es
    // como avanzara el servidor. Los giros que lleguen entre dos pasos se suman y no se pierden.
    // Devuelve los pasos dados.
    static constexpr double kFixedStep = 1.0 / 60.0;
    int StepFixed(double realSeconds, const ShipControls& controls, float focalPixels = 800.0f);

    // Ejecuta una orden del jugador. Es la unica puerta por la que la interfaz y la consola cambian
    // la partida; los metodos de abajo siguen ahi para las pruebas y para la propia sesion.
    CommandResult Apply(const ShipCommand& command);

    void SetMode(GameModeId mode) { world_.mode = mode; }
    GameModeId Mode() const { return world_.mode; }

    // Motor.
    bool SetDrive(DriveMode mode);
    void SetThrottle(double value);

    // Piloto automatico: orienta, acelera, frena y se queda en orbita del objetivo.
    bool SetAutopilot(const TargetRef& target);
    void CancelAutopilot();
    const std::optional<TargetRef>& Autopilot() const { return vessel_.autopilot; }
    const std::optional<TargetRef>& Orbiting() const { return vessel_.orbiting; }

    // Acciones sobre el objetivo. Devuelven false y dejan el motivo en los mensajes.
    bool DeployPower(const TargetRef& target);
    // Recoge de una central, entrega material a la origami o explora una estructura.
    bool Interact(const TargetRef& target);
    bool UpgradeStation(int id, ModuleKind module);

    // Estacion origami: se suelta de las bodegas donde esta la nave, dentro de un sistema, con la
    // nave casi parada. Se abre alrededor de la nave, se aparta hacia popa y saca los brazos.
    bool DeployShipyard();
    bool CanDeployShipyard(std::string* why = nullptr) const;
    // Segundos reales que dura todo el despliegue, y como esta una origami en el instante age:
    // ring de 0 a 1 es la apertura del anillo; slide, lo que se ha apartado; arms, los brazos.
    struct ShipyardPhase { double ring = 0, slide = 0, arms = 0; };
    static ShipyardPhase ShipyardDeploy(double age);
    // Donde estaba la nave al soltarla: ahi se queda el andamio de transporte, que se desecha.
    eps::Vec3d ShipyardLaunchPosition(const Station& s) const;
    static constexpr double kShipyardOpenSeconds = 9.0, kShipyardSlideSeconds = 12.0, kShipyardArmsSeconds = 6.0;

    // Drones mineros: a un planeta o un campo analizado y a su alcance. Repiten viajes hasta que se
    // recogen, la nave se aleja demasiado o enciende algo mas que el impulso.
    bool DeployDrones(const TargetRef& target);
    void RecallDrones();
    const std::vector<MiningDrone>& Drones() const { return vessel_.drones; }
    const std::optional<TargetRef>& DroneSite() const { return vessel_.droneSite; }
    int DronesOut() const;
    // Extraccion selectiva: el elemento (1 a 118) que sacan los drones al volver a cargar; 0 = a
    // granel. Se elige en la ventana de analisis. Lo extraido va al inventario de elementos, en kg.
    void SetExtractElement(int z);
    int ExtractElement() const { return vessel_.extractZ; }
    // Los 118 elementos de un astro analizado (con la semilla del astro), o nada si no se conoce.
    std::optional<eps::astro::PeriodicShares> ElementSharesOf(const TargetRef& target) const;
    // kt/h de ese elemento por dron en ese astro: ritmo x riqueza x fraccion x recuperacion. 0 si no se mina.
    double ElementYieldRate(const TargetRef& target, int z) const;
    // Donde esta la bahia de un dron, en el mundo (km), y hacia donde mira.
    eps::Vec3d DroneBayPosition(int slot) const;
    eps::Vec3d DroneBayNormal(int slot) const;
    // Donde trabajan, en el mundo: la superficie del planeta del lado de la nave, o el cumulo.
    std::optional<eps::Vec3d> DroneWorkPoint() const;

    // Combate. Se dispara contra una nave o un bastion fijado como objetivo. El computador de
    // punteria resuelve el tiro con la gravedad de los astros que conoce; el canon espinal sale por
    // donde mira el casco, asi que hay que girar hasta la solucion antes de disparar.
    bool Fire(WeaponKind kind, const TargetRef& target);
    int FireAll(const TargetRef& target);       // todo lo que este listo; devuelve cuantas armas
    std::optional<FiringSolution> Solution(const TargetRef& target, WeaponKind kind) const;
    bool AimAtSolution(const TargetRef& target, WeaponKind kind);  // apunta el rumbo a la solucion
    const std::vector<Projectile>& Projectiles() const { return bubble_.projectiles; }
    const std::vector<ImpactFlash>& Impacts() const { return bubble_.impacts; }
    double WeaponRange(WeaponKind kind) const;
    bool WeaponReady(WeaponKind kind) const;     // alguna de esa familia ha recargado
    double WeaponCooldown(WeaponKind kind) const;  // segundos simulados hasta la primera que recargue
    int WeaponTier(WeaponKind kind) const;

    // El rumbo sigue la solucion del canon espinal hasta que el raton lo mueva o el blanco desaparezca.
    void TrackTarget(const TargetRef& target);
    void StopTracking() { vessel_.tracking.reset(); }
    const std::optional<TargetRef>& Tracking() const { return vessel_.tracking; }
    double ShipRadius() const { return rules_.ship.radius; }
    eps::Vec3d ShipVelocity() const;             // en el mundo
    std::vector<GravitySource> GravitySources() const;  // astros que tiran de los proyectiles aqui
    int Kills() const { return pilot_.kills; }

    // Escudos y calor (Defense.h). Solo un campo encendido a la vez; los conos van siempre puestos.
    bool SetShield(ShieldKind kind);            // elige el campo y lo enciende; false si no se tiene
    void SetShieldOn(bool on);
    ShieldKind CycleShield();                   // siguiente campo que se tenga; tras el ultimo, apagado
    bool FieldActive() const;                   // encendido, con energia, con polvo y sin sobrecalentar
    bool Overheated() const;
    void SetCoolant(CoolantKind kind, bool on);
    double RadiatorEfficiency() const { return vessel_.radiators; }  // 0 junto a una estrella, 1 lejos
    bool Ramming() const { return vessel_.ramming; }   // en curvatura, con la presion de ariete encima

    // Atraque en estaciones propias. Si esta lejos, primero va la ruta. Un haz guia los ultimos km:
    // no hay rebotes ni forma de pasarse. Al dar mando, o con Undock, la nave se suelta.
    bool Dock(const TargetRef& target, bool instant = false);
    void Undock();
    DockPhase Docking() const { return vessel_.dockPhase; }
    bool Docked() const { return vessel_.dockPhase == DockPhase::Docked; }
    // La estacion mas cercana a menos de 3000 km, y la velocidad maxima que permite ahora.
    using ApproachInfo = game::ApproachInfo;
    const std::optional<ApproachInfo>& Approach() const { return vessel_.approach; }
    const Station* DockedStation() const;

    // A pie, en la cubierta de la estacion donde esta atracada.
    bool Disembark();
    bool Board(bool anywhere = false);
    bool OnFoot() const { return pilot_.onFoot; }
    bool CanBoard() const;
    const eps::HumanoidState& Player() const { return pilot_.avatar; }
    const eps::HumanoidRules& PersonRules() const { return body_; }
    const std::vector<Person>& Crew() const { return bubble_.crew; }
    const DeckPlan& Deck() const { return bubble_.deck; }
    StationKind DeckKind() const { return bubble_.deckKind; }
    eps::Vec3d DeckOrigin() const { return vessel_.ship.position; }  // km: la cubierta va donde esta la nave
    bool FirstPerson() const { return pilot_.firstPerson; }
    void ToggleFootView() { pilot_.firstPerson = !pilot_.firstPerson; }

    // Escaner: gasta mucha energia, exige la nave parada y encuentra lo que alcance su nivel.
    bool StartScan();
    void CancelScan();
    bool Scanning() const { return vessel_.scanProgress >= 0.0; }
    double ScanProgress() const { return vessel_.scanProgress; }
    double ScanRange() const;
    double EchoRange() const;    // hasta donde oye el eco de lo frio: ScannerRules::echoFactor veces el alcance
    const std::string& LastScan() const { return vessel_.lastScan; }
    const EchoHint& Echo() const { return vessel_.echo; }
    std::optional<eps::Vec3d> EchoPosition() const;  // el punto estimado, en el mundo; nada sin pista
    bool EchoGuaranteed() const;  // el tutorial esta en el paso de la estructura: pista exacta

    // Perder la nave.
    GamePhase Phase() const { return vessel_.phase; }
    double WreckAge() const { return vessel_.wreckAge; }
    const eps::Vec3d& WreckPosition() const { return vessel_.wreckPosition; }
    void SaveCheckpoint(const std::string& reason);  // avisa y pide guardar la partida; no hay vuelta atras
    // La nave reaparece reparada en la ultima estacion propia donde atraco (o en el punto de partida),
    // con la bodega que deje el modo; el mundo sigue como esta. En modo realista, todo a cero.
    void ContinueAfterDefeat();

    // Partidas guardadas. Si la nave acaba de perderse, se guarda como esta, con una marca: al cargar, reaparece.
    // Quien juega: su identificador (el nombre de su archivo en la partida) y como se llama.
    void SetPlayer(const std::string& id, const std::string& name) {
        pilot_.id = SafePlayerId(id);
        pilot_.name = name;
    }
    const std::string& PlayerId() const { return pilot_.id; }
    // El mundo con sus naves y lo de este jugador (SaveGame.h).
    SaveData Snapshot() const;
    bool Restore(const SaveData& data, std::string* error = nullptr);
    bool TakeSaveRequest();      // hay algo nuevo que guardar: ha pasado un punto de control
    bool TakeSaveDeletion();     // modo realista: la partida se ha perdido para siempre
    void Announce(const std::string& text) { Say(text, Audience::Player); }

    // Solo se conocen los sistemas alcanzados por un barrido o visitados.
    bool KnownSystem(int system) const;
    int KnownSystems() const;
    bool Visited(int system) const;
    int VisitedSystems() const;
    std::string SystemName(int system) const;

    // Lo que se sabe de un sistema, en orden: estrellas, planetas con sus lunas, campos, anomalias
    // vistas y estaciones propias. No necesita tener el sistema cargado.
    std::vector<AtlasRow> AtlasRows(int system) const;

    // Un astro no se conoce hasta analizarlo: nombre, recursos y estadisticas salen del barrido.
    bool Scanned(const TargetRef& target) const;
    int ScannedCount() const { return static_cast<int>(pilot_.scanned.size()); }
    void MarkScanned(int system);  // para pruebas y comandos
    void MarkKnown(int system);    // localiza un sistema y analiza todo lo que tiene

    // Modo creativo, desde la consola: nave invulnerable, energia, antimateria y materiales sin fin,
    // todos los motores y niveles, y todos los sistemas localizados. No se guarda.
    void SetCreative(bool on);
    bool Creative() const { return vessel_.creative; }
    // Crea una anomalia a km de la nave, delante de ella. Devuelve su id, o -1.
    // level: nivel de estacion (1 a 6; 6 es el planeta coraza, 0 lo elige su id). shipClass: clase de
    // la nave (-1, la que le toque por su id), en su variante normal.
    int SpawnAnomaly(AnomalyKind kind, Stance stance, double km, int level = 0, int shipClass = -1);
    // Alerta de la especie por sistema, rendicion temporal y revancha (fase F).
    const SpeciesState& Species() const { return world_.species; }
    SpeciesState& Species() { return world_.species; }
    double SystemAlert(int system) const;
    bool Surrender() const { return Surrendered(world_.species, bubble_.simTime); }
    void MarkAnalysisUsed() { pilot_.analysisUsed = true; }
    bool AnalysisUsed() const { return pilot_.analysisUsed; }

    // Parte del tiempo normal que corre ahora: se frena en los momentos criticos.
    double TimeFocus() const { return bubble_.timeFocus; }

    // Atencion: mueve la nave directamente. Es para pruebas y capturas.
    bool JumpTo(const TargetRef& target, double distance);

    std::optional<TargetInfo> Resolve(const TargetRef& target) const;
    bool InRange(const TargetRef& target, double range) const;
    double Visibility() const;  // km; depende de lo espesa que este la nebulosa aqui
    float NebulaTone() const;   // de 0 a 1: punto de la paleta de la nebulosa en esta zona

    const ShipState& Ship() const { return vessel_.ship; }
    ShipState& Ship() { return vessel_.ship; }
    const StationNetwork& Stations() const { return world_.stations; }
    StationNetwork& Stations() { return world_.stations; }
    const std::vector<Anomaly>& Anomalies() const { return world_.anomalies; }
    std::vector<Anomaly>& Anomalies() { return world_.anomalies; }
    const std::vector<Contact>& Contacts() const { return vessel_.contacts; }
    const std::deque<std::string>& Log() const { return pilot_.log; }
    const Balance& Rules() const { return rules_; }
    double SimTime() const { return bubble_.simTime; }
    double& TimeScale() { return world_.timeScale; }
    int NearestSystem() const { return vessel_.nearestSystem; }
    bool InHeatZone() const { return vessel_.inHeatZone; }
    bool Exploring() const { return vessel_.exploring >= 0; }

    // Tiempo: cuantas veces mas rapido pasa fuera que a bordo, por velocidad y por gravedad.
    double ExteriorFactor() const { return vessel_.exteriorFactor; }

    // Vuelo. Espacio frena y mantiene la nave quieta. En orbita no hace falta motor.
    void HoldPosition();
    bool CanScan() const;
    bool EnterOrbitHere();                        // circulariza alrededor de lo que domina aqui
    std::string DominantName() const;             // vacio = espacio abierto
    RouteReport Route() const;
    OrbitReport OrbitNow() const;
    const std::optional<CollisionWarning>& Warning() const { return vessel_.warning; }
    std::vector<eps::Vec3d> PredictedPath() const;  // lo que va a recorrer, para dibujarlo
    const std::string& LossReason() const { return vessel_.lossReason; }
    eps::World& GetWorld() { return engine_.GetWorld(); }
    const eps::World& GetWorld() const { return engine_.GetWorld(); }

    Dialogue& Conversation() { return pilot_.dialogue; }
    const Dialogue& Conversation() const { return pilot_.dialogue; }
    void MarkCursorUsed() { pilot_.cursorUsed = true; }
    bool CursorUsed() const { return pilot_.cursorUsed; }
    int Collected() const { return pilot_.collected; }      // veces que se ha recogido de una estacion
    int RevealedStructures() const;

    eps::Vec3d StationPosition(const Station& s) const;  // km; solo vale con su sistema cargado
    bool StationVisible(const Station& s) const;
    eps::Quatd StationRotation(const Station& s) const;  // la central mira a su estrella
    static double StationRadius(StationKind kind);       // km: la esfera que la envuelve
    // La escotilla de atraque, en el mundo: donde esta y hacia donde mira. La proa se arrima a ella.
    struct DockPort { eps::Vec3d position, normal; };
    DockPort Port(const Station& s) const;
    // Atracada, la nave va de costado: su compuerta de estribor mira a la escotilla.
    eps::Quatd DockAttitude(const Station& s) const;
    // El ducto origami que une la compuerta de la nave con la escotilla: sus dos extremos (km) y
    // cuanto se ha desplegado (0 a 1). active es false si no esta atracada.
    struct DockTube { bool active = false; eps::Vec3d shipEnd, stationEnd; double extension = 0; };
    DockTube Tube() const;
    // Lo que va a recorrer la nave en los proximos segundos reales con el mando y el giro que lleva.
    std::vector<eps::Vec3d> ManeuverPath(double realSeconds) const;
    eps::Vec3d AnomalyPosition(const Anomaly& a) const;

    using LostPiece = game::LostPiece;
    const std::vector<LostPiece>& LostPieces() const { return bubble_.lostPieces; }
    // De 0 a 1 por zona: lo recien golpeado, que aun brilla. Se enfria en unos segundos.
    const HullZones& ZoneHeat() const { return vessel_.zoneHeat; }
    // Golpe en una zona concreta, en unidades de casco: para pruebas y la consola. Con from (unitaria,
    // en el modelo de la nave), cuenta como un impacto sin escudos que deja su crater.
    void DamageZone(int zone, double hullAmount, const eps::Vec3d* from = nullptr);
    // Un proyectil que recorre start -> end (m, en el modelo de la nave) y da donde lo lleve su
    // trayectoria, sin escudos de por medio: para pruebas y la consola. Devuelve la zona.
    int ShootHull(const eps::Vec3d& start, const eps::Vec3d& end, double hullAmount);
    // Lo mismo contra una anomalia (start y end en su modelo, m), como si disparase el jugador.
    // Devuelve la zona, o -1 si no existe o no tiene zonas.
    int ShootAnomaly(int id, const eps::Vec3d& start, const eps::Vec3d& end, double hullAmount);
    // Golpe del jugador en una zona concreta de una anomalia, sin escudos: para pruebas y la consola.
    bool DamageAnomalyZone(int id, int zone, double hullAmount);
    // Golpe del jugador contra una anomalia, con su escudo y sin zona: para pruebas.
    bool StrikeAnomaly(int id, DamageType type, double amount);

    using StrikeMark = game::StrikeMark;
    const StrikeMark& LastStrike() const { return pilot_.lastStrike; }
    // Modo cinematica (CinematicRules): tras cada impacto del jugador en un blanco, la camara lo rodea
    // mirando al punto del golpe y el tiempo va mas despacio. Se activa con la N o con la consola.
    bool Cinematic() const { return pilot_.cinematic; }
    void SetCinematic(bool on);
    // La toma esta en marcha: modo activo e impacto reciente (segundos reales).
    bool CinematicView() const;
    double StrikeAge() const { return pilot_.strikeAge; }
    // Donde va la camara de la toma y hacia donde mira (+Z adelante, -Y arriba). false sin toma.
    bool CinematicShot(eps::Vec3d& eye, eps::Quatd& look) const;

    // Taller: se cambian modulos y motor atracado en una estacion propia (ShipModules.h). Montar
    // cuesta metal y silicato de la bodega; lo que se quita devuelve la mitad. why dice por que no.
    bool CanRefit() const { return Docked() || vessel_.creative; }  // en creativo, en cualquier sitio
    void Notice(const std::string& text) { Say(text, Audience::Player); }  // una linea en el registro, desde fuera
    bool InstallModule(int slot, ShipModuleKind kind, std::string* why = nullptr);
    bool SetImpulseEngine(int tier, std::string* why = nullptr);

    // Armamento humano (Armament.h). Montar un arma pide su plano, metal con su masa real y elementos del
    // inventario; la clase de la nave decide si la admite. Las bahias de drones de serie se mueven de
    // hueco (MoveModule) pero no salen de la nave.
    bool MoveModule(int from, int to, std::string* why = nullptr);
    // Plano de un arma: se fabrica atracada en la estacion origami (en creativo, donde sea).
    bool FabricateBlueprint(int id, std::string* why = nullptr);
    // Fabrica misiles para una bahia (atracada, con energia, metal y elementos); count 0 = hasta llenarla.
    int ManufactureMissiles(int slot, int count, std::string* why = nullptr);
    // Puntos de defensa puntual montados (0 hasta DefensePoints de la clase); montar cuesta metal.
    bool SetPointDefense(int points, std::string* why = nullptr);
    int PointDefenseCapacity() const;
    double PointDefenseRate() const;  // probabilidad por segundo de derribar un guiado cercano
    // Dispara el arma humana de una ranura contra un blanco fijado; FireAll tambien lo hace con todas las listas.
    // quiet: no escribe en el registro si no puede (FireAll prueba todas cada vez).
    bool FireModule(int slot, const TargetRef& target, std::string* why = nullptr, bool quiet = false);
    // Tras fijar otra ShipDesign: ajusta los modulos y las piezas de la nave a ella.
    void RefitToDesign();

    using HullHit = game::HullHit;
    const std::vector<HullHit>& HullHits() const { return vessel_.hullHits; }

    // La zona roja que acabo con la nave (-1 = ninguna) y si la apago o la hizo estallar.
    int FailedZone() const { return vessel_.failedZone; }
    bool ShutDown() const { return vessel_.failedZone >= 0 && ZoneInfo(vessel_.failedZone).red == RedZone::Shutdown; }
    eps::Vec3d DriftVelocity() const { return vessel_.driftVelocity; }

private:
    // A quien va un mensaje del registro. Hoy todos acaban en el del unico jugador; con varios, Crew
    // llegara a quienes van en la nave, Player solo a quien hizo algo y Everyone a todos.
    enum class Audience : int { Crew, Player, Everyone };
    void Say(const std::string& text, Audience audience = Audience::Crew);
    void Steer(const ShipControls& controls, double realSeconds);
    DriveInput FlyRoute(double seconds, eps::Vec3d& wanted);
    void CheckRocks(const eps::Vec3d& from);
    void CheckFieldRocks(const eps::Vec3d& from);
    void Impact(const std::string& name, const eps::Vec3d& normal, const eps::Vec3d& bodyVelocity, bool star);
    void LoseShipTo(const std::string& reason);
    void UpdateFrame();
    void RefreshFrame(LocalFrame& frame) const;
    double GravityWell() const;
    std::optional<CollisionWarning> FindWarning() const;
    void UpdateFocus(double realSeconds);
    void ScanBodies(double range, int& found);
    double ObjectMass(const eps::Body& body) const;
    eps::Vec3d WorldVelocity(const eps::Body& body) const;
    void EnterOrbit(const TargetRef& target, const TargetInfo& info, double radius);
    void LeaveOrbit();
    double OrbitMu(const TargetRef& target) const;
    void KeepOutOfBodies(const eps::Vec3d& from);
    void KeepOutOfStructures(const eps::Vec3d& from);
    void BeginDock(const TargetRef& target);
    void StepDock(double realSeconds);
    void LimitNearStations(double realSeconds);
    eps::Vec3d DockGoal(const Station& s) const;  // donde queda el centro de la nave atracada, respecto a la estacion
    double StationTop(double distance, bool closing) const;  // km/s respecto a una estacion a esa distancia
    // La estacion propia mas cercana dentro de stationNear, o -1. Junto a ella la nave vuela en su
    // marco: la estacion la lleva consigo y su gravedad no la separa.
    int StationFrame() const;
    void StepFoot(const FootControls& controls, double realSeconds);
    void UpdateAnomalies(double seconds);
    void UpdateCombat(double seconds);
    void StepProjectiles(double seconds);
    void FireAlien(Anomaly& a, const eps::Vec3d& position);
    void OnAlienDestroyed(const Anomaly& a);  // alerta, rendicion
    // toward: direccion desde la nave hacia donde viene el golpe (los conos solo cubren la proa).
    // attacker: id de la anomalia que lo causa, o -1.
    // impact: donde dio el proyectil, por su trayectoria; sin el, la zona sale de toward.
    void Hurt(DamageType type, double amount, const std::string& source, const eps::Vec3d& toward, int attacker = -1, const ZoneImpact* impact = nullptr);
    // Reparte en las zonas el casco perdido. worldFrom: direccion desde la nave hacia el golpe.
    void WoundHull(double amount, const eps::Vec3d& worldFrom, bool blunt = false);
    void WoundAt(double amount, const ZoneImpact& impact);  // donde dio de verdad
    void WoundPiece(int zone, const eps::Vec3d& point, double fraction);  // la pieza que recibe el golpe
    void DetachPiece(int piece);
    void LoseModules(int zone, int piece);  // lo montado en esa zona o en esa pieza se pierde
    void SyncModuleWeapons();               // las torretas de los modulos, en vessel_.ship.weapons
    void WoundZone(int zone, double fraction);  // fraction: parte de la integridad de la zona
    void RecordHit(int zone, const eps::Vec3d& localFrom, double intensity, bool blunt, const eps::Vec3d* point = nullptr);
    void FailRedZone(int zone);
    void ShutDownShip(const std::string& reason);
    void SpreadWound(double amount);            // sin direccion: calor, presion
    void UpdateLostPieces(double seconds, double realSeconds);
    void Strike(Anomaly& a, const Projectile& p, const ZoneImpact* impact = nullptr);
    // impact: donde dio, en el modelo de la anomalia (m); sin el, solo baja su casco entero.
    void DamageAnomaly(Anomaly& a, DamageType type, double amount, int team, const ZoneImpact* impact = nullptr);
    void WoundAnomaly(Anomaly& a, int zone, double fraction, int team);  // como WoundZone, en la anomalia
    void FailAnomalyZone(Anomaly& a, int zone, int team);
    void HitAnomalyUnshielded(Anomaly& a, const ZoneImpact& impact, double hullAmount);
    void UpdateThermal(double proper, double realSeconds);
    Anomaly* FindAnomaly(int id);
    const Anomaly* FindAnomaly(int id) const;
    void UpdateHeat(double seconds);
    void UpdateRepair(double seconds);
    void UpdateExploration(double hours);
    void UpdateDrones(double seconds, double hours);
    void SyncDrones();                       // un dron por puesto de cada bahia que funcione
    void UpdateScan(double realSeconds);
    void ListenForEcho(double range);
    void UpdateContacts();
    void RefreshContacts();
    void LoseShip();
    void ResetCampaign();
    void PlaceAtStart();
    void PlaceInSystem(int system);
    void Respawn();          // la nave perdida reaparece en su ultima estacion (ver Session.cpp)
    bool FinishRespawn();    // la atraca en cuanto el sistema de la estacion esta cargado

    void TrackStrike();           // el blanco de la toma, donde esta ahora

    // Los tramos de Step (ver Session.cpp). StepClock es lo que dura el paso en cada medida de tiempo;
    // VesselStep, lo que una nave tiene que recordar de un tramo a otro.
    struct StepClock {
        double real = 0;     // segundos reales (ya frenados por la toma cinematica)
        double proper = 0;   // segundos a bordo
        double seconds = 0;  // segundos de fuera: lo que avanza el reloj de la burbuja
        double hours = 0;
    };
    struct VesselStep {
        bool flying = false;
        double hullStart = 0;          // el casco al empezar: lo perdido sin direccion se reparte al final
        eps::Vec3d before;             // donde estaba la nave
        eps::Vec3d frameBefore;        // y el cuerpo que domina
        bool hadFrame = false;
        int anchorCandidate = -1;      // la estacion junto a la que vuela, y donde estaba antes del reloj
        eps::Vec3d anchorStart;
        int anchor = -1;               // la estacion en cuyo marco se ha movido en este paso
        eps::Vec3d anchorBefore, anchorOffset;
        eps::Vec3d shift;              // lo que se ha movido el origen del mundo
    };
    StepClock BeginBubble(double realSeconds, VesselStep& t);
    void StepVessel(const ShipControls& controls, const StepClock& k, VesselStep& t);
    void AdvanceBubble(const StepClock& k, VesselStep& t, float focalPixels);
    bool SettleVessel(const StepClock& k, VesselStep& t);
    void FinishVessel(const StepClock& k, VesselStep& t);

    eps::Engine& engine_;
    Balance rules_;
    eps::HumanoidRules body_;
    WorldState world_;
    Bubble bubble_;
    Vessel vessel_;
    PlayerState pilot_;
    bool saveWanted_ = false, saveLost_ = false;

    // Paso fijo: tiempo real aun sin simular y mando acumulado desde el ultimo paso.
    double fixedPending_ = 0;
    ShipControls fixedControls_;
};

}  // namespace game
