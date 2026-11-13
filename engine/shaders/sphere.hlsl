// sphere.hlsl - Esferas calculadas por pixel (DXIL, Shader Model 6.0).
//
// No hay malla. El vertex shader coloca un cuadro que atraviesa el centro de la esfera y
// mira a la camara, con tamano justo para cubrir la silueta. Por pixel se resuelve el
// choque rayo-esfera, se obtiene la normal real y se gira hasta el espacio del cuerpo.
//
// El calculo usa el radio como unidad. Asi se evita restar numeros enormes a gran distancia,
// lo que en float haria temblar el borde.
//
// Registros de SDL_GPU: t[n] space0 = buffers de vertice; b[n] space1 = uniforms de vertice;
//                       t[n] space2 = buffers de fragmento; b[n] space3 = uniforms de fragmento.

cbuffer FrameCB : register(b0, space1) {
    float4x4 uProjection;
    float2 uViewport;
    uint uFirstInstance;  // Direct3D no suma first_instance a SV_InstanceID
    float uFramePad;
};

ByteAddressBuffer gSpheresVS : register(t0, space0);
ByteAddressBuffer gSpheresPS : register(t0, space2);

cbuffer SphereCB : register(b0, space3) {
    float uNearZ;
    float uTime;
    float uStarBoost;    // las estrellas superan 1 para que el bloom las haga brillar
    float uDetail;       // calidad de la luz: 3 = ultra
    float4 uAmbient;     // luz de la nebulosa que llega por todos lados (0 fuera de ella)
};

// Oren-Nayar: la luz sobre un suelo rugoso. Un planeta de roca o de tierra no es plastico: hacia el
// borde no se apaga tanto como dice Lambert y devuelve mas luz hacia donde viene el sol.
float OrenNayar(float3 n, float3 l, float3 v, float sigma) {
    const float cosI = dot(n, l), cosR = dot(n, v);
    if (cosI <= 0.0) return 0.0;
    const float s2 = sigma * sigma;
    const float A = 1.0 - 0.5 * s2 / (s2 + 0.33);
    const float B = 0.45 * s2 / (s2 + 0.09);
    const float3 li = l - n * cosI, vr = v - n * cosR;
    const float cosPhi = dot(li, vr) * rsqrt(max(dot(li, li) * dot(vr, vr), 1e-8));
    const float thetaI = acos(saturate(cosI)), thetaR = acos(saturate(max(cosR, 0.0)));
    const float alpha = max(thetaI, thetaR), beta = min(thetaI, thetaR);
    return cosI * (A + B * max(cosPhi, 0.0) * sin(alpha) * tan(min(beta, 1.5)));
}

static const uint kStride = 208;
static const float kAtmosphere = 0.13;  // grueso maximo de la atmosfera visible, en radios

struct VsOut {
    float4 position : SV_Position;
    float2 plane : TEXCOORD0;                 // coordenadas sobre el cuadro, en radios
    nointerpolation uint instance : TEXCOORD1;
};

// Base ortonormal con w apuntando de la camara al centro.
void MakeBasis(float3 w, out float3 u, out float3 v) {
    const float3 reference = abs(w.y) < 0.9 ? float3(0, 1, 0) : float3(1, 0, 0);
    u = normalize(cross(reference, w));
    v = cross(w, u);
}

