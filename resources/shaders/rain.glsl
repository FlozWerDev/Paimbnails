#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

float rHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float rainLayer(vec2 uv, float speed, float density, float layer) {
    vec2 r = uv * vec2(density, 1.0);
    r.y += u_time * speed + rHash(vec2(floor(r.x), layer)) * 100.0;
    float drop = smoothstep(0.0, 0.02, fract(r.y * 0.1) - 0.97);
    float mask = smoothstep(0.45, 0.5, abs(fract(r.x) - 0.5));
    return drop * (1.0 - mask);
}

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float str = u_intensity * 0.15;
    // 3 layers unrolled
    float rain = rainLayer(v_texCoord, 4.0, 80.0, 0.0)
               + rainLayer(v_texCoord, 6.0, 120.0, 1.0) * 0.75
               + rainLayer(v_texCoord, 8.0, 160.0, 2.0) * 0.5;
    color.rgb += vec3(0.7, 0.8, 1.0) * rain * str * str;
    color.rgb *= 1.0 - str * 0.2;
    gl_FragColor = color * v_fragmentColor;
}
