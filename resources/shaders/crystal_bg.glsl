#ifdef GL_ES
precision mediump float;
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
    float fw = max(fwidth(facetLine), 1.0 / max(u_texSize.x, 1.0));
    float facets = 1.0 - smoothstep(0.18 - fw, 0.22 + fw, facetLine);
    vec3 col = vec3(0.05, 0.08, 0.16) + vec3(0.4, 0.9, 1.0) * facets;
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
