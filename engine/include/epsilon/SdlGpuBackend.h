// SdlGpuBackend.h - Backend de GPU sobre SDL_GPU (Direct3D 12).
//
// Reglas de hilos de SDL_GPU:
//   * La textura del swapchain se adquiere en el hilo que creo la ventana.
//   * Un command buffer se usa y se envia en el hilo que lo adquirio.
// Por eso todos los metodos de esta clase se llaman desde el hilo principal, y la
// simulacion vive en otro hilo y entrega FramePacket por un TripleBuffer.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "epsilon/Mesh.h"
#include "epsilon/Mesh.h"
#include "epsilon/RenderTypes.h"

struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUSampler;
struct SDL_GPUTexture;
struct SDL_GPUBuffer;
struct SDL_GPUTransferBuffer;
struct SDL_GPUCommandBuffer;
struct SDL_GPURenderPass;

namespace eps {

// Capa dibujada encima de la escena, sin depth buffer: interfaz, menus o depuracion.
// El motor no sabe que libreria usa; solo le da el command buffer en dos momentos.
class IGpuOverlay {
public:
    virtual ~IGpuOverlay() = default;
    virtual void Prepare(SDL_GPUCommandBuffer* cmd) = 0;                       // antes de la pasada
    virtual void Draw(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass) = 0;  // dentro de ella
};

class SdlGpuBackend final : public IRenderBackend {
public:
    static constexpr TextureId kWhiteTexture = 0;
    static constexpr TextureId kGlowTexture = 1;
    static constexpr TextureId kRingTexture = 2;

    SdlGpuBackend() = default;
    ~SdlGpuBackend() override;

    SdlGpuBackend(const SdlGpuBackend&) = delete;
    SdlGpuBackend& operator=(const SdlGpuBackend&) = delete;

    // Devuelve false y deja el motivo en LastError().
    bool Initialize(SDL_Window* window, bool debugLayer);
    void Shutdown();

    // PNG a textura con mipmaps. Devuelve kNoTexture si falla.
    // maxSize > 0 reduce la imagen antes de subirla: un sprite que solo se ve de lejos
    // no necesita ocupar memoria de video a resolucion completa.
    TextureId LoadTexture(const std::string& path, int maxSize = 0);

    // RGBA de 8 bits por canal, ya en memoria: texturas generadas por el juego.
    TextureId CreateTexture(const void* rgba, int width, int height);

    // Mapas PBR de una textura de color, para las mallas que la usen: relieve (normal de tipo OpenGL)
    // y oclusion-rugosidad-metal en R, G y B. kNoTexture deja el valor neutro.
    void SetTextureMaps(TextureId color, TextureId normal, TextureId orm);

    // Sube una malla a la GPU. La malla en memoria puede liberarse despues.
    MeshId CreateMesh(const MeshData& mesh);
    void ReleaseMesh(MeshId id);
    std::size_t MeshCount() const;

    // Efecto de pantalla propio, en DXIL; el contrato esta en shaders/effects/scanlines.hlsl.
    // Se aplica en orden, sobre la imagen ya a 8 bits y antes de la interfaz. Devuelve su indice o -1.
    // Con behindForeground, el efecto se aplica a la escena antes de dibujar lo que va en primer
    // plano: lo que ocurre fuera de la nave no le afecta a ella.
    int AddPostEffect(const std::string& name, const unsigned char* pixelShader, std::size_t size, bool behindForeground = false);
    void SetPostEffect(int index, bool enabled, float intensity = 1.0f);
    // Doce valores libres para el efecto: llegan en b0 de space3, tras uIntensity.
    void SetPostEffectParams(int index, const float (&params)[12]);
    int FindPostEffect(const std::string& name) const;

    // false desactiva la sincronizacion vertical; sirve para medir el rendimiento.
    bool SetVsync(bool enabled);

    // Parte de la resolucion a la que se dibuja la escena, de 0.25 a 1; al final se amplia. La
    // interfaz y los efectos del juego siguen a resolucion completa, asi que el texto no se emborrona.
    // Es lo que mas alivia a una tarjeta modesta: a 0.5 se pinta la cuarta parte de los pixeles.
    void SetRenderScale(float scale);
    float RenderScale() const { return renderScale_; }

    // Sombras de la nave sobre si misma. Apagadas se ahorra dibujarla una segunda vez desde su
    // estrella y las doce lecturas por pixel del mapa de sombras.
    void SetShadows(bool enabled) { shadows_ = enabled; }

