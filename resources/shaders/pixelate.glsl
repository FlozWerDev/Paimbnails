#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_screenSize;
uniform float u_intensity;

void main() {
    float pixelSize = 2.0 + u_intensity * 15.0;
    vec2 coord = floor(v_texCoord * u_screenSize / pixelSize) * pixelSize / u_screenSize;
    gl_FragColor = texture2D(u_texture, coord) * v_fragmentColor;
}
