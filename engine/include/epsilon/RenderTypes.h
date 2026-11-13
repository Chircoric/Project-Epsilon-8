// RenderTypes.h - El contrato entre la simulacion y el backend grafico.
//
// FramePacket es una copia inmutable de lo que se debe dibujar. No contiene punteros al
// mundo del juego, asi que el hilo de render nunca toca datos que la simulacion modifica.
//
// Cualquier backend (SDL_GPU, software, otro) solo tiene que implementar IRenderBackend.
#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "epsilon/Math3D.h"

namespace eps {

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
};

using TextureId = std::uint16_t;
inline constexpr TextureId kNoTexture = 0xFFFF;
using MeshId = std::uint16_t;
inline constexpr MeshId kNoMesh = 0xFFFF;

enum class BlendMode : std::uint8_t {
    Alpha,     // ALLEGRO_ADD, ALLEGRO_ALPHA, ALLEGRO_INVERSE_ALPHA
    Additive,  // ALLEGRO_ADD, ALLEGRO_ALPHA, ALLEGRO_ONE (halos y brillos)
};

// --- Lo que describe el juego ------------------------------------------------

// Superficie procedural de una esfera. El shader decide el dibujo y los colores la paleta.
enum class SurfaceKind : std::uint8_t { Terrestrial, Gas, Lava, Rock, Star };

struct SurfaceLook {
    SurfaceKind kind = SurfaceKind::Rock;
    Color a, b, c;    // paleta; su significado depende del tipo
    Color atmosphere{0, 0, 0, 0};  // a = intensidad del brillo en el borde
    float oceanLevel = 0.5f;  // fraccion cubierta de agua
    float iceCap = 0;         // 0 = sin hielo, 1 = cubierta por completo
    float cloudCover = 0.5f;
    float relief = 1;         // contraste del terreno; en gaseosos, densidad de las bandas
    float flattening = 0;     // achatamiento por rotacion: (ecuador - polo) / ecuador
};

// Reparto de trabajo segun el tamano en pantalla.
struct LodSettings {
    float sphereMinPixels = 6;  // por debajo se dibuja el sprite, que es mucho mas barato
    float detailBias = 0;       // positivo = mas nitido; negativo = mas difuso
    float maxOctaves = 10;      // capas de ruido por pixel como maximo
};

struct Renderable {
    std::int32_t node = -1;    // NodeId; -1 = hueco libre, se ignora
    bool hidden = false;       // no se calcula ni se dibuja
    float radius = 1;          // en unidades locales del nodo
    float minPixelRadius = 2;  // las estrellas lejanas no deben desaparecer
    float maxPixelRadius = 200;
    Color tint;
    TextureId texture = 0;
    BlendMode blend = BlendMode::Alpha;

    // Esfera: solo si surface >= 0. Lejos se dibuja el sprite de arriba.
    std::int32_t surface = -1;    // indice en FrameInput::surfaces
    std::int32_t lightNode = -1;  // NodeId de la estrella que la ilumina
    std::int32_t lightNode2 = -1; // segunda estrella; -1 = solo hay una
    float lightPower = 1, lightPower2 = 0;  // luminosidad: se compara con la distancia en cada imagen
    Color lightColor, lightColor2;
    MeshId mesh = kNoMesh;        // de cerca usa esta malla en lugar de una esfera
    bool asteroid = false;        // se apaga junto con los asteroides
    Vec3f scarDirection;          // centro del crater, en coordenadas del cuerpo
    float scarSize = 0;           // radio angular, rad; 0 = sin crater
    float scarHeat = 0;           // 0 = frio, 1 = roca fundida
    float fogReach = 1;           // su brillo atraviesa tantas veces mas niebla que lo normal

    float spriteScale = 1;        // radio del sprite / radio del disco dibujado en la imagen
    float seed = 0;               // cambia el dibujo de la superficie
    float fadeDistance = 0;       // el sprite se apaga entre esta distancia y el doble; 0 = nunca
    float hideInside = 0;         // el sprite no se dibuja mas cerca de esto; 0 = siempre
    double spinRate = 0;          // rad/s, en tiempo simulado
    double spinPhase = 0;         // rad
    Quatf tilt;                   // lleva el norte del cuerpo (-Y) a su eje de giro, en el mundo
};