    void Render(const FramePacket& frame) override;

    // nullptr la quita. La capa debe sobrevivir hasta que se quite o se apague el backend.
    void SetOverlay(IGpuOverlay* overlay) { overlay_ = overlay; }
    SDL_GPUDevice* Device() const { return device_; }
    int TargetFormat() const;  // SDL_GPUTextureFormat de la imagen final

    // Guarda como PNG el proximo fotograma, leido de la GPU.
    void RequestScreenshot(const std::string& path) { screenshotPath_ = path; }

    const std::string& LastError() const { return lastError_; }
    std::string DriverName() const;
    std::uint64_t PresentedFrames() const { return presentedFrames_; }
    std::uint64_t TextureBytes() const { return textureBytes_; }  // memoria de video en texturas

private:
    enum class DepthMode { None, Ignore, Test, TestAndWrite };
    enum class Blend { Alpha, Additive, Opaque, One, Nebula };

    struct PipelineDesc {
        const unsigned char* vs = nullptr;
        std::size_t vsSize = 0;
        int vsUniforms = 0, vsStorage = 0;
        const unsigned char* fs = nullptr;
        std::size_t fsSize = 0;
        int fsSamplers = 0, fsUniforms = 0, fsStorage = 0;
        Blend blend = Blend::Alpha;
        DepthMode depth = DepthMode::None;
        int format = 0;       // SDL_GPUTextureFormat
        bool meshInput = false;
        bool twoSided = false;  // mallas: sin descartar las caras de atras
    };

    bool CreatePipelines();
    SDL_GPUGraphicsPipeline* CreatePipeline(const PipelineDesc& desc);
    SDL_GPUGraphicsPipeline* CreateShadowPipeline();  // solo profundidad, sin color
    // Dibuja la nave (las mallas de primer plano) vista desde su estrella. Devuelve si hay sombras y
    // deja en shadowUniforms la matriz de vista al mapa.
    bool DrawShadowMap(SDL_GPUCommandBuffer* cmd, const FramePacket& frame, float* shadowUniforms);
    TextureId UploadTexture(const void* rgba, int width, int height, int pitch, bool mipmaps);
    bool CreateNoiseVolume();
    bool EnsureTarget(int width, int height);
    bool EnsureBuffers(std::uint32_t spriteBytes, std::uint32_t lineBytes, std::uint32_t sphereBytes, std::uint32_t meshBytes);
    void ReleaseTargets();
    void BuildGpuData(const FramePacket& frame);
    void UploadGpuData(SDL_GPUCommandBuffer* cmd);
    void DrawNebulae(SDL_GPUCommandBuffer* cmd, const FramePacket& frame);
    void DrawBlackHoles(SDL_GPUCommandBuffer* cmd, const FramePacket& frame);
    void DrawBloom(SDL_GPUCommandBuffer* cmd, const FramePacket& frame);
    void DrawComposite(SDL_GPUCommandBuffer* cmd, const FramePacket& frame);
    void SaveScreenshot(SDL_GPUCommandBuffer* cmd);

    struct GpuSprite {
        float x, y, z, pixelRadius;
        float r, g, b, a;
    };
    struct GpuLine {
        float ax, ay, az, pad;
        float bx, by, bz, widthPx;
        float r, g, b, a;
    };
    struct GpuSphere {  // 208 bytes; misma disposicion que sphere.hlsl
        float x, y, z, radius;
        float qx, qy, qz, qw;
        float lightX, lightY, lightZ, pixelRadius;
        float detail, seed, kind, flattening;
        Color a, b, c, atmosphere;
        float oceanLevel, iceCap, cloudCover, relief;
        float light2X, light2Y, light2Z, light2Strength;
        float lightR, lightG, lightB, scarHeat;
        float light2R, light2G, light2B, pad;
        float scarX, scarY, scarZ, scarSize;
    };
    struct GpuMeshInstance {  // 144 bytes; misma disposicion que mesh.hlsl
        float x, y, z, scale;
        float qx, qy, qz, qw;
        float lightX, lightY, lightZ, ambient;
        Color color;
        float emissive, stretchX, stretchY, damage;  // dano + 2 x semilla
        Color lightColor;
        float light2X, light2Y, light2Z, damageDetail;  // nivel + calor
        Color light2Color;
        Color ambientColor;
    };
    struct GpuMesh {
        SDL_GPUBuffer* vertices = nullptr;
        SDL_GPUBuffer* indices = nullptr;
        std::uint32_t indexCount = 0;
        float radius = 0;  // del vertice mas lejano al origen, en unidades de la malla
    };
    struct PostEffect {
        std::string name;
        SDL_GPUGraphicsPipeline* pipeline = nullptr;
        bool enabled = false;
        float intensity = 1.0f;
        float params[12];
        bool behindForeground = false;
    };
    static constexpr int kBloomLevels = 5;

