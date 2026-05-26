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
    vec2 center = u_cursor;
    vec2 uv = v_texCoord - center;
    
    // Number of segments increases with click
    float segments = 6.0 + u_click * 6.0;
    
    // Convert to polar
    float r = length(uv);
    float a = atan(uv.y, uv.x);
    
    // Rotate over time
    a += u_time * 0.3 * u_intensity * 0.1;
    
    // Mirror in segments
    float segAngle = 3.14159 * 2.0 / segments;
    a = mod(a, segAngle);
    a = abs(a - segAngle * 0.5);
    
    // Back to cartesian
    vec2 mirroredUV = center + vec2(cos(a), sin(a)) * r;
    
    // Slight zoom pulse
    float pulse = 1.0 + sin(u_time * 2.0) * 0.02 * u_intensity * 0.1;
    mirroredUV = center + (mirroredUV - center) * pulse;
    
    vec4 color = texture2D(u_texture, mirroredUV);
    
    // Rainbow tint at center
    float glow = smoothstep(0.2, 0.0, r) * u_intensity * 0.1;
    color.rgb += vec3(
        sin(u_time * 1.5) * 0.5 + 0.5,
        sin(u_time * 1.5 + 2.09) * 0.5 + 0.5,
        sin(u_time * 1.5 + 4.18) * 0.5 + 0.5
    ) * glow;
    
    gl_FragColor = color * v_fragmentColor;
}
