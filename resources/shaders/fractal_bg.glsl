#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 z = v_texCoord * 2.0 - 1.0;
    vec2 c = vec2(-0.4 + 0.1 * sin(u_time * 0.3), 0.6 + 0.1 * cos(u_time * 0.2));
    float m = 0.0;
    for (int i = 0; i < 12; ++i) {
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        m += exp(-3.0 * dot(z, z));
    }
    vec3 col = vec3(0.1, 0.2, 0.5) * m + vec3(0.7, 0.2, 1.0) * m * m;
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
