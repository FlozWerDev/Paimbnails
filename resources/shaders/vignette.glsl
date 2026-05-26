#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    vec2 pos = v_texCoord - 0.5;
    float dist = length(pos);
    float vignette = smoothstep(0.8, 0.3 * (1.0 - u_intensity), dist);
    gl_FragColor = vec4(color.rgb * vignette, color.a) * v_fragmentColor;
}
