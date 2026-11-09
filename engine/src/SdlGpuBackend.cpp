#include "epsilon/SdlGpuBackend.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

#include "epsilon/FrameBuilder.h"
#include "epsilon/Math3D.h"

// Generados con dxc durante la compilacion.
#include "vs_sprite.h"
#include "vs_line.h"
#include "vs_fullscreen.h"
#include "ps_textured.h"
#include "ps_line.h"
#include "ps_sky.h"
#include "vs_sphere.h"
#include "ps_sphere.h"
#include "vs_mesh.h"
#include "ps_mesh.h"
#include "vs_post.h"
#include "ps_bloom_down.h"
#include "ps_bloom_up.h"
#include "ps_composite.h"
#include "ps_copy.h"
#include "ps_nebula.h"
#include "ps_star_corona.h"
#include "ps_blackhole.h"

namespace eps {

namespace {

constexpr SDL_GPUTextureFormat kTargetFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
constexpr SDL_GPUTextureFormat kSceneFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
constexpr SDL_GPUTextureFormat kDepthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
constexpr float kSpriteMipBias = 0.75f;  // los sprites usan un mipmap mas difuso y barato
constexpr float kLineWidthPx = 1.5f;
constexpr float kSkyDistance = 1.0e4f;  // lejos del plano cercano para que no se recorte
constexpr int kMaxNebulae = 4;
constexpr int kMaxStarLights = 2;
constexpr int kMaxAtmospheres = 2;
constexpr int kNoiseSize = 64;

struct FrameUniforms {
    Mat4f projection;
    float viewportW, viewportH;
    std::uint32_t firstInstance;
    float pad;
};
struct SphereUniforms {
    float nearZ, time, starBoost, pad;
};
struct BloomUniforms {
    float texelX, texelY, threshold, strength;
};
struct CompositeUniforms {
    float bloomStrength, exposure, vignette, aberration, grain, time, pad0, pad1;
};
struct EffectUniforms {
    float width, height, time, intensity;
    float params[12];
};
struct NebulaUniforms {
    Mat4f viewToWorld;
    float viewportW, viewportH, focalPx, nearZ;
    float time, count, fogTone, fogRampOn;
    float fog[4];        // color y distancia
    float fogOrigin[4];  // w = estrellas
    float ramp[4][4];
    float starCenter[kMaxStarLights][4];  // km en vista, radio en km
    float starColor[kMaxStarLights][4];   // a = intensidad
    float starShape[kMaxStarLights][4];   // semilla, corona
    float atmosphereCount[4];
    float atmosphereCenter[kMaxAtmospheres][4];  // km en vista, radio en km
    float atmosphereColor[kMaxAtmospheres][4];   // a = intensidad
    float atmosphereSun[kMaxAtmospheres][4];     // direccion, semilla
    float atmosphereAxis[kMaxAtmospheres][4];    // eje de giro, achatamiento
    float center[kMaxNebulae][4];
    float inner[kMaxNebulae][4];
    float outer[kMaxNebulae][4];
};
struct BlackHoleUniforms {
    Mat4f viewToWorld;
    float centerX, centerY, centerZ, rs;
    float axisX, axisY, axisZ, spin;
    float viewportW, viewportH, focalPx, nearZ;
    float discInner, discOuter, brightness, seed;
    float time, horizon, reach, hasSky;
    Color skyTint;
};
struct BatchUniforms {
    std::uint32_t first, pad0, pad1, pad2;
};
struct SkyUniforms {
    Mat4f cameraToWorld;
    Color tint;
    float viewportW, viewportH, focalPx, pad;
};

// 0 alfa, 1 suma, 2 opaco, 3 uno mas uno, 4 nebulosa.
SDL_GPUColorTargetBlendState MakeBlendState(int mode) {
    SDL_GPUColorTargetBlendState b{};
    if (mode == 2) return b;
    b.enable_blend = true;
    b.color_blend_op = SDL_GPU_BLENDOP_ADD;
    b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    b.dst_color_blendfactor = mode == 1 ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    if (mode == 3) {
        b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    }
    if (mode == 4) {  // luz sumada; lo de detras se atenua segun la transparencia de la nube
        b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    }
    // El alfa del destino se mantiene en 1 para que las capturas sean opacas.
    b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    return b;
}

// Ruido que se repite sin costuras: la nube lo muestrea varias veces con escalas distintas.
std::vector<std::uint8_t> MakeNoiseVolume(int size) {
    std::vector<float> lattice(static_cast<std::size_t>(size) * size * size);
    std::uint32_t state = 0x1234ABCDu;
    for (float& v : lattice) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        v = static_cast<float>(state & 0xFFFF) / 65535.0f;
    }
    const auto at = [&](int x, int y, int z) {
        const int m = size - 1;
        return lattice[static_cast<std::size_t>(((z & m) * size + (y & m)) * size + (x & m))];
    };
    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

    std::vector<float> sum(lattice.size(), 0.0f);
    float amplitude = 0.5f, total = 0.0f;
    for (int period = size / 4; period >= 2; period /= 2) {
        const float scale = 1.0f / static_cast<float>(period);
        const auto grid = [&](int i, int j, int k) { return at(i * period, j * period, k * period); };
        std::size_t index = 0;
        for (int z = 0; z < size; ++z) {
            for (int y = 0; y < size; ++y) {
                for (int x = 0; x < size; ++x, ++index) {
                    const int ix = x / period, iy = y / period, iz = z / period;
                    const float fx = static_cast<float>(x % period) * scale, fy = static_cast<float>(y % period) * scale, fz = static_cast<float>(z % period) * scale;
                    const float sx = fx * fx * (3.0f - 2.0f * fx), sy = fy * fy * (3.0f - 2.0f * fy), sz = fz * fz * (3.0f - 2.0f * fz);
                    const float low = lerp(lerp(grid(ix, iy, iz), grid(ix + 1, iy, iz), sx), lerp(grid(ix, iy + 1, iz), grid(ix + 1, iy + 1, iz), sx), sy);
                    const float high = lerp(lerp(grid(ix, iy, iz + 1), grid(ix + 1, iy, iz + 1), sx), lerp(grid(ix, iy + 1, iz + 1), grid(ix + 1, iy + 1, iz + 1), sx), sy);
                    sum[index] += lerp(low, high, sz) * amplitude;
                }
            }
        }
        total += amplitude;
        amplitude *= 0.5f;
    }
    std::vector<std::uint8_t> out(sum.size());
    for (std::size_t i = 0; i < sum.size(); ++i) out[i] = static_cast<std::uint8_t>(std::clamp(sum[i] / total, 0.0f, 1.0f) * 255.0f + 0.5f);
    return out;
}

std::uint32_t MipCount(int width, int height) {
    std::uint32_t levels = 1;
    for (int size = std::max(width, height); size > 1; size /= 2) ++levels;
    return levels;
}

}  // namespace

SdlGpuBackend::~SdlGpuBackend() { Shutdown(); }

bool SdlGpuBackend::Initialize(SDL_Window* window, bool debugLayer) {
    device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL, debugLayer, "direct3d12");
    if (!device_ && debugLayer) device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL, false, "direct3d12");
    if (!device_) {
        lastError_ = std::string("SDL_CreateGPUDevice: ") + SDL_GetError();
        return false;
    }
    if (!SDL_ClaimWindowForGPUDevice(device_, window)) {
        lastError_ = std::string("SDL_ClaimWindowForGPUDevice: ") + SDL_GetError();
        return false;
    }
    window_ = window;

    SDL_GPUSamplerCreateInfo sampler{};
    sampler.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    sampler.max_lod = 1000.0f;
    sampler.mip_lod_bias = kSpriteMipBias;
    sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    clampSampler_ = SDL_CreateGPUSampler(device_, &sampler);
    sampler.mip_lod_bias = 0.0f;
    postSampler_ = SDL_CreateGPUSampler(device_, &sampler);
    sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    repeatSampler_ = SDL_CreateGPUSampler(device_, &sampler);
    sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    pointSampler_ = SDL_CreateGPUSampler(device_, &sampler);
    if (!clampSampler_ || !postSampler_ || !repeatSampler_ || !pointSampler_) {
        lastError_ = std::string("SDL_CreateGPUSampler: ") + SDL_GetError();
        return false;
    }

    if (!CreatePipelines() || !CreateNoiseVolume()) return false;

    // Texturas integradas: blanco liso y halo suave.
    const std::uint32_t white = 0xFFFFFFFFu;
    if (UploadTexture(&white, 1, 1, 4, false) != kWhiteTexture) return false;

    constexpr int kGlowSize = 128;
    std::vector<std::uint32_t> glow(kGlowSize * kGlowSize);
    for (int y = 0; y < kGlowSize; ++y) {
        for (int x = 0; x < kGlowSize; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f) / kGlowSize * 2.0f - 1.0f;
            const float dy = (static_cast<float>(y) + 0.5f) / kGlowSize * 2.0f - 1.0f;
            const float fall = std::max(0.0f, 1.0f - std::sqrt(dx * dx + dy * dy));
            const auto alpha = static_cast<std::uint32_t>(fall * fall * 255.0f + 0.5f);
            glow[static_cast<std::size_t>(y) * kGlowSize + x] = 0x00FFFFFFu | (alpha << 24);
        }
    }
    if (UploadTexture(glow.data(), kGlowSize, kGlowSize, kGlowSize * 4, true) != kGlowTexture) return false;

    // Anillo fino con el borde exterior nitido: la onda de choque de las explosiones.
    constexpr int kRingSize = 256;
    std::vector<std::uint32_t> ring(kRingSize * kRingSize);
    for (int y = 0; y < kRingSize; ++y) {
        for (int x = 0; x < kRingSize; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f) / kRingSize * 2.0f - 1.0f;
            const float dy = (static_cast<float>(y) + 0.5f) / kRingSize * 2.0f - 1.0f;
            const float r = std::sqrt(dx * dx + dy * dy);
            const float outer = std::clamp((0.97f - r) / 0.02f, 0.0f, 1.0f);
            const float inner = std::clamp((r - 0.78f) / 0.19f, 0.0f, 1.0f);
            const auto alpha = static_cast<std::uint32_t>(outer * inner * inner * inner * 255.0f + 0.5f);
            ring[static_cast<std::size_t>(y) * kRingSize + x] = 0x00FFFFFFu | (alpha << 24);
        }
    }
    return UploadTexture(ring.data(), kRingSize, kRingSize, kRingSize * 4, true) == kRingTexture;
}

