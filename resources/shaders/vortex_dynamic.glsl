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
    vec2 delta = v_texCoord - u_cursor;
    float dist = length(delta);
    
    // Vortex rotation — stronger when clicking
    float baseStrength = u_intensity * 0.3;
    float clickBoost = u_click * 2.0;
    float angle = (baseStrength + clickBoost) / (dist * 10.0 + 0.3);
    angle *= smoothstep(0.6, 0.0, dist); // fade at edges
    
    // Animate rotation
    angle += sin(u_time * 2.0) * 0.2 * u_intensity * 0.1;
    
    float s = sin(angle);
    float c = cos(angle);
    vec2 rotated = vec2(
        delta.x * c - delta.y * s,
        delta.x * s + delta.y * c
    );
    uv = u_cursor + rotated;
    
    vec4 color = texture2D(u_texture, uv);
    
    // Subtle color shift in vortex center
    float centerGlow = smoothstep(0.2, 0.0, dist) * (0.3 + u_click * 0.5);
    color.rgb += vec3(0.2, 0.1, 0.4) * centerGlow * u_intensity * 0.15;
    
    gl_FragColor = color * v_fragmentColor;
}
