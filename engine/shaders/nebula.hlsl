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
    float4 uCloud;                // km de la baldosa de la nube (0 = niebla lisa), pasos, calidad, planetas
    float4 uBlocker[2];           // planetas que tapan el sol: centro en vista (km), radio
};

// Cuanta luz del sol llega a un punto (en vista, km) sin que un planeta se interponga: 1 libre,
// 0 en plena sombra. El borde es una penumbra del ancho aparente del sol visto desde alli.
float SunVisible(float3 p, float3 sunCenter, float sunRadius) {
    float lit = 1.0;
    const float3 toSun = sunCenter - p;
    const float sunDistance = length(toSun);
    const float3 l = toSun / sunDistance;
    const float sunAngle = max(sunRadius / sunDistance, 1e-4);
    [unroll] for (int k = 0; k < 2; ++k) {
        if (float(k) < uCloud.w) {
            const float3 c = uBlocker[k].xyz - p;
            const float along = dot(c, l);
            if (along > 0.0 && along < sunDistance) {
                const float planetAngle = asin(min(uBlocker[k].w / length(c), 1.0));
                const float gap = acos(clamp(dot(normalize(c), l), -1.0, 1.0));
                lit *= smoothstep(planetAngle - sunAngle, planetAngle + sunAngle, gap);
            }
        }
    }
    return lit;
}

