// vortex_beat.glsl — swirl/whirlpool effect whose strength and rotation
// speed react to the beat.
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

    // Swirl strength: at rest small, big with bass+beat.
    float swirl = (u_bass * 0.6 + u_beat * 0.9) * u_intensity;

    // Rotation falls off toward edges, peaks near center.
    float angle = swirl * smoothstep(0.7, 0.0, r) * 4.0;

    // Continuous rotation with mid frequency.
    angle += sin(u_time * 0.7 + r * 4.0) * u_mid * u_intensity * 0.4;

    float ca = cos(angle), sa = sin(angle);
    c = mat2(ca, -sa, sa, ca) * c;

    // Treble shimmer ripple.
    c *= 1.0 + sin(r * 30.0 - u_time * 6.0) * u_treble * u_intensity * 0.01;

    vec4 col = texture2D(u_texture, c + 0.5);

    // Vignette + beat brightness.
    col.rgb *= mix(0.6, 1.0, smoothstep(1.2, 0.0, r));
    col.rgb *= 1.0 + u_beat * 0.35;

    gl_FragColor = col * v_fragmentColor;
}
