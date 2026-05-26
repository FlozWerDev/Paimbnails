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
    
    // Heat rises from cursor position
    float heatDist = length(v_texCoord - u_cursor);
    float heatMask = smoothstep(0.5, 0.0, heatDist) * (0.4 + u_click * 0.6);
    
    // Wavy distortion that rises upward
    float rise = (u_cursor.y - v_texCoord.y);
    float riseMask = smoothstep(0.0, 0.4, rise) * smoothstep(0.8, 0.3, rise);
    
    float wave1 = sin(v_texCoord.y * 40.0 + u_time * 3.0 + v_texCoord.x * 10.0);
    float wave2 = sin(v_texCoord.y * 25.0 - u_time * 2.5 + v_texCoord.x * 8.0);
    float distortion = (wave1 + wave2 * 0.5) * 0.003 * u_intensity * 0.1;
    
    uv.x += distortion * heatMask * riseMask;
    uv.y += abs(distortion) * 0.5 * heatMask * riseMask;
    
    vec4 color = texture2D(u_texture, uv);
    
    // Warm color shift in heat zone
    float warmth = heatMask * riseMask * u_intensity * 0.08;
    color.r += warmth * 0.3;
    color.g += warmth * 0.1;
    color.b -= warmth * 0.2;
    
    // Shimmer highlights
    float shimmer = pow(max(wave1 * wave2, 0.0), 4.0) * heatMask * riseMask;
    color.rgb += vec3(1.0, 0.9, 0.7) * shimmer * u_intensity * 0.05;
    
    gl_FragColor = color * v_fragmentColor;
}
