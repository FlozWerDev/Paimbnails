// a soft glass blob refracts the background under the cursor with a rim highlight
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

    float radius = 0.2 + u_click * 0.08 + sin(t * 1.5) * 0.01;
    float h = smoothstep(radius, 0.0, dist);
    float lens = h * h * (u_intensity * 0.08 + 0.04);

    vec2 dir = delta / max(dist, 1e-4);
    vec2 uv = clamp(v_texCoord - dir * lens, 0.0, 1.0);
    vec4 color = texture2D(u_texture, uv);

    float rim = smoothstep(radius, radius - 0.05, dist) * smoothstep(radius - 0.09, radius - 0.05, dist);
    color.rgb += vec3(0.8, 0.9, 1.0) * rim * u_intensity * 0.1;

    vec2 hp = u_cursor - dir * radius * 0.45;
    float spec = smoothstep(0.06, 0.0, length(v_texCoord - hp)) * h;
    color.rgb += vec3(1.0) * spec * 0.25;

    gl_FragColor = color * v_fragmentColor;
}
