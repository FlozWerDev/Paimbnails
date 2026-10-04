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
    vec4 color = texture2D(u_texture, uv);

    // wrap keeps the orbit phase stable in mediump on long sessions
    float t = mod(u_time, 628.318);
    float dist = length(v_texCoord - u_cursor);

    float trail = 0.0;
    vec3 trailColor = vec3(0.0);
    for (int i = 0; i < 6; i++) {
        float fi = float(i);
        float ph = t * 2.0 - fi * 0.45;
        vec2 pastPos = u_cursor + vec2(sin(ph), cos(ph)) * 0.022 * fi;
        float d = length(v_texCoord - pastPos);
        float seg = smoothstep(0.045 + fi * 0.004, 0.0, d) * (1.0 - fi * 0.15);
        vec3 segColor = 0.5 + 0.5 * sin(vec3(fi * 1.2 + t, fi * 1.2 + t + 2.09, fi * 1.2 + t + 4.18));
        trail += seg;
        trailColor += segColor * seg;
    }

    float glow = u_intensity * 0.14 * (0.5 + u_click * 0.5);
    color.rgb += trailColor * glow;

    float bloom = smoothstep(0.12, 0.0, dist) * trail * 0.35;
    color.rgb += vec3(0.5, 0.3, 1.0) * bloom * u_intensity * 0.06;

    float burst = smoothstep(0.18, 0.0, dist) * u_click;
    color.rgb += vec3(1.0, 0.8, 1.0) * burst * u_intensity * 0.1;

    gl_FragColor = color * v_fragmentColor;
}
