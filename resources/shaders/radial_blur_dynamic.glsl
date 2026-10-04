#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;
uniform vec2 u_cursor;

void main() {
    vec2 center = u_cursor;
    vec2 dir = v_texCoord - center;
    float str = u_intensity * 0.05;
    vec4 c = vec4(0.0);
    for (int i = 0; i < 8; i++) {
        float k = float(i) / 7.0;
        c += texture2D(u_texture, clamp(center + dir * (1.0 - str * k), 0.0, 1.0));
    }
    gl_FragColor = (c * 0.125) * v_fragmentColor;
}
