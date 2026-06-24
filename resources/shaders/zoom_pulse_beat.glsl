// zoom_pulse_beat.glsl — radial zoom that pumps with the beat.
// Mid drives a slight rotation; treble adds barrel distortion shimmer.
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

    // Zoom factor: larger on beat onset, mild expansion with bass.
    float zoom = 1.0
               + u_beat * 0.10 * u_intensity
               + u_bass * 0.03 * u_intensity
               - u_energy * 0.02;
    c /= zoom;

    // Mid-frequency-driven slow rotation.
    float ang = (u_mid - 0.5) * u_intensity * 0.4;
    float ca = cos(ang), sa = sin(ang);
    c = mat2(ca, -sa, sa, ca) * c;

    // Treble barrel shimmer.
    c *= 1.0 + sin(r * 18.0 + u_time * 4.0) * u_treble * u_intensity * 0.015;

    vec2 uv = c + 0.5;
    vec4 col = texture2D(u_texture, uv);

    // Beat brightens; mid color-shifts subtly.
    col.rgb *= 1.0 + u_beat * 0.40;
    col.rb = mix(col.rb, col.br, sin(u_time * 1.3) * u_mid * u_intensity * 0.10);

    gl_FragColor = col * v_fragmentColor;
}