struct LinkRenderable {
    std::int32_t a = -1, b = -1;  // NodeId de cada extremo
    Color color;
};

struct SkyStar {
    Vec3f direction;     // unitaria; las estrellas lejanas solo rotan, no se trasladan
    float size = 1;      // radio en pixeles
    float baseBrightness = 1;
    float twinkleRate = 0;
    float phase = 0;
};

// --- Lo que recibe el backend ------------------------------------------------

// Espacio de vista: +Z hacia dentro de la pantalla. Una instancia equivale a un cuadro
// orientado a la camara (billboard).
struct SpriteInstance {
    Vec3f viewPosition;
    float pixelRadius = 1;
    Color tint;
    std::uint32_t id = 0;  // indice del Renderable; desempata el orden
    TextureId texture = 0;
    BlendMode blend = BlendMode::Alpha;
    float fogReach = 1;
};

// Esfera calculada por pixel en el shader. La silueta y la rotacion son reales.
struct SphereInstance {
    Vec3f viewCenter;
    float radius = 1;
    Quatf viewToBody;       // de vista a coordenadas del cuerpo; incluye su giro
    Vec3f lightDirection;   // unitaria, en vista; (0, 0, 0) si no tiene luz
    float pixelRadius = 1;  // radio aparente de la silueta
    float detail = 1;       // capas de ruido, con decimales para que aparezcan sin saltos
    float seed = 0;
    float distance = 0;
    std::uint32_t id = 0;
    std::uint16_t surface = 0;
    Vec3f light2Direction;  // unitaria, en vista
    float light2Strength = 0;
    Color lightColor, light2Color;
    Vec3f scarDirection;
    float scarSize = 0, scarHeat = 0;
};

struct LineInstance {
    Vec3f viewA, viewB;
    Color color;
};

// Malla opaca. La posicion es relativa a la camara, asi que un tornillo a 2 m y una roca a
// 2000 km comparten el mismo depth buffer con precision proporcional a su distancia.
// Un crater en una malla, en sus propias unidades: hunde los vertices, quema alrededor y, si es
// hondo, abre un agujero con el borde fundido. Lo calcula la GPU en mesh.hlsl.
struct MeshCrater {
    Vec3f center;
    float radius = 0;
    float depth = 0;   // 0 a 1: por encima de ~0.45 perfora
    float heat = 0;    // 0 a 1: lo reciente, que aun brilla
    float seed = 0;
    float pad = 0;
};
inline constexpr int kMaxMeshCraters = 4;

// Una lampara pequena (una baliza, un foco de la bahia): ilumina las mallas de su alrededor y se
// apaga con la distancia hasta range. Si hay mas de kMaxPointLights, el shader de mallas usa las
// que quedan mas cerca de la camara.
struct PointLight {
    Vec3f viewPosition;   // km
    float range = 0.05f;  // km: mas lejos ya no ilumina
    Color color;          // color por intensidad (puede pasar de 1)
};
inline constexpr int kMaxPointLights = 64;

