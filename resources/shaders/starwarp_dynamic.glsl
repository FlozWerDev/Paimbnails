// warp-speed starfield streaking out from the cursor as a vanishing point
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
    float dist = length(delta);
    float ang = atan(delta.y, delta.x);

    float speed = 0.3 + u_intensity * 0.08 + u_click * 0.4;
    float stars = 0.0;
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        float lane = floor(ang * (18.0 + fi * 6.0) / 6.2832);
        float seed = hash(vec2(lane, fi));
        float trav = fract(seed + t * speed * (0.5 + seed));
        float r = trav * 0.9;
        float streak = smoothstep(0.02, 0.0, abs(dist - r)) * smoothstep(0.9, 0.2, r);
        stars += streak * (0.4 + seed);
    }
    color.rgb += vec3(0.8, 0.85, 1.0) * stars * u_intensity * 0.1;

    gl_FragColor = color * v_fragmentColor;
}
