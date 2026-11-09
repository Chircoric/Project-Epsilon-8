// Session.h - Una partida: une la nave, las estaciones, las anomalias y el universo.
//
// No conoce la ventana. Recibe lo que hace el jugador y deja listo lo que hay que mostrar, asi
// que una prueba puede jugar una partida entera sin GPU.
#pragma once

#include <deque>
#include <map>
#include <set>
#include <optional>
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

// Lo necesario para volver atras al perder la nave. Solo vive en memoria mientras no haya
// partidas guardadas.
struct Checkpoint {
    ShipState ship;
    eps::Vec3d absolutePosition;
    StationNetwork stations;
    std::vector<Anomaly> anomalies;
    double simTime = 0;
    int collected = 0, stationsBuilt = 0;
    std::vector<char> known;
    std::set<std::uint64_t> scanned;
};

class GameSession {
public:
    GameSession(eps::Engine& engine, const Balance& rules, std::uint32_t seed = 20261105, GameModeId mode = GameModeId::Normal);

    // Avanza la partida. Llama a Engine::Advance: el juego no debe llamarlo por su cuenta.
    void Step(double realSeconds, const ShipControls& controls, float focalPixels = 800.0f);

    void SetMode(GameModeId mode) { mode_ = mode; }
    GameModeId Mode() const { return mode_; }

    // Motor.
    bool SetDrive(DriveMode mode);
    void SetThrottle(double value);

    // Piloto automatico: orienta, acelera, frena y se queda en orbita del objetivo.
    bool SetAutopilot(const TargetRef& target);
    void CancelAutopilot();
    const std::optional<TargetRef>& Autopilot() const { return autopilot_; }
    const std::optional<TargetRef>& Orbiting() const { return orbiting_; }

    // Acciones sobre el objetivo. Devuelven false y dejan el motivo en los mensajes.
    bool DeployMining(const TargetRef& target);
    bool DeployPower(const TargetRef& target);
    bool Interact(const TargetRef& target);   // recoge de una estacion o explora una estructura
    bool UpgradeStation(int id, ModuleKind module);

    // Combate. Se dispara contra una nave o un bastion fijado como objetivo. El computador de
    // punteria resuelve el tiro con la gravedad de los astros que conoce; el canon espinal sale por
    // donde mira el casco, asi que hay que girar hasta la solucion antes de disparar.
    bool Fire(WeaponKind kind, const TargetRef& target);
    int FireAll(const TargetRef& target);       // todo lo que este listo; devuelve cuantas armas
    std::optional<FiringSolution> Solution(const TargetRef& target, WeaponKind kind) const;
    bool AimAtSolution(const TargetRef& target, WeaponKind kind);  // apunta el rumbo a la solucion
    const std::vector<Projectile>& Projectiles() const { return projectiles_; }
    const std::vector<ImpactFlash>& Impacts() const { return impacts_; }
    double WeaponRange(WeaponKind kind) const;
    bool WeaponReady(WeaponKind kind) const;     // alguna de esa familia ha recargado
    double WeaponCooldown(WeaponKind kind) const;  // segundos simulados hasta la primera que recargue
    int WeaponTier(WeaponKind kind) const;

    // El rumbo sigue la solucion del canon espinal hasta que el raton lo mueva o el blanco desaparezca.
    void TrackTarget(const TargetRef& target);
    void StopTracking() { tracking_.reset(); }
    const std::optional<TargetRef>& Tracking() const { return tracking_; }
    double ShipRadius() const { return rules_.ship.radius; }
    eps::Vec3d ShipVelocity() const;             // en el mundo
    std::vector<GravitySource> GravitySources() const;  // astros que tiran de los proyectiles aqui
    int Kills() const { return kills_; }

    // Escudos y calor (Defense.h). Solo un campo encendido a la vez; los conos van siempre puestos.
    bool SetShield(ShieldKind kind);            // elige el campo y lo enciende; false si no se tiene
    void SetShieldOn(bool on);
    ShieldKind CycleShield();                   // siguiente campo que se tenga; tras el ultimo, apagado
    bool FieldActive() const;                   // encendido, con energia, con polvo y sin sobrecalentar
    bool Overheated() const;
    void SetCoolant(CoolantKind kind, bool on);
    double RadiatorEfficiency() const { return radiators_; }  // 0 junto a una estrella, 1 lejos
    bool Ramming() const { return ramming_; }   // en curvatura, con la presion de ariete encima

