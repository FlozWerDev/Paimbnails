#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord;
    float n = sin(uv.x * 14.0 + u_time * 0.6) * sin(uv.y * 13.0 - u_time * 0.4);
    n += sin((uv.x + uv.y) * 17.0 + u_time * 0.3) * 0.5;
    n = 0.5 + 0.5 * n;
    vec3 col = mix(vec3(0.05, 0.06, 0.10), vec3(0.45, 0.7, 1.0), n);
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
