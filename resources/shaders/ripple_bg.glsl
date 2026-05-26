#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord;
    vec2 c1 = vec2(0.35 + 0.1 * sin(u_time * 0.7), 0.45);
    vec2 c2 = vec2(0.65 + 0.1 * cos(u_time * 0.6), 0.55);
    float r1 = sin(length(uv - c1) * 40.0 - u_time * 3.0);
    float r2 = sin(length(uv - c2) * 36.0 - u_time * 2.5);
    float v = 0.5 + 0.25 * (r1 + r2);
    vec3 col = mix(vec3(0.02, 0.06, 0.10), vec3(0.25, 0.70, 1.0), v);
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
