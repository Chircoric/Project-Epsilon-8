// nebula.hlsl - Nubes volumetricas (DXIL, Shader Model 6.0).
//
// Se avanza por cada rayo sumando luz y restando transparencia. Corre a media resolucion: una
// nube no tiene bordes nitidos y esto la hace cuatro veces mas barata.
//
// Todo se calcula en radios de la nube: asi el float aguanta una nebulosa de decenas de anos
// luz. Las muestras se giran al mundo para que la nube no gire con la camara.

struct VsOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Texture3D<float> gNoise : register(t0, space2);
SamplerState gRepeat : register(s0, space2);
Texture2D<float> gDepth : register(t1, space2);
SamplerState gPoint : register(s1, space2);

static const int kMaxVolumes = 4;
static const int kSteps = 26;
static const int kMaxStars = 2;
static const int kMaxAir = 2;

cbuffer NebulaCB : register(b0, space3) {
    float4x4 uViewToWorld;
    float2 uViewport;
    float uFocalPx;
    float uNearZ;
    float uTime;
    float uCount;
    float uFogTone;
    float uFogRampOn;
    float4 uFog;        // color y distancia; 0 = sin niebla
    float4 uFogOrigin;  // w = estrellas
    float4 uRamp[4];
    float4 uStarCenter[kMaxStars];  // km en vista, radio en km
    float4 uStarColor[kMaxStars];   // a = intensidad
    float4 uStarShape[kMaxStars];   // semilla, corona
    float4 uAirCount;
    float4 uAirCenter[kMaxAir];     // km en vista, radio en km
    float4 uAirColor[kMaxAir];      // a = intensidad
    float4 uAirSun[kMaxAir];        // direccion, semilla
    float4 uAirAxis[kMaxAir];       // eje de giro en vista, achatamiento
    float4 uCenter[kMaxVolumes];  // km en vista, radio en km
    float4 uInner[kMaxVolumes];   // a = densidad
    float4 uOuter[kMaxVolumes];   // a = semilla
};

float Hash(float2 p) {
    return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));  // ruido de Jimenez
}

float Cloud(float3 p, float3 worldP, float seed) {
    const float3 q = worldP + seed;
    const float a = gNoise.SampleLevel(gRepeat, q * 0.31, 0);
    const float3 warp = q * 0.97 + (a - 0.5) * 0.6;
    const float b = gNoise.SampleLevel(gRepeat, warp, 0);
    const float c = gNoise.SampleLevel(gRepeat, warp * 3.1, 0);
    const float shape = a * 0.58 + b * 0.30 + c * 0.12;

    // Filamentos con huecos entre ellos, no una bola de niebla uniforme.
    const float falloff = saturate(1.0 - dot(p, p));
    return saturate((shape - 0.56 + falloff * 0.05) * 7.0) * falloff * (2.0 - falloff);  // sin borde: una raiz lo marcaria
}

float3 Ramp(float t) {
    const float at = saturate(t) * 3.0;
    const int k = min(int(at), 2);
    return lerp(uRamp[k].rgb, uRamp[k + 1].rgb, at - float(k));
}

float NoiseAt(float3 p) { return gNoise.SampleLevel(gRepeat, p, 0); }

// Corona: lo que se ve junto al disco de una estrella. Depende de la direccion desde la estrella,
// asi que se ven lineas que salen del disco; las protuberancias son lazos pegados al borde.
float3 Corona(float3 ray, float3 center, float radius, float3 color, float seed, float limitKm) {
    const float along = dot(center, ray);
    if (limitKm < along - radius * 1.2) return float3(0, 0, 0);
    // Un rayo que se aleja de la estrella tiene su punto mas cercano en la propia camara.
    const float3 closest = ray * max(along, 0.0) - center;
    const float b = length(closest) / radius;
    if (b < 1.0) return float3(0, 0, 0);

    const float h = b - 1.0;
    const float3 side = mul((float3x3)uViewToWorld, closest / (b * radius));
    const float t = uTime;
    const float wide = saturate((NoiseAt(side * 0.11 + seed + float3(0, 0, t * 0.0016)) - 0.36) * 3.2);
    const float fine = saturate((NoiseAt(side * 0.37 - seed + float3(t * 0.0022, 0, 0)) - 0.40) * 2.6);
    const float3 white = lerp(color, float3(1, 1, 1), 0.55);

    // Mirando hacia fuera, entre la camara y el vacio apenas hay corona.
    const float facing = saturate(along / length(center) * 1.8 + 0.5);
    float3 light = white * exp(-h * 2.4) * (0.05 + wide * 0.7) * (0.35 + fine * 1.3) * facing;
    light += white * exp(-h * 0.65) * wide * wide * fine * fine * 0.7 * facing;

    // Cromosfera: un filo rojizo justo sobre la superficie.
    const float3 hot = float3(1.0, 0.30, 0.10);
    light += hot * exp(-h * 26.0) * 1.4 * facing;

    const float arcs = NoiseAt(side * 0.24 + float3(h * 0.05, t * 0.0026, seed));
    const float patch_ = saturate((NoiseAt(side * 0.09 + float3(seed, -t * 0.0012, h * 0.03)) - 0.50) * 7.0);
    const float strand = saturate(1.0 - abs(arcs - 0.5) * 16.0);
    return light + hot * strand * patch_ * exp(-h * 5.0) * 3.4 * facing;
}

