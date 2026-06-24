#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;
uniform vec2 u_texSize;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453); }

void main() {
    vec2 uv = v_texCoord;
    float cols = 40.0;
    vec2 grid = uv * vec2(cols, cols * 2.0);
    vec2 cell = floor(grid);
    vec2 cellUv = fract(grid);
    float speed = 2.0 + hash(vec2(cell.x, 0.0)) * 3.0;
    float head = fract(cell.y / (cols * 2.0) - u_time * speed * 0.12);
    float glow = smoothstep(0.0, 0.2, head) * smoothstep(1.0, 0.7, head);
    float glyph = smoothstep(0.48, 0.18, abs(cellUv.x - 0.5)) * smoothstep(0.85, 0.2, cellUv.y);
    float soft = smoothstep(0.0, 0.02 + 2.0 / max(u_texSize.y, 1.0), glyph);
    vec3 col = vec3(0.01, 0.03, 0.01) + vec3(0.1, 1.0, 0.3) * glow * soft;
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