void SdlGpuBackend::Shutdown() {
    if (!device_) return;
    SDL_WaitForGPUIdle(device_);

    for (SDL_GPUTexture* t : textures_) SDL_ReleaseGPUTexture(device_, t);
    textures_.clear();
    ReleaseTargets();
    if (noiseVolume_) SDL_ReleaseGPUTexture(device_, noiseVolume_);
    noiseVolume_ = nullptr;
    for (const GpuMesh& m : meshes_) {
        if (m.vertices) SDL_ReleaseGPUBuffer(device_, m.vertices);
        if (m.indices) SDL_ReleaseGPUBuffer(device_, m.indices);
    }
    meshes_.clear();
    freeMeshes_.clear();
    if (spriteBuffer_) SDL_ReleaseGPUBuffer(device_, spriteBuffer_);
    if (lineBuffer_) SDL_ReleaseGPUBuffer(device_, lineBuffer_);
    if (sphereBuffer_) SDL_ReleaseGPUBuffer(device_, sphereBuffer_);
    if (meshBuffer_) SDL_ReleaseGPUBuffer(device_, meshBuffer_);
    if (uploadBuffer_) SDL_ReleaseGPUTransferBuffer(device_, uploadBuffer_);
    for (SDL_GPUSampler* s : {clampSampler_, postSampler_, repeatSampler_, pointSampler_})
        if (s) SDL_ReleaseGPUSampler(device_, s);
    for (SDL_GPUGraphicsPipeline* p : {alphaPipeline_, additivePipeline_, linePipeline_, skyPipeline_, spherePipeline_, miniaturePipeline_, starCoronaPipeline_, meshPipeline_, meshTwoSidedPipeline_,
                                       nebulaPipeline_, nebulaBlendPipeline_, blackHolePipeline_, bloomDownPipeline_, bloomUpPipeline_, compositePipeline_})
        if (p) SDL_ReleaseGPUGraphicsPipeline(device_, p);
    for (const PostEffect& e : effects_)
        if (e.pipeline) SDL_ReleaseGPUGraphicsPipeline(device_, e.pipeline);
    effects_.clear();

    if (window_) SDL_ReleaseWindowFromGPUDevice(device_, window_);
    SDL_DestroyGPUDevice(device_);

    device_ = nullptr;
    window_ = nullptr;
    spriteBuffer_ = lineBuffer_ = sphereBuffer_ = meshBuffer_ = nullptr;
    uploadBuffer_ = nullptr;
    clampSampler_ = postSampler_ = repeatSampler_ = pointSampler_ = nullptr;
    alphaPipeline_ = additivePipeline_ = linePipeline_ = skyPipeline_ = spherePipeline_ = miniaturePipeline_ = starCoronaPipeline_ = meshPipeline_ = meshTwoSidedPipeline_ = nullptr;
    nebulaPipeline_ = nebulaBlendPipeline_ = blackHolePipeline_ = bloomDownPipeline_ = bloomUpPipeline_ = compositePipeline_ = nullptr;
    spriteCapacity_ = lineCapacity_ = sphereCapacity_ = meshCapacity_ = uploadCapacity_ = 0;
}

int SdlGpuBackend::TargetFormat() const { return static_cast<int>(kTargetFormat); }

std::string SdlGpuBackend::DriverName() const { return device_ ? SDL_GetGPUDeviceDriver(device_) : ""; }

bool SdlGpuBackend::SetVsync(bool enabled) {
    const SDL_GPUPresentMode mode = enabled ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;
    if (!device_ || !SDL_WindowSupportsGPUPresentMode(device_, window_, mode)) return false;
    return SDL_SetGPUSwapchainParameters(device_, window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
}

SDL_GPUGraphicsPipeline* SdlGpuBackend::CreatePipeline(const PipelineDesc& d) {
    SDL_GPUShaderCreateInfo info{};
    info.format = SDL_GPU_SHADERFORMAT_DXIL;
    info.code = d.vs;
    info.code_size = d.vsSize;
    info.entrypoint = "main";
    info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    info.num_uniform_buffers = static_cast<Uint32>(d.vsUniforms);
    info.num_storage_buffers = static_cast<Uint32>(d.vsStorage);
    SDL_GPUShader* vertex = SDL_CreateGPUShader(device_, &info);

    SDL_GPUShaderCreateInfo finfo{};
    finfo.format = SDL_GPU_SHADERFORMAT_DXIL;
    finfo.code = d.fs;
    finfo.code_size = d.fsSize;
    finfo.entrypoint = "main";
    finfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    finfo.num_samplers = static_cast<Uint32>(d.fsSamplers);
    finfo.num_uniform_buffers = static_cast<Uint32>(d.fsUniforms);
    finfo.num_storage_buffers = static_cast<Uint32>(d.fsStorage);
    SDL_GPUShader* fragment = SDL_CreateGPUShader(device_, &finfo);

    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    if (vertex && fragment) {
        SDL_GPUColorTargetDescription color{};
        color.format = static_cast<SDL_GPUTextureFormat>(d.format);
        color.blend_state = MakeBlendState(static_cast<int>(d.blend));

        SDL_GPUGraphicsPipelineCreateInfo p{};
        p.vertex_shader = vertex;
        p.fragment_shader = fragment;
        p.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        p.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        p.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        p.target_info.color_target_descriptions = &color;
        p.target_info.num_color_targets = 1;
        if (d.depth != DepthMode::None) {
            p.target_info.has_depth_stencil_target = true;
            p.target_info.depth_stencil_format = kDepthFormat;
            p.depth_stencil_state.enable_depth_test = d.depth != DepthMode::Ignore;
            p.depth_stencil_state.enable_depth_write = d.depth == DepthMode::TestAndWrite;
            p.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;  // Z invertida
        }

        SDL_GPUVertexBufferDescription buffer{};
        SDL_GPUVertexAttribute attributes[3]{};
        if (d.meshInput) {
            buffer.slot = 0;
            buffer.pitch = sizeof(MeshVertex);
            buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
            attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, static_cast<Uint32>(offsetof(MeshVertex, position))};
            attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, static_cast<Uint32>(offsetof(MeshVertex, normal))};
            attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, static_cast<Uint32>(offsetof(MeshVertex, u))};
            p.vertex_input_state.vertex_buffer_descriptions = &buffer;
            p.vertex_input_state.num_vertex_buffers = 1;
            p.vertex_input_state.vertex_attributes = attributes;
            p.vertex_input_state.num_vertex_attributes = 3;
            p.rasterizer_state.cull_mode = d.twoSided ? SDL_GPU_CULLMODE_NONE : SDL_GPU_CULLMODE_BACK;
            p.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        }
        pipeline = SDL_CreateGPUGraphicsPipeline(device_, &p);
    }
    if (!pipeline) lastError_ = std::string("Pipeline: ") + SDL_GetError();

    if (vertex) SDL_ReleaseGPUShader(device_, vertex);
    if (fragment) SDL_ReleaseGPUShader(device_, fragment);
    return pipeline;
}

