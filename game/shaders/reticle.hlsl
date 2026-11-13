// reticle.hlsl - Punto de mira y rumbo apuntado, con el color inverso de lo que tienen detras.
//
// Se dibuja sobre la imagen ya terminada porque ningun color fijo se veria en todas partes:
// una estrella se traga el naranja, y el vacio se traga el negro. Cada pixel lee lo que tiene
// debajo. El gris no tiene inverso visible, asi que ahi se usa cian, que no existe en la nebulosa.
//
// Mismo contrato que effects/scanlines.hlsl, mas doce valores propios.

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
    float4 uPoints;   // xy: donde mira la nave; zw: adonde se le ha mandado mirar
    float4 uFlags;    // x: escala; y: nariz visible; z: apuntado visible; w: linea entre ambos
    float4 uUnused;
};

static const float kTau = 6.2831853;

float Stroke(float distance, float halfWidth) {
    return 1.0 - smoothstep(halfWidth - 0.6, halfWidth + 0.6, distance);
}

float Segment(float2 p, float2 a, float2 b) {
    const float2 ab = b - a;
    return length(p - a - ab * saturate(dot(p - a, ab) / max(dot(ab, ab), 1e-3)));
}

// Cuatro marcas sobre los ejes: basta con calcular una.
float Ticks(float2 p, float inner, float outer) {
    float2 q = abs(p);
    if (q.y > q.x) q = q.yx;
    return Segment(q, float2(inner, 0.0), float2(outer, 0.0));
}

float Nose(float2 p, float s) {
    const float radius = length(p);
    float mask = Stroke(abs(radius - 3.0 * s), 1.1 * s);
    mask = max(mask, Stroke(Ticks(p, 18.0 * s, 25.0 * s), 1.2 * s));

    // Tres arcos que giran.
    const float third = kTau / 3.0;
    const float angle = atan2(p.y, p.x) - uTime * 0.5;
    const float inArc = 1.0 - step(0.8, angle - floor(angle / third) * third);
    return max(mask, Stroke(abs(radius - 30.0 * s), 1.1 * s) * inArc);
}

float Aim(float2 p, float s) {
    const float2 turned = float2(p.x + p.y, p.y - p.x) * 0.7071;
    return max(Stroke(abs(length(p) - 7.0 * s), 1.2 * s), Stroke(Ticks(turned, 7.0 * s, 12.0 * s), 1.2 * s));
}

// Marca pegada al borde: una flecha que apunta hacia fuera, hacia donde quedo lo que se salio.
float EdgeArrow(float2 p, float2 at, float s, float size) {
    const float2 away = normalize(at - uResolution * 0.5 + float2(1e-3, 0.0));
    const float2 side = float2(-away.y, away.x);
    const float2 q = float2(dot(p - at, away), dot(p - at, side));
    const float2 tip = float2(size, 0.0) * s, left = float2(-size * 0.6, size * 0.75) * s, right = float2(-size * 0.6, -size * 0.75) * s;
    float edge = min(Segment(q, tip, left), Segment(q, tip, right));
    edge = min(edge, Segment(q, left, right));
    return Stroke(edge, 1.2 * s);
}

float4 PSEffect(VsOut i) : SV_Target0 {
    const float3 scene = gScene.SampleLevel(gSampler, i.uv, 0).rgb;
    const float2 pixel = i.position.xy;
    const float s = uFlags.x;

    // 1: la marca entera; 2: se salio de la pantalla y queda una flecha en el borde.
    float mask = 0.0;
    if (uFlags.y > 1.5) mask = max(mask, EdgeArrow(pixel, uPoints.xy, s, 14.0));
    else if (uFlags.y > 0.5) mask = max(mask, Nose(pixel - uPoints.xy, s));
    if (uFlags.z > 1.5) mask = max(mask, EdgeArrow(pixel, uPoints.zw, s, 10.0));
    else if (uFlags.z > 0.5) mask = max(mask, Aim(pixel - uPoints.zw, s));
    if (uFlags.w > 0.5) mask = max(mask, Stroke(Segment(pixel, uPoints.xy, uPoints.zw), 0.7) * 0.45);
    if (mask <= 0.0) return float4(scene, 1.0);

    // Sobre un color con tono (el rojo y el amarillo de la nebulosa), su inverso: cian o azul.
    // Sobre lo que no tiene tono (el gris, el vacio, el blanco de una estrella) el inverso no se
    // distingue, y ahi se usa cian: claro sobre lo oscuro y marino sobre lo claro.
    const float light = dot(scene, float3(0.299, 0.587, 0.114));
    const float chroma = max(scene.r, max(scene.g, scene.b)) - min(scene.r, min(scene.g, scene.b));
    const float grey = 1.0 - smoothstep(0.10, 0.28, chroma);
    const float3 cyan = light < 0.55 ? float3(0.27, 0.91, 1.0) : float3(0.0, 0.16, 0.42);
    float3 inverse = 1.0 - scene;
    inverse = lerp(inverse, cyan, grey);
    // Un inverso que quede cerca de la luz de lo de detras (rojo medio y su cian medio) se aclara
    // o se oscurece para separarse.
    const float gap = dot(inverse, float3(0.299, 0.587, 0.114)) - light;
    inverse = saturate(inverse + sign(gap + 1e-3) * max(0.0, 0.35 - abs(gap)));
    return float4(lerp(scene, inverse, mask * uIntensity), 1.0);
}