    SDL_Window* window_ = nullptr;
    SDL_GPUDevice* device_ = nullptr;

    SDL_GPUGraphicsPipeline* alphaPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* additivePipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* softAlphaPipeline_ = nullptr;     // sprites del mundo que se funden con lo opaco
    SDL_GPUGraphicsPipeline* softAdditivePipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* linePipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* skyPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* spherePipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* miniaturePipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* starCoronaPipeline_ = nullptr;  // corona de la miniatura del mapa
    SDL_GPUGraphicsPipeline* meshPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* meshTwoSidedPipeline_ = nullptr;  // con crateres: por un agujero se ve el interior
    SDL_GPUGraphicsPipeline* nebulaPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* nebulaBlendPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* blackHolePipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* bloomDownPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* bloomUpPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* compositePipeline_ = nullptr;

    SDL_GPUSampler* clampSampler_ = nullptr;
    SDL_GPUSampler* repeatSampler_ = nullptr;
    SDL_GPUSampler* postSampler_ = nullptr;    // sin mipmaps ni sesgo: lee imagenes completas
    SDL_GPUSampler* pointSampler_ = nullptr;
    SDL_GPUSampler* meshSampler_ = nullptr;    // repeticion con anisotropia: las texturas de las mallas
    TextureId flatNormal_ = kNoTexture, plainOrm_ = kNoTexture;
    std::vector<std::array<TextureId, 2>> textureMaps_;  // por textura de color: relieve y ORM

    SDL_GPUTexture* target_ = nullptr;       // imagen final de 8 bits
    SDL_GPUTexture* scene_ = nullptr;        // escena HDR
    SDL_GPUTexture* lensSource_ = nullptr;   // copia de la escena que deforma el agujero negro
    SDL_GPUTexture* nebula_ = nullptr;       // nubes a media resolucion
    SDL_GPUTexture* shadowMap_ = nullptr;    // la nave vista desde su estrella: sombras sobre si misma
    SDL_GPUGraphicsPipeline* shadowPipeline_ = nullptr;
    SDL_GPUTexture* effectTarget_ = nullptr; // efectos del juego, alternando con target_
    SDL_GPUTexture* bloom_[kBloomLevels] = {};
    SDL_GPUTexture* noiseVolume_ = nullptr;
    SDL_GPUTexture* depth_ = nullptr;
    int targetWidth_ = 0, targetHeight_ = 0;
    int sceneWidth_ = 0, sceneHeight_ = 0;  // la escena, a renderScale_ de lo anterior
    float renderScale_ = 1.0f, targetScale_ = 1.0f;  // la pedida y la de las imagenes creadas

    SDL_GPUBuffer* spriteBuffer_ = nullptr;
    SDL_GPUBuffer* lineBuffer_ = nullptr;
    SDL_GPUBuffer* sphereBuffer_ = nullptr;
    SDL_GPUBuffer* meshBuffer_ = nullptr;
    SDL_GPUTransferBuffer* uploadBuffer_ = nullptr;
    std::uint32_t spriteCapacity_ = 0, lineCapacity_ = 0, sphereCapacity_ = 0, meshCapacity_ = 0, uploadCapacity_ = 0;

    std::vector<SDL_GPUTexture*> textures_;
    std::uint64_t textureBytes_ = 0;
    std::vector<GpuSprite> gpuSprites_;
    std::vector<GpuLine> gpuLines_;
    std::vector<GpuSphere> gpuSpheres_;
    std::vector<GpuMeshInstance> gpuMeshes_;
    std::vector<GpuMesh> meshes_;
    std::vector<MeshId> freeMeshes_;
    std::vector<PostEffect> effects_;
    std::uint32_t worldSphereCount_ = 0;
    IGpuOverlay* overlay_ = nullptr;
    std::uint32_t skyStarCount_ = 0;
    std::uint32_t worldSpriteFirst_ = 0;
    bool hasScreenTint_ = false;
    bool shadows_ = true;

    std::string screenshotPath_;
    std::string lastError_;
    std::uint64_t presentedFrames_ = 0;
};

}  // namespace eps
