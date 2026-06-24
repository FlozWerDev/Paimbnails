#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    float scanline = sin(v_texCoord.y * u_texSize.y * 0.5) * 0.1 * u_intensity;
    color.rgb -= scanline;
    gl_FragColor = color;
}
