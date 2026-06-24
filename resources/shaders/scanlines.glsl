#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform vec2 u_screenSize;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float scanline = sin(v_texCoord.y * u_screenSize.y * 3.14159 * (1.0 + u_intensity * 2.0)) * 0.5 + 0.5;
    scanline = mix(1.0, scanline, u_intensity * 0.5);
    gl_FragColor = vec4(color.rgb * scanline, color.a) * v_fragmentColor;
}
