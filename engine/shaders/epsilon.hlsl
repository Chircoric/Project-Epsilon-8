// epsilon.hlsl - Shaders del backend SDL_GPU (DXIL, Shader Model 6.0).
//
// Registros que exige SDL_GPU en Direct3D 12:
//   Vertice:   t[n] space0 = texturas y storage buffers;  b[n] space1 = uniforms
//   Fragmento: t[n], s[n] space2 = texturas y samplers;   b[n] space3 = uniforms

cbuffer FrameCB : register(b0, space1) {
    float4x4 uProjection;
    float2 uViewport;
    float2 uFramePad;
};

// SV_InstanceID no incluye first_instance en todas las APIs, por eso el lote pasa su inicio aqui.
cbuffer BatchCB : register(b1, space1) {
    uint uFirstInstance;
    uint3 uBatchPad;
};

// SDL_GPU crea en Direct3D 12 vistas RAW sin stride: StructuredBuffer lee datos erroneos,
// asi que los elementos se calculan a mano.
ByteAddressBuffer gSprites : register(t0, space0);  // 32 bytes: xyz + radio, tinte
ByteAddressBuffer gLines : register(t1, space0);    // 48 bytes: A, B + grosor, color

struct VsOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float2 soft : TEXCOORD1;  // sprites: profundidad del centro y radio en el mundo, en km
};

