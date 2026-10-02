varying vec2 v_texCoord;

uniform sampler2D u_current;
uniform sampler2D u_history;
uniform sampler2D u_histVar;
uniform sampler2D u_guideNow;
uniform sampler2D u_guidePrev;
uniform vec2  u_texel;
uniform float u_temporal;
uniform float u_clampSigma;
uniform vec3  u_reprojRow0;
uniform vec3  u_reprojRow1;
uniform float u_historyValid;
uniform float u_outVariance;

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

    vec2 histUV = vec2(dot(u_reprojRow0, vec3(uv, 1.0)),
                       dot(u_reprojRow1, vec3(uv, 1.0)));
    bool varPass = u_outVariance > 0.5;
    vec2 border = u_texel * 0.5;
    if (u_historyValid < 0.5 || any(lessThan(histUV, border))
        || any(greaterThan(histUV, vec2(1.0) - border))) {
        if (varPass) {
            gl_FragColor = vec4(kVarReset, 0.0, 0.0, 1.0);
        } else {
            gl_FragColor = current;
        }
        return;
    }
    vec4 histRaw = texture2D(u_history, histUV);
    vec3 guideNow = texture2D(u_guideNow, uv).rgb;
    vec3 guidePrev = texture2D(u_guidePrev, histUV).rgb;
    float agreement = exp(-distance(guideNow, guidePrev) * 24.0
                          - abs(luma(guideNow) - luma(guidePrev)) * 16.0);

    vec2 vpx = (histUV - uv) / max(u_texel, vec2(0.0000001));
    float adapt = exp(-length(vpx) * 0.12);
    float fb = clamp(u_temporal, 0.0, 0.97) * adapt * agreement;

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
    if (u_clampSigma >= 0.0) {
        // intersection never empty: mn <= m1 <= mx.
        vec4 lo = max(mn, m1 - sigma * u_clampSigma);
        vec4 hi = min(mx, m1 + sigma * u_clampSigma);
        hist = clamp(hist, lo, hi);
    }

    gl_FragColor = mix(current, hist, fb);
}