bool SdlGpuBackend::CreatePipelines() {
    const int scene = static_cast<int>(kSceneFormat), target = static_cast<int>(kTargetFormat);
    const auto make = [&](const unsigned char* vs, std::size_t vsSize, int vsUniforms, int vsStorage, const unsigned char* fs, std::size_t fsSize,
                          int samplers, int fsUniforms, int fsStorage, Blend blend, DepthMode depth, int format, bool mesh = false, bool twoSided = false) {
        return CreatePipeline({vs, vsSize, vsUniforms, vsStorage, fs, fsSize, samplers, fsUniforms, fsStorage, blend, depth, format, mesh, twoSided});
    };
#define EPS_CODE(name) name, sizeof(name)
    alphaPipeline_ = make(EPS_CODE(gVsSprite), 2, 1, EPS_CODE(gPsTextured), 1, 0, 0, Blend::Alpha, DepthMode::Test, scene);
    additivePipeline_ = make(EPS_CODE(gVsSprite), 2, 1, EPS_CODE(gPsTextured), 1, 0, 0, Blend::Additive, DepthMode::Test, scene);
    linePipeline_ = make(EPS_CODE(gVsLine), 2, 2, EPS_CODE(gPsLine), 0, 0, 0, Blend::Additive, DepthMode::Test, scene);
    skyPipeline_ = make(EPS_CODE(gVsFullscreen), 0, 0, EPS_CODE(gPsSky), 1, 1, 0, Blend::Alpha, DepthMode::Ignore, scene);
    spherePipeline_ = make(EPS_CODE(gVsSphere), 1, 1, EPS_CODE(gPsSphere), 0, 1, 1, Blend::Alpha, DepthMode::TestAndWrite, scene);
    miniaturePipeline_ = make(EPS_CODE(gVsSphere), 1, 1, EPS_CODE(gPsSphere), 0, 1, 1, Blend::Alpha, DepthMode::TestAndWrite, target);
    starCoronaPipeline_ = make(EPS_CODE(gVsPost), 0, 0, EPS_CODE(gPsStarCorona), 1, 1, 0, Blend::Additive, DepthMode::None, target);
    meshPipeline_ = make(EPS_CODE(gVsMesh), 1, 1, EPS_CODE(gPsMesh), 1, 0, 0, Blend::Opaque, DepthMode::TestAndWrite, scene, true);
    meshTwoSidedPipeline_ = make(EPS_CODE(gVsMesh), 1, 1, EPS_CODE(gPsMesh), 1, 0, 0, Blend::Opaque, DepthMode::TestAndWrite, scene, true, true);
    nebulaPipeline_ = make(EPS_CODE(gVsPost), 0, 0, EPS_CODE(gPsNebula), 2, 1, 0, Blend::Opaque, DepthMode::None, scene);
    nebulaBlendPipeline_ = make(EPS_CODE(gVsPost), 0, 0, EPS_CODE(gPsCopy), 1, 0, 0, Blend::Nebula, DepthMode::None, scene);
    blackHolePipeline_ = make(EPS_CODE(gVsPost), 0, 0, EPS_CODE(gPsBlackHole), 3, 1, 0, Blend::Opaque, DepthMode::None, scene);
    bloomDownPipeline_ = make(EPS_CODE(gVsPost), 0, 0, EPS_CODE(gPsBloomDown), 1, 1, 0, Blend::Opaque, DepthMode::None, scene);
    bloomUpPipeline_ = make(EPS_CODE(gVsPost), 0, 0, EPS_CODE(gPsBloomUp), 1, 1, 0, Blend::One, DepthMode::None, scene);
    compositePipeline_ = make(EPS_CODE(gVsPost), 0, 0, EPS_CODE(gPsComposite), 2, 1, 0, Blend::Opaque, DepthMode::None, target);
#undef EPS_CODE
    return alphaPipeline_ && additivePipeline_ && linePipeline_ && skyPipeline_ && spherePipeline_ && miniaturePipeline_ && starCoronaPipeline_ && meshPipeline_ && meshTwoSidedPipeline_ && nebulaPipeline_ &&
           nebulaBlendPipeline_ && blackHolePipeline_ && bloomDownPipeline_ && bloomUpPipeline_ && compositePipeline_;
}

