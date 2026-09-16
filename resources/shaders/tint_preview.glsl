#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform sampler2D u_roleMask;
uniform vec3 u_color1;
uniform vec3 u_color2;
uniform vec3 u_detailColor;
uniform vec3 u_glowColor;
uniform float u_brightness;
uniform float u_saturation;
uniform float u_contrast;
uniform float u_darkThreshold;
uniform float u_glowReplace;
uniform float u_applyDetail;

// GPU mirror of packgen::tintPixelFast: same op order in the 0..255 domain,
// floor(x + 0.5) instead of lround (identical for non-negative values;
// negatives clamp to 0 either way). Preview only: the export bake stays on
// the CPU kernel, so shipped packs remain bit-identical.
float rec601(vec3 c) {
    return 0.30 * c.r + 0.59 * c.g + 0.11 * c.b;
}

vec3 tintByLum(vec3 src, vec3 tint) {
    float factor = rec601(src) / u_brightness;
    vec3 f = clamp(tint * factor, 0.0, 255.0);
    if (u_saturation != 1.0) {
        float luma = rec601(f);
        f = luma + (f - luma) * u_saturation;
    }
    if (u_contrast != 0.0) {
        f = (f - 127.5) * (1.0 + u_contrast) + 127.5;
    }
    return clamp(floor(f + 0.5), 0.0, 255.0);
}

// Mirror of blendPixelFast; w is the mask byte (0..255) recovered from the
// NEAREST-sampled mask, so the == 0 / == 255 fast paths stay exact.
void blendRole(inout vec3 base, vec3 tinted, float w, float replaceFlag) {
    if (w < 0.5) return;
    if (replaceFlag > 0.5 || w > 254.5) {
        base = tinted;
    } else {
        float alpha = w / 255.0;
        base = floor(tinted * alpha + base * (1.0 - alpha) + 0.5);
    }
}

void main() {
    vec4 tex = texture2D(u_texture, v_texCoord);
    vec4 m = texture2D(u_roleMask, v_texCoord);
    vec3 src = tex.rgb * 255.0;
    float srcA = floor(tex.a * 255.0 + 0.5);
    float wC1 = floor(m.r * 255.0 + 0.5);
    float wC2 = floor(m.g * 255.0 + 0.5);
    float wDet = floor(m.b * 255.0 + 0.5);
    float wGlow = floor(m.a * 255.0 + 0.5);

    // Untouched pixels keep the verbatim copy, like the kernel's dst.
    vec3 base = src;
    float maxW = 0.0;
    bool gated = srcA < 0.5 ||
        (u_darkThreshold > 0.5 && rec601(src) < u_darkThreshold);
    if (!gated) {
        if (wC1 > 0.5) {
            blendRole(base, tintByLum(src, u_color1), wC1, 0.0);
            maxW = max(maxW, wC1);
        }
        if (wC2 > 0.5) {
            blendRole(base, tintByLum(src, u_color2), wC2, 0.0);
            maxW = max(maxW, wC2);
        }
        if (u_applyDetail > 0.5 && wDet > 0.5) {
            blendRole(base, tintByLum(src, u_detailColor), wDet, 0.0);
            maxW = max(maxW, wDet);
        }
        if (wGlow > 0.5) {
            blendRole(base, tintByLum(src, u_glowColor), wGlow, u_glowReplace);
            maxW = max(maxW, wGlow);
        }
    }

    float outA = max(srcA, maxW);
    gl_FragColor = vec4(base / 255.0, outA / 255.0) * v_fragmentColor;
}
