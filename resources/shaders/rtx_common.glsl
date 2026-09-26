// rtx shared: linear-space work, inverse/tonemap pairs intact.
#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

// inverses diverge at pure white.
const float kHdrCeil = 0.9995;
const float kU2White = 0.72519;

float luma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 toLinear(vec3 c) { return c * c; }
vec3 toDisplay(vec3 c) { return sqrt(max(c, 0.0)); }

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// ANGLE fails on empty or inverted edges.
float safeSmoothstep(float e0, float e1, float x) {
    if (abs(e1 - e0) < 0.00001) return step(e0, x);
    if (e1 < e0) return 1.0 - smoothstep(e1, e0, x);
    return smoothstep(e0, e1, x);
}

// halton for temporal dither.
float halton(float idx, float base) {
    float f = 1.0;
    float r = 0.0;
    float v = max(idx, 0.0);
    for (int b = 0; b < 10; b++) {
        f /= base;
        r += f * mod(v, base);
        v = floor(v / base);
        if (v <= 0.0) break;
    }
    return fract(r);
}

// guards HDR Inf/NaN.
vec3 softClampHi(vec3 c) {
    float l = luma(c);
    return c / (1.0 + max(l - 1.0, 0.0));
}

vec3 tmReinhard(vec3 c) { return c / (1.0 + c); }
vec3 tmReinhardInv(vec3 c) { return c / (1.0 - min(c, kHdrCeil)); }

vec3 tmAces(vec3 c) {
    return clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
}
vec3 tmAcesInv(vec3 c) {
    vec3 y = min(c, kHdrCeil);
    return (sqrt(max(-10127.0 * y * y + 13702.0 * y + 9.0, 0.0)) + 59.0 * y - 3.0)
         / (502.0 - 486.0 * y);
}

// filmic bakes sRGB in; square back to linear.
vec3 tmFilmic(vec3 c) {
    vec3 x = max(vec3(0.0), c - 0.004);
    vec3 s = (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
    return s * s;
}
vec3 tmFilmicInv(vec3 c) {
    vec3 s = min(toDisplay(c), kHdrCeil);
    vec3 a = 6.2 * (s - 1.0);
    // no div-by-zero on out-of-range input.
    vec3 ax = abs(a);
    a.x = ax.x < 0.001 ? -0.001 : a.x;
    a.y = ax.y < 0.001 ? -0.001 : a.y;
    a.z = ax.z < 0.001 ? -0.001 : a.z;
    vec3 b = 1.7 * s - 0.5;
    vec3 k = 0.06 * s;
    return (-b - sqrt(max(b * b - 4.0 * a * k, 0.0))) / (2.0 * a) + 0.004;
}

vec3 tmU2Curve(vec3 x) {
    return ((x * (0.15 * x + 0.05) + 0.004) / (x * (0.15 * x + 0.50) + 0.06)) - 0.066667;
}
vec3 tmUncharted(vec3 c) { return tmU2Curve(c * 2.0) / kU2White; }
vec3 tmUnchartedInv(vec3 c) {
    vec3 q = min(c, kHdrCeil) * kU2White + 0.066667;
    vec3 a = 0.15 * (q - 1.0);
    vec3 b = 0.50 * q - 0.05;
    vec3 k = 0.06 * q - 0.004;
    return ((-b - sqrt(max(b * b - 4.0 * a * k, 0.0))) / (2.0 * a)) * 0.5;
}

vec3 tonemapApply(vec3 c, float mode) {
    if (mode > 3.5) return tmUncharted(c);
    if (mode > 2.5) return tmFilmic(c);
    if (mode > 1.5) return tmAces(c);
    if (mode > 0.5) return tmReinhard(c);
    return c;
}

vec3 tonemapInverse(vec3 c, float mode) {
    if (mode > 3.5) return tmUnchartedInv(c);
    if (mode > 2.5) return tmFilmicInv(c);
    if (mode > 1.5) return tmAcesInv(c);
    if (mode > 0.5) return tmReinhardInv(c);
    return c;
}
