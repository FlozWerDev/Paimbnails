// layered caustic cells ripple over the image, concentrating around the cursor
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;
uniform vec2 u_cursor;
uniform float u_click;

void main() {
    vec2 uv = v_texCoord;
    float t = mod(u_time, 628.318);

    vec2 p = uv * 9.0;
    float c = 0.0;
    for (int i = 0; i < 4; i++) {
        float fi = float(i);
        vec2 q = p + vec2(sin(t * 0.7 + fi), cos(t * 0.6 - fi)) * 1.3;
        c += abs(sin(q.x + sin(q.y + t)) * cos(q.y - cos(q.x - t * 0.8)));
        p *= 1.3;
    }
    c = pow(c * 0.25, 2.5);

    float focus = smoothstep(0.6, 0.0, length(uv - u_cursor));
    float refr = c * (u_intensity * 0.006) * (0.5 + focus + u_click * 0.5);
    vec2 duv = clamp(uv + vec2(sin(c * 6.0), cos(c * 6.0)) * refr, 0.0, 1.0);
    vec4 color = texture2D(u_texture, duv);

    color.rgb += vec3(0.3, 0.6, 0.8) * c * u_intensity * 0.08 * (0.5 + focus);
    color.b += c * 0.03;

    gl_FragColor = color * v_fragmentColor;
}
