// edge_pulse_beat.glsl — Sobel-style edge detection where edges glow neon
// and pulse with the beat. Original colors fade in/out with energy.
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;

uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;
uniform float u_time;
uniform float u_bass;
uniform float u_mid;
uniform float u_treble;
uniform float u_beat;
uniform float u_energy;

float lum(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
    vec2 px = 1.0 / max(u_texSize, vec2(2.0));

    // Sobel kernel — 8 neighbor samples.
    float lTL = lum(texture2D(u_texture, v_texCoord + vec2(-px.x,  px.y)).rgb);
    float lT  = lum(texture2D(u_texture, v_texCoord + vec2( 0.0,   px.y)).rgb);
    float lTR = lum(texture2D(u_texture, v_texCoord + vec2( px.x,  px.y)).rgb);
    float lL  = lum(texture2D(u_texture, v_texCoord + vec2(-px.x,  0.0  )).rgb);
    float lR  = lum(texture2D(u_texture, v_texCoord + vec2( px.x,  0.0  )).rgb);
    float lBL = lum(texture2D(u_texture, v_texCoord + vec2(-px.x, -px.y)).rgb);
    float lB  = lum(texture2D(u_texture, v_texCoord + vec2( 0.0,  -px.y)).rgb);
    float lBR = lum(texture2D(u_texture, v_texCoord + vec2( px.x, -px.y)).rgb);

    float gx = -lTL - 2.0 * lL - lBL + lTR + 2.0 * lR + lBR;
    float gy = -lTL - 2.0 * lT - lTR + lBL + 2.0 * lB + lBR;
    float edge = clamp(sqrt(gx * gx + gy * gy), 0.0, 1.0);

    // Edge thickness modulated by bass.
    edge = pow(edge, mix(2.0, 0.6, clamp(u_bass * u_intensity, 0.0, 1.0)));

    // Original color attenuated, edges in neon.
    vec4 base = texture2D(u_texture, v_texCoord);
    float h = u_time * 0.25 + v_texCoord.x * 0.6;
    vec3 neon = 0.5 + 0.5 * sin(vec3(h, h + 2.094, h + 4.189));
    neon *= 1.5 + u_beat * 1.5 + u_energy * 0.4;

    // Fade base by intensity (so high intensity → edges-only neon).
    vec3 col = mix(base.rgb, vec3(0.05, 0.05, 0.08), 0.6 * u_intensity);
    col += neon * edge * (0.7 + u_intensity * 0.5);

    // Treble shimmer on edges.
    col += vec3(u_treble * edge * 0.4 * u_intensity);

    gl_FragColor = vec4(col, base.a) * v_fragmentColor;
}
