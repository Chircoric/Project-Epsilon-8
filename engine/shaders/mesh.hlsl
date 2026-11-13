// mesh.hlsl - Mallas opacas con instancias (DXIL, Shader Model 6.0).
//
// Sirve igual para un tornillo y para un asteroide: la instancia trae su posicion respecto a
// la camara y su escala, y comparten el mismo depth buffer con Z invertida.

cbuffer FrameCB : register(b0, space1) {
    float4x4 uProjection;
    float2 uViewport;
    uint uFirstInstance;
    float uFramePad;
};

ByteAddressBuffer gInstances : register(t0, space0);

static const uint kStride = 144;

struct VsIn {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    uint instance : SV_InstanceID;
};

struct VsOut {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float3 viewPosition : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float3 light : TEXCOORD3;
    float4 color : TEXCOORD4;
    nointerpolation float2 shading : TEXCOORD5;  // ambiente, emision
    nointerpolation float3 lightColor : TEXCOORD6;
    nointerpolation float3 light2 : TEXCOORD7;
    nointerpolation float3 light2Color : TEXCOORD8;
    nointerpolation float3 ambientColor : TEXCOORD9;
    float3 metres : TEXCOORD10;                   // posicion en la malla, en metros: ahi se dibuja el dano
    nointerpolation float4 damage : TEXCOORD11;   // dano, semilla, nivel, calor
    // Crateres (MeshCrater), ya en metros: centro y radio; hondura, calor, semilla y si existe.
    nointerpolation float4 crater0 : TEXCOORD12;
    nointerpolation float4 craterInfo0 : TEXCOORD13;
    nointerpolation float4 crater1 : TEXCOORD14;
    nointerpolation float4 craterInfo1 : TEXCOORD15;
    nointerpolation float4 crater2 : TEXCOORD16;
    nointerpolation float4 craterInfo2 : TEXCOORD17;
    nointerpolation float4 crater3 : TEXCOORD18;
    nointerpolation float4 craterInfo3 : TEXCOORD19;
};

// Hundimiento de un crater en un punto: un cuenco hondo y un labio levantado alrededor, como el de
// un impacto de verdad.
float CraterDent(float distance, float radius, float depth) {
    const float q = distance / max(radius, 1e-6);
    if (q >= 1.5) return 0.0;
    if (q < 1.0) {
        const float t = 1.0 - q * q;
        return -radius * depth * 0.55 * t * sqrt(t) + radius * depth * 0.12 * q * q * q * q;
    }
    const float rim = (q - 1.0) / 0.5;
    return radius * depth * 0.12 * (1.0 - rim) * (1.0 - rim);
}

// Lo que SdlGpuBackend empaqueta en los dos huecos libres de la instancia.
float4 DamageOf(float packedDamage, float packedDetail) {
    const float seed = floor(packedDamage * 0.5);
    const float level = floor(packedDetail);
    return float4(packedDamage - 2.0 * seed, seed, level, saturate((packedDetail - level) / 0.9));
}

