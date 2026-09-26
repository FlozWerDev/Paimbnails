// dream_beat.glsl — soft "dreamy" effect that breathes with the music.
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

    float step = 0.003 + u_mid * u_intensity * 0.012 + u_beat * 0.010;
    vec4 c0 = texture2D(u_texture, uv);
    vec4 c1 = texture2D(u_texture, uv + vec2( step, 0.0));
    vec4 c2 = texture2D(u_texture, uv - vec2( step, 0.0));
    vec4 c3 = texture2D(u_texture, uv + vec2(0.0,  step));
    vec4 c4 = texture2D(u_texture, uv - vec2(0.0,  step));
    vec4 blurred = (c0 + c1 + c2 + c3 + c4) * 0.2;

    float softness = clamp(u_mid * u_intensity * 0.7 + u_beat * 0.3, 0.0, 0.9);
    vec4 col = mix(c0, blurred, softness);

    float lum = dot(col.rgb, vec3(0.299, 0.587, 0.114));
    float halo = smoothstep(0.55, 0.95, lum);
    col.rgb += halo * vec3(1.0, 0.85, 0.7) * u_bass * u_intensity * 0.8;

    float spark = step(0.997 - u_treble * u_intensity * 0.005,
                       hash(floor(uv * 800.0) + floor(u_time * 20.0)));
    col.rgb += vec3(1.0) * spark * 0.8 * u_treble * u_intensity;

    col.rgb *= 1.0 + u_beat * 0.4;

    col.rgb = mix(col.rgb, col.rgb * vec3(1.05, 0.98, 1.10), u_intensity * 0.25);

    gl_FragColor = col * v_fragmentColor;
}
