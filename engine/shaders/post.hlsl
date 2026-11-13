// post.hlsl - Etapa final de la imagen (DXIL, Shader Model 6.0).
//
//   Escena HDR > bloom > composicion a 8 bits > efectos del juego > interfaz > pantalla.
//
// La curva de tono deja intacto casi todo lo que vale menos de 1 y comprime solo lo que brilla
// mas: los planetas se ven igual que antes y las estrellas pueden saturar sin recortarse.

struct VsOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VsOut VSPost(uint vertexId : SV_VertexID) {
    const float2 pos = float2(vertexId == 1 ? 3.0 : -1.0, vertexId == 2 ? 3.0 : -1.0);
    VsOut o;
    o.position = float4(pos, 0.0, 1.0);
    o.uv = float2(pos.x * 0.5 + 0.5, 0.5 - pos.y * 0.5);
    return o;
}

Texture2D<float4> gSource : register(t0, space2);
SamplerState gLinear : register(s0, space2);
Texture2D<float4> gBloom : register(t1, space2);
SamplerState gLinear2 : register(s1, space2);

cbuffer BloomCB : register(b0, space3) {
    float2 uTexel;       // tamano de un pixel de la fuente
    float uThreshold;    // < 0 = sin umbral
    float uStrength;
};

float3 Prefilter(float3 c) {
    if (uThreshold < 0.0) return c;
    const float brightest = max(c.r, max(c.g, c.b));
    const float soft = saturate((brightest - uThreshold * 0.7) / max(uThreshold * 0.6, 1e-4));
    // Limite: un solo pixel de 1000 no debe parpadear al moverse.
    return min(c, float3(16.0, 16.0, 16.0)) * soft * soft;
}

float4 PSBloomDown(VsOut i) : SV_Target0 {
    // 13 muestras de Jimenez (2014): al reducir no aparecen cuadros ni parpadeos.
    const float2 t = uTexel;
    float3 sum = gSource.SampleLevel(gLinear, i.uv, 0).rgb * 0.125;
    sum += (gSource.SampleLevel(gLinear, i.uv + t * float2(-1, -1), 0).rgb + gSource.SampleLevel(gLinear, i.uv + t * float2(1, -1), 0).rgb +
            gSource.SampleLevel(gLinear, i.uv + t * float2(-1, 1), 0).rgb + gSource.SampleLevel(gLinear, i.uv + t * float2(1, 1), 0).rgb) * 0.125;
    sum += (gSource.SampleLevel(gLinear, i.uv + t * float2(-2, 0), 0).rgb + gSource.SampleLevel(gLinear, i.uv + t * float2(2, 0), 0).rgb +
            gSource.SampleLevel(gLinear, i.uv + t * float2(0, -2), 0).rgb + gSource.SampleLevel(gLinear, i.uv + t * float2(0, 2), 0).rgb) * 0.0625;
    sum += (gSource.SampleLevel(gLinear, i.uv + t * float2(-2, -2), 0).rgb + gSource.SampleLevel(gLinear, i.uv + t * float2(2, -2), 0).rgb +
            gSource.SampleLevel(gLinear, i.uv + t * float2(-2, 2), 0).rgb + gSource.SampleLevel(gLinear, i.uv + t * float2(2, 2), 0).rgb) * 0.03125;
    return float4(Prefilter(sum), 1.0);
}

float4 PSBloomUp(VsOut i) : SV_Target0 {
    const float2 t = uTexel;
    float3 sum = gSource.SampleLevel(gLinear, i.uv, 0).rgb * 4.0;
    sum += (gSource.SampleLevel(gLinear, i.uv + t * float2(-1, 0), 0).rgb + gSource.SampleLevel(gLinear, i.uv + t * float2(1, 0), 0).rgb +
            gSource.SampleLevel(gLinear, i.uv + t * float2(0, -1), 0).rgb + gSource.SampleLevel(gLinear, i.uv + t * float2(0, 1), 0).rgb) * 2.0;
    sum += gSource.SampleLevel(gLinear, i.uv + t * float2(-1, -1), 0).rgb + gSource.SampleLevel(gLinear, i.uv + t * float2(1, -1), 0).rgb +
           gSource.SampleLevel(gLinear, i.uv + t * float2(-1, 1), 0).rgb + gSource.SampleLevel(gLinear, i.uv + t * float2(1, 1), 0).rgb;
    return float4(sum / 16.0 * uStrength, 1.0);
}

