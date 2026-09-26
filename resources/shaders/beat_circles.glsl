// beat_circles.glsl — concentric pulsing rings reactive to the beat pulse.
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

void main() {
    vec2 uv = v_texCoord - 0.5;
    // aspect fudge for 16:9; fine for menu/editor.
    uv.x *= 1.78;
    float r = length(uv);

    float ringSpeed = 0.6 + u_intensity * 0.4 + u_energy * 0.3;
    float t = u_time * ringSpeed;

    float rings = 0.0;
    for (int i = 0; i < 5; i++) {
        float fi = float(i);
        float phase = t - fi * 0.22 + u_beat * 0.35;
        float radius = fract(phase) * (0.7 + u_bass * 0.4);
        float ringMask = exp(-pow((r - radius) * 22.0, 2.0));
        rings += ringMask * (1.0 - fract(phase));
    }
    rings *= 0.6 + u_intensity * 0.6;

    float h = u_time * 0.18 + r * 1.5;
    vec3 col = 0.5 + 0.5 * sin(vec3(h, h + 2.094, h + 4.189));
    col *= 0.55 + u_mid * 0.6 + u_treble * 0.4;

    float center = exp(-r * 14.0) * (0.4 + u_beat * 1.4);

    float alpha = clamp(rings + center, 0.0, 1.0);
    gl_FragColor = vec4(col * alpha, alpha) * v_fragmentColor;
}
