#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

void main() {
    float pulse = 1.0 + 0.4 * sin(u_time * 1.8) + 0.2 * sin(u_time * 3.1);
    float amount = u_intensity * 0.01 * pulse;
    float angle = u_time * 0.5;
    vec2 dir = vec2(cos(angle), sin(angle));
    vec2 offset = (v_texCoord - 0.5) * amount;
    vec2 oR = offset + dir * amount * 0.3;
    vec2 oB = offset - dir * amount * 0.3;
    vec4 center = texture2D(u_texture, v_texCoord);
    float r = texture2D(u_texture, v_texCoord + oR).r;
    float b = texture2D(u_texture, v_texCoord - oB).b;
    gl_FragColor = vec4(r, center.g, b, center.a) * v_fragmentColor;
}
