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
    
    // Caustic-like water distortion
    float wave1 = sin(uv.x * 15.0 + u_time * 1.5) * cos(uv.y * 12.0 + u_time * 1.2);
    float wave2 = sin(uv.x * 8.0 - u_time * 0.8) * cos(uv.y * 10.0 + u_time * 1.0);
    
    float distortStr = u_intensity * 0.004 * (0.5 + u_click * 0.5);
    uv.x += (wave1 + wave2 * 0.5) * distortStr;
    uv.y += (wave2 + wave1 * 0.3) * distortStr;
    
    // Cursor creates a bubble/ripple
    float dist = length(v_texCoord - u_cursor);
    float bubble = sin(dist * 40.0 - u_time * 6.0) * smoothstep(0.2, 0.0, dist);
    uv += normalize(v_texCoord - u_cursor + 0.001) * bubble * 0.005 * u_click;
    
    vec4 color = texture2D(u_texture, uv);
    
    // Deep blue-green tint
    float depth = u_intensity * 0.08;
    color.r *= 1.0 - depth * 0.4;
    color.g *= 1.0 - depth * 0.1;
    color.b *= 1.0 + depth * 0.2;
    
    // Caustic light patterns
    float caustic = pow(max(wave1 * wave2 + 0.5, 0.0), 3.0);
    color.rgb += vec3(0.3, 0.6, 0.5) * caustic * u_intensity * 0.04;
    
    // Light rays from top
    float ray = smoothstep(0.3, 0.0, abs(v_texCoord.x - 0.5 + sin(u_time * 0.5) * 0.2));
    ray *= smoothstep(0.0, 1.0, v_texCoord.y);
    color.rgb += vec3(0.2, 0.4, 0.3) * ray * 0.1 * u_intensity * 0.1;
    
    // Bubble highlight at cursor
    float bubbleGlow = smoothstep(0.05, 0.0, abs(dist - 0.04)) * u_click;
    color.rgb += vec3(0.5, 0.8, 1.0) * bubbleGlow * 0.3;
    
    gl_FragColor = color * v_fragmentColor;
}
