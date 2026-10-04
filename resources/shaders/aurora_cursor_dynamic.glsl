// aurora ribbons bend toward the cursor; click brightens the curtain
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
    vec2 uv = v_texCoord;
    vec4 color = texture2D(u_texture, uv);
    float t = mod(u_time, 628.318);

    float pull = smoothstep(0.7, 0.0, length(uv - u_cursor));
    float band = 0.0;
    for (int i = 0; i < 4; i++) {
        float fi = float(i);
        float base = 0.3 + fi * 0.18 + mix(0.0, (u_cursor.y - 0.5), pull) * 0.4;
        float wobble = sin(uv.x * 6.0 + t * (0.6 + fi * 0.2) + fi) * 0.06
                     + sin(uv.x * 13.0 - t * 0.4) * 0.02;
        float d = abs(uv.y - (base + wobble));
        band += smoothstep(0.09, 0.0, d) * (0.6 + 0.4 * sin(t + fi));
    }

    vec3 aur = mix(vec3(0.1, 0.9, 0.5), vec3(0.4, 0.3, 1.0), uv.y);
    float glow = band * u_intensity * 0.12 * (0.7 + u_click * 0.6);
    color.rgb += aur * glow;

    gl_FragColor = color * v_fragmentColor;
}
