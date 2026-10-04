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

    float waveRadius = fract(u_time * 0.8) * 1.2;
    float waveWidth = 0.08 + u_intensity * 0.01;
    float wave = smoothstep(waveRadius - waveWidth, waveRadius, dist)
               * smoothstep(waveRadius + waveWidth, waveRadius, dist);

    float strength = u_intensity * 0.04 * wave * (0.3 + u_click * 0.7);
    vec2 dir = delta / max(dist, 1e-4);
    uv += dir * strength;

    float wave2Radius = fract(u_time * 0.8 - 0.3) * 1.2;
    float wave2 = smoothstep(wave2Radius - waveWidth * 0.7, wave2Radius, dist)
                * smoothstep(wave2Radius + waveWidth * 0.7, wave2Radius, dist);
    uv += dir * strength * 0.4 * wave2;

    vec4 color = texture2D(u_texture, clamp(uv, 0.0, 1.0));
    color.rgb += vec3(0.8, 0.9, 1.0) * wave * strength * 8.0;

    gl_FragColor = color * v_fragmentColor;
}
