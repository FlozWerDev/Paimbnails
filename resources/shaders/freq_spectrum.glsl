// freq_spectrum.glsl — radial frequency spectrum visualizer.
// 64 spokes around the center grow/shrink based on frequency bands.
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;

uniform float u_time;
uniform float u_intensity;
uniform float u_bass;
uniform float u_mid;
uniform float u_treble;
uniform float u_beat;
uniform float u_energy;

float hash(float n) { return fract(sin(n) * 43758.5453); }

void main() {
    vec2 uv = v_texCoord - 0.5;
    uv.x *= 1.78;
    float r = length(uv);
    float a = atan(uv.y, uv.x);

    // 64 spokes around the center.
    float spokes = 64.0;
    float spokeIdx = floor((a / 6.2832 + 0.5) * spokes);
    float spokePos = spokeIdx / spokes;

    // Map angular position to a frequency band (with mirror to keep symmetry).
    float bandPos = abs(spokePos * 2.0 - 1.0);
    float fLow  = smoothstep(0.0, 0.35, 1.0 - bandPos);
    float fHigh = smoothstep(0.6, 1.0, bandPos);
    float fMid  = 1.0 - fLow - fHigh;
    float freq  = u_bass * fLow + u_mid * fMid + u_treble * fHigh;

    // Per-spoke jitter for organic feel.
    float jitter = hash(spokeIdx) * 0.2 + 0.8;
    float spokeLen = 0.18 + freq * 0.32 * jitter * (0.5 + u_intensity);

    // Inner radius (donut).
    float inner = 0.18;
    float bar = smoothstep(inner + 0.005, inner + 0.02, r) * smoothstep(inner + spokeLen + 0.02, inner + spokeLen, r);

    // Spoke separation: cut between spokes.
    float spokeWidth = 6.2832 / spokes;
    float spokeFrac = fract((a / 6.2832 + 0.5) * spokes);
    float spokeMask = smoothstep(0.0, 0.15, spokeFrac) * smoothstep(1.0, 0.85, spokeFrac);

    // Color: hue rotates with time + brightness from energy.
    float h = u_time * 0.25 + spokePos * 2.0;
    vec3 col = 0.55 + 0.45 * sin(vec3(h, h + 2.094, h + 4.189));
    col *= 0.6 + u_energy * 0.6 + u_beat * 0.4;

    // Center pulse from beat.
    float center = exp(-r * 11.0) * (0.4 + u_beat * 1.5);

    float alpha = clamp(bar * spokeMask + center, 0.0, 1.0);
    gl_FragColor = vec4(col * alpha, alpha) * v_fragmentColor;
}