bool SdlGpuBackend::CreateNoiseVolume() {
    const std::vector<std::uint8_t> data = MakeNoiseVolume(kNoiseSize);

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_3D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = info.height = kNoiseSize;
    info.layer_count_or_depth = kNoiseSize;
    info.num_levels = 1;
    noiseVolume_ = SDL_CreateGPUTexture(device_, &info);

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<Uint32>(data.size());
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
    if (!noiseVolume_ || !transfer) {
        lastError_ = std::string("Volumen de ruido: ") + SDL_GetError();
        if (transfer) SDL_ReleaseGPUTransferBuffer(device_, transfer);
        return false;
    }
    std::memcpy(SDL_MapGPUTransferBuffer(device_, transfer, false), data.data(), data.size());
    SDL_UnmapGPUTransferBuffer(device_, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer;
    source.pixels_per_row = kNoiseSize;
    source.rows_per_layer = kNoiseSize;
    SDL_GPUTextureRegion region{};
    region.texture = noiseVolume_;
    region.w = region.h = region.d = kNoiseSize;
    SDL_UploadToGPUTexture(copy, &source, &region, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device_, transfer);
    textureBytes_ += data.size();
    return true;
}

MeshId SdlGpuBackend::CreateMesh(const MeshData& mesh) {
    if (!device_ || mesh.vertices.empty() || mesh.indices.empty()) return kNoMesh;
    const auto vertexBytes = static_cast<Uint32>(mesh.vertices.size() * sizeof(MeshVertex));
    const auto indexBytes = static_cast<Uint32>(mesh.indices.size() * sizeof(std::uint32_t));

    GpuMesh gpu;
    SDL_GPUBufferCreateInfo info{};
    info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    info.size = vertexBytes;
    gpu.vertices = SDL_CreateGPUBuffer(device_, &info);
    info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    info.size = indexBytes;
    gpu.indices = SDL_CreateGPUBuffer(device_, &info);
    gpu.indexCount = static_cast<std::uint32_t>(mesh.indices.size());

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = vertexBytes + indexBytes;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
    if (!gpu.vertices || !gpu.indices || !transfer) {
        lastError_ = std::string("CreateMesh: ") + SDL_GetError();
        if (gpu.vertices) SDL_ReleaseGPUBuffer(device_, gpu.vertices);
        if (gpu.indices) SDL_ReleaseGPUBuffer(device_, gpu.indices);
        if (transfer) SDL_ReleaseGPUTransferBuffer(device_, transfer);
        return kNoMesh;
    }

    auto* mapped = static_cast<char*>(SDL_MapGPUTransferBuffer(device_, transfer, false));
    std::memcpy(mapped, mesh.vertices.data(), vertexBytes);
    std::memcpy(mapped + vertexBytes, mesh.indices.data(), indexBytes);
    SDL_UnmapGPUTransferBuffer(device_, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation source{transfer, 0};
    SDL_GPUBufferRegion target{gpu.vertices, 0, vertexBytes};
    SDL_UploadToGPUBuffer(copy, &source, &target, false);
    source.offset = vertexBytes;
    target = {gpu.indices, 0, indexBytes};
    SDL_UploadToGPUBuffer(copy, &source, &target, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device_, transfer);

    if (!freeMeshes_.empty()) {
        const MeshId id = freeMeshes_.back();
        freeMeshes_.pop_back();
        meshes_[id] = gpu;
        return id;
    }
    meshes_.push_back(gpu);
    return static_cast<MeshId>(meshes_.size() - 1);
}

void SdlGpuBackend::ReleaseMesh(MeshId id) {
    if (id >= meshes_.size() || !meshes_[id].vertices) return;
    SDL_ReleaseGPUBuffer(device_, meshes_[id].vertices);  // SDL espera a que la GPU termine
    SDL_ReleaseGPUBuffer(device_, meshes_[id].indices);
    meshes_[id] = {};
    freeMeshes_.push_back(id);
}

std::size_t SdlGpuBackend::MeshCount() const { return meshes_.size() - freeMeshes_.size(); }

int SdlGpuBackend::AddPostEffect(const std::string& name, const unsigned char* pixelShader, std::size_t size, bool behindForeground) {
    PipelineDesc d{};
    d.vs = gVsPost;
    d.vsSize = sizeof(gVsPost);
    d.fs = pixelShader;
    d.fsSize = size;
    d.fsSamplers = 1;
    d.fsUniforms = 1;
    d.blend = Blend::Opaque;
    d.format = static_cast<int>(behindForeground ? kSceneFormat : kTargetFormat);
    SDL_GPUGraphicsPipeline* pipeline = CreatePipeline(d);
    if (!pipeline) return -1;
    effects_.push_back({name, pipeline, false, 1.0f, {}, behindForeground});
    return static_cast<int>(effects_.size()) - 1;
}

void SdlGpuBackend::SetPostEffect(int index, bool enabled, float intensity) {
    if (index < 0 || static_cast<std::size_t>(index) >= effects_.size()) return;
    effects_[static_cast<std::size_t>(index)].enabled = enabled;
    effects_[static_cast<std::size_t>(index)].intensity = intensity;
}

void SdlGpuBackend::SetPostEffectParams(int index, const float (&params)[12]) {
    if (index < 0 || static_cast<std::size_t>(index) >= effects_.size()) return;
    std::memcpy(effects_[static_cast<std::size_t>(index)].params, params, sizeof(params));
}

int SdlGpuBackend::FindPostEffect(const std::string& name) const {
    for (std::size_t i = 0; i < effects_.size(); ++i)
        if (effects_[i].name == name) return static_cast<int>(i);
    return -1;
}

TextureId SdlGpuBackend::LoadTexture(const std::string& path, int maxSize) {
    SDL_Surface* loaded = SDL_LoadPNG(path.c_str());
    if (!loaded) {
        lastError_ = path + ": " + SDL_GetError();
        return kNoTexture;
    }
    SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (rgba && maxSize > 0 && std::max(rgba->w, rgba->h) > maxSize) {
        const float k = static_cast<float>(maxSize) / static_cast<float>(std::max(rgba->w, rgba->h));
        SDL_Surface* smaller = SDL_ScaleSurface(rgba, std::max(1, static_cast<int>(static_cast<float>(rgba->w) * k)),
                                                std::max(1, static_cast<int>(static_cast<float>(rgba->h) * k)), SDL_SCALEMODE_LINEAR);
        SDL_DestroySurface(rgba);
        rgba = smaller;
    }
    if (!rgba) {
        lastError_ = path + ": " + SDL_GetError();
        return kNoTexture;
    }
    const TextureId id = UploadTexture(rgba->pixels, rgba->w, rgba->h, rgba->pitch, true);
    SDL_DestroySurface(rgba);
    return id;
}

TextureId SdlGpuBackend::CreateTexture(const void* rgba, int width, int height) { return UploadTexture(rgba, width, height, width * 4, true); }

TextureId SdlGpuBackend::UploadTexture(const void* rgba, int width, int height, int pitch, bool mipmaps) {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = kTargetFormat;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (mipmaps ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
    info.width = static_cast<Uint32>(width);
    info.height = static_cast<Uint32>(height);
    info.layer_count_or_depth = 1;
    info.num_levels = mipmaps ? MipCount(width, height) : 1;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device_, &info);

    const auto bytes = static_cast<Uint32>(pitch) * static_cast<Uint32>(height);
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = bytes;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);

    if (!texture || !transfer) {
        lastError_ = std::string("UploadTexture: ") + SDL_GetError();
        if (texture) SDL_ReleaseGPUTexture(device_, texture);
        if (transfer) SDL_ReleaseGPUTransferBuffer(device_, transfer);
        return kNoTexture;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
    std::memcpy(mapped, rgba, bytes);
    SDL_UnmapGPUTransferBuffer(device_, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer;
    source.pixels_per_row = static_cast<Uint32>(pitch / 4);
    source.rows_per_layer = static_cast<Uint32>(height);
    SDL_GPUTextureRegion region{};
    region.texture = texture;
    region.w = static_cast<Uint32>(width);
    region.h = static_cast<Uint32>(height);
    region.d = 1;
    SDL_UploadToGPUTexture(copy, &source, &region, false);
    SDL_EndGPUCopyPass(copy);
    if (mipmaps) SDL_GenerateMipmapsForGPUTexture(cmd, texture);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device_, transfer);

    const std::uint64_t baseBytes = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4;
    textureBytes_ += mipmaps ? baseBytes * 4 / 3 : baseBytes;  // la cadena de mipmaps suma un tercio
    textures_.push_back(texture);
    return static_cast<TextureId>(textures_.size() - 1);
}

void SdlGpuBackend::ReleaseTargets() {
    for (SDL_GPUTexture** t : {&target_, &scene_, &lensSource_, &nebula_, &effectTarget_, &depth_}) {
        if (*t) SDL_ReleaseGPUTexture(device_, *t);
        *t = nullptr;
    }
    for (SDL_GPUTexture*& t : bloom_) {
        if (t) SDL_ReleaseGPUTexture(device_, t);
        t = nullptr;
    }
}

void SdlGpuBackend::SetRenderScale(float scale) { renderScale_ = std::clamp(scale, 0.25f, 1.0f); }

bool SdlGpuBackend::EnsureTarget(int width, int height) {
    if (target_ && targetWidth_ == width && targetHeight_ == height && targetScale_ == renderScale_) return true;
    ReleaseTargets();
    // La imagen final va a la resolucion de la ventana; la escena, a la que se haya pedido.
    const int sceneW = std::max(static_cast<int>(static_cast<float>(width) * renderScale_ + 0.5f), 1);
    const int sceneH = std::max(static_cast<int>(static_cast<float>(height) * renderScale_ + 0.5f), 1);

    const auto make = [&](SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, int w, int h) {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = format;
        info.usage = usage;
        info.width = static_cast<Uint32>(std::max(w, 1));
        info.height = static_cast<Uint32>(std::max(h, 1));
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        return SDL_CreateGPUTexture(device_, &info);
    };
    constexpr SDL_GPUTextureUsageFlags kDraw = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    target_ = make(kTargetFormat, kDraw, width, height);
    effectTarget_ = make(kTargetFormat, kDraw, width, height);
    scene_ = make(kSceneFormat, kDraw, sceneW, sceneH);
    lensSource_ = make(kSceneFormat, kDraw, sceneW, sceneH);
    nebula_ = make(kSceneFormat, kDraw, sceneW / 2, sceneH / 2);
    depth_ = make(kDepthFormat, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, sceneW, sceneH);

    bool ready = target_ && effectTarget_ && scene_ && lensSource_ && nebula_ && depth_;
    for (int i = 0; i < kBloomLevels; ++i) {
        bloom_[i] = make(kSceneFormat, kDraw, sceneW >> (i + 1), sceneH >> (i + 1));
        ready = ready && bloom_[i];
    }
    targetWidth_ = width;
    targetHeight_ = height;
    sceneWidth_ = sceneW;
    sceneHeight_ = sceneH;
    targetScale_ = renderScale_;
    if (!ready) lastError_ = std::string("Destino de render: ") + SDL_GetError();
    return ready;
}

bool SdlGpuBackend::EnsureBuffers(std::uint32_t spriteBytes, std::uint32_t lineBytes, std::uint32_t sphereBytes, std::uint32_t meshBytes) {
    const auto grow = [this](SDL_GPUBuffer*& buffer, std::uint32_t& capacity, std::uint32_t needed) {
        if (needed <= capacity) return true;
        if (buffer) SDL_ReleaseGPUBuffer(device_, buffer);
        capacity = std::max<std::uint32_t>(needed + needed / 2, 64 * 1024);
        SDL_GPUBufferCreateInfo info{};
        info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        info.size = capacity;
        buffer = SDL_CreateGPUBuffer(device_, &info);
        return buffer != nullptr;
    };
    // Sprites y lineas se enlazan juntos: los dos deben existir aunque uno este vacio.
    if (!grow(spriteBuffer_, spriteCapacity_, std::max<std::uint32_t>(spriteBytes, 1)) || !grow(lineBuffer_, lineCapacity_, std::max<std::uint32_t>(lineBytes, 1)) ||
        !grow(sphereBuffer_, sphereCapacity_, sphereBytes) || !grow(meshBuffer_, meshCapacity_, meshBytes))
        return false;

    const std::uint32_t total = spriteBytes + lineBytes + sphereBytes + meshBytes;
    if (total > uploadCapacity_) {
        if (uploadBuffer_) SDL_ReleaseGPUTransferBuffer(device_, uploadBuffer_);
        uploadCapacity_ = std::max<std::uint32_t>(total + total / 2, 128 * 1024);
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = uploadCapacity_;
        uploadBuffer_ = SDL_CreateGPUTransferBuffer(device_, &info);
    }
    return uploadBuffer_ != nullptr;
}

void SdlGpuBackend::BuildGpuData(const FramePacket& f) {
    gpuSprites_.clear();
    gpuLines_.clear();
    gpuSpheres_.clear();
    gpuMeshes_.clear();

    for (const MeshInstance& m : f.meshes) {
        // El dano va en los dos huecos libres (mesh.hlsl, DamageOf): dano + 2 x semilla entera, y
        // nivel de detalle + calor por debajo de 1.
        float damage = 0.0f, detail = 0.0f;
        if (m.damageDetail > 0 && m.damage > 0.0f) {
            const float seed = std::floor(std::fmod(std::fabs(m.damageSeed), 64.0f));
            damage = std::clamp(m.damage, 0.0f, 1.0f) + 2.0f * seed;
            detail = static_cast<float>(std::clamp(m.damageDetail, 1, 3)) + std::clamp(m.damageHeat, 0.0f, 1.0f) * 0.9f;
        }
        gpuMeshes_.push_back({m.viewPosition.x, m.viewPosition.y, m.viewPosition.z, m.scale,
                              m.rotation.x, m.rotation.y, m.rotation.z, m.rotation.w,
                              m.lightDirection.x, m.lightDirection.y, m.lightDirection.z, m.ambient,
                              m.color, m.emissive, m.stretchX, m.stretchY, damage,
                              m.lightColor, m.light2Direction.x, m.light2Direction.y, m.light2Direction.z, detail, m.light2Color, m.ambientColor});
        gpuMeshes_.back().lightColor.a = 0.0f;   // indice del bloque de crateres; 0 = ninguno
        gpuMeshes_.back().light2Color.a = 0.0f;  // cuantos
    }
    // Los crateres van detras de las instancias, en bloques del mismo tamano: cuatro por bloque.
    // mesh.hlsl los lee con el indice que lleva la instancia en el alfa de sus colores de luz.
    const std::size_t instances = gpuMeshes_.size();
    for (std::size_t i = 0; i < instances; ++i) {
        const MeshInstance& m = f.meshes[i];
        const std::uint32_t count = std::min<std::uint32_t>(m.craterCount, kMaxMeshCraters);
        if (count == 0 || m.craterFirst + count > f.craters.size()) continue;
        GpuMeshInstance block{};
        float* slots = reinterpret_cast<float*>(&block);
        for (std::uint32_t k = 0; k < count; ++k) {
            const MeshCrater& c = f.craters[m.craterFirst + k];
            float* at = slots + k * 8;
            at[0] = c.center.x;
            at[1] = c.center.y;
            at[2] = c.center.z;
            at[3] = c.radius;
            at[4] = c.depth;
            at[5] = c.heat;
            at[6] = c.seed;
            at[7] = 0.0f;
        }
        gpuMeshes_[i].lightColor.a = static_cast<float>(gpuMeshes_.size());
        gpuMeshes_[i].light2Color.a = static_cast<float>(count);
        gpuMeshes_.push_back(block);
    }

    const auto addSphere = [&](const SphereInstance& s) {
        const SurfaceLook& look = (*f.surfaces)[s.surface];
        gpuSpheres_.push_back({s.viewCenter.x, s.viewCenter.y, s.viewCenter.z, s.radius,
                               s.viewToBody.x, s.viewToBody.y, s.viewToBody.z, s.viewToBody.w,
                               s.lightDirection.x, s.lightDirection.y, s.lightDirection.z, s.pixelRadius,
                               s.detail, s.seed, static_cast<float>(look.kind), look.flattening,
                               look.a, look.b, look.c, look.atmosphere,
                               look.oceanLevel, look.iceCap, look.cloudCover, look.relief,
                               s.light2Direction.x, s.light2Direction.y, s.light2Direction.z, s.light2Strength,
                               s.lightColor.r, s.lightColor.g, s.lightColor.b, s.scarHeat,
                               s.light2Color.r, s.light2Color.g, s.light2Color.b, 0.0f,
                               s.scarDirection.x, s.scarDirection.y, s.scarDirection.z, s.scarSize});
    };
    for (const SphereInstance& s : f.spheres) addSphere(s);
    worldSphereCount_ = static_cast<std::uint32_t>(gpuSpheres_.size());
    if (f.hasMiniature && f.surfaces) addSphere(f.miniature);  // va al final: se dibuja aparte

    if (f.sky) {
        for (const SkyStar& s : *f.sky) {
            const Vec3f v = f.viewRotation.TransformDirection(s.direction);
            if (v.z <= 0.01f) continue;
            const float b = SkyStarBrightness(s, f.time);
            gpuSprites_.push_back({v.x * kSkyDistance, v.y * kSkyDistance, v.z * kSkyDistance, (s.size + 1.0f) * 1.5f, b, b, b, 1.0f});
        }
    }
    skyStarCount_ = static_cast<std::uint32_t>(gpuSprites_.size());
    worldSpriteFirst_ = skyStarCount_;

    for (const SpriteInstance& s : f.sprites) {
        gpuSprites_.push_back({s.viewPosition.x, s.viewPosition.y, s.viewPosition.z, s.pixelRadius,
                               s.tint.r, s.tint.g, s.tint.b, s.tint.a});
    }

    hasScreenTint_ = f.screenTint.a > 0.0f;
    if (hasScreenTint_) {
        const auto cover = static_cast<float>(std::max(f.width, f.height));
        gpuSprites_.push_back({0, 0, f.nearZ * 2.0f, cover, f.screenTint.r, f.screenTint.g, f.screenTint.b, f.screenTint.a});
    }

    const float nearZ = f.nearZ * 1.001f;
    for (const LineInstance& l : f.lines) {
        Vec3f a = l.viewA, b = l.viewB;
        if (a.z < nearZ || b.z < nearZ) {  // recorte contra el plano cercano
            const float t = (nearZ - a.z) / (b.z - a.z);
            const Vec3f cut = a + (b - a) * t;
            (a.z < nearZ ? a : b) = cut;
        }
        gpuLines_.push_back({a.x, a.y, a.z, 0, b.x, b.y, b.z, kLineWidthPx, l.color.r, l.color.g, l.color.b, l.color.a});
    }
}

void SdlGpuBackend::UploadGpuData(SDL_GPUCommandBuffer* cmd) {
    const auto spriteBytes = static_cast<Uint32>(gpuSprites_.size() * sizeof(GpuSprite));
    const auto lineBytes = static_cast<Uint32>(gpuLines_.size() * sizeof(GpuLine));
    const auto sphereBytes = static_cast<Uint32>(gpuSpheres_.size() * sizeof(GpuSphere));
    const auto meshBytes = static_cast<Uint32>(gpuMeshes_.size() * sizeof(GpuMeshInstance));
    if (spriteBytes + lineBytes + sphereBytes + meshBytes == 0) return;

    auto* mapped = static_cast<char*>(SDL_MapGPUTransferBuffer(device_, uploadBuffer_, true));
    if (spriteBytes) std::memcpy(mapped, gpuSprites_.data(), spriteBytes);
    if (lineBytes) std::memcpy(mapped + spriteBytes, gpuLines_.data(), lineBytes);
    if (sphereBytes) std::memcpy(mapped + spriteBytes + lineBytes, gpuSpheres_.data(), sphereBytes);
    if (meshBytes) std::memcpy(mapped + spriteBytes + lineBytes + sphereBytes, gpuMeshes_.data(), meshBytes);
    SDL_UnmapGPUTransferBuffer(device_, uploadBuffer_);

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    const auto send = [&](SDL_GPUBuffer* buffer, Uint32 offset, Uint32 bytes) {
        if (!bytes) return;
        SDL_GPUTransferBufferLocation src{uploadBuffer_, offset};
        SDL_GPUBufferRegion dst{buffer, 0, bytes};
        SDL_UploadToGPUBuffer(copy, &src, &dst, true);
    };
    send(spriteBuffer_, 0, spriteBytes);
    send(lineBuffer_, spriteBytes, lineBytes);
    send(sphereBuffer_, spriteBytes + lineBytes, sphereBytes);
    send(meshBuffer_, spriteBytes + lineBytes + sphereBytes, meshBytes);
    SDL_EndGPUCopyPass(copy);
}

void SdlGpuBackend::DrawNebulae(SDL_GPUCommandBuffer* cmd, const FramePacket& f) {
    NebulaUniforms u{};
    u.viewToWorld = InverseAffine(f.viewRotation);
    u.viewportW = static_cast<float>(f.width);
    u.viewportH = static_cast<float>(f.height);
    u.focalPx = f.focalPx;
    u.nearZ = f.nearZ;
    u.time = static_cast<float>(std::fmod(f.time, 3600.0));
    const std::size_t first = f.nebulae.size() > kMaxNebulae ? f.nebulae.size() - kMaxNebulae : 0;  // las mas cercanas
    u.count = static_cast<float>(f.nebulae.size() - first);
    u.fog[0] = f.fogColor.r;
    u.fog[1] = f.fogColor.g;
    u.fog[2] = f.fogColor.b;
    u.fog[3] = f.fogDistance;
    u.fogOrigin[0] = f.fogOrigin.x;
    u.fogOrigin[1] = f.fogOrigin.y;
    u.fogOrigin[2] = f.fogOrigin.z;
    u.fogTone = f.fogTone;
    u.fogRampOn = f.fogRampOn ? 1.0f : 0.0f;
    for (std::size_t i = 0; i < 4; ++i) {
        u.ramp[i][0] = f.fogRamp[i].r;
        u.ramp[i][1] = f.fogRamp[i].g;
        u.ramp[i][2] = f.fogRamp[i].b;
    }
    const std::size_t stars = std::min<std::size_t>(f.starLights.size(), kMaxStarLights);
    u.fogOrigin[3] = static_cast<float>(stars);
    const std::size_t airs = std::min<std::size_t>(f.atmospheres.size(), kMaxAtmospheres);
    u.atmosphereCount[0] = static_cast<float>(airs);
    for (std::size_t i = 0; i < airs; ++i) {
        const AtmosphereLight& a = f.atmospheres[i];
        u.atmosphereCenter[i][0] = a.viewCenter.x;
        u.atmosphereCenter[i][1] = a.viewCenter.y;
        u.atmosphereCenter[i][2] = a.viewCenter.z;
        u.atmosphereCenter[i][3] = a.radius;
        u.atmosphereColor[i][0] = a.color.r;
        u.atmosphereColor[i][1] = a.color.g;
        u.atmosphereColor[i][2] = a.color.b;
        u.atmosphereColor[i][3] = a.color.a;
        u.atmosphereSun[i][0] = a.sunDirection.x;
        u.atmosphereSun[i][1] = a.sunDirection.y;
        u.atmosphereSun[i][2] = a.sunDirection.z;
        u.atmosphereSun[i][3] = a.seed;
        u.atmosphereAxis[i][0] = a.pole.x;
        u.atmosphereAxis[i][1] = a.pole.y;
        u.atmosphereAxis[i][2] = a.pole.z;
        u.atmosphereAxis[i][3] = a.flattening;
    }
    for (std::size_t i = 0; i < stars; ++i) {
        const StarLight& s = f.starLights[i];
        u.starCenter[i][0] = s.viewCenter.x;
        u.starCenter[i][1] = s.viewCenter.y;
        u.starCenter[i][2] = s.viewCenter.z;
        u.starCenter[i][3] = s.radius;
        u.starColor[i][0] = s.color.r;
        u.starColor[i][1] = s.color.g;
        u.starColor[i][2] = s.color.b;
        u.starColor[i][3] = s.strength;
        u.starShape[i][0] = s.seed;
        u.starShape[i][1] = s.corona;
    }
    for (std::size_t i = first; i < f.nebulae.size(); ++i) {
        const NebulaInstance& n = f.nebulae[i];
        const std::size_t k = i - first;
        u.center[k][0] = n.viewCenter.x;
        u.center[k][1] = n.viewCenter.y;
        u.center[k][2] = n.viewCenter.z;
        u.center[k][3] = n.radius;
        u.inner[k][0] = n.inner.r;
        u.inner[k][1] = n.inner.g;
        u.inner[k][2] = n.inner.b;
        u.inner[k][3] = n.density;
        u.outer[k][0] = n.outer.r;
        u.outer[k][1] = n.outer.g;
        u.outer[k][2] = n.outer.b;
        u.outer[k][3] = n.seed;
    }

    SDL_GPUColorTargetInfo color{};
    color.texture = nebula_;
    color.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
    color.load_op = SDL_GPU_LOADOP_CLEAR;
    color.store_op = SDL_GPU_STOREOP_STORE;
    color.cycle = true;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color, 1, nullptr);
    SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof(u));
    SDL_BindGPUGraphicsPipeline(pass, nebulaPipeline_);
    SDL_GPUTextureSamplerBinding bindings[2] = {{noiseVolume_, repeatSampler_}, {depth_, pointSampler_}};
    SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);

    SDL_GPUColorTargetInfo scene{};
    scene.texture = scene_;
    scene.load_op = SDL_GPU_LOADOP_LOAD;
    scene.store_op = SDL_GPU_STOREOP_STORE;
    pass = SDL_BeginGPURenderPass(cmd, &scene, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(pass, nebulaBlendPipeline_);
    SDL_GPUTextureSamplerBinding source{nebula_, postSampler_};
    SDL_BindGPUFragmentSamplers(pass, 0, &source, 1);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

void SdlGpuBackend::DrawBlackHoles(SDL_GPUCommandBuffer* cmd, const FramePacket& f) {
    // El shader trabaja con los pixeles de la imagen que escribe: la escena, quiza reducida.
    const float width = static_cast<float>(sceneWidth_), height = static_cast<float>(sceneHeight_);
    const float scale = width / static_cast<float>(std::max(f.width, 1));
    const float focal = f.focalPx * scale;
    const Mat4f viewToWorld = InverseAffine(f.viewRotation);
    const TextureId sky = f.skyTexture != kNoTexture && f.skyTexture < textures_.size() ? f.skyTexture : kWhiteTexture;

    for (const BlackHoleInstance& h : f.blackHoles) {
        // Cada uno lee la imagen con los anteriores ya dibujados.
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureLocation from{scene_, 0, 0, 0, 0, 0};
        SDL_GPUTextureLocation to{lensSource_, 0, 0, 0, 0, 0};
        SDL_CopyGPUTextureToTexture(copy, &from, &to, static_cast<Uint32>(sceneWidth_), static_cast<Uint32>(sceneHeight_), 1, true);
        SDL_EndGPUCopyPass(copy);

        BlackHoleUniforms u{};
        u.viewToWorld = viewToWorld;
        u.centerX = h.viewCenter.x;
        u.centerY = h.viewCenter.y;
        u.centerZ = h.viewCenter.z;
        u.rs = h.schwarzschildRadius;
        u.axisX = h.viewAxis.x;
        u.axisY = h.viewAxis.y;
        u.axisZ = h.viewAxis.z;
        u.spin = h.spin;
        u.viewportW = width;
        u.viewportH = height;
        u.focalPx = focal;
        u.nearZ = f.nearZ;
        u.discInner = h.discInner;
        u.discOuter = h.discOuter;
        u.brightness = h.discBrightness;
        u.seed = h.seed;
        u.time = static_cast<float>(std::fmod(f.time, 3600.0));
        u.horizon = static_cast<float>(0.5 * (1.0 + std::sqrt(std::max(1.0 - static_cast<double>(h.spin) * h.spin, 0.0))));
        u.reach = h.reach;
        u.hasSky = f.skyTexture != kNoTexture ? 1.0f : 0.0f;
        u.skyTint = f.skyTint;

        SDL_GPUColorTargetInfo color{};
        color.texture = scene_;
        color.load_op = SDL_GPU_LOADOP_LOAD;
        color.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color, 1, nullptr);

        // Solo los pixeles que puede afectar: lejos, el coste es el de un circulo pequeno.
        SDL_Rect scissor{0, 0, sceneWidth_, sceneHeight_};
        if (h.viewCenter.z > 0.0f && h.distance > h.reach * h.schwarzschildRadius * 1.01f) {
            const float cx = h.viewCenter.x / h.viewCenter.z * focal + width * 0.5f;
            const float cy = h.viewCenter.y / h.viewCenter.z * focal + height * 0.5f;
            const float r = h.pixelRadius * scale + 2.0f;
            const int left = static_cast<int>(std::clamp(cx - r, 0.0f, width)), top = static_cast<int>(std::clamp(cy - r, 0.0f, height));
            const int right = static_cast<int>(std::clamp(cx + r, 0.0f, width)), bottom = static_cast<int>(std::clamp(cy + r, 0.0f, height));
            scissor = {left, top, right - left, bottom - top};
        }
        if (scissor.w > 0 && scissor.h > 0) {
            SDL_SetGPUScissor(pass, &scissor);
            SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof(u));
            SDL_BindGPUGraphicsPipeline(pass, blackHolePipeline_);
            SDL_GPUTextureSamplerBinding bindings[3] = {{lensSource_, postSampler_}, {depth_, pointSampler_}, {textures_[sky], repeatSampler_}};
            SDL_BindGPUFragmentSamplers(pass, 0, bindings, 3);
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        }
        SDL_EndGPURenderPass(pass);
    }
}

