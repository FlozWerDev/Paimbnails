varying vec2 v_texCoord;

uniform sampler2D u_src;
uniform sampler2D u_guide;
uniform sampler2D u_var;
uniform vec2  u_texel;
uniform float u_stride;
uniform float u_phi;
uniform float u_wide;

// variance ~0 on stable history, high on disocclusions.
const float kVarCeil = 0.12;
const float kVarGain = 7.0;

float bspline(int i) {
    return i == 0 ? 0.5 : 0.25;
}

float bsplineW(int i) {
    int a = abs(i);
    if (a == 0) return 0.375;
    if (a == 1) return 0.25;
    return 0.0625;
}

float sanitizeVar(float v) {
    if (!(v == v)) return 0.0;
    return clamp(v, 0.0, 4.0);
}

void main() {
    vec2 uv = v_texCoord;
    vec3 center = texture2D(u_guide, uv).rgb;
    float centerL = luma(center);
    float phi = max(u_phi, 0.001);

    float vv = sanitizeVar(texture2D(u_var, uv).r);
    float blur = safeSmoothstep(0.0, kVarCeil, vv);
    phi = max(phi / (1.0 + blur * kVarGain), 8.0);

    bool wide = u_wide > 0.5;
    int R = wide ? 2 : 1;

    vec4 sum = vec4(0.0);
    float wsum = 0.0;

    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            if (abs(x) > R || abs(y) > R) continue;
            vec2 o = vec2(float(x), float(y)) * u_texel * u_stride;
            vec3 guide = texture2D(u_guide, uv + o).rgb;
            float dl = abs(luma(guide) - centerL);
            float dc = distance(guide, center);
            float b = wide ? bsplineW(x) * bsplineW(y) : bspline(x) * bspline(y);
            float w = b * exp(-dl * phi - dc * phi * 0.5);
            sum += texture2D(u_src, uv + o) * w;
            wsum += w;
        }
    }

    gl_FragColor = sum / max(wsum, 0.0001);
}
