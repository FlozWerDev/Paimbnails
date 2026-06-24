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
    vec3 col = vec3(0.03, 0.06, 0.12);
    for (int i = 0; i < 8; ++i) {
        float fi = float(i);
        vec2 p = vec2(hash(vec2(fi, 1.0)), fract(hash(vec2(fi, 2.0)) + u_time * (0.05 + fi * 0.01)));
        float r = 0.03 + 0.05 * hash(vec2(fi, 3.0));
        float d = abs(length(uv - p) - r);
        col += vec3(0.5, 0.8, 1.0) * exp(-120.0 * d) * 0.4;
    }
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
