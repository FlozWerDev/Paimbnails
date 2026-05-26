#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord * 2.0 - 1.0;
    uv += 0.08 * vec2(sin(uv.y * 8.0 + u_time), cos(uv.x * 8.0 - u_time));
    float bands = sin(uv.x * 10.0) + cos(uv.y * 10.0) + sin((uv.x + uv.y) * 8.0 - u_time * 2.0);
    vec3 col = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + bands + u_time * 0.4);
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
