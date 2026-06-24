#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    float t = u_time * 0.4;
    float hue = v_texCoord.x + v_texCoord.y * 0.5 + t;
    vec3 col = hsv2rgb(vec3(fract(hue), 0.85, 1.0));

    float edge = smoothstep(0.0, 0.15, v_texCoord.y) * smoothstep(1.0, 0.85, v_texCoord.y);
    edge *= smoothstep(0.0, 0.08, v_texCoord.x) * smoothstep(1.0, 0.92, v_texCoord.x);

    float pulse = 0.75 + 0.25 * sin(t * 2.0 + v_texCoord.x * 6.2832);
    col *= edge * pulse;

    gl_FragColor = vec4(col, edge) * v_fragmentColor;
}