// Aire iluminado: sube desde el disco y se apaga poco a poco hacia el espacio. Su grueso cambia de
// un punto a otro y con el tiempo, asi que el borde no es un aro perfecto.
float3 Atmosphere(float3 ray, float3 center, float radius, float4 color, float4 sun, float4 axis, float limitKm) {
    const float along = dot(center, ray);
    if (along <= 0.0 || limitKm < along - radius * 1.4) return float3(0, 0, 0);
    float3 closest = ray * along - center;
    // Un planeta achatado se ve como una elipse: el aire se estira a lo largo del eje proyectado para
    // pegarse a ella. Con un circulo del radio del ecuador quedaba un hueco sobre los polos.
    const float3 projected = axis.xyz - ray * dot(axis.xyz, ray);
    const float s2 = dot(projected, projected);
    if (axis.w > 0.0 && s2 > 1e-6) {
        const float3 a = projected * rsqrt(s2);
        const float squash = sqrt((1.0 - axis.w) * (1.0 - axis.w) * s2 + (1.0 - s2));
        closest += a * dot(closest, a) * (1.0 / squash - 1.0);
    }
    const float b = length(closest) / radius;
    if (b > 1.4) return float3(0, 0, 0);

    const float3 outward = closest / max(b * radius, 1e-3);
    const float3 world = mul((float3x3)uViewToWorld, outward);
    const float t = uTime;
    const float wide = NoiseAt(world * 0.23 + sun.w + float3(t * 0.002, 0, -t * 0.0015));
    const float fine = NoiseAt(world * 0.71 - sun.w + float3(0, t * 0.004, t * 0.002));
    const float thick = 0.035 + 0.05 * wide + 0.022 * fine;

    const float h = (b - 1.0) / thick;
    const float glow = h > 0.0 ? exp(-h * 1.5) : exp(h * 0.42);
    const float day = smoothstep(-0.30, 0.55, dot(outward, sun.xyz));
    return color.rgb * color.a * glow * (0.06 + day * 2.1);
}

// La corona sola, alrededor de la miniatura del mapa: la misma que se ve junto a una estrella real.
// La estrella 0 esta en el centro de la miniatura, a uFogOrigin.xy pixeles de la esquina de este
// rectangulo, y uStarColor[0].a la atenua para que no tape el disco.
float4 PSStarCorona(VsOut i) : SV_Target0 {
    const float2 pixel = i.uv * uViewport - uFogOrigin.xy;
    const float3 ray = normalize(float3(pixel / uFocalPx, 1.0));
    const float3 center = uStarCenter[0].xyz;
    const float radius = uStarCenter[0].w;
    const float3 color = uStarColor[0].rgb;
    float3 light = Corona(ray, center, radius, color, uStarShape[0].x, 1.0e30);

    // La luz que se dispersa alrededor: sin ella la corona acabaria en seco contra el fondo.
    const float b = length(ray * dot(center, ray) - center) / radius;
    if (b > 1.0) light += lerp(color, float3(1.0, 0.72, 0.42), 0.35) * (exp(-(b - 1.0) * 3.2) * 0.38 + exp(-(b - 1.0) * 1.1) * 0.07);
    light *= uStarColor[0].a;
    return float4(1.0 - exp(-light), 1.0);
}