    // Atraque en estaciones propias. Si esta lejos, primero va la ruta. Un haz guia los ultimos km:
    // no hay rebotes ni forma de pasarse. Al dar mando, o con Undock, la nave se suelta.
    bool Dock(const TargetRef& target, bool instant = false);
    void Undock();
    DockPhase Docking() const { return dockPhase_; }
    bool Docked() const { return dockPhase_ == DockPhase::Docked; }
    // La estacion mas cercana a menos de 3000 km, y la velocidad maxima que permite ahora.
    struct ApproachInfo {
        std::string station;
        double distance = 0, top = 0;  // km, km/s
        bool zone = false;             // dentro de la zona de atraque
    };
    const std::optional<ApproachInfo>& Approach() const { return approach_; }
    const Station* DockedStation() const;

    // A pie, en la cubierta de la estacion donde esta atracada.
    bool Disembark();
    bool Board(bool anywhere = false);
    bool OnFoot() const { return onFoot_; }
    bool CanBoard() const;
    const eps::HumanoidState& Player() const { return player_; }
    const eps::HumanoidRules& PersonRules() const { return body_; }
    const std::vector<Person>& Crew() const { return crew_; }
    const DeckPlan& Deck() const { return deck_; }
    StationKind DeckKind() const { return deckKind_; }
    eps::Vec3d DeckOrigin() const { return ship_.position; }  // km: la cubierta va donde esta la nave
    bool FirstPerson() const { return firstPerson_; }
    void ToggleFootView() { firstPerson_ = !firstPerson_; }

    // Escaner: gasta mucha energia, exige la nave parada y encuentra lo que alcance su nivel.
    bool StartScan();
    void CancelScan();
    bool Scanning() const { return scanProgress_ >= 0.0; }
    double ScanProgress() const { return scanProgress_; }
    double ScanRange() const;
    const std::string& LastScan() const { return lastScan_; }

    // Perder la nave.
    GamePhase Phase() const { return phase_; }
    double WreckAge() const { return wreckAge_; }
    const eps::Vec3d& WreckPosition() const { return wreckPosition_; }
    void SaveCheckpoint(const std::string& reason);
    void ContinueAfterDefeat();  // vuelve al punto de control o, en modo realista, a cero

    // Partidas guardadas. Si la nave acaba de perderse, se guarda el ultimo punto de control.
    SaveData Snapshot() const;
    bool Restore(const SaveData& data, std::string* error = nullptr);
    bool TakeSaveRequest();      // hay algo nuevo que guardar: ha pasado un punto de control
    bool TakeSaveDeletion();     // modo realista: la partida se ha perdido para siempre
    void Announce(const std::string& text) { Say(text); }

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
    int ScannedCount() const { return static_cast<int>(scanned_.size()); }
    void MarkScanned(int system);  // para pruebas y comandos
    void MarkKnown(int system);    // localiza un sistema y analiza todo lo que tiene

    // Modo creativo, desde la consola: nave invulnerable, energia, antimateria y materiales sin fin,
    // todos los motores y niveles, y todos los sistemas localizados. No se guarda.
    void SetCreative(bool on);
    bool Creative() const { return creative_; }
    // Crea una anomalia a km de la nave, delante de ella. Devuelve su id, o -1.
    int SpawnAnomaly(AnomalyKind kind, Stance stance, double km);
    void MarkAnalysisUsed() { analysisUsed_ = true; }
    bool AnalysisUsed() const { return analysisUsed_; }

    // Parte del tiempo normal que corre ahora: se frena en los momentos criticos.
    double TimeFocus() const { return timeFocus_; }

    // Atencion: mueve la nave directamente. Es para pruebas y capturas.
    bool JumpTo(const TargetRef& target, double distance);

    std::optional<TargetInfo> Resolve(const TargetRef& target) const;
    bool InRange(const TargetRef& target, double range) const;
    double Visibility() const;  // km; depende de lo espesa que este la nebulosa aqui
    float NebulaTone() const;   // de 0 a 1: punto de la paleta de la nebulosa en esta zona

    const ShipState& Ship() const { return ship_; }
    ShipState& Ship() { return ship_; }
    const StationNetwork& Stations() const { return stations_; }
    StationNetwork& Stations() { return stations_; }
    const std::vector<Anomaly>& Anomalies() const { return anomalies_; }
    std::vector<Anomaly>& Anomalies() { return anomalies_; }
    const std::vector<Contact>& Contacts() const { return contacts_; }
    const std::deque<std::string>& Log() const { return log_; }
    const Balance& Rules() const { return rules_; }
    double SimTime() const { return simTime_; }
    double& TimeScale() { return timeScale_; }
    int NearestSystem() const { return nearestSystem_; }
    bool InHeatZone() const { return inHeatZone_; }
    bool Exploring() const { return exploring_ >= 0; }

