// pixelate_beat.glsl — variable pixel size driven by bass.
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;

uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;
uniform vec2 u_texSize;
uniform float u_bass;
uniform float u_mid;
uniform float u_treble;
uniform float u_beat;
uniform float u_energy;

void main() {
    float minPx = mix(1.0, 4.0, u_intensity);
    float maxPx = mix(8.0, 32.0, u_intensity);
    float beatBoost = clamp(u_bass * 0.6 + u_beat * 0.8, 0.0, 1.0);
    float px = mix(minPx, maxPx, beatBoost);

    float p = max(1.0, floor(px));

    vec2 size = max(u_texSize, vec2(2.0));
    vec2 quantUV = (floor(v_texCoord * size / p) + 0.5) * (p / size);

    quantUV.x += sin(u_time * 0.6 + quantUV.y * 3.0) * u_mid * u_intensity * 0.012;

    vec4 col = texture2D(u_texture, quantUV);

    float fl = 0.5 + 0.5 * sin(u_time * 12.0 + quantUV.y * 50.0);
    col.rgb *= mix(1.0, fl, u_treble * u_intensity * 0.20);

    col.rgb *= 1.0 + u_beat * 0.35;

    gl_FragColor = col * v_fragmentColor;
}