void SdlGpuBackend::DrawBloom(SDL_GPUCommandBuffer* cmd, const FramePacket& f) {
    const auto draw = [&](SDL_GPUTexture* output, SDL_GPUTexture* input, SDL_GPUGraphicsPipeline* pipeline, bool keep, int inputW, int inputH, float threshold, float strength) {
        SDL_GPUColorTargetInfo color{};
        color.texture = output;
        color.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
        color.load_op = keep ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
        color.store_op = SDL_GPU_STOREOP_STORE;
        color.cycle = !keep;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color, 1, nullptr);
        BloomUniforms u{1.0f / static_cast<float>(std::max(inputW, 1)), 1.0f / static_cast<float>(std::max(inputH, 1)), threshold, strength};
        SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof(u));
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        SDL_GPUTextureSamplerBinding source{input, postSampler_};
        SDL_BindGPUFragmentSamplers(pass, 0, &source, 1);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
    };

    // Cada nivel reduce a la mitad; al volver, se suman: una gran mancha suave y barata.
    const int w = sceneWidth_, h = sceneHeight_;
    draw(bloom_[0], scene_, bloomDownPipeline_, false, w, h, f.post.bloomThreshold, 1.0f);
    for (int i = 1; i < kBloomLevels; ++i) draw(bloom_[i], bloom_[i - 1], bloomDownPipeline_, false, w >> i, h >> i, -1.0f, 1.0f);
    for (int i = kBloomLevels - 1; i > 0; --i) draw(bloom_[i - 1], bloom_[i], bloomUpPipeline_, true, w >> (i + 1), h >> (i + 1), -1.0f, 0.8f);
}

