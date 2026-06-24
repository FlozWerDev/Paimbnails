// liquid_beat.glsl — flowing liquid distortion. Mid drives flow speed,
// bass amplitude, treble adds high-frequency micro-ripples, beat fires a
// localized wave from the center.
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

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    vec2 uv = v_texCoord;

    float speed = 0.6 + u_mid * u_intensity * 1.2 + u_energy * 0.4;
    float amp   = 0.012 * u_intensity + u_bass * 0.025 * u_intensity;

    // Two layers of noise-driven flow.
    vec2 q = vec2(
        noise(uv * 3.5 + vec2(0.0, u_time * speed)),
        noise(uv * 3.5 + vec2(u_time * speed * 0.8, 0.0))
    );
    vec2 r = vec2(
        noise(uv * 6.7 + 1.5 * q + vec2(u_time * speed * 0.4, 0.0)),
        noise(uv * 6.7 + 1.5 * q + vec2(0.0, u_time * speed * 0.6))
    );
    uv += (r - 0.5) * 2.0 * amp;

    // Treble micro shimmer.
    uv += vec2(
        sin(v_texCoord.y * 90.0 + u_time * 12.0),
        cos(v_texCoord.x * 90.0 + u_time * 11.0)
    ) * u_treble * u_intensity * 0.0025;

    // Beat radial wave from center.
    vec2 c2 = v_texCoord - 0.5;
    float rad = length(c2);
    float wave = sin(rad * 18.0 - u_time * 5.0) * exp(-rad * 3.0);
    uv += normalize(c2 + 1e-5) * wave * u_beat * 0.04 * u_intensity;

    vec4 col = texture2D(u_texture, uv);

    // Subtle blue/teal tint.
    col.rgb = mix(col.rgb, col.rgb * vec3(0.95, 1.0, 1.05), u_intensity * 0.25);
    col.rgb *= 1.0 + u_beat * 0.25;

    gl_FragColor = col * v_fragmentColor;
}
