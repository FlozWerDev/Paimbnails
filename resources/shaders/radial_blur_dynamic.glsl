#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;
uniform vec2 u_cursor; // normalized 0..1 cursor/touch position

void main() {
    // Radial blur centered on cursor position
    vec2 center = u_cursor;
    vec2 dir = v_texCoord - center;
    float str = u_intensity * 0.05;
    // 8 fixed samples along radial direction from cursor
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
