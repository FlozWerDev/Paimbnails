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
// anti-alias floor.
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;
uniform vec2 u_texSize;

void main() {
    vec2 grid = v_texCoord * 6.0;
    vec2 uv = abs(fract(grid) - 0.5);
    float facetLine = abs(uv.x - uv.y);
#ifdef PAIMON_NO_DERIVATIVES
    float fw = 1.0 / max(u_texSize.x, 1.0);
#else
    float fw = max(fwidth(facetLine), 1.0 / max(u_texSize.x, 1.0));
#endif
    float facets = 1.0 - smoothstep(0.18 - fw, 0.22 + fw, facetLine);
    vec3 col = vec3(0.05, 0.08, 0.16) + vec3(0.4, 0.9, 1.0) * facets;
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
