#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord;
    float y = uv.y + sin(uv.x * 12.0 + u_time) * 0.08 + sin(uv.x * 24.0 - u_time * 1.7) * 0.03;
    float wave = 0.5 + 0.5 * sin(y * 28.0 - u_time * 2.5);
    vec3 col = mix(vec3(0.02, 0.08, 0.18), vec3(0.12, 0.65, 0.95), wave);
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
