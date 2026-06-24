// shockwave_beat.glsl — radial ring shockwaves emitted on beat onset.
// Several rings travel outward from the screen center, distorting UVs.
// Bass keeps a steady ambient ripple, beat fires sharp transient waves.
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
    vec2 dir = normalize(c + 1e-5);

    float ambient = 0.0;
    // Ambient radial wobble from bass.
    ambient += sin(r * 22.0 - u_time * 5.0) * u_bass * 0.012;
    ambient += sin(r * 40.0 - u_time * 9.0) * u_mid  * 0.008;

    // Beat-fired transient ring: a fast-traveling pulse with falloff.
    // Phase fract gives a moving 0..1 ring; we shape it sharply.
    float phase = fract(u_time * 0.9);
    float ringR = phase * 0.9;
    float ringWidth = 0.05;
    float ring = exp(-pow((r - ringR) / ringWidth, 2.0)) * (1.0 - phase) * u_beat;

    float disp = (ambient + ring * 0.06) * u_intensity;

    vec2 uv = v_texCoord + dir * disp;

    vec4 col = texture2D(u_texture, uv);

    // Ring foreground tint — boosts brightness along the ring.
    col.rgb += vec3(0.6, 0.7, 1.0) * ring * 0.35 * u_intensity;

    // Treble shimmer on edges.
    col.rgb *= 1.0 + u_treble * u_intensity * 0.18;

    gl_FragColor = col * v_fragmentColor;
}
