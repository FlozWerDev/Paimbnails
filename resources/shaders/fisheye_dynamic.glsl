// magnifier bubble: pixels near the cursor are pushed outward for a convex zoom
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

    float radius = 0.22 + u_click * 0.06 + sin(t * 1.2) * 0.008;
    float zoom = u_intensity * 0.05 + 0.08;

    float f = smoothstep(radius, 0.0, dist);
    float scale = 1.0 - f * zoom;
    vec2 uv = clamp(u_cursor + delta * scale, 0.0, 1.0);
    vec4 color = texture2D(u_texture, uv);

    float rim = smoothstep(radius, radius - 0.03, dist) * smoothstep(radius - 0.07, radius - 0.03, dist);
    color.rgb += vec3(0.7, 0.85, 1.0) * rim * u_intensity * 0.08;

    gl_FragColor = color * v_fragmentColor;
}
