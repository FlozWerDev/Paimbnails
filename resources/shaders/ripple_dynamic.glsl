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
    float dist = length(v_texCoord - u_cursor);
    
    // Multiple concentric ripples from cursor
    float rippleFreq = 20.0 + u_intensity * 5.0;
    float rippleSpeed = u_time * 4.0;
    float ripple = sin(dist * rippleFreq - rippleSpeed) * 0.5 + 0.5;
    
    // Ripple fades with distance
    float fade = smoothstep(0.6, 0.0, dist);
    float strength = u_intensity * 0.015 * fade * ripple;
    
    // Click creates stronger, faster ripples
    float clickRipple = sin(dist * rippleFreq * 1.5 - u_time * 8.0) * 0.5 + 0.5;
    strength += u_click * u_intensity * 0.025 * fade * clickRipple;
    
    // Displace UVs along radial direction
    vec2 dir = normalize(v_texCoord - u_cursor + 0.001);
    uv += dir * strength;
    
    vec4 color = texture2D(u_texture, uv);
    
    // Subtle caustic coloring
    float caustic = ripple * fade * 0.3;
    color.rgb += vec3(0.1, 0.2, 0.4) * caustic * u_intensity * 0.05;
    
    gl_FragColor = color * v_fragmentColor;
}