struct MeshInstance {
    Vec3f viewPosition;          // km
    float scale = 1;             // unidad de la malla > km; una malla en metros usa 0.001
    float stretchX = 1, stretchY = 1;  // escala extra en X e Y; la usan los modelos 2D
    Quatf rotation;              // de la malla a la vista
    Vec3f lightDirection;        // unitaria, en vista
    float ambient = 0.03f;
    Color color;
    float emissive = 0;          // el color brilla por si mismo
    std::uint32_t id = 0;
    MeshId mesh = 0;
    TextureId texture = 0;
    bool foreground = false;     // se dibuja tras las lentes: nunca aparece curvada en ellas
    Color lightColor;            // color e intensidad de la luz principal
    Vec3f light2Direction;       // segunda luz: otra estrella o lo que refleja un planeta
    Color light2Color{0, 0, 0, 0};
    Color ambientColor;          // tinte de la luz ambiente
    // Dano del casco, en el propio shader: 0 entero, 1 deshecho. damageDetail dice cuanto se ensena
    // (0 nada, 1 quemaduras, 2 grietas al rojo, 3 agujeros) y damageHeat, de 0 a 1, lo reciente
    // que es: lo recien golpeado aun brilla. Con damage o damageDetail a 0 no cuesta nada.
    float damage = 0;
    float damageHeat = 0;
    int damageDetail = 0;
    float damageSeed = 0;        // cada pieza rompe con su propio dibujo
    // Crateres: FramePacket::craters[craterFirst .. craterFirst + craterCount), hasta kMaxMeshCraters.
    std::uint32_t craterFirst = 0;
    std::uint32_t craterCount = 0;
    bool doubleSided = false;    // se ve por dentro: hace falta cuando un agujero deja ver el interior
};

struct MeshBatch {
    std::uint32_t first = 0, count = 0;
    MeshId mesh = 0;
    TextureId texture = 0;
    bool foreground = false;
    bool doubleSided = false;
};

// Una estrella cercana vista a traves de la niebla: la dispersa, ilumina y, de cerca, enseña su corona.
struct StarLight {
    Vec3f viewCenter;     // km
    float radius = 1;     // km
    Color color;
    float strength = 0;   // cuanto ilumina la niebla aqui
    float seed = 0;
    float corona = 1;     // 0 = sin corona: un agujero negro
};

// El aire iluminado de un planeta. Va con la niebla: dibujado antes, esta se lo comeria.
struct AtmosphereLight {
    Vec3f viewCenter;     // km
    float radius = 1;     // km
    Color color;          // a = intensidad
    Vec3f sunDirection;   // unitaria, en vista
    float seed = 0;
    Vec3f pole{0, 1, 0};  // eje de giro, en vista: el aire se ciñe al achatado
    float flattening = 0;
};

// Nube volumetrica con forma de esfera difusa.
struct NebulaInstance {
    Vec3f viewCenter;   // km
    float radius = 1;   // km
    Color inner, outer;  // color del nucleo denso y del borde
    float density = 1;
    float seed = 0;
    float distance = 0;
};

// spin = 0: Schwarzschild, no rota. spin > 0: Kerr, rota y su sombra se deforma.
struct BlackHoleInstance {
    Vec3f viewCenter;          // km
    float schwarzschildRadius = 1;  // km
    Vec3f viewAxis{0, -1, 0};  // eje de giro y normal del disco
    float spin = 0;            // a/M, de 0 a menos de 1
    float discInner = 3, discOuter = 12;  // en radios de Schwarzschild
    float discBrightness = 1;
    float seed = 0;
    float reach = 36;          // radio de la zona afectada, en radios de Schwarzschild
    float pixelRadius = 1;     // el mismo radio, en pixeles
    float distance = 0;
};

// Etapa final de la imagen. Lo que vale 0 se omite.
struct PostSettings {
    bool bloom = true;
    float bloomStrength = 0.55f, bloomThreshold = 1.0f;
    float exposure = 1.0f;
    float vignette = 0.22f;
    float aberration = 0.0012f;  // fraccion de la pantalla en las esquinas
    float grain = 0.004f;        // apenas: mas fuerte se veia como polvo sobre la nebulosa
    float starBoost = 1.8f;      // brillo de la superficie de las estrellas
    // Calidad de la luz en los shaders: 0 baja ... 3 ultra. Con 3, los planetas llevan dispersion
    // atmosferica, suelo rugoso y mar con Fresnel; la nebulosa da mas pasos.
    int detail = 2;
};

// Sprites contiguos que comparten estado: un backend de GPU emite un dibujado instanciado por lote.
struct SpriteBatch {
    std::uint32_t first = 0, count = 0;
    TextureId texture = 0;
    BlendMode blend = BlendMode::Alpha;
};

