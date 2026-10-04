#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);

    float pulse = 0.5 + 0.5 * sin(u_time * 3.0);
    float dist = distance(v_texCoord, vec2(0.5));
    float rim = smoothstep(0.5, 0.32, dist);

    vec3 warm = vec3(1.0, 0.85, 0.5);
    color.rgb += warm * rim * pulse * u_intensity * 0.5 * color.a;
    color.rgb *= 1.0 + pulse * u_intensity * 0.12;
    gl_FragColor = color * v_fragmentColor;
}
