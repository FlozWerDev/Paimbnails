#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

void main() {
    vec2 uv = v_texCoord;
    float scan = sin(uv.y * 160.0 - u_time * 6.0) * 0.5 + 0.5;
    scan = pow(scan, 3.0);

    float off = 0.004 * u_intensity;
    vec4 color;
    color.r = texture2D(u_texture, uv + vec2(off, 0.0)).r;
    color.g = texture2D(u_texture, uv).g;
    color.b = texture2D(u_texture, uv - vec2(off, 0.0)).b;
    color.a = texture2D(u_texture, uv).a;

    float h = (u_time * 0.3 + uv.y * 0.5) * 6.2832;
    vec3 tint = 0.5 + 0.5 * sin(vec3(h, h + 2.09, h + 4.19));
    color.rgb = mix(color.rgb, color.rgb * tint * 1.6, u_intensity * 0.25);
    color.rgb += vec3(0.1, 0.4, 0.6) * scan * u_intensity * 0.18 * color.a;
    gl_FragColor = color * v_fragmentColor;
}