void SdlGpuBackend::DrawComposite(SDL_GPUCommandBuffer* cmd, const FramePacket& f) {
    int active = 0;
    for (const PostEffect& e : effects_) active += e.enabled && !e.behindForeground ? 1 : 0;

    // Se alterna entre dos imagenes y la ultima tiene que ser target_.
    SDL_GPUTexture* current = active % 2 == 0 ? target_ : effectTarget_;
    SDL_GPUColorTargetInfo color{};
    color.texture = current;
    color.load_op = SDL_GPU_LOADOP_DONT_CARE;
    color.store_op = SDL_GPU_STOREOP_STORE;
    color.cycle = true;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color, 1, nullptr);

    CompositeUniforms u{};
    u.bloomStrength = f.post.bloom ? f.post.bloomStrength : 0.0f;
    u.exposure = f.post.exposure;
    u.vignette = f.post.vignette;
    u.aberration = f.post.aberration;
    u.grain = f.post.grain;
    u.time = static_cast<float>(std::fmod(f.time, 1000.0));
    SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof(u));
    SDL_BindGPUGraphicsPipeline(pass, compositePipeline_);
    SDL_GPUTextureSamplerBinding bindings[2] = {{scene_, postSampler_}, {bloom_[0], postSampler_}};
    SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);

    for (const PostEffect& e : effects_) {
        if (!e.enabled || e.behindForeground) continue;
        SDL_GPUTexture* next = current == target_ ? effectTarget_ : target_;
        SDL_GPUColorTargetInfo out{};
        out.texture = next;
        out.load_op = SDL_GPU_LOADOP_DONT_CARE;
        out.store_op = SDL_GPU_STOREOP_STORE;
        out.cycle = true;
        SDL_GPURenderPass* effect = SDL_BeginGPURenderPass(cmd, &out, 1, nullptr);
        EffectUniforms eu{static_cast<float>(f.width), static_cast<float>(f.height), static_cast<float>(std::fmod(f.time, 1000.0)), e.intensity, {}};
        std::memcpy(eu.params, e.params, sizeof(eu.params));
        SDL_PushGPUFragmentUniformData(cmd, 0, &eu, sizeof(eu));
        SDL_BindGPUGraphicsPipeline(effect, e.pipeline);
        SDL_GPUTextureSamplerBinding source{current, postSampler_};
        SDL_BindGPUFragmentSamplers(effect, 0, &source, 1);
        SDL_DrawGPUPrimitives(effect, 3, 1, 0, 0);
        SDL_EndGPURenderPass(effect);
        current = next;
    }
}

