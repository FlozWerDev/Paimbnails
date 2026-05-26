#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord - 0.5;
    float a = atan(uv.y, uv.x);
    vec3 col = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + a * 3.0 + u_time * 0.5);
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
