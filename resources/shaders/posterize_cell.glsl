#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    float levels = 10.0 - (u_intensity * 8.0);
    levels = max(2.0, levels);
    vec3 result = floor(color.rgb * levels) / levels;
    gl_FragColor = vec4(result, color.a);
}