// Cuadro orientado a la camara: el centro pasa por P y la esquina se suma en pixeles.
// Multiplicar por w cancela la division de perspectiva, asi el tamano queda fijo en pantalla.
VsOut VSSprite(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {
    static const float2 kCorners[6] = {
        float2(-1, -1), float2(1, -1), float2(1, 1),
        float2(-1, -1), float2(1, 1),  float2(-1, 1)
    };

    const uint offset = (uFirstInstance + instanceId) * 32;
    const float4 data = asfloat(gSprites.Load4(offset));
    const float pixelRadius = data.w;
    const float2 c = kCorners[vertexId];

    float4 clip = mul(uProjection, float4(data.xyz, 1.0));
    clip.x += c.x * pixelRadius * 2.0 / uViewport.x * clip.w;
    clip.y -= c.y * pixelRadius * 2.0 / uViewport.y * clip.w;

    VsOut o;
    o.position = clip;
    o.uv = c * 0.5 + 0.5;
    o.color = asfloat(gSprites.Load4(offset + 16));
    // Lo que mide el cuadro en el mundo a su distancia: un pixel de radio son w / (P00 * ancho / 2).
    const float p00 = mul(uProjection, float4(1, 0, 0, 0)).x;
    o.soft = float2(data.z, pixelRadius * 2.0 / uViewport.x * clip.w / max(abs(p00), 1e-6));
    return o;
}

// Segmento con grosor constante: cada extremo se desplaza en pixeles sobre la normal 2D.
VsOut VSLine(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {
    static const float2 kPoints[6] = {
        float2(0, -1), float2(1, -1), float2(1, 1),
        float2(0, -1), float2(1, 1),  float2(0, 1)
    };

    const uint offset = instanceId * 48;
    const float3 a = asfloat(gLines.Load3(offset));
    const float4 b = asfloat(gLines.Load4(offset + 16));  // w = grosor en pixeles
    const float2 p = kPoints[vertexId];

    const float4 clipA = mul(uProjection, float4(a, 1.0));
    const float4 clipB = mul(uProjection, float4(b.xyz, 1.0));
    const float2 pixelA = clipA.xy / clipA.w * uViewport * 0.5;
    const float2 pixelB = clipB.xy / clipB.w * uViewport * 0.5;
    const float2 delta = pixelB - pixelA;
    const float len = max(length(delta), 1e-4);
    const float2 normal = float2(-delta.y, delta.x) / len;

    float4 clip = p.x > 0.5 ? clipB : clipA;
    clip.xy += normal * p.y * b.w / uViewport * clip.w;

    VsOut o;
    o.position = clip;
    o.uv = float2(p.x, p.y);
    o.color = asfloat(gLines.Load4(offset + 32));
    o.soft = float2(0, 0);
    return o;
}

// Triangulo que cubre toda la pantalla.
VsOut VSFullscreen(uint vertexId : SV_VertexID) {
    const float2 pos = float2(vertexId == 1 ? 3.0 : -1.0, vertexId == 2 ? 3.0 : -1.0);
    VsOut o;
    o.position = float4(pos, 0.0, 1.0);
    o.uv = pos * 0.5 + 0.5;
    o.color = float4(1, 1, 1, 1);
    o.soft = float2(0, 0);
    return o;
}

Texture2D<float4> gTexture : register(t0, space2);
SamplerState gSampler : register(s0, space2);

cbuffer SkyCB : register(b0, space3) {
    float4x4 uCameraToWorld;
    float4 uSkyTint;
    float2 uSkyViewport;
    float uFocalPx;
    float uSkyPad;
};

float4 PSTextured(VsOut i) : SV_Target0 {
    return gTexture.Sample(gSampler, i.uv) * i.color;
}

// Sprite suave: cada destello es una bola de luz, no un cartel plano. Se mira cuanto de su grueso
// queda delante de lo opaco y se apaga en proporcion. Asi una estela o una explosion se funden con
// el casco que atraviesan en vez de cortarse en una linea recta.
Texture2D<float> gSceneDepth : register(t1, space2);
cbuffer SoftCB : register(b0, space3) {
    float uSoftNear;  // plano cercano: la profundidad es cerca / z (Z invertida)
    float uSoftVeil;  // la niebla de pantalla que ya se pinto debajo: lo que se suma despues, se vela igual
    float2 uSoftPad;
};

float4 PSSoftSprite(VsOut i) : SV_Target0 {
    float4 c = gTexture.Sample(gSampler, i.uv) * i.color;
    const float depth = gSceneDepth.Load(int3(i.position.xy, 0));
    const float sceneZ = depth > 0.0 ? uSoftNear / depth : 1.0e30;
    const float2 d = i.uv * 2.0 - 1.0;
    // El grosor nunca pasa de una decima de su distancia: el halo de un sol mide en pantalla mas que
    // un planeta cercano, pero esta muchisimo mas lejos y el planeta debe taparlo entero.
    const float radius = clamp(i.soft.y, 1e-9, max(i.soft.x, 1e-9) * 0.1);
    const float half_ = radius * sqrt(saturate(1.0 - dot(d, d)));
    const float visible = saturate((sceneZ - (i.soft.x - half_)) / max(2.0 * half_, radius * 0.08));
    c *= visible;
    c.rgb *= 1.0 - uSoftVeil;
    return c;
}

float4 PSLine(VsOut i) : SV_Target0 {
    float4 c = i.color;
    c.a *= 1.0 - abs(i.uv.y);  // borde suavizado
    return c;
}

// Fondo real en 3D: reconstruye la direccion de cada pixel y la convierte a coordenadas
// de un panorama equirectangular. Solo rota con la camara, nunca se traslada.
float4 PSSky(VsOut i) : SV_Target0 {
    const float2 pixel = i.position.xy - uSkyViewport * 0.5;
    const float3 viewDir = normalize(float3(pixel / uFocalPx, 1.0));
    const float3 worldDir = mul((float3x3)uCameraToWorld, viewDir);

    const float pi = 3.14159265358979;
    const float2 uv = float2(atan2(worldDir.x, worldDir.z) / (2.0 * pi) + 0.5,
                             asin(clamp(worldDir.y, -1.0, 1.0)) / pi + 0.5);
    // Nivel 0 fijo: las derivadas saltan en la costura de atan2 y mostrarian una linea.
    return gTexture.SampleLevel(gSampler, uv, 0.0) * uSkyTint;
}
