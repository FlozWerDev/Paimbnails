#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
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
uniform float u_strength;
uniform float u_glowStrength;
uniform float u_premultiplied;
uniform float u_maskScaleX;
uniform float u_maskScaleY;

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
    vec4 m = texture2D(u_roleMask, v_texCoord * vec2(u_maskScaleX, u_maskScaleY));
    vec3 src = tex.rgb * 255.0;
    if (u_premultiplied > 0.5 && tex.a > 0.0) src /= tex.a;
    float srcA = floor(tex.a * 255.0 + 0.5);
    float wC1 = floor(m.r * 255.0 + 0.5);
    float wC2 = floor(m.g * 255.0 + 0.5);
    float wDet = floor(m.b * 255.0 + 0.5);
    float wGlow = floor(m.a * 255.0 + 0.5);

    vec3 base = src;
    bool gated = srcA < 0.5 ||
        (u_darkThreshold > 0.5 && rec601(src) < u_darkThreshold);
    if (!gated) {
        if (wC1 > 0.5) {
            blendRole(base, tintByLum(src, u_color1), wC1, 0.0);
        }
        if (wC2 > 0.5) {
            blendRole(base, tintByLum(src, u_color2), wC2, 0.0);
        }
        if (u_applyDetail > 0.5 && wDet > 0.5) {
            blendRole(base, tintByLum(src, u_detailColor), wDet, 0.0);
        }
        if (wGlow > 0.5) {
            vec3 beforeGlow = base;
            blendRole(base, tintByLum(src, u_glowColor), wGlow, u_glowReplace);
            base = mix(beforeGlow, base, u_glowStrength);
        }
    }

    vec3 rgb = mix(src, base, u_strength) / 255.0;
    if (u_premultiplied > 0.5) rgb *= tex.a;
    gl_FragColor = vec4(rgb, tex.a) * v_fragmentColor;
}
