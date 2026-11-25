// Paimon Assistant fullscreen effort-reactive backdrop shader.
// Renders 3 calm, independent background animations for Normal, High, and Ultra effort levels.
#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying lowp vec4 v_fragmentColor;
varying mediump vec2 v_texCoord;
#else
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
#endif

uniform vec2 u_resolution;
uniform float u_time;
uniform vec3 u_effortWeights; // x = Normal, y = High, z = Ultra
uniform float u_working;
uniform float u_fade;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// 1) NORMAL: Calm drifting bokeh orbs & gentle mist.
vec4 animNormal(vec2 uv, vec2 p, float aspect, float t, float work) {
    float speed = t * mix(0.28, 0.60, work);
    vec3 col = vec3(0.03, 0.06, 0.11);

    float acc = 0.0;
    vec3 orbTint = vec3(0.0);
    for (int i = 0; i < 5; ++i) {
        float fi = float(i);
        vec2 center = vec2(
            sin(speed * (0.42 + fi * 0.11) + fi * 1.7) * (aspect * 0.36),
            cos(speed * (0.37 + fi * 0.09) + fi * 2.3) * 0.34
        );
        float radius = 0.22 + 0.05 * sin(speed * 0.6 + fi * 1.3);
        float d = length(p - center);
        float orb = exp(-(d * d) / (radius * radius));
        vec3 c = mix(vec3(0.32, 0.74, 0.96), vec3(0.65, 0.88, 0.98), 0.5 + 0.5 * sin(fi * 1.9 + speed * 0.4));
        orbTint += c * orb;
        acc += orb;
    }

    float breath = 0.90 + 0.10 * sin(speed * 0.9);
    col += orbTint * (0.10 + 0.06 * work) * breath;
    float alpha = clamp(0.08 + acc * 0.055 + work * 0.04, 0.0, 0.24);
    return vec4(col, alpha);
}

// 2) HIGH: Flowing diagonal aurora ribbons & harmonic waves.
vec4 animHigh(vec2 uv, vec2 p, float aspect, float t, float work) {
    float speed = t * mix(0.55, 1.05, work);
    vec3 base = mix(vec3(0.04, 0.04, 0.12), vec3(0.03, 0.08, 0.15), uv.y);

    vec3 aurora = vec3(0.0);
    float totalBand = 0.0;
    for (int i = 0; i < 3; ++i) {
        float fi = float(i);
        float freq = 2.2 + fi * 0.9;
        float wave = sin(p.x * freq + speed * (0.75 + fi * 0.18) + fi * 2.1) * 0.13
                   + cos(p.x * (freq * 0.55) - speed * 0.55 + fi) * 0.07;
        float yTarget = (fi - 1.0) * 0.19 + wave;
        float dist = abs(p.y - yTarget);
        float ribbon = exp(-dist * (7.2 - fi * 1.0));
        float shimmer = 0.80 + 0.20 * sin(p.x * 4.2 - speed * 1.2 + fi * 1.4);

        vec3 rCol = (i == 0) ? vec3(0.28, 0.78, 0.96)
                  : (i == 1) ? vec3(0.56, 0.42, 0.96)
                             : vec3(0.84, 0.44, 0.82);
        aurora += rCol * ribbon * shimmer;
        totalBand += ribbon;
    }

    vec3 col = base + aurora * (0.16 + 0.09 * work);
    float alpha = clamp(0.10 + totalBand * 0.075 + work * 0.05, 0.0, 0.28);
    return vec4(col, alpha);
}

// 3) ULTRA: Swirling cosmic solar vortex, subtle energy rays & twinkling starlight.
vec4 animUltra(vec2 uv, vec2 p, float aspect, float t, float work) {
    float speed = t * mix(0.75, 1.30, work);
    float r = length(p);
    float ang = atan(p.y, p.x);

    float swirl = sin(ang * 3.0 - r * 6.5 + speed * 1.1) * 0.5 + 0.5;
    float rays  = pow(0.5 + 0.5 * cos(ang * 6.0 + speed * 0.7 + sin(r * 4.5 - speed)), 2.4);
    float ring  = exp(-abs(r - (0.37 + 0.03 * sin(speed * 1.1))) * 7.0);
    float outerEnergy = smoothstep(0.10, 0.58, r);

    vec3 coreCol  = vec3(0.98, 0.76, 0.30);
    vec3 flareCol = vec3(0.95, 0.42, 0.34);
    vec3 prismCol = vec3(0.86, 0.48, 0.84);

    vec3 plasma = mix(flareCol, coreCol, swirl) * (0.14 * rays + 0.14 * ring) * (0.45 + 0.55 * outerEnergy);
    plasma += prismCol * swirl * exp(-r * 2.2) * 0.10;

    vec2 grid = uv * vec2(aspect * 16.0, 16.0);
    vec2 cell = floor(grid);
    vec2 f = fract(grid) - 0.5;
    float rnd = hash21(cell);
    float starMask = step(0.87, rnd);
    float twinkle = 0.50 + 0.50 * sin(speed * (1.6 + rnd * 2.2) + rnd * 25.0);
    float star = starMask * exp(-dot(f, f) * 40.0) * twinkle;

    vec3 base = vec3(0.06, 0.03, 0.05);
    vec3 col = base + plasma * (0.85 + 0.35 * work) + vec3(1.0, 0.92, 0.74) * star * 0.38;
    float alpha = clamp(0.11 + (rays * 0.07 + ring * 0.08 + star * 0.14) + work * 0.05, 0.0, 0.32);
    return vec4(col, alpha);
}

void main() {
    float aspect = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 p = (v_texCoord - 0.5) * vec2(aspect, 1.0);

    vec4 accum = vec4(0.0);
    if (u_effortWeights.x > 0.001) {
        accum += animNormal(v_texCoord, p, aspect, u_time, u_working) * u_effortWeights.x;
    }
    if (u_effortWeights.y > 0.001) {
        accum += animHigh(v_texCoord, p, aspect, u_time, u_working) * u_effortWeights.y;
    }
    if (u_effortWeights.z > 0.001) {
        accum += animUltra(v_texCoord, p, aspect, u_time, u_working) * u_effortWeights.z;
    }

    float centerSoft = smoothstep(0.06, 0.54, length(p * vec2(0.75, 1.05)));
    float readBoost = mix(0.65, 1.0, centerSoft);

    gl_FragColor = vec4(accum.rgb * readBoost, accum.a * u_fade * v_fragmentColor.a);
}
