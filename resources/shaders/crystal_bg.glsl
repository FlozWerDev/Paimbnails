#ifdef GL_ES
// NOTE: #extension first; strict drivers reject it after any other token.
#extension GL_OES_standard_derivatives : enable
#ifndef GL_OES_standard_derivatives
#define PAIMON_NO_DERIVATIVES 1
#endif
precision mediump float;
// fwidth needs the ext on ES 2.0; without it use a texel-size floor.
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
