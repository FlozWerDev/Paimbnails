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
    
    // Plasma energy ball at cursor
    float radius = 0.08 + u_intensity * 0.02 + u_click * 0.1;
    float plasma = 0.0;
    
    // Multiple plasma layers
    plasma += sin(dist * 40.0 - u_time * 6.0 + sin(u_time * 2.0) * 3.0);
    plasma += sin(dist * 25.0 + u_time * 4.0) * 0.7;
    plasma += sin((v_texCoord.x - u_cursor.x) * 30.0 + u_time * 5.0) * 0.5;
    plasma = plasma * 0.33 * 0.5 + 0.5;
    
    // Mask to cursor area
    float mask = smoothstep(radius + 0.1, radius * 0.3, dist);
    
    // Color cycling
    float h = u_time * 0.5 + plasma * 2.0;
    vec3 plasmaColor = 0.5 + 0.5 * sin(vec3(h, h + 2.094, h + 4.189));
    
    // Blend plasma over image
    float blend = mask * (0.4 + u_click * 0.4) * u_intensity * 0.12;
    color.rgb = mix(color.rgb, plasmaColor, blend);
    
    // Bright core
    float core = smoothstep(radius * 0.5, 0.0, dist);
    color.rgb += plasmaColor * core * 0.3 * (0.5 + u_click * 0.5);
    
    gl_FragColor = color * v_fragmentColor;
}