struct FrameStats {
    std::size_t renderablesTotal = 0;
    std::size_t spritesVisible = 0;
    std::size_t spheresVisible = 0;
    std::size_t linesVisible = 0;
    std::size_t meshesVisible = 0;
    std::size_t asteroidsCalculated = 0;  // rocas y polvo calculados este fotograma
    std::size_t blastsVisible = 0;        // explosiones en curso
};

struct FramePacket {
    std::uint64_t frameNumber = 0;
    double time = 0;
    int width = 1920, height = 1080;
    float focalPx = 960;
    float nearZ = 1;

    Mat4f viewRotation = Mat4f::Identity();
    Mat4f projection = Mat4f::Identity();

    Color clearColor{0.02f, 0.02f, 0.04f, 1.0f};
    Color screenTint{0, 0, 0, 0};  // niebla de nebulosa
    float fogDistance = 0;         // km en que queda un 37 % de lo que hay detras; 0 = sin niebla
    Color fogColor;
    Vec3f fogOrigin;               // desplaza su textura para que no parezca pegada a la camara
    // Con fogScale > 0 la niebla es una nube de verdad alrededor: fogOrigin es la camara dentro de
    // una baldosa de ruido (de 0 a 1) y fogScale los km que mide esa baldosa.
    float fogScale = 0;
    bool fogRampOn = false;        // la niebla recorre esta paleta en lugar de un solo color
    std::array<Color, 4> fogRamp{};
    float fogTone = 0;             // punto de la paleta en esta zona, de 0 a 1
    std::vector<StarLight> starLights;
    std::vector<AtmosphereLight> atmospheres;
    // Planetas y lunas grandes en pantalla, de mayor a menor (xyz = centro en vista, km; w = radio):
    // tapan la luz de la estrella y dejan su sombra en la nebulosa.
    std::vector<Vec4f> lightBlockers;

    TextureId skyTexture = kNoTexture;  // panorama equirectangular (2:1)
    Color skyTint;

    std::shared_ptr<const std::vector<SkyStar>> sky;  // inmutable; se comparte sin copiar
    std::shared_ptr<const std::vector<SurfaceLook>> surfaces;  // inmutable
    std::vector<SphereInstance> spheres;              // de atras hacia adelante
    std::vector<SpriteInstance> sprites;              // de atras hacia adelante
    std::vector<SpriteBatch> batches;
    std::vector<LineInstance> lines;
    std::vector<MeshInstance> meshes;       // agrupadas por malla y textura
    std::vector<MeshBatch> meshBatches;
    std::vector<MeshCrater> craters;        // los de cada malla: MeshInstance::craterFirst
    std::vector<PointLight> pointLights;    // lamparas; de mas de kMaxPointLights, cuentan las mas cercanas
    std::vector<NebulaInstance> nebulae;    // de atras hacia adelante
    std::vector<BlackHoleInstance> blackHoles;  // de atras hacia adelante
    PostSettings post;
    FrameStats stats;

    // Esfera extra dibujada en un rectangulo de la pantalla, encima de todo. La usa la interfaz.
    bool hasMiniature = false;
    SphereInstance miniature;
    int miniatureX = 0, miniatureY = 0, miniatureSize = 0;  // pixeles
    float miniatureFocalPx = 1;
    // Corona de una estrella alrededor de la miniatura: el mismo calculo que la niebla hace junto a
    // una estrella real, en un rectangulo mayor que la miniatura y por debajo de ella.
    bool miniatureCorona = false;
    int coronaX = 0, coronaY = 0, coronaW = 0, coronaH = 0;  // pixeles: donde puede verse
    Vec3f coronaColor{1, 1, 1};
    float coronaIntensity = 0.6f;  // por debajo de 1 para no tapar el disco
    float coronaSeed = 0;
    Quatf coronaView;              // de la vista de la miniatura al mundo: gira con el arrastre
};

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;
    // Se llama siempre desde el mismo hilo de render.
    virtual void Render(const FramePacket& frame) = 0;
};

}  // namespace eps
