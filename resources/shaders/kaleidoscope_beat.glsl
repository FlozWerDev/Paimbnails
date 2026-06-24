// kaleidoscope_beat.glsl — kaleidoscopic mirror effect with beat-driven
// segment count and rotation speed.
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

void main() {
    vec2 c = v_texCoord - 0.5;
    float r = length(c);
    float a = atan(c.y, c.x);

    // Segment count grows with bass. Min 4, max ~16 at peak bass.
    float segments = mix(4.0, 16.0, clamp(u_bass * u_intensity * 0.5, 0.0, 1.0));
    float segAngle = 6.2832 / segments;

    // Rotate the whole kaleidoscope based on time + mid frequency.
    float rotate = u_time * (0.4 + u_mid * u_intensity * 0.6);
    a += rotate;

    // Mirror within each segment.
    a = mod(a, segAngle);
    a = abs(a - segAngle * 0.5);

    // Beat punch — radial pulse.
    r *= 1.0 - u_beat * 0.15 * u_intensity;

    vec2 uv = vec2(cos(a), sin(a)) * r + 0.5;

    // Sample with treble shimmer.
    float shimmer = sin(u_time * 8.0 + r * 30.0) * u_treble * 0.005 * u_intensity;
    vec4 col = texture2D(u_texture, uv + vec2(shimmer, shimmer));

    // Color brightens with energy.
    col.rgb *= 1.0 + u_energy * 0.25 + u_beat * 0.3;

    gl_FragColor = col * v_fragmentColor;
}
