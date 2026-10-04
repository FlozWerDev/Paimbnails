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
    vec2 center = u_cursor;
    vec2 uv = v_texCoord - center;
    float t = mod(u_time, 628.318);

    float segments = 6.0 + u_click * 6.0;
    float r = length(uv);
    float a = atan(uv.y, uv.x) + t * 0.3 * u_intensity * 0.1;

    float segAngle = 3.14159 * 2.0 / segments;
    a = mod(a, segAngle);
    a = abs(a - segAngle * 0.5);

    vec2 mirroredUV = center + vec2(cos(a), sin(a)) * r;
    float pulse = 1.0 + sin(t * 2.0) * 0.02 * u_intensity * 0.1;
    mirroredUV = center + (mirroredUV - center) * pulse;

    vec4 color = texture2D(u_texture, clamp(mirroredUV, 0.0, 1.0));
    float glow = smoothstep(0.2, 0.0, r) * u_intensity * 0.1;
    color.rgb += (0.5 + 0.5 * sin(vec3(t * 1.5, t * 1.5 + 2.09, t * 1.5 + 4.18))) * glow;

    gl_FragColor = color * v_fragmentColor;
}
