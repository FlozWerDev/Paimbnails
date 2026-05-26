#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord * 2.0 - 1.0;
    float p1 = sin((uv.x * 40.0 + uv.y * 10.0) + u_time);
    float p2 = sin((uv.x * 12.0 - uv.y * 38.0) - u_time * 0.7);
    float m = 0.5 + 0.5 * (p1 * p2);
    vec3 col = mix(vec3(0.04, 0.02, 0.08), vec3(0.9, 0.3, 1.0), m);
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
