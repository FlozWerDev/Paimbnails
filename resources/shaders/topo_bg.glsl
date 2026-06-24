#ifdef GL_ES
precision mediump float;
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
    float fw = max(fwidth(h * 2.0), 1.0 / max(u_texSize.y, 1.0));
    float topo = smoothstep(0.48 - fw, 0.5 + fw, lines);
    vec3 col = vec3(0.02, 0.07, 0.06) + vec3(0.15, 0.95, 0.7) * topo;
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
