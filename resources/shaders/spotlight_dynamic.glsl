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
    vec4 color = texture2D(u_texture, v_texCoord);
    
    float dist = length(v_texCoord - u_cursor);
    
    // Spotlight radius — click makes it bigger
    float radius = 0.15 + u_intensity * 0.03 + u_click * 0.12;
    float softness = 0.08 + u_intensity * 0.02;
    
    // Spotlight mask
    float spotlight = 1.0 - smoothstep(radius - softness, radius + softness, dist);
    
    // Darken everything outside spotlight
    float darkness = 0.1 + u_intensity * 0.05;
    float ambient = mix(darkness, 1.0, spotlight);
    
    // Slight color temperature shift in spotlight
    vec3 warmLight = vec3(1.05, 1.0, 0.92);
    color.rgb *= ambient;
    color.rgb *= mix(vec3(0.8, 0.85, 1.0), warmLight, spotlight);
    
    // Subtle edge glow
    float edge = smoothstep(radius - softness * 0.5, radius, dist)
               * smoothstep(radius + softness * 0.5, radius, dist);
    color.rgb += vec3(1.0, 0.9, 0.7) * edge * 0.15 * u_intensity * 0.1;
    
    gl_FragColor = color * v_fragmentColor;
}
