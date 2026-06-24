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
    float dist = length(v_texCoord - u_cursor);
    
    // Pixelation radius — click expands
    float radius = 0.2 + u_intensity * 0.04 + u_click * 0.25;
    float pixelMask = smoothstep(radius + 0.05, radius - 0.1, dist);
    
    // Pixel size increases toward cursor center
    float pixelSize = mix(200.0, 8.0 + (10.0 - u_intensity) * 3.0, pixelMask);
    
    // Pixelate
    vec2 pixelUV = floor(uv * pixelSize) / pixelSize;
    vec2 finalUV = mix(uv, pixelUV, pixelMask);
    
    vec4 color = texture2D(u_texture, finalUV);
    
    // Grid lines in pixelated area
    vec2 grid = fract(uv * pixelSize);
    float gridLine = step(0.95, max(grid.x, grid.y));
    color.rgb -= vec3(0.1) * gridLine * pixelMask * 0.5;
    
    // Subtle scan effect
    float scan = sin(v_texCoord.y * pixelSize * 3.14159) * 0.5 + 0.5;
    color.rgb -= vec3(0.03) * scan * pixelMask;
    
    gl_FragColor = color * v_fragmentColor;
}
