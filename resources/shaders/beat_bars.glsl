// beat_bars.glsl — frequency bar visualizer reactive to bass/mid/treble.
// Procedural overlay shader (no texture) — composited over the layer's
// existing background. The vertex shader shared by all paimbnails procedural
// backgrounds (cell_vertex.glsl) provides v_texCoord in [0,1].
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

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    // Number of bars depends on intensity for stylistic control.
    float numBars = mix(16.0, 48.0, u_intensity);
    float barIdx = floor(v_texCoord.x * numBars);
    float barX   = fract(v_texCoord.x * numBars);

    // Map bar index to a frequency band (smooth blend bass→mid→treble).
    float pos = barIdx / max(1.0, numBars - 1.0);
    float fLow  = smoothstep(0.0, 0.4, 1.0 - pos);
    float fHigh = smoothstep(0.55, 1.0, pos);
    float fMid  = 1.0 - fLow - fHigh;
    float freq  = u_bass * fLow + u_mid * fMid + u_treble * fHigh;

    // Per-bar randomization to avoid mechanical look.
    float jitter = hash(vec2(barIdx, 0.0)) * 0.25;
    float height = clamp(freq * (0.85 + jitter) + u_beat * 0.15, 0.0, 1.0);
    height *= mix(0.5, 1.2, u_intensity);

    // Bar mask: gap between bars + height threshold from the bottom.
    float gap = 0.10;
    float barShape = smoothstep(gap * 0.5, gap, barX) * smoothstep(1.0 - gap * 0.5, 1.0 - gap, barX);
    float bar = step(v_texCoord.y, height) * barShape;

    // Color: rainbow gradient horizontally + brighten on beat.
    vec3 colA = vec3(0.10, 0.55, 1.00);
    vec3 colB = vec3(1.00, 0.30, 0.55);
    vec3 colC = vec3(1.00, 0.85, 0.20);
    vec3 col  = mix(colA, colB, pos);
    col       = mix(col, colC, smoothstep(0.6, 1.0, pos));
    col      *= 1.0 + u_beat * 0.6 + u_energy * 0.2;

    // Reflection glow on top of each bar.
    float topGlow = smoothstep(height - 0.04, height, v_texCoord.y) * smoothstep(height + 0.06, height, v_texCoord.y);
    col += vec3(1.0, 1.0, 1.0) * topGlow * 0.7 * barShape;

    float alpha = bar + topGlow * barShape * 0.6;
    gl_FragColor = vec4(col * alpha, alpha) * v_fragmentColor;
}
