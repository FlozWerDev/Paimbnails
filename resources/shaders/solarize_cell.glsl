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
    float threshold = 0.5;
    vec3 solarized = abs(color.rgb - vec3(threshold)) * 2.0;
    vec3 result = mix(color.rgb, solarized, u_intensity);
    gl_FragColor = vec4(result, color.a);
}
