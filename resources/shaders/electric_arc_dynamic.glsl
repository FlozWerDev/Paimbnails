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

float hash(float n) { return fract(sin(n) * 43758.5453); }

float noise1D(float p) {
    float i = floor(p);
    float f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(hash(i), hash(i + 1.0), f);
}

void main() {
    vec2 uv = v_texCoord;
    vec4 color = texture2D(u_texture, uv);
    float t = mod(u_time, 628.318);

    float arcIntensity = u_intensity * 0.15 * (0.3 + u_click * 0.7);
    vec2 delta = v_texCoord - u_cursor;

    float totalArc = 0.0;
    for (int i = 0; i < 4; i++) {
        float fi = float(i);
        float angle = fi * 1.57 + t * (0.5 + fi * 0.3);
        vec2 dir = vec2(cos(angle), sin(angle));
        float proj = dot(delta, dir);
        float perp = length(delta - dir * proj);
        float jag = noise1D(proj * 20.0 + t * 10.0 + fi * 100.0) * 0.03;
        float arc = smoothstep(0.015 + jag, 0.0, perp);
        arc *= smoothstep(0.5, 0.0, abs(proj)) * step(0.0, proj);
        arc *= 0.5 + 0.5 * step(0.3, hash(floor(t * 15.0) + fi));
        totalArc += arc;
    }

    vec3 arcColor = mix(vec3(0.3, 0.5, 1.0), vec3(0.9, 0.95, 1.0), totalArc);
    color.rgb += arcColor * totalArc * arcIntensity;

    float displace = totalArc * 0.003 * u_click;
    vec4 displaced = texture2D(u_texture, clamp(uv + vec2(displace, -displace), 0.0, 1.0));
    color.rgb = mix(color.rgb, displaced.rgb, totalArc * 0.2 * u_click);

    gl_FragColor = color * v_fragmentColor;
}
