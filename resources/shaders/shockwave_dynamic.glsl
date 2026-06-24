#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;
uniform vec2 u_cursor;
uniform float u_click; // 1.0 when pressed, 0.0 when released

void main() {
    vec2 uv = v_texCoord;
    
    // Shockwave expands from cursor on click
    float dist = length(v_texCoord - u_cursor);
    float waveRadius = fract(u_time * 0.8) * 1.2; // expanding ring
    float waveWidth = 0.08 + u_intensity * 0.01;
    float wave = smoothstep(waveRadius - waveWidth, waveRadius, dist)
               * smoothstep(waveRadius + waveWidth, waveRadius, dist);
    
    // Distortion strength based on click state
    float strength = u_intensity * 0.04 * wave * (0.3 + u_click * 0.7);
    vec2 dir = normalize(v_texCoord - u_cursor + 0.001);
    uv += dir * strength;
    
    // Second wave (echo)
    float wave2Radius = fract(u_time * 0.8 - 0.3) * 1.2;
    float wave2 = smoothstep(wave2Radius - waveWidth * 0.7, wave2Radius, dist)
                * smoothstep(wave2Radius + waveWidth * 0.7, wave2Radius, dist);
    uv += dir * strength * 0.4 * wave2;
    
    vec4 color = texture2D(u_texture, uv);
    
    // Bright ring on the wave edge
    color.rgb += vec3(0.8, 0.9, 1.0) * wave * strength * 8.0;
    
    gl_FragColor = color * v_fragmentColor;
}
