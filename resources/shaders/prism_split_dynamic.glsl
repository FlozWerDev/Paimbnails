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
    vec2 delta = uv - u_cursor;
    float dist = length(delta);
    
    // Prismatic RGB split radiating from cursor
    float splitAmount = u_intensity * 0.015 * (0.3 + u_click * 0.7);
    splitAmount *= smoothstep(0.6, 0.0, dist);
    
    // Direction of split rotates over time
    float angle = atan(delta.y, delta.x) + u_time * 0.5;
    vec2 splitDir = vec2(cos(angle), sin(angle)) * splitAmount;
    
    // Sample each channel with offset
    float r = texture2D(u_texture, uv + splitDir).r;
    float g = texture2D(u_texture, uv).g;
    float b = texture2D(u_texture, uv - splitDir).b;
    
    vec4 color = vec4(r, g, b, texture2D(u_texture, uv).a);
    
    // Rainbow refraction at edges
    float edgeDist = abs(dist - 0.15 - u_click * 0.1);
    float rainbow = smoothstep(0.03, 0.0, edgeDist) * u_intensity * 0.12;
    color.rgb += vec3(
        sin(angle * 3.0 + u_time) * 0.5 + 0.5,
        sin(angle * 3.0 + u_time + 2.09) * 0.5 + 0.5,
        sin(angle * 3.0 + u_time + 4.18) * 0.5 + 0.5
    ) * rainbow;
    
    // Sparkle at cursor
    float sparkle = smoothstep(0.03, 0.0, dist) * (0.2 + u_click * 0.5);
    color.rgb += vec3(1.0, 1.0, 1.0) * sparkle * u_intensity * 0.1;
    
    gl_FragColor = color * v_fragmentColor;
}
