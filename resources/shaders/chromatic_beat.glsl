// chromatic_beat.glsl — chromatic aberration that pulses with the beat.
// Sample R/G/B from the same texture at slightly offset UVs, separation
// grows with bass + beat onset.
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

    // Radial separation amount — bigger toward edges, scales with bass + beat.
    float sep = (u_intensity * 0.005)
              + (u_bass * 0.020 * u_intensity)
              + (u_beat * 0.030 * u_intensity);
    sep *= 0.4 + r * 0.9;

    // Direction: from center outward.
    vec2 dir = normalize(c + 1e-5);
    vec4 cr = texture2D(u_texture, uv + dir * sep);
    vec4 cg = texture2D(u_texture, uv);
    vec4 cb = texture2D(u_texture, uv - dir * sep);

    vec3 col = vec3(cr.r, cg.g, cb.b);

    // Treble adds subtle scanline-like luma flicker.
    float scan = sin(uv.y * 600.0 + u_time * 4.0) * 0.5 + 0.5;
    scan = mix(1.0, scan, u_treble * u_intensity * 0.25);
    col *= scan;

    // Beat brightness pulse.
    col *= 1.0 + u_beat * 0.4 + u_energy * 0.12;

    gl_FragColor = vec4(col, cg.a) * v_fragmentColor;
}