float4 PSCopy(VsOut i) : SV_Target0 {
    return gSource.SampleLevel(gLinear, i.uv, 0);
}

// La nube se calcula a media resolucion y con un punto de partida al azar en cada pixel. Al
// ampliarla se mezcla con sus vecinas para que no quede grano ni trama, pero no a traves del
// borde de lo opaco: alli la transparencia cambia de golpe y la vecina no cuenta.
// gBloom lleva aqui la profundidad de la escena a resolucion completa (Z invertida: 0 = cielo).
// Cada texel de la nube se calculo con la profundidad de su centro; solo cuentan los de una
// profundidad parecida a la de este pixel. Sin esto, en el contorno de un planeta los pixeles de
// cielo cogian la nube del planeta (casi sin niebla) y dejaban un borde negro.
float DepthKey(float d) { return d > 0.0 ? -log(d) : 40.0; }

float4 PSNebulaUpsample(VsOut i) : SV_Target0 {
    float w, h;
    gSource.GetDimensions(w, h);
    const float2 size = float2(w, h);
    const float key = DepthKey(gBloom.SampleLevel(gLinear2, i.uv, 0).r);
    const float2 at = i.uv * size - 0.5;
    const int2 base = int2(floor(at));
    float4 sum = float4(0, 0, 0, 0);
    float total = 0.0;
    [unroll] for (int y = -2; y <= 3; ++y) {
        [unroll] for (int x = -2; x <= 3; ++x) {
            const int2 texel = clamp(base + int2(x, y), int2(0, 0), int2(size) - 1);
            const float2 centerUv = (float2(texel) + 0.5) / size;
            const float4 s = gSource.Load(int3(texel, 0));
            const float2 d = float2(texel) - at;
            const float same = DepthKey(gBloom.SampleLevel(gLinear2, centerUv, 0).r) - key;
            // Casi plano en 4 x 4 texeles: asi entra el patron de Bayer completo de la nube.
            const float weight = exp(-dot(d, d) / 9.0) * exp(-min(abs(same) * 6.0, 60.0)) + 1e-12;
            sum += s * weight;
            total += weight;
        }
    }
    return sum / total;
}

cbuffer CompositeCB : register(b0, space3) {
    float uBloomStrength;
    float uExposure;
    float uVignette;
    float uAberration;
    float uGrain;
    float uTime;
    float2 uCompositePad;
};

// Lo oscuro y lo medio quedan intactos (la nebulosa tenue no se hunde en negro); a partir de 0,6
// la luz se comprime poco a poco hacia 1, canal a canal, asi que lo muy brillante se apaga hacia
// el blanco en vez de saturar en un color chillon o quemarse de golpe.
float3 ToneMap(float3 c) {
    const float k = 0.60;
    const float3 over = max(c - k, 0.0);
    return min(c, k) + (1.0 - k) * (1.0 - exp(-over / (1.0 - k)));
}

float Noise(float2 p) {
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

float4 PSComposite(VsOut i) : SV_Target0 {
    const float2 centered = i.uv - 0.5;
    const float corner = dot(centered, centered) * 2.0;

    float3 color;
    if (uAberration > 0.0) {
        // Las lentes separan los colores hacia las esquinas.
        const float2 shift = centered * corner * uAberration * 2.0;
        color.r = gSource.SampleLevel(gLinear, i.uv + shift, 0).r;
        color.g = gSource.SampleLevel(gLinear, i.uv, 0).g;
        color.b = gSource.SampleLevel(gLinear, i.uv - shift, 0).b;
    } else {
        color = gSource.SampleLevel(gLinear, i.uv, 0).rgb;
    }

    if (uBloomStrength > 0.0) color += gBloom.SampleLevel(gLinear2, i.uv, 0).rgb * uBloomStrength;
    color = ToneMap(color * uExposure);
    color *= 1.0 - uVignette * smoothstep(0.1, 1.0, corner);
    if (uGrain > 0.0) color += (Noise(i.position.xy + frac(uTime) * 91.7) - 0.5) * uGrain;
    return float4(color, 1.0);
}
