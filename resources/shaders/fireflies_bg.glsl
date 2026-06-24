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
    vec3 col = vec3(0.03, 0.05, 0.08);
    vec2 cell = floor(uv * 16.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 c = cell + vec2(float(x), float(y));
            vec2 off = vec2(hash(c), hash(c + 3.1));
            vec2 pos = (c + off + vec2(0.2 * sin(u_time + off.x * 6.0), 0.2 * cos(u_time + off.y * 6.0))) / 16.0;
            float d = length(uv - pos);
            col += vec3(1.0, 0.9, 0.4) * exp(-80.0 * d);
        }
    }
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