// Ruido blanco por pixel. El de Jimenez (gradiente entrelazado) deja rayas en diagonal que, a media
// resolucion y ampliadas, parecian el tejido de una sabana.
// El ruido al azar dejaba grano. Ahora es una matriz de Bayer de 4 x 4: cada bloque de 4 x 4 pixeles
// usa los 16 desfases una vez, y la mezcla al ampliar (PSNebulaUpsample) los promedia. Equivale a 16
// veces mas pasos, sin grano ni rayas.
float Hash(float2 p) {
    static const float kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    const uint2 q = uint2(p) & 3u;
    return (kBayer[q.y * 4u + q.x] + 0.5) / 16.0;
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

// --- La nube de la nebulosa, alrededor de la camara ----------------------------------------
// El ruido de la textura vale 0,5 de media y se aparta poco (unas 0,09): los umbrales van cerca.
// Todas las frecuencias son multiplos enteros de la baldosa, asi la nube no salta al cruzar su
// borde (la camara vuelve a 0 al pasar de 1).
struct Gas {
    float glow;  // gas ionizado que brilla
    float dust;  // polvo frio: tapa lo de detras y apenas devuelve luz
    float tone;  // por donde va de la paleta: rojo, naranja, ocre o gris
    float mass;  // la forma grande, para el borde iluminado
};

// detail (0 a 1) apaga el ruido fino: a lo lejos sus manchas miden menos que un pixel y, muestreadas
// a saltos, solo dejan grano. Alli quedan las formas grandes, suaves.
Gas GasAt(float3 q, bool fine, float detail) {
    Gas g;
    const float a = NoiseAt(q);
    const float3 w = q * 2.0 + (a - 0.5) * 0.45;  // torcido por la capa grande: nada de bolas
    const float b = NoiseAt(w);
    const float c = NoiseAt(w * 3.0 + 0.37);
    // Masas grandes con huecos entre ellas, de borde blando.
    g.mass = smoothstep(0.45, 0.62, a * 0.6 + b * 0.4);
    // Filamentos: las crestas de la capa media, donde cruza su valor medio. Anchos: de lejos un
    // filamento fino parpadea de un pixel al siguiente.
    const float ridge = saturate(1.0 - abs(b - 0.5) / 0.06);
    float filament = ridge * ridge * smoothstep(0.40, 0.56, a);
    float grain = lerp(0.5, c, detail);
    if (fine && detail > 0.0) {
        const float d = NoiseAt(w * 6.0 + 0.71);
        grain = lerp(grain, c * 0.7 + d * 0.3, detail);
        filament *= lerp(1.0, 0.75 + 0.5 * d, detail);
    }
    g.glow = saturate(g.mass * (0.35 + 1.3 * (grain - 0.35)) + filament * 0.85);
    // Las calles de polvo van por su cuenta y cortan el gas en siluetas oscuras.
    const float lane = NoiseAt(q * 2.0 + float3(0.31, 0.77, 0.53));
    g.dust = smoothstep(0.56, 0.70, lane * 0.85 + lerp(0.5, c, detail) * 0.15);
    g.tone = NoiseAt(q + float3(0.61, 0.13, 0.89));
    return g;
}

// Cuanta luz de una estrella llega a un punto: cae con el cuadrado de la distancia medida en
// radios de la estrella. A 1 UA de un sol es casi plena; a 5 UA, un cuarto; a 20, casi nada.
float StarReach(float distanceKm, float radiusKm) {
    const float x = distanceKm / max(radiusKm * 600.0, 1.0);
    return 1.0 / (1.0 + x * x);
}

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
    // Una capa fina: en la Tierra el aire visible es un filo de un 1 % del radio, no un aro ancho.
    const float thick = 0.014 + 0.012 * wide + 0.006 * fine;

    const float h = (b - 1.0) / thick;
    const float glow = h > 0.0 ? exp(-h * 1.8) : exp(h * 0.9);
    // Solo brilla donde le da el sol; junto a la linea de sombra la luz llega tras cruzar mucho aire
    // y sale rojiza (como un atardecer visto desde fuera), y en la noche no queda nada.
    const float facing = dot(outward, sun.xyz);
    const float day = smoothstep(-0.18, 0.45, facing);
    const float dusk = smoothstep(-0.18, 0.05, facing) * (1.0 - smoothstep(0.05, 0.35, facing));
    const float3 tint = lerp(color.rgb, float3(1.0, 0.45, 0.18) * max(color.r, max(color.g, color.b)), dusk * 0.7);
    return tint * color.a * glow * (0.015 + day * 0.42);
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
        float kept = exp(-min(limitKm / uFog.w, 40.0) * (1.0 - cleared * 0.985));
        const float3 world = mul((float3x3)uViewToWorld, ray);
        const float swirl = gNoise.SampleLevel(gRepeat, world * 1.3 + uFogOrigin.xyz, 0) * 0.6 + gNoise.SampleLevel(gRepeat, world * 3.1 - uFogOrigin.zxy, 0) * 0.4;
        // El color cambia por zonas, entre cuadros del cielo y entre un lado y el otro de la bóveda.
        const float broad = NoiseAt(world * 0.17 + uFogOrigin.xyz) * 0.65 + NoiseAt(world * 0.46 - uFogOrigin.zxy) * 0.35;
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
            // Detras de un planeta el sol esta tapado: ni su resplandor en la niebla ni su brillo
            // alrededor (la corona, pegada al disco, si asoma por el borde, como en un eclipse).
            const float camLit = uCloud.w > 0.0 ? SunVisible(float3(0, 0, 0), center, radius) : 1.0;
            fog += tint * color * power * 0.9 * (0.35 + 0.65 * camLit);
            fog += color * power * phase * 0.10 * (1.0 - cleared * 0.9) * camLit;

            // A lo lejos el disco no llega entero: la niebla lo convierte en un nucleo borroso.
            const float size = max(asin(min(radius / distance, 0.999)), 0.012);
            const float angle = acos(clamp(cosine, -1.0, 1.0));
            const float veil = 1.0 - exp(-distance / uFog.w);
            const float open = limitKm > dot(center, ray) - radius * 1.2 ? 1.0 : 0.0;
            shine += color * 1.8 * exp(-angle * angle / (size * size) * 0.45) * veil * open * uStarShape[s].y * (1.0 - cleared) * camLit;

            if (uStarShape[s].y > 0.5) {
                const float near = saturate((radius / distance - 0.012) / 0.05);
                if (near > 0.0) shine += Corona(ray, center, radius, color, uStarShape[s].x, limitKm) * near;
            }
        }
        if (uCloud.x > 0.0) {
            // La nube de verdad: se recorre el rayo con pasos que se alargan con la distancia. Cada
            // trozo de gas brilla con su color, mas cuanto mas cerca esta de una estrella, y su
            // borde vuelto hacia ella se enciende. El polvo frio se queda oscuro y tapa lo de detras.
            const int steps = int(uCloud.y);
            const bool fine = uCloud.z >= 3.0;
            const float far = uFog.w * 12.0;
            const float reach = min(limitKm, far);
            const float3 origin = uFogOrigin.xyz;
            const float perKm = 1.0 / uCloud.x;
            const float sigmaScale = (1.0 - cleared * 0.985) * 1.5 / uFog.w;

            // Hacia donde queda la estrella principal, en el mundo: para el borde iluminado.
            float3 toStar = float3(0, 0, 0);
            float g0 = 0.0;
            if (stars > 0) {
                toStar = mul((float3x3)uViewToWorld, normalize(uStarCenter[0].xyz));
                const float cosine = dot(ray, normalize(uStarCenter[0].xyz));
                const float g = 0.6;
                g0 = (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * cosine, 1.5);  // brillo hacia la estrella
            }

            float3 cloudLight = float3(0, 0, 0);
            float cloudT = 1.0;
            float prev = 0.0;
            [loop] for (int s = 0; s < steps; ++s) {
                const float u = (float(s) + jitter) / float(steps);
                const float t = reach * u * u;  // denso cerca, largo lejos
                const float dt = t - prev;
                prev = t;
                const float3 q = origin + world * (t * perKm);
                const Gas gas = GasAt(q, fine, saturate(1.0 - t / (far * 0.3)));

                // Luz que llega a este punto: un fondo tenue de toda la nebulosa y las estrellas.
                float3 lightIn = float3(0.85, 0.80, 0.76);
                float rim = 0.0, sun0 = 0.0;
                [unroll] for (int k = 0; k < kMaxStars; ++k) {
                    if (k < stars) {
                        // En la sombra de un planeta el gas no recibe la luz de ese sol: queda un cono
                        // oscuro detras del planeta, como en la nebulosa de verdad.
                        float reachK = StarReach(length(uStarCenter[k].xyz - ray * t), uStarCenter[k].w);
                        if (uCloud.w > 0.0) reachK *= SunVisible(ray * t, uStarCenter[k].xyz, uStarCenter[k].w);
                        lightIn += uStarColor[k].rgb * min(reachK * 1.0, 0.55);
                        if (k == 0) rim = sun0 = reachK;
                    }
                }
                // Borde iluminado: donde el gas se acaba mirando a la estrella, la luz no tiene
                // nada delante y lo enciende; el lado de detras queda en sombra.
                if (stars > 0) {
                    const float ahead = NoiseAt(q + toStar * 0.035);
                    rim *= saturate((gas.mass - smoothstep(0.45, 0.63, ahead)) * 2.5 + 0.15);
                }

                // La paleta del juego es oscura y sus tonos parecidos: se lleva cada color a un
                // brillo parecido para que rojo, naranja, ocre y gris se distingan de verdad.
                const float3 raw = Ramp(uFogTone + (gas.tone - 0.5) * 7.0 + (gas.glow - 0.5) * 0.45);
                float3 hue = lerp(raw * 1.3, raw / max(max(raw.r, max(raw.g, raw.b)), 0.05) * 0.40, 0.6);
                // Junto a un sol el gas toma su color: se vuelve amarillo y ocre, mas calido.
                if (stars > 0) hue = lerp(hue, hue * 0.55 + uStarColor[0].rgb * 0.22, saturate(sun0 * 1.4));
                // Y se ve mas brillante mirando hacia el sol: el gas dispersa la luz sobre todo hacia
                // delante (g0), asi que el cielo se ordena alrededor de la estrella.
                const float forward = stars > 0 ? 1.0 + min(g0, 6.0) * 0.18 * saturate(sun0 * 3.0) : 1.0;
                const float3 emit = (hue * lightIn * (0.15 + 2.5 * gas.glow) + uStarColor[0].rgb * rim * gas.glow * 0.35 * (stars > 0 ? 1.0 : 0.0)) * forward
                                  + uStarColor[0].rgb * rim * g0 * 0.05;
                // Polvo: gris pardo, devuelve algo de la luz que le llega; tapa, pero no es un muro.
                const float3 dustColor = float3(0.20, 0.175, 0.16) * lightIn * 0.55;
                // La radiacion del sol ha barrido el gas a su alrededor: una burbuja mas limpia.
                const float cavity = 1.0 - 0.75 * saturate(sun0 * 1.6) * saturate(sun0 * 1.6);
                const float wGas = (0.05 + gas.glow * 2.4) * cavity, wDust = gas.dust * 2.2 * cavity;
                // El gas de alrededor es tenue: si no, cuando la camara cae dentro de una masa, esa masa
                // tapa todo el cielo con un solo color. Asi se ven las formas a distancia.
                const float nearFade = 0.12 + 0.88 * smoothstep(0.0, uFog.w * 0.9, t);
                const float sigma = (wGas + wDust) * sigmaScale * nearFade;
                const float3 source = (emit * wGas + dustColor * wDust) / max(wGas + wDust, 1e-5);
                const float absorbed = 1.0 - exp(-sigma * dt);
                cloudLight += cloudT * absorbed * source;
                cloudT *= 1.0 - absorbed;
                if (cloudT < 0.004) break;
            }
            // Mas alla de lo recorrido, la nebulosa lejana: dos muestras gruesas muy lejos para que por
            // los huecos se vean masas de color, y debajo el brillo general de la nube.
            if (limitKm > far) {
                float3 distant = fog * 0.30;
                [unroll] for (int k = 0; k < 2; ++k) {
                    const float t = far * (k == 0 ? 2.8 : 1.6);  // de atras hacia delante
                    const Gas gas = GasAt(origin + world * (t * perKm), false, 0.0);
                    const float3 raw = Ramp(uFogTone + (gas.tone - 0.5) * 7.0);
                    const float3 hue = lerp(raw * 1.3, raw / max(max(raw.r, max(raw.g, raw.b)), 0.05) * 0.40, 0.6);
                    const float cover = saturate(gas.glow * 1.4 + gas.dust * 0.8);
                    const float3 c = lerp(hue * (0.35 + 2.2 * gas.glow), float3(0.05, 0.04, 0.035), gas.dust * 0.8) * (0.85 + uStarColor[0].a * 0.4);
                    distant = lerp(distant, c, cover);  // la de delante tapa a la de detras
                }
                cloudLight += cloudT * distant;
                cloudT = 0.0;
            }
            light = light * cloudT + cloudLight + shine;
            transmittance *= cloudT;
        } else {
            light = light * kept + fog * (1.0 - kept) + shine;
            transmittance *= kept;
        }

        const int airs = int(uAirCount.x);
        for (int s = 0; s < airs; ++s) {
            const float haze = exp(-length(uAirCenter[s].xyz) / uFog.w);  // la niebla tambien la tapa
            light += Atmosphere(ray, uAirCenter[s].xyz, uAirCenter[s].w, uAirColor[s], uAirSun[s], uAirAxis[s], limitKm) * haze;
        }
    }
    return float4(light, transmittance);
}
