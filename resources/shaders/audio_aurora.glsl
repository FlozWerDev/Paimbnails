// audio_aurora.glsl — flowing aurora bands whose intensity & color shifts with
// the frequency mix. The waves curve faster on bass; treble adds spark trails.
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
    float t = u_time * (0.25 + u_intensity * 0.4 + u_bass * 0.3);

    // Flowing curtain — three sine layers with audio-reactive phase.
    float w1 = sin(uv.x * 3.0 + t)              * 0.20;
    float w2 = sin(uv.x * 5.5 - t * 1.4 + u_mid * 1.5) * 0.14;
    float w3 = sin(uv.x * 8.5 + t * 0.7 + u_bass * 1.2) * 0.08;
    float curtain = w1 + w2 + w3 + 0.5;

    // Vertical fall-off from curtain line — softer with energy.
    float dist = abs(uv.y - curtain);
    float falloff = exp(-dist * (10.0 - u_energy * 4.0));

    // Color shifts with frequency mix.
    vec3 colA = vec3(0.10, 0.85, 0.55); // green base
    vec3 colB = vec3(0.30, 0.55, 1.00); // blue mid
    vec3 colC = vec3(1.00, 0.30, 0.85); // pink high
    vec3 col = mix(colA, colB, u_mid);
    col      = mix(col,  colC, u_treble);
    col *= 0.6 + u_energy * 0.7;

    // Treble spark trails (small noise specks).
    float sparks = step(0.985 - u_treble * 0.015, hash(floor(uv * 600.0))) * u_treble;
    col += vec3(1.0) * sparks;

    // Beat flash brightens entire curtain briefly.
    col *= 1.0 + u_beat * 0.5;

    float alpha = clamp(falloff * 0.85 + sparks, 0.0, 1.0);
    gl_FragColor = vec4(col * alpha, alpha) * v_fragmentColor;
}
