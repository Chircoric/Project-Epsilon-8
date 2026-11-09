// scanlines.hlsl - Ejemplo de efecto propio del juego: pantalla de cabina.
//
// Contrato de SdlGpuBackend::AddPostEffect: recibe la imagen en t0/s0 de space2 y estos
// uniforms en b0 de space3. Copia este archivo para escribir otro efecto.

struct VsOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Texture2D<float4> gScene : register(t0, space2);
SamplerState gSampler : register(s0, space2);

cbuffer EffectCB : register(b0, space3) {
    float2 uResolution;
    float uTime;
    float uIntensity;
};

float4 PSEffect(VsOut i) : SV_Target0 {
    const float3 color = gScene.SampleLevel(gSampler, i.uv, 0).rgb;
    const float scan = 0.5 + 0.5 * sin(i.uv.y * uResolution.y * 1.6 - uTime * 3.0);
    const float sweep = smoothstep(0.0, 0.015, abs(frac(i.uv.y * 0.5 + uTime * 0.07) - 0.5));
    return float4(color * lerp(1.0, (0.86 + 0.14 * scan) * (0.94 + 0.06 * sweep), uIntensity), 1.0);
}