float4 PSNebula(VsOut i) : SV_Target0 {
    const float2 pixel = i.uv * uViewport - uViewport * 0.5;
    const float3 ray = normalize(float3(pixel / uFocalPx, 1.0));

    // Hasta donde llega el rayo: lo opaco tapa la nube que tiene detras.
    const float depth = gDepth.SampleLevel(gPoint, i.uv, 0);
    const float limitKm = depth > 0.0 ? uNearZ / depth / ray.z : 1.0e30;

    const float jitter = Hash(i.position.xy);
    float3 light = float3(0, 0, 0);
    float transmittance = 1.0;

    // Vienen ordenadas de atras hacia adelante.
    const int count = int(uCount);
    for (int v = 0; v < count; ++v) {
        const float radius = uCenter[v].w;
        const float3 origin = -uCenter[v].xyz / radius;
        const float along = -dot(origin, ray);
        const float3 closest = origin + ray * along;
        const float miss2 = dot(closest, closest);
        if (miss2 >= 1.0) continue;

        const float half_ = sqrt(1.0 - miss2);
        const float enter = max(along - half_, 0.0);
        const float leave = min(along + half_, limitKm / radius);
        if (leave <= enter) continue;

        const float step_ = (leave - enter) / float(kSteps);
        const float3 inner = uInner[v].rgb, outer = uOuter[v].rgb;
        const float density = uInner[v].a * 3.2, seed = uOuter[v].a;

        float3 volumeLight = float3(0, 0, 0);
        float volumeT = 1.0;
        float t = enter + step_ * jitter;
        [loop] for (int s = 0; s < kSteps; ++s) {
            const float3 p = origin + ray * t;
            const float3 worldP = mul((float3x3)uViewToWorld, p);
            const float cloud = Cloud(p, worldP, seed);
            if (cloud > 0.002) {
                const float absorbed = 1.0 - exp(-cloud * density * step_);
                // El nucleo denso brilla mas y con su color propio; el borde es polvo tenue.
                const float3 glow = lerp(outer, inner, saturate(cloud * 1.4)) * (0.25 + cloud * 1.5);
                volumeLight += volumeT * absorbed * glow;
                volumeT *= 1.0 - absorbed;
                if (volumeT < 0.01) break;
            }
            t += step_;
        }
        light = light * volumeT + volumeLight;
        transmittance *= volumeT;
    }
    // Niebla uniforme delante de todo: cuanto mas lejos esta lo opaco, menos queda de ello.
    if (uFog.w > 0.0) {
        // La radiacion de una estrella barre la niebla a su alrededor: de cerca se ve despejado.
        float cleared = 0.0;
        const int lights = int(uFogOrigin.w);
        for (int s = 0; s < lights; ++s) {
            if (uStarShape[s].y > 0.5) cleared = max(cleared, smoothstep(0.05, 0.40, uStarCenter[s].w / length(uStarCenter[s].xyz)));
        }
        const float kept = exp(-min(limitKm / uFog.w, 40.0) * (1.0 - cleared * 0.985));
        const float3 world = mul((float3x3)uViewToWorld, ray);
        const float swirl = gNoise.SampleLevel(gRepeat, world * 1.3 + uFogOrigin.xyz, 0) * 0.6 + gNoise.SampleLevel(gRepeat, world * 3.1 - uFogOrigin.zxy, 0) * 0.4;
        // El color cambia por zonas, entre cuadros del cielo y entre un lado y el otro de la bóveda.
        const float broad = NoiseAt(world * 0.17 + uFogOrigin.xyz * 0.1) * 0.65 + NoiseAt(world * 0.46 - uFogOrigin.zxy * 0.1) * 0.35;
        const float3 tint = uFogRampOn > 0.5 ? Ramp(uFogTone + (broad - 0.5) * 2.0 + (swirl - 0.5) * 0.25 + dot(world, float3(0.48, 0.20, 0.85)) * 0.34) : uFog.rgb;
        float3 fog = tint * (uFogRampOn > 0.5 ? 0.62 + swirl * 0.75 : 0.45 + swirl * 1.1);

        float3 shine = float3(0, 0, 0);
        const int stars = int(uFogOrigin.w);
        for (int s = 0; s < stars; ++s) {
            const float3 center = uStarCenter[s].xyz;
            const float radius = uStarCenter[s].w, power = uStarColor[s].a;
            const float3 color = uStarColor[s].rgb;
            const float distance = length(center);
            const float cosine = dot(ray, center / distance);

            // Dispersion hacia delante (Henyey-Greenstein): suave y sin bordes en toda la esfera,
            // da igual si la estrella cae dentro o fuera de la pantalla.
            const float g = 0.82;
            const float phase = (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * cosine, 1.5);
            fog += tint * color * power * 0.9;
            fog += color * power * phase * 0.10 * (1.0 - cleared * 0.9);

            // A lo lejos el disco no llega entero: la niebla lo convierte en un nucleo borroso.
            const float size = max(asin(min(radius / distance, 0.999)), 0.012);
            const float angle = acos(clamp(cosine, -1.0, 1.0));
            const float veil = 1.0 - exp(-distance / uFog.w);
            const float open = limitKm > dot(center, ray) - radius * 1.2 ? 1.0 : 0.0;
            shine += color * 1.8 * exp(-angle * angle / (size * size) * 0.45) * veil * open * uStarShape[s].y * (1.0 - cleared);

            if (uStarShape[s].y > 0.5) {
                const float near = saturate((radius / distance - 0.012) / 0.05);
                if (near > 0.0) shine += Corona(ray, center, radius, color, uStarShape[s].x, limitKm) * near;
            }
        }
        light = light * kept + fog * (1.0 - kept) + shine;
        transmittance *= kept;

        const int airs = int(uAirCount.x);
        for (int s = 0; s < airs; ++s) {
            const float haze = exp(-length(uAirCenter[s].xyz) / uFog.w);  // la niebla tambien la tapa
            light += Atmosphere(ray, uAirCenter[s].xyz, uAirCenter[s].w, uAirColor[s], uAirSun[s], uAirAxis[s], limitKm) * haze;
        }
    }
    return float4(light, transmittance);
}
