#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

float hex(vec2 p) {
    p = abs(p);
    return max(dot(p, normalize(vec2(1.0, 1.73))), p.x);
}

void main() {
    vec2 uv = v_texCoord * 2.0 - 1.0;
    vec2 gv = fract(uv * 6.0) - 0.5;
    float h = smoothstep(0.52, 0.48, hex(gv));
    vec3 col = vec3(0.03, 0.04, 0.09) + vec3(0.0, 0.9, 0.7) * h * (0.7 + 0.3 * sin(u_time + uv.x * 4.0));
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
