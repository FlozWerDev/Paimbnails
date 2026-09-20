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
    vec2 uv = v_texCoord + vec2(sin(u_time * 0.3), cos(u_time * 0.25)) * 0.05;
    vec2 grid = uv * 12.0;
    vec2 cell = floor(grid);
#ifdef PAIMON_NO_DERIVATIVES
    vec2 fw = vec2(1.0 / max(u_texSize.x, 1.0), 1.0 / max(u_texSize.y, 1.0));
#else
    vec2 fw = max(fwidth(grid), vec2(1.0 / max(u_texSize.x, 1.0), 1.0 / max(u_texSize.y, 1.0)));
#endif
    vec2 cellUv = abs(fract(grid) - 0.5);
    float edge = 1.0 - smoothstep(0.5 - fw.x * 1.5, 0.5, max(cellUv.x, cellUv.y));
    float c = mod(cell.x + cell.y, 2.0);
    vec3 base = mix(vec3(0.05, 0.08, 0.14), vec3(0.20, 0.65, 0.95), c);
    vec3 col = base + edge * vec3(0.06, 0.08, 0.12);
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
