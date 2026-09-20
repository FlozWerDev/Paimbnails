// Bloom: 5 modos (prefilter/down/up/god rays/brillo) con filtros Jimenez.

varying vec2 v_texCoord;

uniform sampler2D u_src;
uniform sampler2D u_add;
uniform vec2  u_texel;
uniform float u_mode;
uniform float u_threshold;
uniform float u_softKnee;
uniform float u_radius;
uniform float u_blend;
uniform float u_anamorphic;
uniform vec2  u_lightPos;
uniform float u_decay;
uniform float u_density;
uniform float u_tonemap;
uniform float u_hdrRange;
uniform float u_giMix;
uniform float u_adaptRate;
uniform float u_frame;

const int kRaySamples = 24;
// Sombra aproximada por luma, sin G-buffer.
const float kVolBlock = 0.35;

// Nivel 0 viene en sRGB; el resto ya en lineal.
vec3 tap(vec2 uv, float prefilter) {
    vec3 c = texture2D(u_src, uv).rgb;
    if (prefilter > 0.5) c = min(tonemapInverse(toLinear(c), u_tonemap), vec3(max(u_hdrRange, 1.0)));
    return c;
}

vec3 down13(vec2 uv, vec2 t, float prefilter) {
    vec3 a = tap(uv + vec2(-2.0,  2.0) * t, prefilter);
    vec3 b = tap(uv + vec2( 0.0,  2.0) * t, prefilter);
    vec3 c = tap(uv + vec2( 2.0,  2.0) * t, prefilter);
    vec3 d = tap(uv + vec2(-2.0,  0.0) * t, prefilter);
    vec3 e = tap(uv, prefilter);
    vec3 f = tap(uv + vec2( 2.0,  0.0) * t, prefilter);
    vec3 g = tap(uv + vec2(-2.0, -2.0) * t, prefilter);
    vec3 h = tap(uv + vec2( 0.0, -2.0) * t, prefilter);
    vec3 i = tap(uv + vec2( 2.0, -2.0) * t, prefilter);
    vec3 j = tap(uv + vec2(-1.0,  1.0) * t, prefilter);
    vec3 k = tap(uv + vec2( 1.0,  1.0) * t, prefilter);
    vec3 l = tap(uv + vec2(-1.0, -1.0) * t, prefilter);
    vec3 m = tap(uv + vec2( 1.0, -1.0) * t, prefilter);

    vec3 q0 = (a + b + d + e) * 0.25;
    vec3 q1 = (b + c + e + f) * 0.25;
    vec3 q2 = (d + e + g + h) * 0.25;
    vec3 q3 = (e + f + h + i) * 0.25;
    vec3 q4 = (j + k + l + m) * 0.25;

    float w0 = mix(1.0, 1.0 / (1.0 + luma(q0)), prefilter) * 0.125;
    float w1 = mix(1.0, 1.0 / (1.0 + luma(q1)), prefilter) * 0.125;
    float w2 = mix(1.0, 1.0 / (1.0 + luma(q2)), prefilter) * 0.125;
    float w3 = mix(1.0, 1.0 / (1.0 + luma(q3)), prefilter) * 0.125;
    float w4 = mix(1.0, 1.0 / (1.0 + luma(q4)), prefilter) * 0.500;

    return (q0 * w0 + q1 * w1 + q2 * w2 + q3 * w3 + q4 * w4) / (w0 + w1 + w2 + w3 + w4);
}

vec3 tent9(vec2 uv, vec2 t) {
    vec3 c = texture2D(u_src, uv).rgb * 4.0;
    c += texture2D(u_src, uv + vec2(-t.x, 0.0)).rgb * 2.0;
    c += texture2D(u_src, uv + vec2( t.x, 0.0)).rgb * 2.0;
    c += texture2D(u_src, uv + vec2(0.0, -t.y)).rgb * 2.0;
    c += texture2D(u_src, uv + vec2(0.0,  t.y)).rgb * 2.0;
    c += texture2D(u_src, uv + vec2(-t.x, -t.y)).rgb;
    c += texture2D(u_src, uv + vec2( t.x, -t.y)).rgb;
    c += texture2D(u_src, uv + vec2(-t.x,  t.y)).rgb;
    c += texture2D(u_src, uv + vec2( t.x,  t.y)).rgb;
    return c / 16.0;
}

// Ruido barato sin texturas para volumetricos.
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Rodilla suave: el corte duro deja linea y parpadeo.
vec3 knee(vec3 c) {
    float thr = tonemapInverse(vec3(u_threshold * u_threshold), u_tonemap).r;
    float br = max(max(c.r, c.g), c.b);
    float k = thr * u_softKnee + 0.0001;
    float soft = clamp(br - thr + k, 0.0, 2.0 * k);
    soft = soft * soft / (4.0 * k);
    return c * (max(soft, br - thr) / max(br, 0.0001));
}

void main() {
    vec2 uv = v_texCoord;
    vec3 outColor;

    if (u_mode > 3.5) {
        float lum = max(luma(toLinear(texture2D(u_src, vec2(0.5), 14.0).rgb)), 0.0005);
        outColor = vec3(mix(texture2D(u_add, vec2(0.5)).r, lum, u_adaptRate));
    } else if (u_mode > 2.5) {
        // Jitter temporal: sin el, la marcha deja bandas concentricas.
        vec2 delta = (uv - u_lightPos) * u_density / float(kRaySamples);
        // mod 64: evita degradar el hash por precision.
        float frameIx = mod(u_frame, 64.0);
        float j0 = fract(hash12(gl_FragCoord.xy) + halton(frameIx, 2.0));
        vec2 p = uv - delta * j0;
        float illum = 1.0;
        float trans = 1.0;
        float wsum = 0.0;
        float decay = clamp(u_decay, 0.0, 0.999);
        vec3 acc = vec3(0.0);
        for (int i = 0; i < kRaySamples; i++) {
            p -= delta;
            if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) break;
            vec3 s = texture2D(u_src, p).rgb;
            if (!all(equal(s, s))) s = vec3(0.0);
            s = max(s, vec3(0.0));
            // La luma densa absorbe; pesa igual que el decaimiento.
            float w = illum * trans;
            acc += s * w;
            wsum += w;
            trans *= exp(-min(luma(s), 8.0) * kVolBlock);
            illum *= decay;
        }
        // Ruido polar de 2 octavas modula la densidad.
        vec2 rel = uv - u_lightPos;
        float rad = length(rel);
        float ang = 0.0;
        if (rad > 0.0001) ang = atan(rel.y, rel.x);
        vec2 npc = vec2(ang * 1.5 + frameIx * 0.02, rad * 6.0 - frameIx * 0.05);
        float vn = vnoise(npc * 3.0) * 0.65 + vnoise(npc * 7.0 + 13.7) * 0.35;
        float dens = mix(0.75, 1.25, clamp(vn, 0.0, 1.0));
        outColor = acc / max(wsum, 0.0001) * dens;
    } else if (u_mode > 1.5) {
        vec2 spread = u_texel * u_radius * vec2(1.0 + u_anamorphic * 3.0, 1.0);
        outColor = mix(texture2D(u_add, uv).rgb, tent9(uv, spread), u_blend);
    } else if (u_mode > 0.5) {
        outColor = down13(uv, u_texel, 0.0);
    } else {
        outColor = knee(down13(uv, u_texel, 1.0) + texture2D(u_add, uv).rgb * u_giMix);
    }

    gl_FragColor = vec4(outColor, 1.0);
}