void SdlGpuBackend::Render(const FramePacket& f) {
    if (!device_ || f.width <= 0 || f.height <= 0) return;

    BuildGpuData(f);
    const auto spriteBytes = static_cast<std::uint32_t>(gpuSprites_.size() * sizeof(GpuSprite));
    const auto lineBytes = static_cast<std::uint32_t>(gpuLines_.size() * sizeof(GpuLine));
    const auto sphereBytes = static_cast<std::uint32_t>(gpuSpheres_.size() * sizeof(GpuSphere));
    const auto meshBytes = static_cast<std::uint32_t>(gpuMeshes_.size() * sizeof(GpuMeshInstance));
    if (!EnsureTarget(f.width, f.height) || !EnsureBuffers(spriteBytes, lineBytes, sphereBytes, meshBytes)) return;

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd) {
        lastError_ = std::string("SDL_AcquireGPUCommandBuffer: ") + SDL_GetError();
        return;
    }
    UploadGpuData(cmd);

    SDL_GPUColorTargetInfo color{};
    color.texture = scene_;
    color.clear_color = {f.clearColor.r, f.clearColor.g, f.clearColor.b, 1.0f};
    color.load_op = SDL_GPU_LOADOP_CLEAR;
    color.store_op = SDL_GPU_STOREOP_STORE;
    color.cycle = true;

    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = depth_;
    depth.clear_depth = 0.0f;  // Z invertida: 0 es el infinito
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_STORE;  // la nebulosa y el agujero negro la leen
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth.cycle = true;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color, 1, &depth);

    const auto bindTexture = [&](TextureId id, SDL_GPUSampler* sampler) {
        const TextureId valid = id < textures_.size() ? id : kWhiteTexture;
        SDL_GPUTextureSamplerBinding binding{textures_[valid], sampler};
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    };

    FrameUniforms frame{};
    frame.projection = f.projection;
    frame.viewportW = static_cast<float>(f.width);
    frame.viewportH = static_cast<float>(f.height);
    frame.firstInstance = 0;
    SDL_PushGPUVertexUniformData(cmd, 0, &frame, sizeof(frame));

    const auto drawSprites = [&](std::uint32_t first, std::uint32_t count) {
        BatchUniforms batch{first, 0, 0, 0};
        SDL_PushGPUVertexUniformData(cmd, 1, &batch, sizeof(batch));
        SDL_DrawGPUPrimitives(pass, 6, count, 0, 0);
    };

    if (f.skyTexture != kNoTexture && f.skyTexture < textures_.size()) {
        SkyUniforms sky{};
        sky.cameraToWorld = InverseAffine(f.viewRotation);
        sky.tint = f.skyTint;
        sky.viewportW = frame.viewportW;
        sky.viewportH = frame.viewportH;
        sky.focalPx = f.focalPx;
        SDL_PushGPUFragmentUniformData(cmd, 0, &sky, sizeof(sky));
        SDL_BindGPUGraphicsPipeline(pass, skyPipeline_);
        bindTexture(f.skyTexture, repeatSampler_);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    }

    const auto bindSpriteBuffers = [&] {
        SDL_GPUBuffer* buffers[] = {spriteBuffer_, lineBuffer_};
        SDL_BindGPUVertexStorageBuffers(pass, 0, buffers, 2);
    };
    bindSpriteBuffers();

    if (skyStarCount_ > 0) {
        SDL_BindGPUGraphicsPipeline(pass, additivePipeline_);
        bindTexture(kGlowTexture, clampSampler_);
        drawSprites(0, skyStarCount_);
    }

    // Las esferas van antes que lo transparente: escriben la profundidad que lo oculta.
    SphereUniforms sphere{};
    sphere.nearZ = f.nearZ;
    sphere.time = static_cast<float>(std::fmod(f.time, 3600.0));
    sphere.starBoost = f.post.starBoost;  // las estrellas superan 1 y el bloom las hace brillar
    if (worldSphereCount_ > 0) {
        SDL_PushGPUFragmentUniformData(cmd, 0, &sphere, sizeof(sphere));
        SDL_BindGPUGraphicsPipeline(pass, spherePipeline_);
        SDL_BindGPUVertexStorageBuffers(pass, 0, &sphereBuffer_, 1);
        SDL_BindGPUFragmentStorageBuffers(pass, 0, &sphereBuffer_, 1);
        SDL_DrawGPUPrimitives(pass, 6, worldSphereCount_, 0, 0);
        bindSpriteBuffers();
    }

    const auto drawMeshes = [&](bool foreground) {
        SDL_GPUGraphicsPipeline* bound = meshPipeline_;
        SDL_BindGPUGraphicsPipeline(pass, bound);
        SDL_BindGPUVertexStorageBuffers(pass, 0, &meshBuffer_, 1);
        for (const MeshBatch& b : f.meshBatches) {
            if (b.foreground != foreground || b.mesh >= meshes_.size() || !meshes_[b.mesh].vertices) continue;
            SDL_GPUGraphicsPipeline* wanted = b.doubleSided ? meshTwoSidedPipeline_ : meshPipeline_;
            if (wanted != bound) {
                bound = wanted;
                SDL_BindGPUGraphicsPipeline(pass, bound);
                SDL_BindGPUVertexStorageBuffers(pass, 0, &meshBuffer_, 1);
            }
            const GpuMesh& mesh = meshes_[b.mesh];
            FrameUniforms batch = frame;
            batch.firstInstance = b.first;
            SDL_PushGPUVertexUniformData(cmd, 0, &batch, sizeof(batch));
            bindTexture(b.texture, repeatSampler_);
            SDL_GPUBufferBinding vertices{mesh.vertices, 0};
            SDL_GPUBufferBinding indices{mesh.indices, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &vertices, 1);
            SDL_BindGPUIndexBuffer(pass, &indices, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            SDL_DrawGPUIndexedPrimitives(pass, mesh.indexCount, b.count, 0, 0, 0);
        }
        SDL_PushGPUVertexUniformData(cmd, 0, &frame, sizeof(frame));
    };
    bool hasForeground = false;
    for (const MeshBatch& b : f.meshBatches) hasForeground = hasForeground || b.foreground;
    if (!f.meshBatches.empty()) {
        drawMeshes(false);
        bindSpriteBuffers();
    }

    // Las nubes necesitan la profundidad de lo opaco; lineas y sprites van encima de ellas.
    if (!f.nebulae.empty() || f.fogDistance > 0.0f) {
        SDL_EndGPURenderPass(pass);
        DrawNebulae(cmd, f);
        color.load_op = SDL_GPU_LOADOP_LOAD;
        color.cycle = false;
        depth.load_op = SDL_GPU_LOADOP_LOAD;
        depth.cycle = false;
        pass = SDL_BeginGPURenderPass(cmd, &color, 1, &depth);
        bindSpriteBuffers();
    }

    if (!gpuLines_.empty()) {
        SDL_BindGPUGraphicsPipeline(pass, linePipeline_);
        SDL_DrawGPUPrimitives(pass, 6, static_cast<Uint32>(gpuLines_.size()), 0, 0);
    }

    bool additiveBound = false, anyBound = false;
    for (const SpriteBatch& b : f.batches) {
        const bool additive = b.blend == BlendMode::Additive;
        if (!anyBound || additive != additiveBound) {
            SDL_BindGPUGraphicsPipeline(pass, additive ? additivePipeline_ : alphaPipeline_);
            additiveBound = additive;
            anyBound = true;
        }
        bindTexture(b.texture, clampSampler_);
        drawSprites(worldSpriteFirst_ + b.first, b.count);
    }

    if (hasScreenTint_) {
        SDL_BindGPUGraphicsPipeline(pass, alphaPipeline_);
        bindTexture(kWhiteTexture, clampSampler_);
        drawSprites(static_cast<std::uint32_t>(gpuSprites_.size()) - 1, 1);
    }
    SDL_EndGPURenderPass(pass);

    if (!f.blackHoles.empty()) DrawBlackHoles(cmd, f);

    // Efectos sobre la escena: leen una copia y escriben encima.
    for (const PostEffect& e : effects_) {
        if (!e.enabled || !e.behindForeground) continue;
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureLocation from{scene_, 0, 0, 0, 0, 0};
        SDL_GPUTextureLocation to{lensSource_, 0, 0, 0, 0, 0};
        SDL_CopyGPUTextureToTexture(copy, &from, &to, static_cast<Uint32>(sceneWidth_), static_cast<Uint32>(sceneHeight_), 1, true);
        SDL_EndGPUCopyPass(copy);

        SDL_GPUColorTargetInfo out{};
        out.texture = scene_;
        out.load_op = SDL_GPU_LOADOP_LOAD;
        out.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* effect = SDL_BeginGPURenderPass(cmd, &out, 1, nullptr);
        EffectUniforms eu{static_cast<float>(f.width), static_cast<float>(f.height), static_cast<float>(std::fmod(f.time, 1000.0)), e.intensity, {}};
        std::memcpy(eu.params, e.params, sizeof(eu.params));
        SDL_PushGPUFragmentUniformData(cmd, 0, &eu, sizeof(eu));
        SDL_BindGPUGraphicsPipeline(effect, e.pipeline);
        SDL_GPUTextureSamplerBinding source{lensSource_, postSampler_};
        SDL_BindGPUFragmentSamplers(effect, 0, &source, 1);
        SDL_DrawGPUPrimitives(effect, 3, 1, 0, 0);
        SDL_EndGPURenderPass(effect);
    }

    // Lo que va por delante de las lentes: ya no puede salir en ellas.
    if (hasForeground) {
        color.load_op = SDL_GPU_LOADOP_LOAD;
        color.cycle = false;
        depth.load_op = SDL_GPU_LOADOP_LOAD;
        depth.cycle = false;
        pass = SDL_BeginGPURenderPass(cmd, &color, 1, &depth);
        drawMeshes(true);
        SDL_EndGPURenderPass(pass);
    }

    if (f.post.bloom) DrawBloom(cmd, f);
    DrawComposite(cmd, f);

    // La capa va en su propia pasada, sin depth buffer, para no depender de sus pipelines.
    SDL_GPUColorTargetInfo keep{};
    keep.texture = target_;
    keep.load_op = SDL_GPU_LOADOP_LOAD;
    keep.store_op = SDL_GPU_STOREOP_STORE;
    if (overlay_) {
        overlay_->Prepare(cmd);
        SDL_GPURenderPass* overlayPass = SDL_BeginGPURenderPass(cmd, &keep, 1, nullptr);
        overlay_->Draw(cmd, overlayPass);
        SDL_EndGPURenderPass(overlayPass);
    }

    // La corona de una estrella en el mapa, debajo de su miniatura y en un rectangulo mayor.
    if (f.miniatureCorona && f.coronaW > 0 && f.coronaH > 0 && f.miniatureSize > 0) {
        NebulaUniforms u{};
        u.viewToWorld = f.coronaView.ToMatrix();
        u.viewportW = static_cast<float>(f.coronaW);
        u.viewportH = static_cast<float>(f.coronaH);
        u.focalPx = f.miniatureFocalPx;
        u.nearZ = f.nearZ;
        u.time = static_cast<float>(std::fmod(f.time, 3600.0));
        u.fogOrigin[0] = static_cast<float>(f.miniatureX - f.coronaX) + static_cast<float>(f.miniatureSize) * 0.5f;
        u.fogOrigin[1] = static_cast<float>(f.miniatureY - f.coronaY) + static_cast<float>(f.miniatureSize) * 0.5f;
        u.starCenter[0][0] = f.miniature.viewCenter.x;
        u.starCenter[0][1] = f.miniature.viewCenter.y;
        u.starCenter[0][2] = f.miniature.viewCenter.z;
        u.starCenter[0][3] = f.miniature.radius;
        u.starColor[0][0] = f.coronaColor.x;
        u.starColor[0][1] = f.coronaColor.y;
        u.starColor[0][2] = f.coronaColor.z;
        u.starColor[0][3] = f.coronaIntensity;
        u.starShape[0][0] = f.coronaSeed;
        u.starShape[0][1] = 1.0f;
        SDL_GPURenderPass* halo = SDL_BeginGPURenderPass(cmd, &keep, 1, nullptr);
        SDL_GPUViewport viewport{static_cast<float>(f.coronaX), static_cast<float>(f.coronaY), static_cast<float>(f.coronaW), static_cast<float>(f.coronaH), 0.0f, 1.0f};
        SDL_Rect scissor{f.coronaX, f.coronaY, f.coronaW, f.coronaH};
        SDL_SetGPUViewport(halo, &viewport);
        SDL_SetGPUScissor(halo, &scissor);
        SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof(u));
        SDL_BindGPUGraphicsPipeline(halo, starCoronaPipeline_);
        SDL_GPUTextureSamplerBinding noise{noiseVolume_, repeatSampler_};
        SDL_BindGPUFragmentSamplers(halo, 0, &noise, 1);
        SDL_DrawGPUPrimitives(halo, 3, 1, 0, 0);
        SDL_EndGPURenderPass(halo);
    }

    // Miniatura: la misma esfera por pixel, en un rectangulo de la pantalla y encima de la capa.
    if (gpuSpheres_.size() > worldSphereCount_ && f.miniatureSize > 0) {
        depth.load_op = SDL_GPU_LOADOP_CLEAR;
        depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
        depth.cycle = true;
        SDL_GPURenderPass* mini = SDL_BeginGPURenderPass(cmd, &keep, 1, &depth);
        const auto size = static_cast<float>(f.miniatureSize);
        SDL_GPUViewport viewport{static_cast<float>(f.miniatureX), static_cast<float>(f.miniatureY), size, size, 0.0f, 1.0f};
        SDL_Rect scissor{f.miniatureX, f.miniatureY, f.miniatureSize, f.miniatureSize};
        SDL_SetGPUViewport(mini, &viewport);
        SDL_SetGPUScissor(mini, &scissor);

        FrameUniforms miniFrame{};
        miniFrame.projection = PerspectiveReverseZ(f.miniatureFocalPx, size, size, f.nearZ);
        miniFrame.viewportW = miniFrame.viewportH = size;
        miniFrame.firstInstance = worldSphereCount_;
        SDL_PushGPUVertexUniformData(cmd, 0, &miniFrame, sizeof(miniFrame));
        sphere.starBoost = 1.0f;
        SDL_PushGPUFragmentUniformData(cmd, 0, &sphere, sizeof(sphere));
        SDL_BindGPUGraphicsPipeline(mini, miniaturePipeline_);
        SDL_BindGPUVertexStorageBuffers(mini, 0, &sphereBuffer_, 1);
        SDL_BindGPUFragmentStorageBuffers(mini, 0, &sphereBuffer_, 1);
        SDL_DrawGPUPrimitives(mini, 6, 1, 0, 0);
        SDL_EndGPURenderPass(mini);
    }

    SDL_GPUTexture* swapchain = nullptr;
    Uint32 swapW = 0, swapH = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window_, &swapchain, &swapW, &swapH)) {
        lastError_ = std::string("Swapchain: ") + SDL_GetError();
    }
    if (swapchain) {
        SDL_GPUBlitInfo blit{};
        blit.source.texture = target_;
        blit.source.w = static_cast<Uint32>(f.width);
        blit.source.h = static_cast<Uint32>(f.height);
        blit.destination.texture = swapchain;
        blit.destination.w = swapW;
        blit.destination.h = swapH;
        blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
        blit.filter = SDL_GPU_FILTER_LINEAR;
        SDL_BlitGPUTexture(cmd, &blit);
        ++presentedFrames_;
    }

    if (!screenshotPath_.empty()) SaveScreenshot(cmd);
    else SDL_SubmitGPUCommandBuffer(cmd);
}

