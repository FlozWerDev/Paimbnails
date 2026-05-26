#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord;
    float amount = u_intensity * 0.02;
    float r = texture2D(u_texture, uv + vec2(amount, 0.0)).r;
    float g = texture2D(u_texture, uv).g;
    float b = texture2D(u_texture, uv - vec2(amount, 0.0)).b;
    gl_FragColor = vec4(r, g, b, texture2D(u_texture, uv).a) * v_fragmentColor;
}
