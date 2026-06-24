#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453); }

void main() {
    vec2 uv = v_texCoord;
    vec3 col = vec3(0.05, 0.02, 0.01);
    vec2 cell = floor(uv * 18.0);
    vec2 off = vec2(hash(cell), fract(hash(cell + 1.7) + u_time * 0.2));
    float d = length(fract(uv * 18.0) - off);
    float ember = exp(-25.0 * d) * step(0.92, hash(cell + 3.1));
    col += vec3(1.0, 0.45, 0.1) * ember;
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
