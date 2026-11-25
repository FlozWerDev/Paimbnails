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

// RGBA4444 role mask: r = body, g = detail, b = glow

uniform sampler2D u_roleMask;
uniform vec3 u_tonePrimary;
uniform vec3 u_toneSecondary;
uniform vec3 u_toneBlue;
uniform vec3 u_tonePink;
uniform vec3 u_toneRed;
uniform vec3 u_toneSurface;
uniform vec3 u_toneNeutral;
uniform vec3 u_toneBackground;
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

// backgrounds: no mask, the whole texture takes the background tone.

uniform float u_fullPaint;

float rec601(vec3 c) {
    return 0.30 * c.r + 0.59 * c.g + 0.11 * c.b;
}

vec2 lightFactors(float lum) {
    float b = max(u_brightness, 1.0);
    if (lum <= b) return vec2(lum / b, 0.0);
    float t = clamp((lum - b) / max(255.0 - b, 1.0), 0.0, 1.0);
    t = t * t * (3.0 - 2.0 * t);
    return vec2(1.0 - t, 255.0 * t);
}

vec3 tintByLum(vec2 light, vec3 tint) {
    vec3 f = tint * light.x + light.y;
    if (u_saturation != 1.0) {
        float luma = rec601(f);
        f = luma + (f - luma) * u_saturation;
    }
    if (u_contrast != 0.0) {
        f = (f - 127.5) * (1.0 + u_contrast) + 127.5;
    }
    return clamp(floor(f + 0.5), 0.0, 255.0);
}

// Order matches LiveTone in LiveMaskAnalyzer.hpp.
vec3 toneForCode(float code) {
    if (code < 0.5) return u_tonePrimary;
    if (code < 1.5) return u_toneSecondary;
    if (code < 2.5) return u_toneBlue;
    if (code < 3.5) return u_tonePink;
    if (code < 4.5) return u_toneRed;
    if (code < 5.5) return u_toneSurface;
    if (code < 6.5) return u_toneNeutral;
    return u_toneBackground;
}

void main() {
    vec4 tex = texture2D(u_texture, v_texCoord);
    vec3 originalRgb = tex.rgb * v_fragmentColor.rgb;
    float outAlpha = tex.a * v_fragmentColor.a;
    if (u_strength <= 0.0 || tex.a < 0.5 / 255.0) {
        gl_FragColor = vec4(originalRgb, outAlpha);
        return;
    }

    // Straight colors. Premultiplied sprites also premultiply their vertex color.
    bool premultiplied = u_premultiplied > 0.5;
    vec3 texRgb = premultiplied ? tex.rgb / tex.a : tex.rgb;
    vec3 vertexRgb = v_fragmentColor.rgb;
    if (premultiplied && v_fragmentColor.a > 0.0) vertexRgb /= v_fragmentColor.a;
    float vMax = max(max(vertexRgb.r, vertexRgb.g), vertexRgb.b);

    if (u_fullPaint > 0.5) {
        // GD tints gray background art with setColor. Keep the art's shading and
        // the tint's brightness, replace its (often cycling) hue with the tone.
        vec3 backdrop = u_toneBackground / 255.0 * (rec601(texRgb) * vMax);
        if (premultiplied) backdrop *= outAlpha;
        gl_FragColor = vec4(mix(originalRgb, backdrop, u_strength), outAlpha);
        return;
    }

    vec4 m = texture2D(u_roleMask, v_texCoord * vec2(u_maskScaleX, u_maskScaleY));
    float code = floor(m.a * 15.0 + 0.5);
    float body = m.r;
    if (code > 5.5 && code < 6.5) {
        // Gray art tinted by code (color previews, progress fills) keeps that color.
        float vMin = min(min(vertexRgb.r, vertexRgb.g), vertexRgb.b);
        float vSat = vMax > 0.0 ? (vMax - vMin) / vMax : 0.0;
        body *= 1.0 - smoothstep(0.15, 0.35, vSat);
    }
    float detailWeight = m.g * u_applyDetail;
    float glowWeight = m.b * u_glowStrength;
    if (u_glowReplace > 0.5 && m.b > 0.5 / 15.0) glowWeight = u_glowStrength;
    float paintWeight = body + detailWeight + glowWeight;
    if (paintWeight <= 0.0) {
        gl_FragColor = vec4(originalRgb, outAlpha);
        return;
    }
    // Dark protect judges the art itself, so dimmed sprites do not speckle.
    if (u_darkThreshold > 0.5 && rec601(texRgb * 255.0) < u_darkThreshold) {
        gl_FragColor = vec4(originalRgb, outAlpha);
        return;
    }

    // Brightness comes from what is on screen: texture times vertex color.
    vec3 src = clamp(texRgb * vertexRgb, 0.0, 1.0) * 255.0;
    float lum = rec601(src);
    vec2 light = lightFactors(lum);
    vec3 tintedSum = vec3(0.0);
    if (body > 0.0) tintedSum += tintByLum(light, toneForCode(code)) * body;
    if (detailWeight > 0.0) tintedSum += tintByLum(light, u_detailColor) * detailWeight;
    if (glowWeight > 0.0) tintedSum += tintByLum(light, u_glowColor) * glowWeight;

    // Weighted partition: role weights that add up to 100% leave no original hue.
    vec3 paintedRgb = tintedSum / max(paintWeight, 1.0 / 255.0) / 255.0;
    if (premultiplied) paintedRgb *= outAlpha;
    vec3 outRgb = mix(originalRgb, paintedRgb, u_strength * clamp(paintWeight, 0.0, 1.0));
    gl_FragColor = vec4(outRgb, outAlpha);
}
