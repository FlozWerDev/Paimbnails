#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord - 0.5;
    float r = length(uv);
    float pulse = 0.5 + 0.5 * sin(u_time * 3.0 - r * 18.0);
    vec3 col = mix(vec3(0.03, 0.03, 0.09), vec3(1.0, 0.15, 0.6), pulse);
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
