#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord * 2.0 - 1.0;
    float t = u_time * 0.35;
    vec3 col = vec3(0.02, 0.03, 0.08);
    float wave1 = sin(uv.x * 2.7 + t) * 0.25 + 0.25;
    float wave2 = cos(uv.x * 4.1 - t * 1.4) * 0.18 + 0.18;
    float band = smoothstep(wave1 + wave2 - 0.25, wave1 + wave2 + 0.35, uv.y + 0.2);
    col += vec3(0.10, 0.80, 0.55) * band * (0.55 + 0.45 * sin(t + uv.x * 3.0));
    col += vec3(0.35, 0.25, 0.95) * band * (0.45 + 0.55 * cos(t * 1.3 - uv.x * 2.0));
    float glow = exp(-3.5 * length(uv + vec2(0.0, 0.15)));
    col += vec3(0.08, 0.12, 0.25) * glow * (0.6 + u_intensity * 0.4);
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
