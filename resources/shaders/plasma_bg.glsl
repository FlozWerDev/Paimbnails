#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord * 2.0 - 1.0;
    float t = u_time * (0.6 + u_intensity * 0.4);
    float v = 0.0;
    v += sin((uv.x * 4.0 + t));
    v += sin((uv.y * 4.5 - t * 1.2));
    v += sin((uv.x + uv.y) * 4.0 + t * 0.7);
    v += sin(sqrt(dot(uv, uv)) * 10.0 - t * 1.8);
    v *= 0.25;
    vec3 a = vec3(0.12, 0.03, 0.35);
    vec3 b = vec3(0.95, 0.25, 0.70);
    vec3 c = vec3(0.20, 0.80, 1.00);
    vec3 col = mix(a, b, smoothstep(-1.0, 0.2, v));
    col = mix(col, c, smoothstep(0.1, 1.0, v));
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
