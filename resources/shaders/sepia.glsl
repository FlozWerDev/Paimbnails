#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    vec3 sepia;
    sepia.r = dot(color.rgb, vec3(0.393, 0.769, 0.189));
    sepia.g = dot(color.rgb, vec3(0.349, 0.686, 0.168));
    sepia.b = dot(color.rgb, vec3(0.272, 0.534, 0.131));
    vec3 result = mix(color.rgb, sepia, u_intensity);
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
}
