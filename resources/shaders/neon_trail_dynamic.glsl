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
    
    // Neon trail that follows cursor with persistence
    float dist = length(v_texCoord - u_cursor);
    
    // Multiple trail segments at different time offsets
    float trail = 0.0;
    vec3 trailColor = vec3(0.0);
    
    for (int i = 0; i < 5; i++) {
        float fi = float(i);
        float delay = fi * 0.15;
        
        // Simulated past cursor positions (circular motion as approximation)
        vec2 pastPos = u_cursor + vec2(
            sin(u_time * 2.0 - delay * 3.0) * 0.02 * fi,
            cos(u_time * 2.0 - delay * 3.0) * 0.02 * fi
        );
        
        float d = length(v_texCoord - pastPos);
        float seg = smoothstep(0.03 + fi * 0.005, 0.0, d);
        seg *= 1.0 - fi * 0.18; // fade older segments
        
        // Color shifts along trail
        vec3 segColor = vec3(
            sin(fi * 1.2 + u_time) * 0.5 + 0.5,
            sin(fi * 1.2 + u_time + 2.09) * 0.5 + 0.5,
            sin(fi * 1.2 + u_time + 4.18) * 0.5 + 0.5
        );
        
        trail += seg;
        trailColor += segColor * seg;
    }
    
    // Neon glow
    float glow = u_intensity * 0.12 * (0.5 + u_click * 0.5);
    color.rgb += trailColor * glow;
    
    // Bloom around trail
    float bloom = smoothstep(0.1, 0.0, dist) * trail * 0.3;
    color.rgb += vec3(0.5, 0.3, 1.0) * bloom * u_intensity * 0.05;
    
    // Click burst
    float burst = smoothstep(0.15, 0.0, dist) * u_click;
    color.rgb += vec3(1.0, 0.8, 1.0) * burst * u_intensity * 0.08;
    
    gl_FragColor = color * v_fragmentColor;
}
