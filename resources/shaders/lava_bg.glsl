#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord * 2.0 - 1.0;
    float t = u_time * 0.5;
    float v = sin(uv.x * 4.0 + t) + sin(uv.y * 5.0 - t * 1.3) + sin((uv.x + uv.y) * 6.0 + t * 0.7);
    v = 0.5 + 0.5 * sin(v);
    vec3 col = mix(vec3(0.08, 0.01, 0.00), vec3(1.0, 0.35, 0.05), v);
    col += vec3(1.0, 0.85, 0.2) * pow(v, 6.0);
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