VsOut VSSphere(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {
    static const float2 kCorners[6] = {
        float2(-1, -1), float2(1, -1), float2(1, 1),
        float2(-1, -1), float2(1, 1),  float2(-1, 1)
    };

    const uint instance = instanceId + uFirstInstance;
    const uint base = instance * kStride;
    const float4 sphere = asfloat(gSpheresVS.Load4(base));
    const float pixelRadius = asfloat(gSpheresVS.Load(base + 44));
    const float haze = asfloat(gSpheresVS.Load(base + 124));
    const float distance = length(sphere.xyz);
    const float3 w = sphere.xyz / distance;
    float3 u, v;
    MakeBasis(w, u, v);

    // El cono tangente corta el plano del centro a r / cos(a), no a r.
    const float sine = min(sphere.w / distance, 0.9995);
    const float reach = (1.0 + 2.0 / pixelRadius + (haze > 0.0 ? kAtmosphere * 1.1 : 0.0)) / sqrt(1.0 - sine * sine);
    const float2 plane = kCorners[vertexId] * reach;
    const float3 position = sphere.xyz + (u * plane.x + v * plane.y) * sphere.w;

    VsOut o;
    o.position = mul(uProjection, float4(position, 1.0));
    o.plane = plane;
    o.instance = instance;
    return o;
}

// --- Ruido 3D ------------------------------------------------------------------
// No usa texturas: no ocupa memoria de video, no tiene costuras ni polos y cada semilla
// produce un cuerpo distinto.

float Hash(int3 p) {
    uint3 v = uint3(p) * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16;
    v.x += v.y * v.z;
    return float(v.x) * (1.0 / 4294967296.0);
}

float Noise(float3 p) {
    const int3 i = int3(floor(p));
    const float3 f = p - floor(p);
    const float3 s = f * f * (3.0 - 2.0 * f);
    const float x00 = lerp(Hash(i), Hash(i + int3(1, 0, 0)), s.x);
    const float x10 = lerp(Hash(i + int3(0, 1, 0)), Hash(i + int3(1, 1, 0)), s.x);
    const float x01 = lerp(Hash(i + int3(0, 0, 1)), Hash(i + int3(1, 0, 1)), s.x);
    const float x11 = lerp(Hash(i + int3(0, 1, 1)), Hash(i + int3(1, 1, 1)), s.x);
    return lerp(lerp(x00, x10, s.y), lerp(x01, x11, s.y), s.z) * 2.0 - 1.0;
}

// Giro entre capas: evita que las ondulaciones queden alineadas con los ejes.
static const float3x3 kOctaveTurn = float3x3(0.00, 0.80, 0.60, -0.80, 0.36, -0.48, -0.60, -0.48, 0.64);

// Suma capas de ruido. La capa con decimales entra gradualmente, sin saltos al acercarse.
float Fbm(float3 p, float octaves, float gain = 0.54) {
    float sum = 0.0, amplitude = 0.55;
    const int count = int(ceil(octaves));
    [loop] for (int k = 0; k < count; ++k) {
        sum += amplitude * saturate(octaves - float(k)) * Noise(p);
        p = mul(kOctaveTurn, p) * 2.03 + 11.7;
        amplitude *= gain;
    }
    return sum;
}

// --- Superficies ---------------------------------------------------------------

struct Surface {
    float3 albedo;    // reacciona a la luz
    float3 emission;  // brilla por si misma
    float3 slope;     // pendiente del relieve en el espacio del cuerpo: tuerce la normal
    float gloss;      // 1 = agua: refleja el sol
};

Surface MakeSurface(float3 albedo) {
    Surface s;
    s.albedo = albedo;
    s.emission = float3(0, 0, 0);
    s.slope = float3(0, 0, 0);
    s.gloss = 0.0;
    return s;
}

// Dos direcciones tangentes a la esfera en n.
void Tangents(float3 n, out float3 t1, out float3 t2) {
    t1 = normalize(cross(n, abs(n.y) < 0.9 ? float3(0, 1, 0) : float3(1, 0, 0)));
    t2 = cross(n, t1);
}

// Hielo desde los polos. cap = 0 no pone nada; cap = 1 cubre todo el cuerpo.
float IceMask(float3 n, float warp, float cap) {
    const float limit = 1.12 - cap * 1.4;
    return cap <= 0.0 ? 0.0 : smoothstep(limit - 0.02, limit + 0.02, abs(n.y) + warp * 0.10);
}

// Altura de un mundo con agua: continentes deformados y, encima, cordilleras de crestas afiladas.
float TerrainHeight(float3 n, float seed, float detail, float sea, float relief) {
    const float3 p = n * 3.0 + seed;
    const float warp = Fbm(p + 40.0, min(detail, 3.0));
    const float continents = Fbm(p + warp * 0.8, detail) - (sea - 0.5) * 0.6;
    const float ridges = 1.0 - abs(Fbm(p * 2.3 + 17.0 + warp * 0.5, max(detail - 1.0, 1.0), 0.5));
    return continents + ridges * ridges * 0.16 * relief * smoothstep(-0.02, 0.12, continents);
}

// Densidad de las nubes: frentes estirados por el viento, ciclones que las enroscan (al reves en
// cada hemisferio) y, de cerca, cumulos hinchados en grano.
// x = densidad (0 a 1); y = altura de las cimas, sin recortar, para que dentro de una nube espesa
// tambien haya bultos que la luz pueda modelar.
float2 CloudField(float3 n, float seed, float detail, float cover) {
    float3 q = n;
    [unroll] for (int k = 0; k < 3; ++k) {
        const int key = int(seed * 53.0) + k * 7;
        const float3 eye = normalize(float3(Hash(int3(key, 1, 0)) - 0.5, (Hash(int3(key, 2, 0)) - 0.5) * 1.1, Hash(int3(key, 3, 0)) - 0.5));
        const float d2 = dot(q - eye, q - eye);
        const float twist = 3.2 * exp(-d2 / 0.02) * (eye.y > 0.0 ? 1.0 : -1.0);
        const float c = cos(twist), s = sin(twist);
        q = q * c + cross(eye, q) * s + eye * dot(eye, q) * (1.0 - c);
    }
    const float3 p = q * float3(4.6, 7.5, 4.6) + seed * 2.0 + float3(uTime * 0.004, 0, 0);
    const float warp = Fbm(p * 0.7 + 13.0, min(detail, 4.0));
    const float base = Fbm(p + warp * 1.1, min(detail, 7.0), 0.62);
    // Se rompen en celdas y jirones: nunca una mancha blanca lisa.
    const float cells = 1.0 - abs(Fbm(q * 15.0 + seed * 3.0 + warp * 0.7, min(detail, 6.0), 0.55));
    float puff = 0.0;
    if (detail > 4.0) puff = (1.0 - abs(Fbm(n * 34.0 + seed * 4.0 + warp, detail - 3.0, 0.5))) * saturate(detail - 4.0);
    const float start = lerp(0.34, 0.02, cover);
    const float top = base + (cells - 0.6) * 0.22 + puff * 0.14 - 0.07;
    return float2(smoothstep(start, start + 0.22, top), top + puff * 0.10);
}

float CloudDensity(float3 n, float seed, float detail, float cover) { return CloudField(n, seed, detail, cover).x; }

Surface Terrestrial(float3 n, float seed, float detail, float3 ocean, float3 land, float3 peak, float4 shape, float texel, float3 sunBody) {
    const float sea = shape.x, iceCap = shape.y, cover = shape.z, relief = shape.w;
    const float3 p = n * 3.0 + seed;
    const float warp = Fbm(p + 40.0, min(detail, 3.0));
    // El nivel del mar sale de la fraccion cubierta de agua.
    const float height = TerrainHeight(n, seed, detail, sea, relief);
    const float landMask = smoothstep(0.0, 0.012, height);

    // Mar: oscuro en lo hondo y turquesa sobre la plataforma de la costa.
    float3 color = lerp(ocean * 0.30, ocean, smoothstep(-0.30, -0.04, height));
    color = lerp(color, ocean * 1.35 + float3(0.02, 0.10, 0.08), smoothstep(-0.05, 0.0, height) * 0.6);

    // Tierra: la vegetacion se apaga hacia los polos y en las latitudes secas aparece el desierto.
    const float latitude = abs(n.y);
    const float dry = smoothstep(0.05, 0.35, Fbm(p * 0.7 + 77.0, min(detail, 3.0))) * (1.0 - smoothstep(0.30, 0.60, latitude));
    const float cold = smoothstep(0.50, 0.80, latitude + warp * 0.1);
    const float patches = Fbm(n * 11.0 + seed * 1.3, min(detail, 5.0));
    float3 ground = land * (0.80 + patches * 0.45);
    ground = lerp(ground, peak * 1.15 * (0.9 + patches * 0.3), dry * 0.8);
    ground = lerp(ground, lerp(peak, float3(0.45, 0.43, 0.40), 0.5), cold * 0.7);
    color = lerp(color, ground, landMask);
    color = lerp(color, peak * 0.9, smoothstep(0.20 / relief, 0.28 / relief, height));
    color = lerp(color, float3(0.92, 0.94, 0.97), smoothstep(0.34 / relief, 0.42 / relief, height + cold * 0.12));
    const float ice = IceMask(n, warp, iceCap);
    color = lerp(color, float3(0.92, 0.95, 0.98), ice);

    // Grano fino del terreno: solo aparece de cerca, cuando las capas altas caben en pantalla.
    if (detail > 8.0) color *= 1.0 + 0.30 * saturate((detail - 8.0) / 1.5) * Fbm(n * 48.0 + seed, detail - 4.0, 0.72);

    Surface s = MakeSurface(color);
    // Relieve de la tierra firme: la pendiente sale de la altura en dos puntos vecinos.
    if (landMask > 0.0) {
        float3 t1, t2;
        Tangents(n, t1, t2);
        const float e = clamp(texel, 2e-4, 0.01);
        const float lod = max(detail - 1.0, 1.0);
        const float h1 = TerrainHeight(normalize(n + t1 * e), seed, lod, sea, relief);
        const float h2 = TerrainHeight(normalize(n + t2 * e), seed, lod, sea, relief);
        const float h0 = TerrainHeight(n, seed, lod, sea, relief);
        s.slope = (t1 * (h1 - h0) + t2 * (h2 - h0)) / e * 0.035 * relief * landMask * (1.0 - ice * 0.7);
    }
    s.gloss = (1.0 - landMask) * (1.0 - ice);

    if (cover <= 0.0) return s;
    const float2 field = CloudField(n, seed, detail, cover);
    const float cloud = field.x;
    float3 t1, t2;
    Tangents(n, t1, t2);
    const float3 sunAcross = sunBody - n * dot(sunBody, n);
    // Las nubes flotan sobre el suelo: su sombra cae desplazada, lejos del sol.
    if (dot(sunBody, sunBody) > 0.5) {
        const float shadow = CloudDensity(normalize(n - sunAcross * 0.010), seed, min(detail, 6.0), cover);
        s.albedo *= 1.0 - 0.6 * shadow * (1.0 - cloud);
        s.gloss *= 1.0 - shadow;
    }
    // Las nubes tienen relieve: lo que mira al sol se ilumina y lo que no, se queda en penumbra.
    const float e = clamp(texel * 2.0, 6e-4, 0.01);
    const float lod = min(detail, 7.0);
    const float2 f1 = CloudField(normalize(n + t1 * e), seed, lod, cover);
    const float2 f2 = CloudField(normalize(n + t2 * e), seed, lod, cover);
    // La altura de las cimas da los bultos de dentro; la densidad, el escalon del borde.
    const float3 tops = (t1 * (f1.y - field.y) + t2 * (f2.y - field.y)) / e * 0.006 + (t1 * (f1.x - cloud) + t2 * (f2.x - cloud)) / e * 0.004;
    // Las nubes finas dejan ver el suelo y son grises; las espesas, blancas, con la base en sombra.
    const float3 white = lerp(float3(0.58, 0.61, 0.66), float3(0.80, 0.81, 0.83), smoothstep(0.15, 0.95, cloud)) * (0.84 + 0.16 * saturate(field.y * 2.0 + 0.5));
    s.albedo = lerp(s.albedo, white, cloud * 0.92);
    s.slope = lerp(s.slope, tops, cloud);
    s.gloss *= 1.0 - cloud;
    return s;
}

float3 GasGiant(float3 n, float seed, float detail, float3 a, float3 b, float3 c, float4 shape) {
    // Los vientos corren a lo largo de los paralelos: el ruido se estira en longitud y se tuerce con
    // un flujo que cambia de sentido de una banda a la siguiente.
    const float shear = sin(n.y * 15.0 * shape.w + seed) * 0.6;
    const float cs = cos(shear), sn = sin(shear);
    const float3 flow = float3(n.x * cs - n.z * sn, n.y, n.x * sn + n.z * cs);
    const float eddies = Fbm(flow * float3(2.5, 11.0, 2.5) + seed * 1.7 + float3(uTime * 0.002, 0, 0), detail, 0.58);
    const float turbulence = Fbm(flow * float3(3.0, 8.0, 3.0) + seed + eddies * 0.8, detail);

    // Una gran tormenta oval, en una latitud y una longitud que salen de la semilla. Deriva despacio.
    const int key = int(seed * 97.0);
    const float stormLatitude = (Hash(int3(key, 1, 2)) - 0.5) * 1.1;
    const float longitude = atan2(n.z, n.x);
    float apart = longitude - (Hash(int3(key, 5, 9)) * 6.2831853 + uTime * 0.003);
    apart = apart - 6.2831853 * floor((apart + 3.1415927) / 6.2831853);
    const float2 offset = float2(apart / 0.20, (n.y - stormLatitude) / 0.07);
    const float reach = dot(offset, offset);
    const float storm = exp(-reach * 1.6);

    // Las bandas se tuercen alrededor de la tormenta, como en un remolino.
    const float latitude = n.y + turbulence * (0.045 + 0.10 * shape.z) + storm * 0.035 * sin(atan2(offset.y, offset.x) * 2.0 + uTime * 0.02);
    // Bandas de anchura desigual: la fase del seno avanza con un ruido de la latitud.
    const float uneven = Fbm(float3(latitude * 6.0 * shape.w, seed, 3.0), 3.0) * 2.2;
    const float bands = 0.5 + 0.5 * sin(latitude * 15.0 * shape.w + seed + uneven);
    const float fine = 0.5 + 0.5 * sin(latitude * 41.0 * shape.w + seed * 3.0 + uneven * 2.0);
    float3 color = lerp(a, b, smoothstep(0.15, 0.85, bands));
    color = lerp(color, c, smoothstep(0.55, 0.95, fine) * 0.45);
    // Estrias finas dentro de cada banda.
    const float striae = 0.5 + 0.5 * sin(latitude * 113.0 * shape.w + seed * 5.0 + uneven * 3.0);
    color *= 0.93 + 0.12 * striae;
    // En el borde entre bandas, donde el viento cambia, se enroscan remolinos claros y oscuros.
    const float edgeOfBand = 1.0 - abs(bands * 2.0 - 1.0);
    color *= 1.0 + eddies * (0.40 + 0.85 * edgeOfBand);

    // Ovalos blancos: tormentas pequenas en varias latitudes.
    float ovals = 0.0;
    [unroll] for (int k = 0; k < 4; ++k) {
        const float lat = (Hash(int3(key, 11 + k, 3)) - 0.5) * 1.4;
        float dl = longitude - (Hash(int3(key, 21 + k, 7)) * 6.2831853 + uTime * (0.002 + 0.002 * k));
        dl = dl - 6.2831853 * floor((dl + 3.1415927) / 6.2831853);
        const float2 o = float2(dl / 0.07, (n.y - lat) / 0.03);
        ovals = max(ovals, exp(-dot(o, o) * 1.8));
    }
    color = lerp(color, lerp(c, float3(0.95, 0.93, 0.90), 0.6), ovals * 0.75);

    color = lerp(color, a * float3(1.25, 0.72, 0.55), storm * 0.7);                       // el ojo, mas oscuro y rojizo
    color += c * exp(-(sqrt(reach) - 1.0) * (sqrt(reach) - 1.0) * 6.0) * 0.18;            // y un anillo claro
    return color * (0.92 + turbulence * 0.30);
}

Surface Lava(float3 n, float seed, float detail, float3 crust, float3 rock, float3 glow) {
    const float3 p = n * 3.0 + seed;
    const float shape = Fbm(p, detail);
    const float cracks = 1.0 - abs(Fbm(p * 1.4 + 23.0, detail)) * 3.2;

    Surface s = MakeSurface(lerp(crust, rock, smoothstep(-0.25, 0.3, shape)));
    s.emission = glow * smoothstep(0.55, 0.96, cracks) * (0.9 + 0.1 * sin(uTime * 0.6 + shape * 9.0));
    return s;
}

// Perfil de un crater segun d = distancia al centro / radio, con la altura en radios del crater.
// Cuenco parabolico, con fondo plano y pico central en los grandes; un borde levantado que cae
// deprisa hacia dentro y despacio hacia fuera, donde se extiende el manto de eyecta.
float CraterProfile(float d, float big) {
    const float flatFloor = 0.45 * big;
    const float x = max(d, flatFloor);
    float h = d < 1.0 ? (x * x - 1.0) * 0.42 : 0.0;
    const float rim = 0.09;
    h += d < 1.0 ? rim * exp(-(1.0 - d) * (1.0 - d) / 0.012) : rim * pow(d, -3.2) * (1.0 - smoothstep(1.6, 2.4, d));
    h += big * 0.10 * exp(-d * d / 0.018);  // pico central
    return h;
}

// Una capa de crateres: cada celda de una rejilla puede tener uno, con su centro, tamano y edad al
// azar. Sobre la esfera, la rejilla 3D da circulos de todos los tamanos. Basta con mirar las ocho
// celdas mas cercanas porque ningun crater, con su eyecta, pasa de media celda.
// Un crater nuevo borra lo que habia debajo: dentro de su borde, el relieve viejo casi desaparece.
// Devuelve la altura y la pendiente en el espacio del cuerpo, y en albedo lo claro de lo reciente.
void CraterLayer(float3 n, float scale, float3 offset, float density, float strength, inout float height, inout float3 slope, inout float fresh) {
    const float3 p = n * scale + offset;
    const int3 start = int3(floor(p - 0.5));
    [loop] for (int k = 0; k < 8; ++k) {
        const int3 c = start + int3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
        if (Hash(c) > density) continue;
        const float3 center = float3(c) + 0.2 + 0.6 * float3(Hash(c + int3(17, 3, 5)), Hash(c + int3(31, 7, 11)), Hash(c + int3(59, 13, 2)));
        // Hay muchos mas pequenos que grandes, y ninguno es un circulo perfecto.
        const float size = Hash(c + int3(83, 29, 41));
        const float radius = 0.06 + 0.15 * size * sqrt(size);
        const float3 toward = p - center;
        const float r = length(toward);
        const float d = r / radius * (1.0 + 0.09 * Noise(toward / max(r, 1e-5) * 2.5 + float3(c) * 0.37));
        if (d > 2.4) continue;
        const float age = Hash(c + int3(5, 61, 23));            // 0 recien hecho, 1 muy gastado
        const float depth = strength * lerp(1.0, 0.35, age);     // la erosion lo aplana
        const float big = saturate((radius - 0.12) / 0.08);
        const float profile = CraterProfile(d, big) * depth;
        const float change = (CraterProfile(d + 0.02, big) - CraterProfile(d - 0.02, big)) / 0.04 * depth;
        const float3 along = toward / max(r, 1e-5);
        const float inside = 1.0 - smoothstep(0.85, 1.05, d);
        height = lerp(height + profile * radius / scale, height * 0.25 + profile * radius / scale, inside);
        slope = lerp(slope + along * change, slope * 0.25 + along * change, inside);
        fresh = max(fresh, (1.0 - age) * (1.0 - age) * (1.0 - smoothstep(0.6, 2.0, d)));
    }
}

// Relieve de un mundo de roca o hielo: colinas, cordilleras de crestas afiladas y fracturas largas y
// finas, como las de la corteza helada de Enceladus. Devuelve la altura y lo que hay de fractura.
float2 RockRelief(float3 n, float seed, float lod, float icy) {
    const float hills = Fbm(n * 5.1 + seed * 1.7 + 9.0, lod, 0.5);
    float ridges = 1.0 - abs(Fbm(n * 2.6 + seed * 0.9 + 21.0, lod, 0.5));
    ridges = ridges * ridges * ridges;
    const float bend = Fbm(n * 1.3 + seed * 0.4 + 3.0, 3.0);
    const float fracture = pow(saturate(1.0 - abs(Fbm(n * 1.9 + seed * 2.3 + 5.0 + bend * 0.6, min(lod, 5.0), 0.42)) * 1.0), 22.0);
    // La corteza helada se agrieta mucho mas que la roca.
    const float cracked = fracture * (0.2 + 0.8 * icy);
    return float2(hills * 0.6 + ridges * 0.5 + cracked * 0.6, cracked);
}

// Mundos sin aire: roca, polvo, mares de basalto y crateres de todos los tamanos, sombreados por su
// relieve. El color sale de los elementos (dark, light, basalt).
Surface Rock(float3 n, float seed, float detail, float3 dark, float3 light, float3 basalt, float4 shape, float texel) {
    const float3 p = n * 3.0 + seed;
    const float height = Fbm(p, detail);
    const float grain = Fbm(p * 2.1 + 31.0, detail);
    const float contrast = 0.35 * shape.w;
    float3 ground = lerp(dark, light, smoothstep(-contrast, contrast, height)) * (0.86 + grain * 0.35);

    // Mares: grandes llanuras bajas de basalto, lisas y oscuras, con pocos crateres encima.
    const float maria = smoothstep(0.06, 0.24, -Fbm(n * 1.6 + seed * 1.7, min(detail, 4.0)));
    ground = lerp(ground, basalt * (0.90 + grain * 0.2), maria * 0.85);

    // Relieve de fondo: colinas, cordilleras y fracturas, con la pendiente sacada de dos puntos vecinos.
    float3 t1, t2;
    Tangents(n, t1, t2);
    const float e = clamp(texel, 2e-4, 0.01);
    const float lod = clamp(detail - 1.0, 1.0, 7.0);
    const float icy = saturate(shape.y);
    const float2 r0 = RockRelief(n, seed, lod, icy);
    const float hills = 0.010 * shape.w * (1.0 - maria * 0.7);
    float3 slope = (t1 * (RockRelief(normalize(n + t1 * e), seed, lod, icy).x - r0.x) +
                    t2 * (RockRelief(normalize(n + t2 * e), seed, lod, icy).x - r0.x)) / e * hills;
    float relief = r0.x * hills;
    // Las crestas altas son roca desnuda, mas clara; las fracturas, algo mas oscuras.
    ground *= 0.92 + saturate(r0.x - 0.6) * 0.25;
    ground = lerp(ground, ground * 0.7, r0.y * 0.5);

    // Crateres en cuatro escalas, de los viejos y grandes a los pequenos y recientes, que se montan
    // encima. Las escalas finas solo aparecen de cerca, cuando caben en pantalla.
    float fresh = 0.0;
    const float density = shape.x * (1.0 - maria * 0.75);
    if (density > 0.0) {
        CraterLayer(n, 3.0, seed + float3(1.3, 7.1, 4.2), density * 0.7, 1.0, relief, slope, fresh);
        CraterLayer(n, 7.0, seed * 2.0 + float3(5.0, 2.0, 9.0), density * 0.9, 1.0, relief, slope, fresh);
        if (detail > 3.0) {
            float h = relief, f = fresh;
            float3 g = slope;
            CraterLayer(n, 17.0, seed * 3.0 + float3(8.0, 1.0, 3.0), density, 1.0, h, g, f);
            const float fade = saturate(detail - 3.0);
            relief = lerp(relief, h, fade);
            slope = lerp(slope, g, fade);
            fresh = lerp(fresh, f, fade);
        }
        if (detail > 5.0) {
            float h = relief, f = fresh;
            float3 g = slope;
            CraterLayer(n, 41.0, seed * 5.0 + float3(2.0, 6.0, 1.0), density, 1.0, h, g, f);
            const float fade = saturate(detail - 5.0);
            slope = lerp(slope, g, fade);
            fresh = lerp(fresh, f, fade);
        }
    }
    // Lo reciente es mas claro: la roca molida aun no se ha oscurecido al sol.
    ground *= 1.0 + fresh * 0.30;
    slope *= 1.4;  // las paredes de un crater se ven mejor algo exageradas

    // Hielo: casi blanco, con las fracturas teñidas de azul, donde aflora hielo limpio.
    const float ice = IceMask(n, height, shape.y);
    float3 frost = float3(0.86, 0.89, 0.92) * (0.88 + grain * 0.18);
    frost = lerp(frost, float3(0.52, 0.70, 0.78), saturate(r0.y * 1.3));
    Surface s = MakeSurface(lerp(ground, frost, ice));
    s.slope = slope * (1.0 - ice * 0.35);
    return s;
}

// Plasma: granos que hierven, filamentos oscuros, zonas activas y manchas. El brillo se reparte en
// un rango amplio para que el detalle no se pierda en blanco cuando la estrella llena la pantalla.
float3 StarSurface(float3 n, float seed, float detail, float mu, float3 spot, float3 body, float3 hot) {
    const float t = uTime;
    const float3 flow = float3(t * 0.012, t * -0.008, t * 0.01);

    const float currents = Fbm(n * 3.0 - flow + seed * 2.0, min(detail, 5.0));
    const float warp = Fbm(n * 7.0 + flow * 1.5 + seed, min(detail, 3.0));

    // Granulacion: celdas claras separadas por lineas oscuras. Empieza 2,4 octavas por encima
    // de la base porque esas capas no caben en pantalla hasta acercarse.
    const float grain = max(detail - 2.4, 0.0);
    const float3 boil = n * 22.0 + seed + flow * 5.0 + warp * 0.9;
    const float cells = 1.0 - abs(Fbm(boil, grain, 0.48)) * 2.6;
    const float fine = Fbm(n * 70.0 + flow * 9.0 + warp * 1.4 + seed, max(grain - 1.5, 0.0), 0.5);

    float heat = 0.40 + cells * 0.34 + currents * 0.40 + fine * 0.30;

    // Filamentos: hilos frios que cruzan las zonas activas.
    const float strands = 1.0 - abs(Fbm(n * 5.0 + warp * 1.6 - flow * 0.6 + seed * 3.0, min(detail, 5.0))) * 9.0;
    heat -= smoothstep(0.55, 1.0, strands) * 0.32 * smoothstep(-0.05, 0.2, currents);

    // Del plasma frio al caliente. Las estrellas rojas y amarillas tiran a fuego; las azules, no.
    const float blue = saturate((body.b - body.r) * 1.5 + 0.1);
    const float3 cool = body * lerp(float3(0.95, 0.20, 0.03), float3(0.55, 0.60, 1.00), blue);
    const float3 warm = body * lerp(float3(1.05, 0.58, 0.14), float3(0.80, 0.86, 1.00), blue);
    float3 color = heat < 0.5 ? lerp(cool, warm, saturate(heat * 2.0)) : lerp(warm, hot * 1.2, saturate(heat * 2.0 - 1.0));

    // Manchas: nucleo oscuro con un borde algo mas claro.
    const float spots = smoothstep(0.30, 0.46, -currents);
    color = lerp(color, cool * 0.22, spots * 0.88);

    // Zonas activas: fulguraciones que brillan por encima del resto.
    const float flare = smoothstep(0.34, 0.60, currents) * smoothstep(0.62, 0.98, cells);
    color += hot * flare * 0.9;

    return color * (0.46 + 0.62 * pow(max(mu, 0.0), 0.55));  // el borde del disco se oscurece
}

float3 RotateByQuat(float4 q, float3 v) {
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

// El aire iluminado alrededor del disco: una banda que se apaga hacia el espacio y hacia la noche.
// edge es la distancia al centro en radios del cuerpo; dentro del disco cuenta como el borde.
float4 SkyBand(float edge, float2 plane, float3 u, float3 v, float4 viewToBody, float3 sun, float4 atmosphere, float seed, float thickness) {
    const float2 toward = plane / max(length(plane), 1e-5);
    const float3 outward = normalize(u * toward.x + v * toward.y);
    const float3 inBody = RotateByQuat(viewToBody, outward);
    const float wobble = 0.72 + 0.55 * Fbm(inBody * 4.0 + seed + float3(uTime * 0.05, 0, 0), 3.0);  // nunca un aro perfecto
    // El aire se ve de canto como un filo fino que se apaga deprisa hacia el espacio (la densidad cae
    // en exponencial con la altura). En el lado de noche no hay nada; junto a la linea de sombra la
    // luz del sol ha cruzado tanto aire que llega rojiza.
    const float height = max(edge - 1.0, 0.0) / (kAtmosphere * 0.45 * thickness * wobble);
    if (height >= 1.0) return float4(0, 0, 0, 0);
    const bool sunlit = dot(sun, sun) > 0.5;
    const float facing = sunlit ? dot(outward, sun) : 1.0;
    const float day = sunlit ? smoothstep(-0.20, 0.45, facing) : 1.0;
    const float dusk = sunlit ? smoothstep(-0.20, 0.05, facing) * (1.0 - smoothstep(0.05, 0.35, facing)) : 0.0;
    const float falloff = exp(-height * 5.0) * (1.0 - height);
    const float3 hue = lerp(atmosphere.rgb, float3(1.0, 0.48, 0.20) * max(atmosphere.r, max(atmosphere.g, atmosphere.b)), dusk * 0.7);
    return float4(hue * (0.35 + day * 0.4), saturate(falloff * day * atmosphere.a * 0.75));
}

struct PsOut {
    float4 color : SV_Target0;
    float depth : SV_Depth;
};

PsOut PSSphere(VsOut i) {
    const uint base = i.instance * kStride;
    const float4 sphere = asfloat(gSpheresPS.Load4(base));
    const float4 viewToBody = asfloat(gSpheresPS.Load4(base + 16));
    const float4 light = asfloat(gSpheresPS.Load4(base + 32));   // xyz + pixelRadius
    const float4 params = asfloat(gSpheresPS.Load4(base + 48));  // detalle, semilla, tipo
    const float3 colorA = asfloat(gSpheresPS.Load3(base + 64));
    const float3 colorB = asfloat(gSpheresPS.Load3(base + 80));
    const float3 colorC = asfloat(gSpheresPS.Load3(base + 96));
    const float4 atmosphere = asfloat(gSpheresPS.Load4(base + 112));
    const float4 shape = asfloat(gSpheresPS.Load4(base + 128));  // mar, hielo, nubes, relieve
    const float4 light2 = asfloat(gSpheresPS.Load4(base + 144));  // xyz + intensidad
    const float4 tintA = asfloat(gSpheresPS.Load4(base + 160));   // color de la luz + calor del crater
    const float4 tintB = asfloat(gSpheresPS.Load4(base + 176));
    const float4 scar = asfloat(gSpheresPS.Load4(base + 192));    // centro del crater + radio angular

    const float distance = length(sphere.xyz);
    const float3 w = sphere.xyz / distance;
    float3 u, v;
    MakeBasis(w, u, v);

    // Camara en (0, 0, -D), cuadro en z = 0. Distancia del centro al rayo sin restas grandes.
    const float D = distance / sphere.w;
    const float s2 = dot(i.plane, i.plane);
    const float L = sqrt(s2 + D * D);
    const float offset2 = s2 * D * D / (L * L);
    float edge = sqrt(offset2);  // 1 = el borde del cuerpo
    float coverage = saturate((1.0 - edge) * light.w + 0.5);

    const float3 ray = float3(i.plane, D) / L;
    const float h = sqrt(max(1.0 - offset2, 0.0));
    const float3 hit = normalize(float3(i.plane, 0.0) - ray * (s2 / L + h));

    float3 normal = u * hit.x + v * hit.y + w * hit.z;
    const float3 rayView = u * ray.x + v * ray.y + w * ray.z;
    float3 n = normalize(RotateByQuat(viewToBody, normal));
    float3 surfacePoint = normal;

    // Achatado por el giro: en el espacio del cuerpo, comprimir Y convierte el elipsoide en
    // una esfera unidad. El cuadro ya cubre el elipsoide porque el radio es el del ecuador.
    const float flattening = params.w;
    if (flattening > 0.001) {
        const float stretch = 1.0 / (1.0 - flattening);
        float3 origin = RotateByQuat(viewToBody, -w * D);
        float3 direction = RotateByQuat(viewToBody, rayView);
        origin.y *= stretch;
        direction.y *= stretch;
        direction = normalize(direction);

        const float along = dot(origin, direction);
        const float3 closest = origin - direction * along;
        const float miss2 = dot(closest, closest);
        edge = sqrt(miss2);
        coverage = saturate((1.0 - edge) * light.w + 0.5);

        const float3 unitHit = normalize(origin + direction * (-along - sqrt(max(1.0 - miss2, 0.0))));
        const float4 bodyToView = float4(-viewToBody.xyz, viewToBody.w);
        n = unitHit;
        normal = normalize(RotateByQuat(bodyToView, float3(unitHit.x, unitHit.y * stretch, unitHit.z)));
        surfacePoint = RotateByQuat(bodyToView, float3(unitHit.x, unitHit.y / stretch, unitHit.z));
    }

    // El aire se mide desde el elipsoide: desde la esfera del ecuador, un planeta achatado dejaba un
    // hueco. Y va tambien debajo del borde suavizado del disco: si no, en esos pixeles semitransparentes
    // no habria nada detras y quedaria una linea oscura de un pixel entre el planeta y su aire.
    // En un gigante el aire visible es una capa fina sobre las nubes: con el grueso de un mundo
    // rocoso parecia que el planeta se habia separado de su atmosfera.
    const float thickness = int(params.z + 0.5) == 1 ? 0.35 : 1.0;
    const float4 sky = atmosphere.a > 0.0 && coverage < 1.0 ? SkyBand(edge, i.plane, u, v, viewToBody, light.xyz, atmosphere, params.y, thickness) : float4(0, 0, 0, 0);
    if (coverage <= 0.0) {
        if (sky.a <= 0.0) discard;
        PsOut air;
        air.color = sky;
        air.depth = 0.0;
        return air;
    }
    const float mu = saturate(dot(normal, -rayView));

    const float detail = params.x, seed = params.y;
    const int kind = int(params.z + 0.5);

    const float texel = 1.0 / max(light.w, 1.0);  // un pixel, en radios
    const float3 sunBody = dot(light.xyz, light.xyz) > 0.5 ? normalize(RotateByQuat(viewToBody, light.xyz)) : float3(0, 0, 0);
    Surface surface = MakeSurface(float3(0, 0, 0));
    switch (kind) {
        case 0: surface = Terrestrial(n, seed, detail, colorA, colorB, colorC, shape, texel, sunBody); break;
        case 1: surface.albedo = GasGiant(n, seed, detail, colorA, colorB, colorC, shape); break;
        case 2: surface = Lava(n, seed, detail, colorA, colorB, colorC); break;
        case 3: surface = Rock(n, seed, detail, colorA, colorB, colorC, shape, texel); break;
        default: surface.emission = StarSurface(n, seed, detail, mu, colorA, colorB, colorC) * uStarBoost; break;
    }

    // El relieve tuerce la normal: solo lo tangente cuenta, y se lleva a la vista con el giro inverso.
    float3 shading = normal;
    if (dot(surface.slope, surface.slope) > 0.0) {
        const float3 tangent = surface.slope - n * dot(surface.slope, n);
        const float4 bodyToView = float4(-viewToBody.xyz, viewToBody.w);
        shading = normalize(normal - RotateByQuat(bodyToView, tangent));
    }

    // Crater: fondo oscuro, borde levantado y, recien hecho, roca fundida que se apaga.
    if (scar.w > 0.0) {
        const float wobble = 1.0 + 0.28 * Fbm(n * 7.0 + seed, 3.0);
        const float angle = acos(clamp(dot(n, scar.xyz), -1.0, 1.0)) / scar.w * wobble;
        const float bowl = 1.0 - smoothstep(0.72, 1.0, angle);
        const float rim = smoothstep(0.78, 1.0, angle) * (1.0 - smoothstep(1.0, 1.35, angle));
        surface.albedo = lerp(surface.albedo, surface.albedo * 0.22, bowl) + surface.albedo * rim * 0.5;
        const float heat = tintA.w;
        if (heat > 0.0) {
            const float cracks = saturate(1.0 - abs(Fbm(n * 11.0 + seed, 4.0)) * 4.0);
            const float3 molten = lerp(float3(1.0, 0.16, 0.03), float3(1.0, 0.78, 0.36), heat);
            surface.emission += molten * heat * (bowl * (1.2 + cracks * 2.5) + rim * 1.5 + (1.0 - smoothstep(1.0, 2.2, angle)) * cracks * 1.6);
        }
    }

    const bool lit = dot(light.xyz, light.xyz) > 0.5;
    const float facing = lit ? dot(normal, light.xyz) : 1.0;
    const float3 toEye = -rayView;
    const bool airless = kind == 3;
    // Con aire, la luz se reparte y la sombra entra suave. Sin aire, el polvo devuelve la luz como la
    // Luna (Lommel-Seeliger): el disco no se oscurece hacia el borde y la sombra corta en seco; el
    // relieve hace el resto.
    // En los dos casos la luz cae con el coseno hacia la linea de sombra: es lo que hace que un
    // planeta se vea como una bola y no como un disco. Y nada llega a saturar en blanco: el mundo
    // mas claro se queda por debajo de 1, con sitio para que su relieve se distinga.
    const float shade = lit ? dot(shading, light.xyz) : 1.0;
    const bool ultra = uDetail >= 2.5 && kind <= 3;
    float diffuse;
    float3 sunlight = tintA.rgb;
    // Aire: grueso optico hacia arriba. Con mas camino, mas se apaga y mas se tiñe la luz; el
    // color del aire es el que se dispersa (azul en un mundo como la Tierra), asi que lo que pasa
    // se vuelve rojizo.
    const float tau = ultra && !airless ? atmosphere.a * (kind == 1 ? 0.55 : 0.45) : 0.0;
    const float3 airHue = atmosphere.rgb / max(max(atmosphere.r, max(atmosphere.g, atmosphere.b)), 1e-3);
    const float3 extinction = 0.35 + 0.65 * airHue;
    float3 sunPass = float3(1, 1, 1);
    if (tau > 0.0 && lit) sunPass = exp(-extinction * tau * min(1.0 / max(facing + 0.08, 0.04), 14.0));
    if (airless) {
        const float lambert = saturate(shade);
        const float lommel = 2.0 * lambert / (lambert + max(dot(shading, toEye), 0.05));
        // En ultra, roca rugosa (Oren-Nayar) en lugar del Lambert liso.
        const float base = ultra && lit ? OrenNayar(shading, light.xyz, toEye, 0.75) : lambert;
        diffuse = lerp(base, lommel, 0.3) * smoothstep(-0.02, 0.06, facing) * 0.9;
    } else if (ultra && lit && kind == 0) {
        // Mundo con aire en ultra: suelo rugoso donde no hay agua; la luz que entra la tiñe el aire
        // (sunPass), asi que el atardecer sale solo, sin pintarlo encima.
        const float rough = OrenNayar(shading, light.xyz, toEye, 0.6);
        const float smooth = saturate(shade);
        const float wrap = saturate((facing + 0.06) / 1.06);
        diffuse = lerp(rough, smooth, surface.gloss) * 0.95 + (wrap - saturate(facing)) * 0.25;
        sunlight *= sunPass;
    } else {
        // El aire lleva algo de luz mas alla de la linea de sombra, y alli la tiñe de atardecer.
        const float wrap = saturate((facing + 0.10) / 1.10);
        diffuse = min(pow(wrap, 1.15) * clamp(0.55 + 0.45 * shade / max(facing, 0.2), 0.0, 1.25), 1.0) * 0.92;
        if (lit) {
            const float dusk = (1.0 - smoothstep(-0.05, 0.38, facing)) * saturate(atmosphere.a * 1.6);
            sunlight *= lerp(float3(1.0, 1.0, 1.0), float3(1.0, 0.60, 0.36), dusk);
        }
        // Un gigante es gas hasta donde alcanza la vista: su borde se oscurece.
        if (kind == 1) diffuse *= 0.58 + 0.42 * pow(mu, 0.6);
        if (tau > 0.0) sunlight *= lerp(float3(1, 1, 1), sunPass, 0.6);
    }
    // Dentro de una nebulosa el gas brilla por todos lados: el lado de noche se tiñe de su color en
    // vez de quedar negro.
    // Tenue: la nebulosa ilumina mucho menos que un sol. Lo justo para adivinar el lado de noche.
    const float3 ambient = 0.012 + uAmbient.rgb * 0.10 * (0.75 + 0.25 * mu);
    float3 color = surface.albedo * (ambient + diffuse * sunlight) + surface.emission;
    if (ultra && surface.gloss > 0.0) {
        // Mar en ultra: refleja el cielo segun Fresnel (mas espejo cuanto mas de canto) y el sol
        // deja un brillo de microfacetas (GGX) que se ensancha con el oleaje.
        const float fresnel = 0.02 + 0.98 * pow(1.0 - mu, 5.0);
        const float day = lit ? smoothstep(-0.1, 0.3, facing) : 1.0;
        const float3 skyColor = atmosphere.rgb * atmosphere.a * 0.55 * day + uAmbient.rgb * 0.15;
        color = lerp(color, skyColor, fresnel * surface.gloss * 0.85);
        if (lit && facing > 0.0) {
            const float3 halfway = normalize(light.xyz + toEye);
            const float a2 = 0.13 * 0.13 * 0.13 * 0.13;
            const float nh = saturate(dot(shading, halfway));
            const float dd = nh * nh * (a2 - 1.0) + 1.0;
            const float ggx = a2 / (3.14159 * dd * dd);
            const float vh = saturate(dot(toEye, halfway));
            const float f = 0.02 + 0.98 * pow(1.0 - vh, 5.0);
            color += tintA.rgb * sunPass * ggx * f * 0.25 * saturate(facing) / max(mu, 0.15) * surface.gloss;
        }
    } else if (surface.gloss > 0.0 && lit && facing > 0.0) {
        // El sol se refleja en el agua: un brillo concentrado donde el angulo es el justo.
        const float3 halfway = normalize(light.xyz + toEye);
        const float glint = pow(saturate(dot(shading, halfway)), 160.0) * 0.9 + pow(saturate(dot(shading, halfway)), 20.0) * 0.05;
        color += tintA.rgb * glint * surface.gloss * smoothstep(0.0, 0.2, facing);
    }

    // Segundo sol: una segunda linea de sombra y su propio color.
    float rimFacing = facing;
    if (light2.w > 0.0) {
        const float facing2 = dot(normal, light2.xyz);
        const float shade2 = dot(shading, light2.xyz);
        const float diffuse2 = airless ? saturate(shade2) * smoothstep(-0.02, 0.06, facing2) * 0.9 : pow(saturate((facing2 + 0.10) / 1.10), 1.15) * 0.95;
        color += surface.albedo * diffuse2 * light2.w * tintB.rgb;
        rimFacing = max(rimFacing, facing2 * light2.w);
    }

    // Perspectiva aerea (ultra): entre el suelo y el ojo hay aire. Apaga el suelo y le suma la luz
    // del sol que el aire desvia hacia nosotros, mucho mas hacia el borde, donde el camino es largo.
    // Es lo que quita el aspecto de bola pintada: los colores se lavan hacia el limbo.
    if (tau > 0.0) {
        const float path = min(1.0 / max(mu, 0.05), 12.0);
        const float3 viewPass = exp(-extinction * tau * path * 0.5);
        const float day = lit ? smoothstep(-0.18, 0.35, facing) : 1.0;
        const float cosSun = lit ? dot(rayView, light.xyz) : 0.0;
        const float phase = 0.75 * (1.0 + cosSun * cosSun);
        const float3 inscatter = airHue * 0.26 * (1.0 - viewPass) * day * phase * lerp(float3(1, 1, 1), sunPass, 0.5) * tintA.rgb;
        color = color * viewPass + inscatter;
    }

    const float rim = 1.0 - mu;
    // El aire visto de canto: mas camino, mas luz dispersada. Solo donde le da el sol.
    color += atmosphere.rgb * atmosphere.a * (rim * rim * 0.32 + rim * 0.05 + rim * rim * rim * rim * rim * 0.38) * saturate(rimFacing * 1.3 + 0.05) * (tau > 0.0 ? 0.25 : 0.55);

    const float3 position = sphere.xyz + surfacePoint * sphere.w;

    PsOut o;
    // El borde del disco se mezcla con su aire, no con el vacio.
    const float alpha = coverage + sky.a * (1.0 - coverage);
    o.color = float4((color * coverage + sky.rgb * sky.a * (1.0 - coverage)) / max(alpha, 1e-4), alpha);
    // Los pixeles casi transparentes del borde no deben tapar lo que se dibuje despues.
    o.depth = coverage >= 0.5 ? uNearZ / max(position.z, uNearZ) : 0.0;
    return o;
}
