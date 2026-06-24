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
    float stripes = sin(12.0 * a + 20.0 / max(r, 0.08) - u_time * 3.0);
    vec3 col = mix(vec3(0.01, 0.02, 0.05), vec3(0.7, 0.2, 1.0), 0.5 + 0.5 * stripes);
    col *= smoothstep(1.2, 0.05, r);
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
