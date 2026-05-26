#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord;
    float t = u_time * 0.2;
    float n = sin(uv.x * 6.0 + t) + sin(uv.y * 7.0 - t * 1.2) + sin((uv.x + uv.y) * 9.0 + t * 0.8);
    n += cos(uv.x * 12.0 - t * 0.5) * 0.5;
    n = 0.5 + 0.5 * n / 3.5;
    vec3 sky = vec3(0.15, 0.28, 0.55);
    vec3 cloud = vec3(0.9, 0.95, 1.0);
    vec3 col = mix(sky, cloud, smoothstep(0.45, 0.8, n));
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
