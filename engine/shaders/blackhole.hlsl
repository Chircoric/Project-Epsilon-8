// blackhole.hlsl - Lente gravitacional y disco de acrecion (DXIL, Shader Model 6.0).
//
// Cada pixel sigue la trayectoria curva de un rayo de luz, en radios de Schwarzschild:
//
//   a = -1.5 h^2 x / r^5            orbita exacta de un foton alrededor de Schwarzschild
//   a += v x B                      arrastre del espacio de Kerr; es una aproximacion
//
// Es la misma integracion que TraceLensRay, donde se compara con las soluciones exactas.
// La imagen de fondo se lee de la escena ya dibujada en la direccion final del rayo, asi que
// planetas y estrellas se deforman de verdad. Lo que queda fuera de pantalla sale del panorama.

struct VsOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Texture2D<float4> gScene : register(t0, space2);
SamplerState gScene_S : register(s0, space2);
Texture2D<float> gDepth : register(t1, space2);
SamplerState gDepth_S : register(s1, space2);
Texture2D<float4> gSky : register(t2, space2);
SamplerState gSky_S : register(s2, space2);

cbuffer BlackHoleCB : register(b0, space3) {
    float4x4 uViewToWorld;
    float4 uCenter;       // km en vista, rs en km
    float4 uAxis;         // eje en vista, spin
    float2 uViewport;
    float uFocalPx;
    float uNearZ;
    float uDiscInner, uDiscOuter, uBrightness, uSeed;
    float uTime;
    float uHorizon;
    float uReach;         // radio de la zona afectada, en rs
    float uHasSky;
    float4 uSkyTint;
};

static const int kSteps = 110;
static const float kPi = 3.14159265358979;

float Hash(float3 p) {
    p = frac(p * 0.3183099 + 0.1);
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float Noise(float3 p) {
    const float3 i = floor(p), f = p - i;
    const float3 s = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(lerp(Hash(i), Hash(i + float3(1, 0, 0)), s.x), lerp(Hash(i + float3(0, 1, 0)), Hash(i + float3(1, 1, 0)), s.x), s.y),
                lerp(lerp(Hash(i + float3(0, 0, 1)), Hash(i + float3(1, 0, 1)), s.x), lerp(Hash(i + float3(0, 1, 1)), Hash(i + float3(1, 1, 1)), s.x), s.y), s.z);
}

// Del rojo al blanco azulado, como un cuerpo negro cada vez mas caliente.
float3 HeatColor(float t) {
    const float3 cold = float3(1.0, 0.22, 0.04), warm = float3(1.0, 0.62, 0.26), hot = float3(0.82, 0.90, 1.0);
    return t < 0.5 ? lerp(cold, warm, t * 2.0) : lerp(warm, hot, (t - 0.5) * 2.0);
}

// Luz que sale de un punto del disco hacia el rayo.
float4 DiscEmission(float3 hit, float3 rayDirection, float3 axis) {
    const float r = length(hit);
    if (r < uDiscInner || r > uDiscOuter) return float4(0, 0, 0, 0);

    // Temperatura de un disco delgado: T^4 ~ (1 - sqrt(rin / r)) / r^3 (Shakura y Sunyaev, 1973).
    const float x = uDiscInner / r;
    const float heat = pow(saturate(1.0 - sqrt(x)) * x * x * x * 18.0, 0.25);

    // Giro kepleriano: cada anillo va a su velocidad, asi que los grumos se estiran en espiral.
    const float3 radial = hit / r;
    const float3 tangent = normalize(cross(radial, axis));
    const float omega = 0.7071 / pow(r, 1.5);
    const float3 side = normalize(cross(axis, float3(0.31, 0.77, 0.56)));
    const float3 other = cross(axis, side);
    const float angle = atan2(dot(radial, other), dot(radial, side)) + omega * uTime * 1.6;
    const float3 sample_ = float3(cos(angle) * 4.0, sin(angle) * 4.0, r * 1.7) + uSeed;
    const float clumps = 0.45 + 0.9 * (Noise(sample_) * 0.65 + Noise(sample_ * 2.3) * 0.35);

    // Corrimiento: Doppler relativista del gas y corrimiento gravitacional.
    const float beta = saturate(sqrt(0.5 / max(r - uHorizon, 0.25)));
    const float gamma = 1.0 / sqrt(max(1.0 - beta * beta, 0.05));
    const float cosine = dot(tangent, -rayDirection);
    const float gravity = sqrt(saturate(1.0 - uHorizon / r));
    const float shift = gravity / (gamma * max(1.0 - beta * cosine, 0.05));
    const float boost = pow(shift, 3.0);  // brillo observado

    const float edge = smoothstep(uDiscOuter, uDiscOuter * 0.72, r) * smoothstep(uDiscInner, uDiscInner * 1.12, r);
    const float3 color = HeatColor(saturate(heat * shift * 0.85)) * heat * heat * clumps * boost * edge * uBrightness * 4.2;
    return float4(color, saturate(edge * heat * 1.4));
}

