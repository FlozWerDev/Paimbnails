#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord * 2.0 - 1.0;
    float r = length(uv);
    float a = atan(uv.y, uv.x);
    float spiral = sin(a * 6.0 - r * 18.0 + u_time * 2.0);
    vec3 col = mix(vec3(0.02, 0.05, 0.10), vec3(0.75, 0.15, 0.95), 0.5 + 0.5 * spiral);
    col *= 1.0 - smoothstep(0.7, 1.2, r);
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
