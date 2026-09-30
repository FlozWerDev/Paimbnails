// svgf-lite temporal accumulation with layer-transform reprojection.

varying vec2 v_texCoord;

uniform sampler2D u_current;
uniform sampler2D u_history;
uniform sampler2D u_histVar;
uniform vec2  u_texel;
uniform float u_temporal;
uniform float u_clampSigma;
uniform vec2  u_reprojNow;
uniform vec2  u_reprojPrev;
uniform float u_reprojScale;
// 0 = invalid history, 1 = valid reprojection.
uniform float u_historyValid;
// 0 = color, 1 = writes variance to r.
uniform float u_outVariance;

// ldr ceiling; reset on converged noise.
const float kVarMax   = 4.0;
const float kVarReset = 1.0;

// equal(c,c) detects nan; if avoids mix propagation.
vec3 sanitizeColor(vec3 c) {
    if (!all(equal(c, c))) return vec3(0.0);
    return clamp(c, vec3(0.0), vec3(kVarMax));
}

float sanitizeVar(float v) {
    if (!(v == v)) return 0.0;
    return clamp(v, 0.0, kVarMax);
}

void main() {
    vec2 uv = v_texCoord;
    vec4 current = texture2D(u_current, uv);

    vec2 histUV = (uv - u_reprojNow) * u_reprojScale + u_reprojPrev;
    bool varPass = u_outVariance > 0.5;
    if (histUV.x < 0.0 || histUV.x > 1.0 || histUV.y < 0.0 || histUV.y > 1.0) {
        if (varPass) {
            gl_FragColor = vec4(kVarReset, 0.0, 0.0, 1.0);
        } else {
            gl_FragColor = current;
        }
        return;
    }
    if (varPass && u_historyValid < 0.5) {
        gl_FragColor = vec4(kVarReset, 0.0, 0.0, 1.0);
        return;
    }

    vec4 histRaw = texture2D(u_history, histUV);

    vec2 vpx = (histUV - uv) / max(u_texel, vec2(0.0000001));
    float adapt = exp(-length(vpx) * 0.12);
    float fb = clamp(u_temporal, 0.0, 0.97) * mix(0.30, 1.0, adapt);

    if (varPass) {
        vec3 d = sanitizeColor(current.rgb) - sanitizeColor(histRaw.rgb);
        float dist2 = min(dot(d, d) * 0.3333333, kVarMax);
        float hv = sanitizeVar(texture2D(u_histVar, histUV).r);
        gl_FragColor = vec4(mix(dist2, hv, fb), 0.0, 0.0, 1.0);
        return;
    }

    vec4 m1 = vec4(0.0);
    vec4 m2 = vec4(0.0);
    vec4 mn = current;
    vec4 mx = current;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec4 s = texture2D(u_current, uv + vec2(float(x), float(y)) * u_texel);
            m1 += s;
            m2 += s * s;
            mn = min(mn, s);
            mx = max(mx, s);
        }
    }
    m1 /= 9.0;
    m2 /= 9.0;
    vec4 sigma = sqrt(max(m2 - m1 * m1, vec4(0.0)));

    vec4 hist = histRaw;
    if (u_clampSigma > 0.0) {
        // intersection never empty: mn <= m1 <= mx.
        vec4 lo = max(mn, m1 - sigma * u_clampSigma);
        vec4 hi = min(mx, m1 + sigma * u_clampSigma);
        hist = clamp(hist, lo, hi);
    }

    gl_FragColor = mix(current, hist, fb);
}
