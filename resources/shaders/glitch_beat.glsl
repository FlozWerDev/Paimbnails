// glitch_beat.glsl — RGB-channel glitch that intensifies with bass.
// Input: u_texture is the underlying background (image / video frame / GIF).
// Output: distorted version of that texture.
//
// Audio uniforms:
//   u_bass   — drives horizontal slicing offset
//   u_mid    — drives chromatic separation amount
//   u_treble — drives noise grain
//   u_beat   — instantaneous onset → flash spike
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

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    vec2 uv = v_texCoord;

    float bass   = u_bass   * u_intensity;
    float mid    = u_mid    * u_intensity;
    float treble = u_treble * u_intensity;
    float beat   = u_beat   * u_intensity;

    // Horizontal slicing — strips of pixels jump sideways when bass kicks.
    float sliceY = floor(uv.y * 24.0) / 24.0;
    float sliceJump = (hash(vec2(sliceY, floor(u_time * 4.0))) - 0.5) * bass * 0.08;
    sliceJump += (hash(vec2(sliceY + 0.1, floor(u_time * 12.0))) - 0.5) * beat * 0.20;
    uv.x += sliceJump;

    // Chromatic separation grows with mid.
    float sep = mid * 0.012 + beat * 0.018;
    vec4 r = texture2D(u_texture, uv + vec2( sep, 0.0));
    vec4 g = texture2D(u_texture, uv);
    vec4 b = texture2D(u_texture, uv - vec2( sep, 0.0));

    vec3 col = vec3(r.r, g.g, b.b);

    // Treble adds high-frequency noise grain.
    float grain = (hash(uv + u_time) - 0.5) * treble * 0.25;
    col += grain;

    // Beat flash — quick brightness spike.
    col *= 1.0 + beat * 0.45;

    gl_FragColor = vec4(col, g.a) * v_fragmentColor;
}
