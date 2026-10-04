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

    float rippleFreq = 20.0 + u_intensity * 5.0;
    float ripple = sin(dist * rippleFreq - t * 4.0) * 0.5 + 0.5;

    float fade = smoothstep(0.6, 0.0, dist);
    float strength = u_intensity * 0.015 * fade * ripple;

    float clickRipple = sin(dist * rippleFreq * 1.5 - t * 8.0) * 0.5 + 0.5;
    strength += u_click * u_intensity * 0.025 * fade * clickRipple;

    vec2 dir = delta / max(dist, 1e-4);
    uv = clamp(uv + dir * strength, 0.0, 1.0);

    vec4 color = texture2D(u_texture, uv);
    float caustic = ripple * fade * 0.3;
    color.rgb += vec3(0.1, 0.2, 0.4) * caustic * u_intensity * 0.06;

    gl_FragColor = color * v_fragmentColor;
}
