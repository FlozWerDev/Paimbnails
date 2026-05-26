#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float levels = mix(32.0, 3.0, u_intensity);
    vec3 result;
    result.r = floor(color.r * levels) / levels;
    result.g = floor(color.g * levels) / levels;
    result.b = floor(color.b * levels) / levels;
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
}
