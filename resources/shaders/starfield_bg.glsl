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
    vec3 col = vec3(0.01, 0.01, 0.04);
    vec2 pixel = 1.0 / max(u_texSize, vec2(1.0));
    vec2 starGrid = uv * vec2(120.0, 120.0 * (u_texSize.y / max(u_texSize.x, 1.0)));
    vec2 gv = fract(starGrid) - 0.5;
    vec2 id = floor(starGrid);
    float star = step(0.9978, hash(id));
    float d = length(gv + vec2(0.0, fract(u_time * 0.4 + hash(id)) - 0.5) * 0.25);
    float glow = smoothstep(0.09 + pixel.x * 10.0, 0.0, d);
    col += vec3(1.0) * star * glow;
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
