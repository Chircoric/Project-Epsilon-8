// warp.hlsl - Lo que se ve desde dentro de la curvatura.
//
// Cerca de c, la aberracion relativista empuja todo el cielo hacia delante: luz que llegaba
// de atras aparece en un cono frontal. Con K = sqrt((1 + b) / (1 - b)):
//
//     tan(angulo en pantalla / 2) = tan(angulo real / 2) / K
//
// Asi, en el campo de vision cabe casi toda la esfera. Delante, el efecto Doppler azula la luz;
// al alejarse del rumbo recorre el arcoiris hasta el rojo, y detras queda la negrura de lo que
// ya no alcanza a la nave. Las estrellas no son puntos: se estiran en lineas hacia el rumbo.
//
// Los colores son una representacion: la luz real saldria del espectro visible.
//
// Se aplica a la escena antes de dibujar la nave: ella va dentro de la burbuja, y lo que se ve fuera no le afecta.

struct VsOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Texture2D<float4> gScene : register(t0, space2);
SamplerState gSampler : register(s0, space2);

cbuffer EffectCB : register(b0, space3) {
    float2 uResolution;
    float uTime;
    float uIntensity;       // 0 = normal, 1 = curvatura plena
    float4 uForward;        // xyz: rumbo en vista; w: distancia focal en pixeles
    float4 uShape;          // x: K; y: velocidad de las lineas; z: negrura al salir de la lente
    float4 uUp;             // xyz: eje que acompana al rumbo, en vista
};

static const float kPi = 3.14159265;

float Hash(float2 p) {
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

// De azul blanco a rojo y de ahi a negro. t = 0 es el rumbo; t = 1, justo detras.
float3 Spectrum(float t) {
    static const float3 stops[8] = {
        float3(0.72, 0.86, 1.00),  // luz azul del frente
        float3(0.22, 0.42, 1.00),  // azul
        float3(0.16, 0.86, 0.98),  // cian
        float3(0.20, 0.95, 0.35),  // verde
        float3(1.00, 0.92, 0.18),  // amarillo
        float3(1.00, 0.45, 0.08),  // naranja
        float3(0.80, 0.06, 0.04),  // rojo
        float3(0.00, 0.00, 0.00),  // negro de la zona trasera
    };
    const float at = saturate(t) * 7.0;
    const int i = min(int(at), 6);
    return lerp(stops[i], stops[i + 1], at - float(i));
}

// Una capa de lineas: cada sector de azimut lleva una estrella estirada que corre hacia atras.
float Streaks(float azimuth, float along, float count, float seed, float width) {
    const float slot = azimuth / (2.0 * kPi) * count;
    const float cell = floor(slot);
    const float h = Hash(float2(cell, seed));
    if (h < 0.35) return 0.0;
    const float stroke = 1.0 - smoothstep(0.0, width, abs(frac(slot) - 0.5 + (h - 0.5) * 0.5));
    const float rate = 0.6 + h * 1.4;
    const float wave = frac(along * (0.7 + h) + h * 19.0 - uTime * uShape.y * rate);
    return stroke * smoothstep(0.0, 0.08, wave) * (1.0 - smoothstep(0.25, 0.95, wave)) * (0.45 + h);
}

float4 PSEffect(VsOut i) : SV_Target0 {
    const float3 scene = gScene.SampleLevel(gSampler, i.uv, 0).rgb;
    const float strength = saturate(uIntensity);
    if (strength <= 0.001) return float4(scene, 1.0);

    // Rayo de este pixel y su angulo respecto al rumbo.
    const float2 pixel = i.uv * uResolution - uResolution * 0.5;
    const float3 ray = normalize(float3(pixel / uForward.w, 1.0));
    const float3 forward = normalize(uForward.xyz);
    const float cosine = clamp(dot(ray, forward), -1.0, 1.0);
    const float viewAngle = acos(cosine);

    // De donde viene de verdad esta luz.
    const float real = 2.0 * atan(uShape.x * tan(min(viewAngle, 3.13) * 0.5));
    const float t = real / kPi;

    // Azimut alrededor del rumbo: las lineas salen radiales desde el punto de fuga.
    const float3 side = normalize(cross(uUp.xyz, forward));
    const float3 up = cross(forward, side);
    const float azimuth = atan2(dot(ray, up), dot(ray, side)) + kPi;
    const float along = log(max(tan(real * 0.5), 0.001));

    const float lines = Streaks(azimuth, along, 180.0, 1.0, 0.22)
                      + Streaks(azimuth, along * 0.6, 90.0, 2.0, 0.16) * 1.3
                      + Streaks(azimuth, along * 1.7, 320.0, 3.0, 0.30) * 0.7;

    const float3 color = Spectrum(t);
    const float core = exp(-viewAngle * viewAngle * (14.0 + uShape.x * uShape.x * 3.0)) * 1.1;  // la luz azul del frente
    const float haze = 0.10 * (1.0 - smoothstep(0.55, 1.0, t));                                 // resplandor entre las lineas
    float3 light = color * (lines * 0.85 + haze) + float3(0.55, 0.72, 1.0) * core;

    // Lo que queda atras se apaga: la escena se oscurece hasta el negro hacia el borde.
    const float rear = smoothstep(0.62, 0.98, t);
    const float3 faded = scene * (1.0 - strength * (0.30 + 0.70 * rear)) * (1.0 - uShape.z);
    light *= 1.0 - rear * rear;

    return float4(faded + light * strength, 1.0);
}
