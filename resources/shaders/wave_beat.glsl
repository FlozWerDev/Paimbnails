// wave_beat.glsl — sine-wave UV displacement reactive to bass + treble.
// The wave amplitude scales with bass, the frequency with mid, and treble
// adds a high-frequency shimmer ripple.
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
    vec2 uv = v_texCoord;

    float baseAmp = u_intensity * 0.015;
    float bassAmp = u_bass * 0.05 * u_intensity;
    float beatAmp = u_beat * 0.04 * u_intensity;

    // Mid drives the wave frequency (more mid = tighter ripples).
    float freqY = mix(8.0, 22.0, clamp(u_mid * u_intensity, 0.0, 1.0));
    float freqX = mix(10.0, 26.0, clamp(u_mid * u_intensity, 0.0, 1.0));
    float speed = 1.5 + u_energy * 1.5;

    uv.x += sin(uv.y * freqY + u_time * speed) * (baseAmp + bassAmp);
    uv.y += cos(uv.x * freqX + u_time * speed * 0.8) * (baseAmp + bassAmp) * 0.9;

    // Treble shimmer — small high-frequency wobble.
    float trebSh = u_treble * u_intensity * 0.006;
    uv.x += sin(uv.y * 80.0 + u_time * 14.0) * trebSh;
    uv.y += cos(uv.x * 90.0 + u_time * 16.0) * trebSh;

    // Beat punch — brief radial pinch toward the screen center.
    vec2 c = uv - 0.5;
    float r = length(c);
    uv -= c * (u_beat * 0.04);

    vec4 color = texture2D(u_texture, uv);

    // Slight color shift on beat — adds drama.
    color.rgb *= 1.0 + u_beat * 0.30 + u_energy * 0.10;

    gl_FragColor = color * v_fragmentColor;
}
