#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    vec2 uv = v_texCoord * 2.0 - 1.0;
    float t = u_time * 0.18;
    float n = noise(uv * 2.4 + t) * 0.55 + noise(uv * 5.2 - t * 1.7) * 0.30 + noise(uv * 9.5 + t * 0.7) * 0.15;
    vec3 base = mix(vec3(0.03, 0.02, 0.07), vec3(0.35, 0.10, 0.55), n);
    vec3 accent = mix(vec3(0.1, 0.35, 0.9), vec3(0.95, 0.35, 0.6), noise(uv * 3.3 - t));
    vec3 col = mix(base, accent, smoothstep(0.45, 0.95, n) * (0.4 + 0.4 * u_intensity));
    float stars = step(0.9965 - u_intensity * 0.0015, hash(floor((uv + 1.0) * 120.0)));
    col += vec3(stars);
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
