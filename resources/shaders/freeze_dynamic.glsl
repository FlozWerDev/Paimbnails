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

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float dist = length(v_texCoord - u_cursor);
    
    // Freeze radius — click expands the frozen area
    float radius = 0.15 + u_intensity * 0.03 + u_click * 0.2;
    float freezeMask = smoothstep(radius + 0.05, radius - 0.05, dist);
    
    // Ice crystal pattern
    vec2 cell = floor(v_texCoord * 80.0);
    float crystal = hash(cell + floor(u_time * 0.5));
    crystal = step(0.7, crystal) * freezeMask;
    
    // Desaturate + blue tint in frozen area
    float lum = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    vec3 frozen = mix(vec3(lum), vec3(lum * 0.7, lum * 0.85, lum * 1.3), 0.7);
    
    // Frost sparkle
    float sparkle = crystal * (0.5 + 0.5 * sin(u_time * 10.0 + hash(cell) * 50.0));
    frozen += vec3(0.6, 0.8, 1.0) * sparkle * 0.3;
    
    // Edge frost ring
    float edge = smoothstep(radius - 0.02, radius, dist) * smoothstep(radius + 0.04, radius, dist);
    frozen += vec3(0.5, 0.7, 1.0) * edge * 0.4;
    
    color.rgb = mix(color.rgb, frozen, freezeMask);
    
    gl_FragColor = color * v_fragmentColor;
}
