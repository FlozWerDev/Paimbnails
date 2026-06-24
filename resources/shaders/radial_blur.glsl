#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

void main() {
    float cx = 0.5 + 0.15 * sin(u_time * 0.7) + 0.08 * cos(u_time * 1.3);
    float cy = 0.5 + 0.12 * cos(u_time * 0.9) + 0.06 * sin(u_time * 1.7);
    vec2 center = vec2(cx, cy);
    vec2 dir = v_texCoord - center;
    float str = u_intensity * 0.05;
    // 8 fixed samples along radial direction
    vec4 c  = texture2D(u_texture, center + dir * (1.0 - str * 0.000));
    c += texture2D(u_texture, center + dir * (1.0 - str * 0.143));
    c += texture2D(u_texture, center + dir * (1.0 - str * 0.286));
    c += texture2D(u_texture, center + dir * (1.0 - str * 0.429));
    c += texture2D(u_texture, center + dir * (1.0 - str * 0.571));
    c += texture2D(u_texture, center + dir * (1.0 - str * 0.714));
    c += texture2D(u_texture, center + dir * (1.0 - str * 0.857));
    c += texture2D(u_texture, center + dir * (1.0 - str * 1.000));
    gl_FragColor = (c * 0.125) * v_fragmentColor;
}
