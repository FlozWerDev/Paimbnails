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
    vec2 uv = v_texCoord * 2.0 - 1.0;
    float h = sin(uv.x * 6.0 + u_time * 0.3) + cos(uv.y * 7.0 - u_time * 0.4) + sin((uv.x + uv.y) * 5.0);
    float lines = abs(fract(h * 2.0) - 0.5);
#ifdef PAIMON_NO_DERIVATIVES
    float fw = 1.0 / max(u_texSize.y, 1.0);
#else
    float fw = max(fwidth(h * 2.0), 1.0 / max(u_texSize.y, 1.0));
#endif
    float topo = smoothstep(0.48 - fw, 0.5 + fw, lines);
    vec3 col = vec3(0.02, 0.07, 0.06) + vec3(0.15, 0.95, 0.7) * topo;
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
