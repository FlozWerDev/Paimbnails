#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord - 0.5;
    float g1 = exp(-18.0 * length(uv - vec2(0.25 * sin(u_time), 0.25 * cos(u_time * 0.8))));
    float g2 = exp(-18.0 * length(uv - vec2(0.25 * cos(u_time * 0.7), 0.25 * sin(u_time * 1.1))));
    vec3 col = vec3(0.02, 0.02, 0.05) + vec3(1.0, 0.3, 0.7) * g1 + vec3(0.2, 0.8, 1.0) * g2;
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
