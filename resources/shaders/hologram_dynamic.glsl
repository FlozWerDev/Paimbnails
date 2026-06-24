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
    
    // Holographic scan lines
    float scanline = sin(v_texCoord.y * 200.0 + u_time * 5.0) * 0.5 + 0.5;
    scanline = pow(scanline, 4.0);
    
    // Jitter on click
    float jitter = u_click * sin(u_time * 50.0) * 0.003 * u_intensity * 0.1;
    uv.x += jitter;
    
    // Vertical tear near cursor
    float cursorDist = abs(v_texCoord.x - u_cursor.x);
    float tearMask = smoothstep(0.1, 0.0, cursorDist) * u_click;
    uv.y += tearMask * sin(v_texCoord.y * 30.0 + u_time * 8.0) * 0.01;
    
    vec4 color = texture2D(u_texture, uv);
    
    // Holographic blue-cyan tint
    float holoStrength = u_intensity * 0.08;
    vec3 holoTint = vec3(0.2, 0.7, 1.0);
    color.rgb = mix(color.rgb, color.rgb * holoTint * 1.5, holoStrength);
    
    // Scanline darkening
    color.rgb *= 1.0 - scanline * 0.15 * u_intensity * 0.1;
    
    // Edge flicker
    float edgeFlicker = step(0.95, sin(u_time * 20.0 + v_texCoord.y * 5.0));
    color.rgb += vec3(0.0, 0.3, 0.5) * edgeFlicker * 0.1 * u_intensity * 0.1;
    
    // Transparency flicker near cursor
    float dist = length(v_texCoord - u_cursor);
    float proximity = smoothstep(0.3, 0.0, dist);
    float flicker = sin(u_time * 15.0 + dist * 20.0) * 0.5 + 0.5;
    color.a *= 1.0 - proximity * flicker * 0.2 * u_click;
    
    // RGB offset for hologram feel
    float rgbOff = 0.002 * u_intensity * 0.1 * (1.0 + u_click);
    color.r = texture2D(u_texture, uv + vec2(rgbOff, 0.0)).r;
    color.b = texture2D(u_texture, uv - vec2(rgbOff, 0.0)).b;
    
    gl_FragColor = color * v_fragmentColor;
}
