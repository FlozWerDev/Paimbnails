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
    
    float dist = length(v_texCoord - u_cursor);
    
    // Multiple expanding rings from cursor
    float ringSpeed = 1.2 + u_click * 0.8;
    float ringCount = 3.0;
    float totalRing = 0.0;
    
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        float phase = fi / ringCount;
        float radius = fract(u_time * ringSpeed * 0.3 + phase) * 0.8;
        float thickness = 0.008 + u_intensity * 0.002;
        float ring = smoothstep(thickness, 0.0, abs(dist - radius));
        ring *= 1.0 - radius * 1.2; // fade as it expands
        totalRing += ring;
    }
    
    // Sonar green tint on rings
    vec3 sonarColor = vec3(0.1, 1.0, 0.4);
    color.rgb += sonarColor * totalRing * u_intensity * 0.12;
    
    // Reveal effect: brighten areas the ring passes over
    float reveal = fract(u_time * ringSpeed * 0.3) * 0.8;
    float revealMask = smoothstep(reveal + 0.05, reveal - 0.05, dist);
    revealMask *= smoothstep(0.0, 0.02, reveal);
    color.rgb *= 1.0 + revealMask * 0.15 * u_intensity * 0.1 * u_click;
    
    // Ping dot at center
    float ping = smoothstep(0.02, 0.0, dist) * (sin(u_time * 8.0) * 0.5 + 0.5);
    color.rgb += sonarColor * ping * u_intensity * 0.1;
    
    gl_FragColor = color * v_fragmentColor;
}
