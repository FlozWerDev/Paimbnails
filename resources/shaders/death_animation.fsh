#ifdef GL_ES
precision mediump float;
varying lowp vec4 v_fragmentColor;
varying mediump vec2 v_texCoord;
#else
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
#endif

uniform float u_progress;
uniform float u_style;
uniform vec3 u_tint;
uniform vec3 u_tint2;
uniform float u_intensity;
uniform float u_seed;
uniform float u_spin;

const float PI = 3.14159265;

float band(float d, float width) {
    return 1.0 - smoothstep(width, width + 0.02, abs(d));
}

float hash1(float n) {
    return fract(sin(n * 12.9898 + u_seed * 7.13) * 43758.5453);
}

vec2 rot2(vec2 v, float ang) {
    float c = cos(ang);
    float s = sin(ang);
    return vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}

void main() {
    vec2 p = v_texCoord * 2.0 - 1.0;
    float t = clamp(u_progress, 0.0, 1.0);
    float e = 1.0 - pow(1.0 - t, 3.0);
    float spinAng = u_spin * t * 2.5;
    if (abs(u_spin) > 0.001) {
        p = rot2(p, spinAng);
    }

    float r = length(p);
    float a = atan(p.y, p.x);
    float radius = 0.07 + 0.72 * e;

    float light = 0.0;
    float darkHole = 0.0;
    float intensity = (u_intensity > 0.001) ? u_intensity : 1.0;
    vec3 col1 = u_tint;
    vec3 col2 = (dot(u_tint2, u_tint2) > 0.001) ? u_tint2 : mix(u_tint, vec3(1.0), 0.45);
    vec3 col = mix(col1, col2, clamp(r * 1.1, 0.0, 1.0));

    float st = floor(u_style + 0.5);

    // ===== Family 0: Cosmic (0..7) =====
    if (st < 0.5) { // 0: Supernova
        light = band(r - radius, 0.02) * 0.9 + exp(-r * 11.0) * (1.0 - t) * 2.2;
        light += pow(max(0.0, cos(a * 10.0 + t * 2.5 + u_seed)), 20.0) * exp(-abs(r - radius * 0.65) * 8.5);
        col = mix(col1, vec3(1.0), exp(-r * 7.0) * (1.0 - t * 0.6));
    } else if (st < 1.5) { // 1: Black Hole
        float spin = a + 3.2 / (r + 0.14) - t * 9.0;
        float coreR = 0.24 * (1.0 - t * 0.65);
        light = band(r - coreR, 0.025) * 1.1 + band(r - radius * (0.62 + 0.14 * sin(spin * 2.0)), 0.028) * 0.75;
        col = mix(col2, col1, smoothstep(coreR, radius, r));
        darkHole = 1.0 - smoothstep(coreR * 0.72, coreR * 0.96, r);
    } else if (st < 2.5) { // 2: Solar Flare
        col = mix(vec3(1.0, 0.58, 0.12), vec3(1.0, 0.94, 0.55), 1.0 - t * 0.6);
        light = band(r - radius * (0.78 + 0.12 * sin(a * 9.0 - t * 8.0)), 0.036)
              + exp(-abs(p.y) * 44.0 - abs(p.x) * 2.2) * (1.0 - t * 0.75) + exp(-r * 8.0) * (1.0 - t);
    } else if (st < 3.5) { // 3: Plasma Vortex
        float arm = sin(a * 3.0 + r * 11.0 - t * 10.0);
        light = band(r - radius * (0.66 + 0.20 * arm), 0.024) + exp(-r * 10.0) * (1.0 - t);
        col = mix(col1, col2, 0.5 + 0.5 * arm);
    } else if (st < 4.5) { // 4: Nebula Burst
        float w = 0.5 + 0.5 * sin(a * 5.0 + t * 4.0) * cos(r * 12.0 - t * 6.0);
        light = (1.0 - smoothstep(radius * 0.45, radius, r)) * (0.55 + 0.75 * w);
        col = mix(col1, col2, w);
    } else if (st < 5.5) { // 5: Quasar Beam
        light = exp(-abs(p.y) * 32.0) * (1.0 - smoothstep(radius * 0.7, radius, abs(p.x)))
              + band(r - radius * 0.45, 0.018) * 0.8 + exp(-r * 7.5) * 1.4;
        col = mix(col2, vec3(1.0), exp(-abs(p.y) * 20.0) * 0.5);
    } else if (st < 6.5) { // 6: Eclipse Pulse
        float disc = 0.28 + 0.18 * (1.0 - t);
        light = band(r - disc, 0.032) * 1.4 + exp(-abs(r - disc) * 16.0) * 0.5;
        darkHole = 1.0 - smoothstep(disc * 0.82, disc * 0.98, r);
        col = mix(col1, col2, clamp(abs(sin(a * 2.0 + t * 3.0)), 0.0, 1.0));
    } else if (st < 7.5) { // 7: Wormhole Tear
        float tunnel = sin(r * 22.0 - t * 18.0 + a * 2.0);
        light = smoothstep(0.2, 0.95, tunnel) * band(r - radius * 0.68, 0.24);
        col = mix(col1, col2, 0.5 + 0.5 * cos(r * 10.0 - t * 8.0));

    // ===== Family 1: Cyber (8..15) =====
    } else if (st < 8.5) { // 8: Cyber Glitch
        vec2 q = p;
        q.x += (hash1(floor(q.y * 16.0) + floor(t * 14.0)) - 0.5) * 0.28 * (0.3 + t);
        float box = max(abs(q.x), abs(q.y));
        light = band(box - radius * 0.74, 0.025) * step(0.2, hash1(floor(q.y * 20.0) + floor(t * 10.0)))
              + band(abs(q.y) - 0.08 * (1.0 - t), 0.02) * step(abs(q.x), radius * 0.85) * 0.65;
        col = mix(vec3(0.1, 1.0, 0.92), vec3(1.0, 0.15, 0.58), step(0.0, q.x));
    } else if (st < 9.5) { // 9: Pixel Shatter
        vec2 cell = floor(p * 12.0) / 12.0;
        float h = hash1(cell.x * 31.0 + cell.y * 17.0 + 3.0);
        light = step(0.22, h) * band(length(cell) - radius * 0.76, 0.16)
              + exp(-r * 9.0) * (1.0 - t) * 0.85;
        col = mix(col1, col2, h);
    } else if (st < 10.5) { // 10: VHS Static
        float stripe = sin(p.y * 55.0 + t * 30.0);
        float noise = hash1(floor(p.y * 32.0) + floor(t * 24.0) + floor(p.x * 8.0));
        light = (0.45 + 0.55 * step(0.35, noise)) * (0.5 + 0.5 * stripe) * (1.0 - smoothstep(radius * 0.5, radius, r));
        col = mix(col1, col2, noise);
    } else if (st < 11.5) { // 11: Matrix Rain
        float colId = floor((p.x + 1.0) * 9.0);
        float speed = 0.6 + hash1(colId) * 0.8;
        float yShift = fract(p.y * 2.5 + t * speed * 2.2 + hash1(colId + 5.0));
        light = (1.0 - smoothstep(0.0, 0.45, yShift)) * step(abs(p.x), radius * 0.8) * step(abs(p.y), radius * 0.8);
        col = mix(vec3(0.1, 1.0, 0.35), col1, 0.35);
    } else if (st < 12.5) { // 12: CRT Shutdown
        float sq = mix(0.85, 0.06, clamp(t * 1.4, 0.0, 1.0));
        light = exp(-abs(p.y) / max(0.02, sq * 0.35)) * step(abs(p.x), radius * (1.0 - t * 0.35))
              + exp(-r * 12.0) * smoothstep(0.4, 0.85, t) * 1.8;
        col = mix(vec3(0.85, 0.96, 1.0), col1, 0.45);
    } else if (st < 13.5) { // 13: Neon Cross
        vec2 q = rot2(p, t * 1.8);
        light = (exp(-abs(q.x) * 32.0) + exp(-abs(q.y) * 32.0)) * (1.0 - smoothstep(0.0, radius, r))
              + band(r - radius * 0.85, 0.016) * 0.85;
    } else if (st < 14.5) { // 14: Laser Grid
        vec2 g = abs(fract(p * 4.5 + t * 0.5) - 0.5);
        float grid = smoothstep(0.07, 0.01, min(g.x, g.y));
        light = grid * band(r - radius * 0.7, 0.22);
        col = mix(col1, col2, 0.5 + 0.5 * sin(r * 12.0));
    } else if (st < 15.5) { // 15: Holo Fracture
        float slice = sin(p.y * 18.0 - t * 15.0);
        float tri = max(abs(p.x) * 0.866 + p.y * 0.5, -p.y);
        light = band(tri - radius * 0.65, 0.022) * (0.6 + 0.5 * step(0.0, slice));
        col = 0.55 + 0.45 * cos(vec3(0.0, 2.0, 4.0) + p.y * 6.0 + t * 5.0);

    // ===== Family 2: Elemental (16..23) =====
    } else if (st < 16.5) { // 16: Inferno Embers
        col = mix(vec3(1.0, 0.32, 0.06), vec3(1.0, 0.85, 0.25), 1.0 - t);
        light = band(r - radius * 0.78, 0.025) * 0.45 * (1.0 - t);
        for (int i = 0; i < 14; i++) {
            float n = float(i);
            float ang = n * 2.39996 + hash1(n) * 0.6;
            vec2 q = p - vec2(cos(ang), sin(ang)) * e * (0.22 + 0.56 * hash1(n + 1.0)) - vec2(0.0, t * t * 0.22);
            light += exp(-dot(q, q) * 1400.0) * (0.65 + 0.5 * hash1(n + 3.0));
        }
    } else if (st < 17.5) { // 17: Cryo Crystal
        col = mix(vec3(0.45, 0.88, 1.0), vec3(0.88, 0.98, 1.0), 0.5);
        light = pow(max(0.0, cos(a * 6.0)), 32.0) * (1.0 - smoothstep(radius - 0.1, radius, r))
              + band(r - radius * 0.62, 0.014) * (0.4 + 0.6 * pow(max(0.0, cos(a * 6.0)), 4.0));
    } else if (st < 18.5) { // 18: Lightning Strike
        for (int i = 0; i < 7; i++) {
            float n = float(i);
            float ang = (n / 7.0) * PI * 2.0 + hash1(n + floor(t * 18.0)) * 0.5;
            vec2 dir = vec2(cos(ang), sin(ang));
            vec2 perp = vec2(-dir.y, dir.x);
            float proj = dot(p, dir);
            float zig = sin(proj * 26.0 + n * 4.0 + floor(t * 20.0) * 3.0) * 0.06;
            light += exp(-abs(dot(p, perp) - zig) * 58.0) * smoothstep(0.0, 0.08, proj) * (1.0 - smoothstep(radius * 0.65, radius, proj));
        }
        light += exp(-r * 9.0) * (1.0 - t) * 1.4;
        col = mix(col1, vec3(0.75, 0.92, 1.0), 0.55);
    } else if (st < 19.5) { // 19: Sakura Lotus
        light = band(r - radius * (0.56 + 0.28 * cos(a * 5.0 - t * 2.4)), 0.022)
              + 0.55 * band(r - radius * (0.40 + 0.18 * cos(a * 5.0 + t * 2.2)), 0.014);
        col = mix(vec3(1.0, 0.48, 0.78), col2, 0.35);
    } else if (st < 20.5) { // 20: Tsunami Wave
        float wave = sin(r * 18.0 - t * 14.0 + sin(a * 3.0) * 0.8);
        light = smoothstep(0.0, 0.9, wave) * band(r - radius * 0.75, 0.20);
        col = mix(vec3(0.08, 0.55, 1.0), vec3(0.65, 0.95, 1.0), wave * 0.5 + 0.5);
    } else if (st < 21.5) { // 21: Tornado Spiral
        float vortex = sin(a * 4.0 + log(max(0.05, r)) * 9.0 - t * 14.0);
        light = smoothstep(0.1, 0.9, vortex) * (1.0 - smoothstep(radius * 0.55, radius, r));
        col = mix(col1, col2, 0.5 + 0.5 * vortex);
    } else if (st < 22.5) { // 22: Magma Crack
        float c1 = abs(sin(a * 7.0 + r * 6.0));
        light = exp(-c1 * 12.0) * (1.0 - smoothstep(radius * 0.5, radius, r)) * 1.5;
        col = mix(vec3(1.0, 0.22, 0.02), vec3(1.0, 0.88, 0.2), light);
    } else if (st < 23.5) { // 23: Aurora Ribbon
        float rib = sin(p.x * 5.0 + t * 6.0) * 0.22 + sin(p.x * 9.0 - t * 4.0) * 0.1;
        light = exp(-abs(p.y - rib) * 14.0) * (1.0 - smoothstep(radius * 0.55, radius, abs(p.x)));
        col = mix(vec3(0.18, 0.98, 0.68), vec3(0.58, 0.35, 1.0), 0.5 + 0.5 * sin(p.x * 3.0 + t * 5.0));

    // ===== Family 3: Impact (24..31) =====
    } else if (st < 24.5) { // 24: Prism Shatter
        float hex = cos(mod(a + PI / 6.0, PI / 3.0) - PI / 6.0) * r;
        light = band(hex - radius * 0.82, 0.016) + 0.55 * band(hex - radius * 0.48, 0.012);
        col = 0.55 + 0.45 * cos(vec3(0.0, 2.1, 4.2) + a * 1.5 + t * 3.5);
    } else if (st < 25.5) { // 25: Shockwave Rings
        light = band(r - radius, 0.016) + 0.7 * band(r - radius * 0.72, 0.014) + 0.45 * band(r - radius * 0.44, 0.012);
    } else if (st < 26.5) { // 26: Hexagon Pulse
        float hex = cos(mod(a + PI / 6.0, PI / 3.0) - PI / 6.0) * r;
        light = band(hex - radius * 0.78, 0.024) * 1.15 + band(hex - radius * 0.42, 0.015) * 0.7;
    } else if (st < 27.5) { // 27: Origami Fold
        float diam = abs(p.x) + abs(p.y);
        light = band(diam - radius * 0.95, 0.022) + band(diam - radius * 0.55, 0.015) * 0.75;
    } else if (st < 28.5) { // 28: Glass Break
        float ray = pow(max(0.0, cos(a * 11.0 + u_seed)), 28.0);
        light = (ray * (1.0 - smoothstep(radius * 0.4, radius, r)) + band(r - radius * 0.65, 0.014)) * 1.1;
        col = mix(vec3(0.78, 0.94, 1.0), col1, 0.4);
    } else if (st < 29.5) { // 29: Comic Pow
        float spikes = 0.52 + 0.34 * cos(a * 12.0 + t * 2.0);
        light = band(r - radius * spikes, 0.032) + step(r, radius * spikes * 0.55) * 0.45;
        col = mix(vec3(1.0, 0.92, 0.2), vec3(1.0, 0.28, 0.12), step(0.5, fract(a * 3.0 / PI)));
    } else if (st < 30.5) { // 30: Gravity Implode
        float imp = (1.0 - e) * 0.75 + 0.06;
        light = band(r - imp, 0.026) * 1.2 + exp(-r * 10.0) * smoothstep(0.6, 1.0, t) * 2.0;
    } else if (st < 31.5) { // 31: Sonic Boom
        vec2 q = rot2(p, -0.5);
        light = band(abs(q.y) - abs(q.x) * 0.7 - radius * 0.22, 0.024) * (1.0 - smoothstep(radius * 0.6, radius, r))
              + band(r - radius * 0.85, 0.02) * 0.8;

    // ===== Family 4: Cinema (32..39) =====
    } else if (st < 32.5) { // 32: Dimensional Rift
        vec2 q = rot2(p, 0.66);
        light = band(abs(q.x) - 0.055 * sin(q.y * 16.0 + t * 9.0) * (1.0 - t), 0.022)
              * (1.0 - smoothstep(radius * 0.62, radius, abs(q.y)))
              + 0.45 * band(length(q * vec2(2.6, 1.0)) - radius, 0.016);
        col = mix(vec3(0.62, 0.28, 1.0), col1, 0.4);
    } else if (st < 33.5) { // 33: Stardust Nova
        for (int i = 0; i < 16; i++) {
            float n = float(i);
            float ang = n * 2.39996 + t * (hash1(n + 2.0) - 0.5) * 1.4;
            vec2 q = p - vec2(cos(ang), sin(ang)) * e * (0.20 + hash1(n + 1.0) * 0.58);
            light += exp(-length(q) * 75.0) * (0.6 + 0.4 * sin(t * 14.0 + n));
        }
        light += pow(max(0.0, cos(a * 4.0)), 18.0) * exp(-r * 4.5) * 1.2;
        col = mix(col1, vec3(0.95, 0.90, 1.0), 0.5);
    } else if (st < 34.5) { // 34: Void Snap
        float imp = t < 0.38 ? (1.0 - t / 0.38) * 0.65 : ((t - 0.38) / 0.62) * 0.82;
        light = band(r - imp, 0.025) * 1.15 + pow(max(0.0, cos(a * 8.0 + t * 5.0)), 14.0) * band(r - imp * 0.8, 0.09);
        col = mix(col2, col1, smoothstep(0.0, 0.6, t));
    } else if (st < 35.5) { // 35: Time Freeze
        light = band(r - 0.58, 0.02) + band(abs(sin(a * 6.0)), 0.05) * step(r, 0.58) * 0.55;
        col = mix(vec3(0.55, 0.92, 1.0), vec3(1.0, 1.0, 1.0), 0.4);
    } else if (st < 36.5) { // 36: Soul Ascend
        vec2 q = p - vec2(0.0, t * 0.45);
        light = exp(-length(q * vec2(1.8, 0.7)) * 7.5) * 1.3 + exp(-abs(p.x) * 18.0) * step(-0.2, p.y) * (1.0 - t) * 0.7;
        col = mix(col1, vec3(0.95, 0.98, 1.0), 0.55);
    } else if (st < 37.5) { // 37: Phantom Echo
        light = band(r - radius * 0.9, 0.02) * 0.9 + band(r - radius * 0.65, 0.02) * 0.6 + band(r - radius * 0.35, 0.02) * 0.4;
        col = mix(col1, vec3(0.45, 0.25, 0.85), t);
    } else if (st < 38.5) { // 38: Blood Moon
        float moonR = 0.42;
        light = band(r - moonR, 0.03) * 1.2 + band(r - radius, 0.018) * 0.75;
        darkHole = (1.0 - smoothstep(moonR * 0.82, moonR * 0.98, r)) * 0.65;
        col = vec3(1.0, 0.14, 0.22);
    } else { // 39: Arcade KO
        float star = 0.5 + 0.42 * cos(a * 5.0 + t * 6.0);
        light = band(r - radius * star, 0.028) * 1.2 + band(r - radius * 0.82, 0.02) * 0.8;
        col = mix(vec3(1.0, 0.88, 0.15), vec3(1.0, 0.2, 0.45), step(0.5, fract(t * 6.0)));
    }

    float fade = (1.0 - smoothstep(0.46, 1.0, t)) * smoothstep(0.0, 0.04, t);
    float edgeMask = 1.0 - smoothstep(0.84, 0.98, r);
    float glowAlpha = clamp(light * intensity * fade, 0.0, 0.95) * edgeMask;
    float finalAlpha = max(glowAlpha, darkHole * fade * 0.92 * edgeMask);
    vec3 finalRgb = mix(col, vec3(0.02, 0.02, 0.04), darkHole * (1.0 - glowAlpha * 0.5));

    gl_FragColor = vec4(finalRgb, finalAlpha) * v_fragmentColor;
}
