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
    vec2 delta = v_texCoord - u_cursor;
    float dist = length(delta);
    float t = mod(u_time, 628.318);

    float force = u_intensity * 0.06 / (dist * 5.0 + 0.2);
    force *= smoothstep(0.7, 0.0, dist);
    float direction = mix(1.0, -1.5, u_click);

    vec2 dir = delta / max(dist, 1e-4);
    vec2 displacement = dir * force * direction;
    displacement *= 1.0 + 0.2 * sin(t * 3.0 + dist * 10.0);
    uv = clamp(uv - displacement, 0.0, 1.0);

    vec4 color = texture2D(u_texture, uv);
    float glow = smoothstep(0.15, 0.0, dist) * u_intensity * 0.2;
    vec3 glowColor = mix(vec3(0.2, 0.5, 1.0), vec3(1.0, 0.3, 0.2), u_click);
    color.rgb += glowColor * glow;

    gl_FragColor = color * v_fragmentColor;
}