void SdlGpuBackend::SaveScreenshot(SDL_GPUCommandBuffer* cmd) {
    const std::string path = screenshotPath_;
    screenshotPath_.clear();

    const auto bytes = static_cast<Uint32>(targetWidth_) * static_cast<Uint32>(targetHeight_) * 4;
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = bytes;
    SDL_GPUTransferBuffer* download = SDL_CreateGPUTransferBuffer(device_, &info);
    if (!download) {
        lastError_ = std::string("Captura: ") + SDL_GetError();
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion region{};
    region.texture = target_;
    region.w = static_cast<Uint32>(targetWidth_);
    region.h = static_cast<Uint32>(targetHeight_);
    region.d = 1;
    SDL_GPUTextureTransferInfo destination{};
    destination.transfer_buffer = download;
    SDL_DownloadFromGPUTexture(copy, &region, &destination);
    SDL_EndGPUCopyPass(copy);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence) {
        SDL_WaitForGPUFences(device_, true, &fence, 1);
        SDL_ReleaseGPUFence(device_, fence);

        void* pixels = SDL_MapGPUTransferBuffer(device_, download, false);
        SDL_Surface* surface = SDL_CreateSurfaceFrom(targetWidth_, targetHeight_, SDL_PIXELFORMAT_RGBA32, pixels, targetWidth_ * 4);
        if (!surface || !SDL_SavePNG(surface, path.c_str())) lastError_ = std::string("Captura: ") + SDL_GetError();
        if (surface) SDL_DestroySurface(surface);
        SDL_UnmapGPUTransferBuffer(device_, download);
    }
    SDL_ReleaseGPUTransferBuffer(device_, download);
}

}  // namespace eps