    // Tiempo: cuantas veces mas rapido pasa fuera que a bordo, por velocidad y por gravedad.
    double ExteriorFactor() const { return exteriorFactor_; }

    // Vuelo. Espacio frena y mantiene la nave quieta. En orbita no hace falta motor.
    void HoldPosition();
    bool CanScan() const;
    bool EnterOrbitHere();                        // circulariza alrededor de lo que domina aqui
    std::string DominantName() const;             // vacio = espacio abierto
    RouteReport Route() const;
    OrbitReport OrbitNow() const;
    const std::optional<CollisionWarning>& Warning() const { return warning_; }
    std::vector<eps::Vec3d> PredictedPath() const;  // lo que va a recorrer, para dibujarlo
    const std::string& LossReason() const { return lossReason_; }
    eps::World& GetWorld() { return engine_.GetWorld(); }
    const eps::World& GetWorld() const { return engine_.GetWorld(); }

    Dialogue& Conversation() { return dialogue_; }
    const Dialogue& Conversation() const { return dialogue_; }
    void MarkCursorUsed() { cursorUsed_ = true; }
    bool CursorUsed() const { return cursorUsed_; }
    int Collected() const { return collected_; }      // veces que se ha recogido de una estacion
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

    // Una zona del casco que se ha soltado: se aleja girando y se pierde.
    struct LostPiece {
        int zone = 0;
        int piece = -1;           // >= 0: solo esa pieza del modelo; -1: la zona entera
        eps::Vec3d pivot;         // m, en el modelo: el centro de lo que se ha soltado
        eps::Vec3d absolute;      // km, sin restar el origen del mundo
        eps::Vec3d velocity;      // km/s
        eps::Quatd orientation;   // la de la nave al soltarse, mas lo que ha girado
        eps::Vec3d spinAxis{0, 1, 0};
        double spin = 0;          // rad/s
        double age = 0;           // segundos reales
    };
    const std::vector<LostPiece>& LostPieces() const { return lostPieces_; }
    // De 0 a 1 por zona: lo recien golpeado, que aun brilla. Se enfria en unos segundos.
    const HullZones& ZoneHeat() const { return zoneHeat_; }
    // Golpe en una zona concreta, en unidades de casco: para pruebas y la consola. Con from (unitaria,
    // en el modelo de la nave), cuenta como un impacto sin escudos que deja su crater.
    void DamageZone(int zone, double hullAmount, const eps::Vec3d* from = nullptr);
    // Un proyectil que recorre start -> end (m, en el modelo de la nave) y da donde lo lleve su
    // trayectoria, sin escudos de por medio: para pruebas y la consola. Devuelve la zona.
    int ShootHull(const eps::Vec3d& start, const eps::Vec3d& end, double hullAmount);

    // Taller: se cambian modulos y motor atracado en una estacion propia (ShipModules.h). Montar
    // cuesta metal y silicato de la bodega; lo que se quita devuelve la mitad. why dice por que no.
    bool CanRefit() const { return Docked() || creative_; }  // en creativo, en cualquier sitio
    void Notice(const std::string& text) { Say(text); }  // una linea en el registro, desde fuera
    bool InstallModule(int slot, ShipModuleKind kind, std::string* why = nullptr);
    bool SetImpulseEngine(int tier, std::string* why = nullptr);
    // Tras fijar otra ShipDesign: ajusta los modulos y las piezas de la nave a ella.
    void RefitToDesign();

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
        double time = 0;           // simTime_ del golpe
        double age = 0;            // segundos reales desde el golpe
        eps::Vec3d absolute;       // km: centro de la nave entonces, sin restar el origen del mundo
        eps::Vec3d velocity;       // km/s de la nave entonces
        eps::Quatd orientation;    // de la nave entonces
    };
    const std::vector<HullHit>& HullHits() const { return hullHits_; }

    // La zona roja que acabo con la nave (-1 = ninguna) y si la apago o la hizo estallar.
    int FailedZone() const { return failedZone_; }
    bool ShutDown() const { return failedZone_ >= 0 && ZoneInfo(failedZone_).red == RedZone::Shutdown; }
    eps::Vec3d DriftVelocity() const { return driftVelocity_; }

