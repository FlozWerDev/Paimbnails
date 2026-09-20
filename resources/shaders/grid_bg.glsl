#ifdef GL_ES
// NOTE: #extension must come before any non-preprocessor token (including
// the precision statement) or strict drivers reject the shader.
#extension GL_OES_standard_derivatives : enable
#ifndef GL_OES_standard_derivatives
#define PAIMON_NO_DERIVATIVES 1
#endif
precision mediump float;
// fwidth() needs OES_standard_derivatives on OpenGL ES 2.0: strict drivers
// (iOS) reject the shader when it is used without the extension. Where the
// extension is missing, PAIMON_NO_DERIVATIVES falls back to a texel-size
// anti-alias floor computed from u_texSize.
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;
uniform vec2 u_texSize;

void main() {
    vec2 uv = v_texCoord * 2.0 - 1.0;
    float t = u_time * 0.7;
    vec2 gridUv = uv * (8.0 + u_intensity * 6.0);
#ifdef PAIMON_NO_DERIVATIVES
    vec2 g = abs(fract(gridUv - 0.5) - 0.5)
        / max((8.0 + u_intensity * 6.0) * 2.0 / max(u_texSize, vec2(1.0)), vec2(1e-4));
#else
    vec2 g = abs(fract(gridUv - 0.5) - 0.5) / fwidth(gridUv);
#endif
    float line = 1.0 - min(min(g.x, g.y), 1.0);
    float sweep = exp(-6.0 * abs(uv.y + 0.8 - fract(t * 0.35) * 1.8));
    vec3 base = vec3(0.02, 0.04, 0.08);
    vec3 grid = vec3(0.1, 0.8, 1.0) * line;
    vec3 col = base + grid * 0.45 + vec3(0.08, 0.6, 0.9) * sweep * 0.35;
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
