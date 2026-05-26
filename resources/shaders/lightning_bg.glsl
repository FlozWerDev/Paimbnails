#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord;
    float bolt = abs(uv.x - (0.5 + 0.12 * sin(uv.y * 16.0 + u_time * 3.0)));
    float line = exp(-120.0 * bolt);
    vec3 col = vec3(0.03, 0.03, 0.08) + vec3(0.6, 0.8, 1.0) * line;
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