private:
    void Say(const std::string& text);
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
    void SyncModuleWeapons();               // las torretas de los modulos, en ship_.weapons
    void WoundZone(int zone, double fraction);  // fraction: parte de la integridad de la zona
    void RecordHit(int zone, const eps::Vec3d& localFrom, double intensity, bool blunt, const eps::Vec3d* point = nullptr);
    void FailRedZone(int zone);
    void ShutDownShip(const std::string& reason);
    void SpreadWound(double amount);            // sin direccion: calor, presion
    void UpdateLostPieces(double seconds, double realSeconds);
    void Strike(Anomaly& a, const Projectile& p);
    void DamageAnomaly(Anomaly& a, DamageType type, double amount, int team);
    void UpdateThermal(double proper, double realSeconds);
    Anomaly* FindAnomaly(int id);
    const Anomaly* FindAnomaly(int id) const;
    void UpdateHeat(double seconds);
    void UpdateRepair(double seconds);
    void UpdateExploration(double hours);
    void UpdateScan(double hours);
    void UpdateContacts();
    void RefreshContacts();
    void LoseShip();
    void ResetCampaign();
    void PlaceAtStart();

    eps::Engine& engine_;
    Balance rules_;
    std::uint32_t seed_;
    GameModeId mode_;
    ShipState ship_;
    StationNetwork stations_;
    std::vector<Anomaly> anomalies_;
    std::vector<Contact> contacts_;
    std::vector<Projectile> projectiles_;
    std::vector<ImpactFlash> impacts_;
    DockPhase dockPhase_ = DockPhase::None;
    int dockWanted_ = -1;       // estacion a la que atracar al acabar la ruta
    int undockGrace_ = -1;      // estacion recien soltada: no choca hasta que la nave salga de ella
    double dockedAge_ = 0;      // s reales atracada: el ducto se despliega en los primeros
    bool creative_ = false;
    std::optional<ApproachInfo> approach_;
    bool onFoot_ = false, firstPerson_ = false;
    eps::HumanoidRules body_;
    eps::HumanoidState player_;
    DeckPlan deck_;
    StationKind deckKind_ = StationKind::Mining;
    std::vector<Person> crew_;
    int kills_ = 0;
    int hitStreak_ = 0;
    double hitDamage_ = 0, hitShield_ = 0;
    double directedLoss_ = 0;     // casco perdido en este paso por golpes con direccion
    HullZones zoneHeat_{};
    std::vector<LostPiece> lostPieces_;
    std::vector<HullHit> hullHits_;
    int nextHit_ = 1;
    int failedZone_ = -1;
    eps::Vec3d driftVelocity_;    // km/s reales de la nave apagada, para la cinematica
    double lastWarpCharge_ = 0;   // para calentar mientras se forma la burbuja
    double radiators_ = 1.0;
    bool ramming_ = false, ramWarned_ = false, overheatWarned_ = false;
    bool gasWarned_ = false;      // ya se aviso de que no queda gas para maniobrar
    std::deque<std::string> log_;
    std::mt19937 rng_;
    std::optional<TargetRef> autopilot_, orbiting_, tracking_;
    eps::Vec3d orbitOffset_;
    eps::Vec3d orbitNormal_{0, -1, 0};
    double orbitRate_ = 0;  // rad/s simulados; 0 = se queda quieta junto a su objetivo
    LocalFrame frame_;
    std::set<std::uint64_t> scanned_;
    bool analysisUsed_ = false;
    double timeFocus_ = 1.0;
    std::optional<CollisionWarning> warning_;
    double exteriorFactor_ = 1.0;
    RoutePhase routePhase_ = RoutePhase::None;
    double routeDistance_ = 0;
    bool fuelWarned_ = false;  // ya se aviso de que esta ruta no puede formar burbuja
    std::string lossReason_;
    std::map<std::uint32_t, std::vector<eps::AsteroidRock>> rocks_;
    std::vector<int> pins_;
    double simTime_ = 0, timeScale_ = 60.0;
    int nearestSystem_ = 0;
    int exploring_ = -1;
    bool inHeatZone_ = false;
    int steps_ = 0;

    Dialogue dialogue_;
    bool cursorUsed_ = false;
    int collected_ = 0;

    double scanProgress_ = -1.0;
    std::string lastScan_;

    GamePhase phase_ = GamePhase::Flying;
    double wreckAge_ = 0;
    eps::Vec3d wreckPosition_;
    std::optional<Checkpoint> checkpoint_;
    std::vector<char> known_;  // uno por sistema
    std::vector<char> visited_;  // donde la nave ha estado de verdad
    bool saveWanted_ = false, saveLost_ = false;
};

}  // namespace game
