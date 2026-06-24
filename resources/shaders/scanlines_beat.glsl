// scanlines_beat.glsl — CRT-style scanlines whose density and brightness pulse
// with the audio. Treble drives the scan frequency, beat the bright bursts.
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

    // Mild horizontal wobble from bass.
    uv.x += sin(uv.y * 25.0 + u_time * 2.0) * u_bass * u_intensity * 0.004;

    vec4 col = texture2D(u_texture, uv);

    // Scanline density grows with treble.
    float density = mix(180.0, 480.0, clamp(u_treble * u_intensity, 0.0, 1.0));
    float scan = sin(uv.y * density + u_time * 10.0) * 0.5 + 0.5;
    scan = mix(0.85, 1.05, scan);
    col.rgb *= scan;

    // A second slower scan band that scrolls.
    float bandY = fract(u_time * 0.15);
    float band = exp(-pow((uv.y - bandY) * 8.0, 2.0));
    col.rgb += vec3(0.4, 0.7, 1.0) * band * u_beat * 0.6 * u_intensity;

    // Vignette + slight desaturation for that CRT feel.
    float r = length(uv - 0.5);
    col.rgb *= 1.0 - r * 0.3;
    float lum = dot(col.rgb, vec3(0.299, 0.587, 0.114));
    col.rgb = mix(vec3(lum), col.rgb, 0.85);

    // Beat brightness boost.
    col.rgb *= 1.0 + u_beat * 0.25;

    gl_FragColor = col * v_fragmentColor;
}
