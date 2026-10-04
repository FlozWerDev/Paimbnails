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
    vec2 delta = uv - u_cursor;
    float dist = length(delta);
    float t = mod(u_time, 628.318);

    float radius = 0.25 + u_click * 0.15;
    float falloff = smoothstep(radius, 0.0, dist);

    float angle = falloff * u_intensity * 0.4 * sin(t * 1.5);
    float s = sin(angle);
    float c = cos(angle);
    vec2 rotDelta = vec2(delta.x * c - delta.y * s, delta.x * s + delta.y * c);
    rotDelta *= 1.0 + falloff * u_click * 0.3;

    float ripple = sin(dist * 30.0 - t * 4.0) * 0.005 * u_intensity * 0.1 * falloff;
    vec2 dir = delta / max(dist, 1e-4);
    uv = clamp(u_cursor + rotDelta + dir * ripple, 0.0, 1.0);

    vec4 color = texture2D(u_texture, uv);
    float sheen = falloff * 0.15 * u_intensity * 0.1;
    color.rgb += (0.5 + 0.5 * sin(vec3(dist * 20.0 + t, dist * 20.0 + t + 2.0, dist * 20.0 + t + 4.0))) * sheen;

    gl_FragColor = color * v_fragmentColor;
}
