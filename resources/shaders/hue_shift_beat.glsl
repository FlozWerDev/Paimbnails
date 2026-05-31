// hue_shift_beat.glsl — full-screen hue rotation reactive to mid + beat.
// Bass adds saturation pumping; treble adds slight horizontal jitter.
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;

uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;
uniform float u_bass;
uniform float u_mid;
uniform float u_treble;
uniform float u_beat;
uniform float u_energy;

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)),
                d / (q.x + e),
                q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    vec2 uv = v_texCoord;

    // Treble jitter on x.
    uv.x += sin(uv.y * 80.0 + u_time * 12.0) * u_treble * u_intensity * 0.003;

    vec4 base = texture2D(u_texture, uv);
    vec3 hsv = rgb2hsv(base.rgb);

    // Hue shift: continuous from time + impulse from beat + mid bias.
    float hueShift = u_time * 0.08
                   + u_mid * u_intensity * 0.35
                   + u_beat * 0.2;
    hsv.x = fract(hsv.x + hueShift * u_intensity);

    // Saturation pump with bass.
    hsv.y = clamp(hsv.y * (1.0 + u_bass * u_intensity * 0.6), 0.0, 1.0);

    // Brightness with energy + beat.
    hsv.z *= 1.0 + u_beat * 0.35 + u_energy * 0.15;

    vec3 col = hsv2rgb(hsv);
    gl_FragColor = vec4(col, base.a) * v_fragmentColor;
}
