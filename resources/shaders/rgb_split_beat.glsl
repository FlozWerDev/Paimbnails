// rgb_split_beat.glsl — heavy chromatic split + zoom pulse on beat.
// Mixes chromatic separation, slight radial zoom, and a vignette darkening
// to give a "drop" feel on each beat onset.
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
    vec2 c = uv - 0.5;
    float r = length(c);

    // Radial zoom pulse on beat — image squeezes/expands.
    float zoom = 1.0 + (u_beat * 0.06 + u_bass * 0.025) * u_intensity;
    uv = c / zoom + 0.5;

    // Chromatic separation grows with bass + treble (treble for fine detail).
    float sepR = (u_bass * 0.020 + u_treble * 0.010 + u_beat * 0.025) * u_intensity;
    float sepB = sepR * 1.15;
    vec2 dir = normalize(c + 1e-5);

    float fr = texture2D(u_texture, uv + dir * sepR).r;
    vec4 g  = texture2D(u_texture, uv);
    float fb = texture2D(u_texture, uv - dir * sepB).b;

    vec3 col = vec3(fr, g.g, fb);

    // Mid drives a hue rotation by mixing R and B based on time.
    float midShift = sin(u_time * 1.5) * u_mid * u_intensity * 0.18;
    col.rb = mix(col.rb, col.br, midShift);

    // Beat brightness lift + slight vignette so contrast feels punchier.
    float vig = smoothstep(1.1, 0.4, r);
    col *= mix(1.0, vig, 0.5);
    col *= 1.0 + u_beat * 0.5 + u_energy * 0.15;

    gl_FragColor = vec4(col, g.a) * v_fragmentColor;
}