struct PsOut {
    float4 color : SV_Target0;
};

float4 PSBlackHole(VsOut i) : SV_Target0 {
    const float2 pixel = i.position.xy - uViewport * 0.5;
    const float3 startRay = normalize(float3(pixel / uFocalPx, 1.0));

    const float rs = uCenter.w;
    const float3 camera = -uCenter.xyz / rs;
    const float3 axis = uAxis.xyz;
    const float spin = uAxis.w;

    // Lo que esta delante del agujero no se deforma.
    const float depth = gDepth.SampleLevel(gDepth_S, i.uv, 0);
    if (depth > 0.0 && uNearZ / depth < uCenter.z - uReach * rs) discard;

    // Tramo recto hasta entrar en la zona afectada.
    float3 x = camera;
    const float along = -dot(camera, startRay);
    const float distance2 = dot(camera, camera);
    if (distance2 > uReach * uReach) {
        const float3 closest = camera + startRay * along;
        const float miss2 = dot(closest, closest);
        if (miss2 >= uReach * uReach || along < 0.0) discard;
        x = camera + startRay * (along - sqrt(uReach * uReach - miss2));
    }

    float3 v = startRay;
    const float3 momentum = cross(x, v);
    const float h2 = dot(momentum, momentum);

    float3 light = float3(0, 0, 0);
    float transmittance = 1.0;
    bool captured = false;
    const float dragSpin = min(spin, 0.9);

    [loop] for (int step_ = 0; step_ < kSteps; ++step_) {
        const float r = length(x);
        if (r < uHorizon) {
            captured = true;
            break;
        }
        if (r > uReach && dot(x, v) > 0.0) break;

        const float dt = clamp(r * 0.075, 0.015, 2.5);
        const float r2 = r * r;
        float3 a = x * (-1.5 * h2 / (r2 * r2 * r));
        if (spin > 0.0) {
            const float3 n = x / r;
            const float3 field = (n * (3.0 * dot(axis, n)) - axis) * (0.30 * dragSpin / (r2 * r));
            a += cross(v, field);
        }
        const float3 previous = x;
        v += a * dt;
        x += v * dt;

        // El rayo cruza el plano del disco: la curvatura hace que lo cruce varias veces,
        // y por eso se ve la parte de atras del disco por encima y por debajo de la sombra.
        const float before = dot(previous, axis), after = dot(x, axis);
        if (uBrightness > 0.0 && before * after < 0.0) {
            const float3 hit = previous + (x - previous) * (before / (before - after));
            const float4 emission = DiscEmission(hit - axis * dot(hit, axis), normalize(v), axis);
            light += transmittance * emission.rgb;
            transmittance *= 1.0 - emission.a;
            if (transmittance < 0.01) break;
        }
    }

    float3 background = float3(0, 0, 0);
    if (!captured && transmittance > 0.01) {
        float3 direction = normalize(v);

        // La lente se apaga al llegar al borde de su zona: asi la imagen no tiene un corte.
        const float impact = sqrt(h2);
        const float fade = 1.0 - smoothstep(uReach * 0.55, uReach * 0.98, impact);
        direction = normalize(startRay + (direction - startRay) * fade);

        bool onScreen = false;
        if (direction.z > 0.02) {
            const float2 target = direction.xy / direction.z * uFocalPx;
            const float2 uv = (target + uViewport * 0.5) / uViewport;
            onScreen = all(uv >= 0.0) && all(uv <= 1.0);
            if (onScreen) background = gScene.SampleLevel(gScene_S, uv, 0).rgb;
        }
        if (!onScreen && uHasSky <= 0.5) {
            // Sin panorama, lo que cae fuera sale del borde de la imagen: nunca un agujero negro de mas.
            const float2 side = normalize(direction.xy + float2(1e-5, 0.0));
            const float reachEdge = 0.5 / max(max(abs(side.x), abs(side.y)), 1e-3);
            background = gScene.SampleLevel(gScene_S, 0.5 + side * reachEdge * 0.98, 0).rgb;
        }
        if (!onScreen && uHasSky > 0.5) {
            const float3 world = mul((float3x3)uViewToWorld, direction);
            const float2 uv = float2(atan2(world.x, world.z) / (2.0 * kPi) + 0.5, asin(clamp(world.y, -1.0, 1.0)) / kPi + 0.5);
            background = gSky.SampleLevel(gSky_S, uv, 0).rgb * uSkyTint.rgb;
        }
    }

    return float4(light + background * transmittance, 1.0);
}