float3 RotateByQuat(float4 q, float3 v) {
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

VsOut VSMesh(VsIn i) {
    const uint base = (uFirstInstance + i.instance) * kStride;
    const float4 place = asfloat(gInstances.Load4(base));         // posicion, escala
    const float4 rotation = asfloat(gInstances.Load4(base + 16));
    const float4 light = asfloat(gInstances.Load4(base + 32));    // direccion, ambiente
    const float4 color = asfloat(gInstances.Load4(base + 48));
    const float4 extra = asfloat(gInstances.Load4(base + 64));    // emision, estirar X, estirar Y

    float3 local = i.position;
    local.xy *= extra.yz;

    // Crateres: un bloque de cuatro detras de las instancias (SdlGpuBackend::BuildGpuData).
    const float block = asfloat(gInstances.Load(base + 92));
    const uint craters = (uint)asfloat(gInstances.Load(base + 124));
    float4 c[4] = {float4(0, 0, 0, 0), float4(0, 0, 0, 0), float4(0, 0, 0, 0), float4(0, 0, 0, 0)};
    float4 ci[4] = {float4(0, 0, 0, 0), float4(0, 0, 0, 0), float4(0, 0, 0, 0), float4(0, 0, 0, 0)};
    const float3 rest = local;  // sin hundir: el dibujo del dano no se mueve con la abolladura
    if (craters > 0) {
        const uint at = (uint)block * kStride;
        [unroll] for (uint k = 0; k < 4; ++k) if (k < craters) {
            c[k] = asfloat(gInstances.Load4(at + k * 32));
            ci[k] = float4(asfloat(gInstances.Load3(at + k * 32 + 16)), 1.0);
            local += i.normal * CraterDent(distance(rest, c[k].xyz), c[k].w, ci[k].x);
            c[k] *= place.w * 1000.0;  // a metros, como o.metres
        }
    }
    const float3 viewPosition = place.xyz + RotateByQuat(rotation, local * place.w);

    // La normal sigue la pendiente del hundimiento: sin esto la abolladura no se veria con la luz.
    float3 normal = i.normal;
    if (craters > 0) {
        const float scale = place.w * 1000.0;
        [unroll] for (uint k = 0; k < 4; ++k) if (k < craters) {
            const float3 radial = rest - c[k].xyz / scale;
            const float3 along = radial - i.normal * dot(radial, i.normal);
            const float d = length(radial), r = c[k].w / scale, e = r * 0.02;
            const float slope = (CraterDent(d + e, r, ci[k].x) - CraterDent(max(d - e, 0.0), r, ci[k].x)) / (2.0 * e);
            if (length(along) > 1e-6) normal -= normalize(along) * slope;
        }
        normal = normalize(normal);
    }

    VsOut o;
    o.crater0 = c[0];
    o.craterInfo0 = ci[0];
    o.crater1 = c[1];
    o.craterInfo1 = ci[1];
    o.crater2 = c[2];
    o.craterInfo2 = ci[2];
    o.crater3 = c[3];
    o.craterInfo3 = ci[3];
    o.position = mul(uProjection, float4(viewPosition, 1.0));
    o.normal = RotateByQuat(rotation, normal);
    o.viewPosition = viewPosition;
    o.uv = i.uv;
    o.light = light.xyz;
    o.color = color;
    o.shading = float2(light.w, extra.x);
    o.lightColor = asfloat(gInstances.Load3(base + 80));
    o.light2 = asfloat(gInstances.Load3(base + 96));
    o.light2Color = asfloat(gInstances.Load3(base + 112));
    o.ambientColor = asfloat(gInstances.Load3(base + 128));
    o.metres = rest * place.w * 1000.0;
    o.damage = DamageOf(extra.w, asfloat(gInstances.Load(base + 108)));
    return o;
}

// Ruido de valor en 3D, para que el dano no repita dibujo.
float Hash3(float3 p) {
    p = frac(p * 0.3183099 + 0.1);
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float ValueNoise(float3 p) {
    const float3 i = floor(p);
    const float3 f = frac(p);
    const float3 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(lerp(Hash3(i), Hash3(i + float3(1, 0, 0)), u.x), lerp(Hash3(i + float3(0, 1, 0)), Hash3(i + float3(1, 1, 0)), u.x), u.y),
                lerp(lerp(Hash3(i + float3(0, 0, 1)), Hash3(i + float3(1, 0, 1)), u.x), lerp(Hash3(i + float3(0, 1, 1)), Hash3(i + float3(1, 1, 1)), u.x), u.y), u.z);
}

float Fbm(float3 p) {
    float sum = 0.0, amplitude = 0.5;
    [unroll] for (int k = 0; k < 4; ++k) {
        sum += ValueNoise(p) * amplitude;
        p = p * 2.03 + 11.7;
        amplitude *= 0.5;
    }
    return sum / 0.9375;
}

// Casco danado: manchas de quemadura que crecen con el dano; con mas detalle, grietas y bordes al
// rojo que se enfrian con el tiempo, y por ultimo agujeros. albedo se oscurece; devuelve lo que brilla.
float3 HullDamage(float3 metres, float4 damage, inout float3 albedo) {
    const float amount = damage.x;
    const float3 p = metres * 0.022 + damage.y * 13.37;
    const float n = Fbm(p);
    const float reach = amount * 0.95;
    const float burn = smoothstep(n - 0.07, n + 0.07, reach);
    albedo = lerp(albedo, albedo * 0.12 + float3(0.020, 0.016, 0.012), burn);

    float3 glow = 0.0;
    const float3 hot = float3(1.6, 0.55, 0.12);
    if (damage.z >= 2.0) {
        // Grietas: las crestas de otro ruido, solo dentro de lo quemado.
        const float ridge = 1.0 - abs(2.0 * Fbm(p * 2.7 + 5.1) - 1.0);
        const float crack = smoothstep(0.90, 0.97, ridge) * burn;
        // El borde de la quemadura, mientras esta reciente.
        const float rim = (1.0 - smoothstep(0.0, 0.035, abs(n - reach))) * step(0.02, amount);
        glow += hot * (crack * (0.35 + 2.5 * damage.w) + rim * 2.2 * damage.w);
    }
    if (damage.z >= 3.0 && amount > 0.45) {
        // Agujeros: lo peor se ha ido. Su borde sigue fundido un rato.
        const float hole = reach - 0.5;
        if (n < hole) discard;
        const float lip = 1.0 - smoothstep(0.0, 0.04, n - hole);
        glow += hot * lip * (0.6 + 2.0 * damage.w);
        albedo *= 1.0 - 0.7 * lip;
    }
    return glow;
}

// Un crater visto desde fuera: borde mellado, quemadura en rayos, brasas y, si es hondo, un agujero
// con el labio fundido. Desde dentro (cara de atras vista por el agujero) solo cuenta lo cerca que esta.
void CraterShade(float3 m, float4 c, float4 info, bool front, inout float3 albedo, inout float3 glow, inout float inside) {
    if (info.w <= 0.0) return;
    const float3 offset = m - c.xyz;
    const float d = length(offset) / c.w;
    if (d > 1.7) return;
    const float jag = (Fbm(m * (3.0 / c.w) + info.z * 7.1) - 0.5) * 0.55;
    const float q = d + jag;
    const float hole = info.x > 0.4 ? (info.x - 0.28) * 0.85 : -1.0;
    const float3 hot = float3(1.8, 0.6, 0.12);
    if (!front) {
        inside = max(inside, saturate(1.2 - d) * (0.3 + info.y));
        return;
    }
    if (hole > 0.0 && q < hole) discard;

    const float streak = Fbm(normalize(offset + 1e-4) * 5.0 + info.z * 3.3);
    const float scorch = 1.0 - smoothstep(0.35, 1.55 + streak * 0.25, q);
    albedo *= 1.0 - 0.9 * scorch * (0.65 + 0.35 * streak);
    // El cuenco queda en su propia sombra, con una grieta oscura justo dentro del labio y el labio
    // levantado mas claro: asi se lee como crater aunque la luz venga de frente.
    albedo *= lerp(0.08, 1.0, smoothstep(0.1, 0.95, q));
    const float crack = exp(-pow((q - 0.93) / 0.035, 2.0));
    const float ridge = exp(-pow((q - 1.1) / 0.09, 2.0));
    albedo *= (1.0 - 0.75 * crack) * (1.0 + 1.4 * ridge);
    const float lip = hole > 0.0 ? 1.0 - smoothstep(0.0, 0.14, q - hole) : 0.0;
    glow += hot * lip * (0.25 + 3.0 * info.y);
    if (hole <= 0.0) glow += hot * (1.0 - smoothstep(0.0, 0.6, q)) * info.y * 1.2;  // golpe que no perfora
    const float ember = step(0.84, Fbm(m * (9.0 / c.w) + 3.3)) * scorch;
    glow += hot * ember * (0.12 + 0.9 * info.y);
}

Texture2D<float4> gTexture : register(t0, space2);
SamplerState gSampler : register(s0, space2);
Texture2D<float4> gNormalMap : register(t1, space2);  // relieve, de tipo OpenGL
SamplerState gNormalSampler : register(s1, space2);
Texture2D<float4> gOrmMap : register(t2, space2);     // R oclusion, G rugosidad, B metal
SamplerState gOrmSampler : register(s2, space2);
Texture2D<float> gShadowMap : register(t3, space2);   // profundidad vista desde la estrella
SamplerState gShadowSampler : register(s3, space2);

static const uint kMaxPointLights = 64;  // RenderTypes.h

cbuffer MeshCB : register(b0, space3) {
    float4x4 uShadow;    // de vista al mapa de sombras: xy de -1 a 1, z mas alto cuanto mas cerca de la luz
    float4 uShadowInfo;  // x = hay sombras, y = un texel (uv), z = margen de profundidad, w = desplazamiento por la normal (km)
    float4 uLampInfo;    // x = cuantas lamparas hay
    float4 uLampPosition[kMaxPointLights];  // xyz en vista (km), w = alcance (km)
    float4 uLampColor[kMaxPointLights];     // rgb = color por intensidad
};

// Sombra de la nave sobre si misma: el mapa guarda lo que ve la estrella. Doce muestras en disco,
// giradas al azar en cada pixel, dan un borde de penumbra suave en lugar de escalones.
float ShadowAt(float3 viewPosition, float3 geometricNormal, float2 pixel) {
    if (uShadowInfo.x < 0.5) return 1.0;
    const float4 s = mul(uShadow, float4(viewPosition + geometricNormal * uShadowInfo.w, 1.0));
    const float2 uv = float2(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5);
    if (uv.x <= 0.0 || uv.y <= 0.0 || uv.x >= 1.0 || uv.y >= 1.0 || s.z <= 0.0 || s.z >= 1.0) return 1.0;
    static const float2 kDisk[12] = {
        float2(-0.326, -0.406), float2(-0.840, -0.074), float2(-0.696, 0.457), float2(-0.203, 0.621),
        float2(0.962, -0.195), float2(0.473, -0.480), float2(0.519, 0.767), float2(0.185, -0.893),
        float2(0.507, 0.064), float2(0.896, 0.412), float2(-0.322, -0.933), float2(-0.792, -0.598)};
    const float angle = frac(sin(dot(pixel, float2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
    const float c = cos(angle), sn = sin(angle);
    float lit = 0.0;
    [unroll] for (int k = 0; k < 12; ++k) {
        const float2 d = float2(kDisk[k].x * c - kDisk[k].y * sn, kDisk[k].x * sn + kDisk[k].y * c);
        const float depth = gShadowMap.SampleLevel(gShadowSampler, uv + d * uShadowInfo.y * 2.2, 0);
        lit += depth <= s.z + uShadowInfo.z ? 1.0 : 0.0;
    }
    return lit / 12.0;
}

// Oren-Nayar: un material mate y rugoso (la pintura ceramica blanca de las naves de la NASA) no
// se apaga hacia el borde como dice Lambert y devuelve algo de luz hacia el sol. Es el factor que
// multiplica al coseno.
float OrenNayar(float3 n, float3 l, float3 v, float sigma) {
    const float cosI = saturate(dot(n, l)), cosR = saturate(dot(n, v));
    const float s2 = sigma * sigma;
    const float A = 1.0 - 0.5 * s2 / (s2 + 0.33);
    const float B = 0.45 * s2 / (s2 + 0.09);
    const float3 li = l - n * cosI, vr = v - n * cosR;
    const float cosPhi = dot(li, vr) * rsqrt(max(dot(li, li) * dot(vr, vr), 1e-8));
    const float thetaI = acos(cosI), thetaR = acos(cosR);
    return A + B * max(cosPhi, 0.0) * sin(max(thetaI, thetaR)) * tan(min(min(thetaI, thetaR), 1.4));
}

void PSShadow() {}

// Tuerce la normal con el mapa de relieve. La malla no trae tangentes: salen de como cambian la
// posicion y las UV de un pixel al vecino, asi que vale para cualquier modelo.
float3 BumpNormal(float3 n, float3 dp1, float3 dp2, float2 duv1, float2 duv2, float3 bump) {
    const float3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
    const float3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    const float3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    const float longest = max(dot(t, t), dot(b, b));
    if (!(longest > 0.0)) return n;  // sin UV (tapas, mallas sin textura): no hay hacia donde torcer
    const float inv = rsqrt(longest);
    // En glTF la V crece hacia abajo y el verde del mapa apunta hacia arriba: por eso el signo.
    return normalize(t * (bump.x * inv) - b * (bump.y * inv) + n * bump.z);
}

// Luz de una estrella sobre un material: difusa de Lambert y brillo de microfacetas (GGX), con el
// reflejo del color del metal. Sin dividir por pi: la luz ya llega en esa escala.
float3 Shine(float3 n, float3 l, float3 toEye, float3 albedo, float rough, float metal, float3 lightColor) {
    const float nl = saturate(dot(n, l));
    if (nl <= 0.0) return 0.0;
    const float3 h = normalize(l + toEye);
    const float nh = saturate(dot(n, h)), nv = max(dot(n, toEye), 0.02), vh = saturate(dot(toEye, h));
    const float a = rough * rough, a2 = a * a;
    const float d = nh * nh * (a2 - 1.0) + 1.0;
    const float distribution = a2 / (d * d);
    const float k = (rough + 1.0) * (rough + 1.0) * 0.125;
    const float visible = 1.0 / ((nl * (1.0 - k) + k) * (nv * (1.0 - k) + k));
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metal);
    const float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vh, 5.0);
    const float3 specular = min(fresnel * (distribution * visible * 0.25), 4.0);
    // Lo rugoso (rugosidad > 0,4) se reparte como un mate de verdad.
    const float matte = OrenNayar(n, l, toEye, saturate((rough - 0.35) / 0.5) * 0.65);
    return (albedo * (1.0 - metal) * (1.0 - fresnel) * matte + specular) * nl * lightColor;
}

float4 PSMesh(VsOut i, bool front : SV_IsFrontFace) : SV_Target0 {
    const float4 texel = gTexture.Sample(gSampler, i.uv);
    const float3 bump = gNormalMap.Sample(gNormalSampler, i.uv).xyz * 2.0 - 1.0;
    const float3 orm = gOrmMap.Sample(gOrmSampler, i.uv).rgb;
    const float3 dp1 = ddx(i.viewPosition), dp2 = ddy(i.viewPosition);
    const float2 duv1 = ddx(i.uv), duv2 = ddy(i.uv);
    if (texel.a < 0.5) discard;  // recorte: permite modelos 2D con silueta

    float3 n = BumpNormal(normalize(i.normal), dp1, dp2, duv1, duv2, bump);
    const float3 toEye = normalize(-i.viewPosition);
    float3 albedo = texel.rgb * i.color.rgb;
    // Ninguna chapa envejece igual que la de al lado: manchas anchas, en metros de la pieza, que
    // rompen la repeticion de la textura vista desde lejos.
    const float wear = Fbm(i.metres * 0.011 + 3.7);
    const float streaks = ValueNoise(i.metres * float3(0.21, 0.21, 0.018) + 9.1);
    albedo *= 0.84 + 0.24 * wear + 0.08 * (streaks - 0.5);
    float rough = clamp(orm.g + (wear - 0.5) * 0.22, 0.10, 1.0);
    const float metal = orm.b;
    float occlusion = orm.r;
    const float3 pure = texel.rgb * i.color.rgb;  // lo que brilla por si mismo no se ensucia
    float3 damageGlow = 0.0;
    if (i.damage.z >= 1.0 && i.damage.x > 0.0) damageGlow = HullDamage(i.metres, i.damage, albedo);
    float inside = 0.0;
    CraterShade(i.metres, i.crater0, i.craterInfo0, front, albedo, damageGlow, inside);
    CraterShade(i.metres, i.crater1, i.craterInfo1, front, albedo, damageGlow, inside);
    CraterShade(i.metres, i.crater2, i.craterInfo2, front, albedo, damageGlow, inside);
    CraterShade(i.metres, i.crater3, i.craterInfo3, front, albedo, damageGlow, inside);
    if (!front) {
        // El interior, visto por un agujero: casi a oscuras, con el resplandor del metal fundido.
        n = -n;
        albedo *= 0.15;
        damageGlow += float3(1.8, 0.6, 0.12) * inside * 0.5;
    }

    // En el vacio no hay luz difusa ambiente: el lado en sombra casi no se ve. Lo poco que llega
    // (la nebulosa, el resto del cielo) se cuela peor en juntas y rincones, y un metal pulido la
    // devuelve como reflejo, mas fuerte cuanto mas de refilon se mira.
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metal);
    const float grazing = pow(1.0 - saturate(dot(n, toEye)), 5.0);
    const float3 mirror = f0 + (max(1.0 - rough, f0) - f0) * grazing;
    // Sombra arrojada: lo que la estrella no ve, no recibe su luz directa.
    const float shadow = front ? ShadowAt(i.viewPosition, normalize(i.normal), i.position.xy) : 0.0;
    const float towardSun = dot(n, i.light);
    // El cielo de la nebulosa no es igual por todos lados: la niebla brilla mas hacia la estrella
    // (dispersa su luz hacia delante), asi que la cara que la mira recibe mas luz de ambiente.
    const float3 sky = i.ambientColor * i.shading.x * occlusion * (0.72 + 0.38 * towardSun);
    float3 lit = sky * (albedo * (1.0 - metal) + mirror * (1.0 - 0.5 * rough));
    // La oclusion tambien apaga algo la luz directa en las juntas.
    const float crevice = 0.55 + 0.45 * occlusion;
    lit += Shine(n, i.light, toEye, albedo, rough, metal, i.lightColor) * crevice * shadow;

    // Rebote: las chapas blancas iluminadas devuelven parte de la luz (su albedo ronda 0,8) a lo
    // que tienen alrededor. Llega sobre todo a las caras de espaldas al sol y a lo que queda a la
    // sombra de otra pieza; por eso la sombra no es negra, sino del color de la luz, mas tenue.
    const float bounceIn = 0.13 * saturate(0.55 - 0.45 * towardSun) + 0.12 * (1.0 - shadow) * saturate(towardSun + 0.2);
    lit += albedo * (1.0 - metal) * i.lightColor * bounceIn * occlusion;

    // Segunda luz: otra estrella o el reflejo de un planeta. Un planeta cercano es una lampara
    // enorme: ademas de su brillo directo, baña de luz suave todo el lado que lo mira.
    if (dot(i.light2Color, i.light2Color) > 0.0) {
        lit += Shine(n, i.light2, toEye, albedo, rough, metal, i.light2Color) * crevice;
        lit += albedo * (1.0 - metal) * i.light2Color * saturate(0.5 + 0.5 * dot(n, i.light2)) * 0.18 * occlusion;
    }

    // Lamparas de la nave (balizas, luces de navegacion, focos): iluminan lo que tienen cerca y se
    // notan sobre todo en lo que esta a la sombra. Se apagan suaves hasta su alcance.
    const uint lamps = (uint)uLampInfo.x;
    for (uint k = 0; k < lamps && k < kMaxPointLights; ++k) {
        const float3 toLamp = uLampPosition[k].xyz - i.viewPosition;
        const float d = length(toLamp);
        const float reach = uLampPosition[k].w;
        if (d >= reach || d <= 1e-6) continue;
        const float edge = 1.0 - d / reach;
        const float closeness = d / (reach * 0.18);
        const float fall = edge * edge / (1.0 + closeness * closeness);
        const float3 l = toLamp / d;
        lit += Shine(n, l, toEye, albedo, rough, metal, uLampColor[k].rgb * fall) * crevice;
        lit += albedo * (1.0 - metal) * uLampColor[k].rgb * fall * 0.06 * occlusion;  // lo que rebota alrededor
    }
    return float4(lit + pure * i.shading.y + damageGlow, 1.0);
}
