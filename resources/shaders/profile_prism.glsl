#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

vec3 hue(float h) {
    return clamp(abs(mod(h * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
}

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float h = fract(v_texCoord.x * 0.5 + v_texCoord.y * 0.5 + u_time * 0.15);
    vec3 rainbow = hue(h);
    float lum = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    color.rgb = mix(color.rgb, color.rgb * 0.6 + rainbow * (0.4 + lum * 0.4), u_intensity * 0.4);
    gl_FragColor = color * v_fragmentColor;
}
