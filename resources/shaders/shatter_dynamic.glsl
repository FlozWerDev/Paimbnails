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
    vec2 uv = v_texCoord;
    
    // Voronoi-like shatter cells
    float cellSize = 8.0 + u_intensity * 2.0;
    vec2 cell = floor(v_texCoord * cellSize);
    vec2 cellFract = fract(v_texCoord * cellSize);
    
    float dist = length(v_texCoord - u_cursor);
    float shatterRadius = u_click * 0.5 + 0.05;
    float shatterMask = smoothstep(shatterRadius + 0.1, shatterRadius - 0.05, dist);
    
    // Each cell gets a random offset when shattered
    float cellHash = hash(cell);
    float cellHash2 = hash(cell + 100.0);
    
    vec2 offset = vec2(
        (cellHash - 0.5) * 0.04,
        (cellHash2 - 0.5) * 0.04 + 0.02 // slight gravity
    ) * shatterMask * u_intensity * 0.1 * (0.3 + u_click * 0.7);
    
    // Rotation per cell
    float cellAngle = (cellHash - 0.5) * 0.3 * shatterMask * u_click;
    vec2 centered = cellFract - 0.5;
    float s = sin(cellAngle);
    float c = cos(cellAngle);
    vec2 rotated = vec2(centered.x * c - centered.y * s, centered.x * s + centered.y * c);
    
    vec2 finalUV = (cell + rotated + 0.5) / cellSize + offset;
    vec4 color = texture2D(u_texture, finalUV);
    
    // Dark edges between cells
    float edge = smoothstep(0.0, 0.05, cellFract.x) * smoothstep(0.0, 0.05, cellFract.y)
               * smoothstep(0.0, 0.05, 1.0 - cellFract.x) * smoothstep(0.0, 0.05, 1.0 - cellFract.y);
    float edgeDark = 1.0 - (1.0 - edge) * shatterMask * 0.8;
    color.rgb *= edgeDark;
    
    // Bright crack lines
    float crack = (1.0 - edge) * shatterMask;
    color.rgb += vec3(0.8, 0.9, 1.0) * crack * u_intensity * 0.06;
    
    gl_FragColor = color * v_fragmentColor;
}
