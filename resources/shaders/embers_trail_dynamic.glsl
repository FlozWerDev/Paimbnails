// embers rise and flicker from the cursor; click fans the flames
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;
uniform vec2 u_cursor;
uniform float u_click;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    vec2 uv = v_texCoord;
    vec4 color = texture2D(u_texture, uv);
    float t = mod(u_time, 300.0);

    vec2 delta = uv - u_cursor;
    float field = smoothstep(0.5, 0.0, length(delta));

    float embers = 0.0;
    for (int i = 0; i < 10; i++) {
        float fi = float(i);
        float seed = hash(vec2(fi, 3.0));
        float life = fract(t * (0.4 + seed * 0.5) + seed);
        vec2 pos = u_cursor + vec2(sin((seed + t * 0.5) * 6.28) * 0.08 * life, life * 0.4);
        float d = length(uv - pos);
        float sz = (0.012 - life * 0.008) * (1.0 + u_click);
        embers += smoothstep(sz, 0.0, d) * (1.0 - life);
    }

    vec3 fire = mix(vec3(1.0, 0.9, 0.3), vec3(1.0, 0.25, 0.05), 1.0 - field);
    color.rgb += fire * embers * u_intensity * 0.16;
    color.rgb += vec3(1.0, 0.4, 0.1) * field * u_click * u_intensity * 0.04;

    gl_FragColor = color * v_fragmentColor;
}
