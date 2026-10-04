// gravitational lens: pixels bend around the cursor, click deepens the pull
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

void main() {
    vec2 delta = v_texCoord - u_cursor;
    float dist = length(delta);
    float t = mod(u_time, 628.318);

    float pull = (u_intensity * 0.02 + u_click * 0.04) / (dist + 0.05);
    pull *= smoothstep(0.65, 0.0, dist);
    vec2 dir = delta / max(dist, 1e-4);
    vec2 uv = clamp(v_texCoord - dir * pull, 0.0, 1.0);

    vec4 color = texture2D(u_texture, uv);

    float ringR = 0.09 + u_click * 0.05;
    float disk = smoothstep(0.03, 0.0, abs(dist - ringR));
    float spin = 0.5 + 0.5 * sin(atan(delta.y, delta.x) * 3.0 + t * 2.0);
    color.rgb += mix(vec3(1.0, 0.6, 0.2), vec3(1.0, 0.9, 0.6), spin) * disk * u_intensity * 0.14;

    color.rgb *= 1.0 - smoothstep(0.06, 0.0, dist) * 0.9;

    gl_FragColor = color * v_fragmentColor;
}
