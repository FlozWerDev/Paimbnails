// mosaic_beat.glsl — voronoi-like cell mosaic where cells pulse and shift
// with the beat. Bass increases cell size, mid colors them, treble adds
// shimmer, beat brightens.
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
    // Cell density: more bass = bigger blocks.
    float density = mix(40.0, 12.0, clamp(u_bass * u_intensity, 0.0, 1.0));
    vec2 grid = floor(v_texCoord * density);
    vec2 cellUV = (grid + 0.5) / density;

    // Each cell jitters its sample point a bit with time.
    vec2 jitter = vec2(
        hash(grid + vec2(0.0, floor(u_time * 2.0))) - 0.5,
        hash(grid + vec2(floor(u_time * 2.0), 0.0)) - 0.5
    ) * u_mid * u_intensity * 0.03;

    vec4 base = texture2D(u_texture, cellUV + jitter);

    // Per-cell pulse driven by beat — random subset flashes.
    float cellRand = hash(grid * 0.37);
    float pulse = step(1.0 - u_beat * 0.6, cellRand);
    base.rgb += vec3(0.6, 0.7, 1.0) * pulse * u_beat * 0.5;

    // Treble adds local cell color flicker.
    float flick = sin(u_time * 14.0 + cellRand * 30.0) * 0.5 + 0.5;
    base.rgb *= mix(1.0, flick * 0.6 + 0.7, u_treble * u_intensity * 0.4);

    // Cell border darkening for visible mosaic look at high intensity.
    vec2 cellLocal = fract(v_texCoord * density);
    float border = min(min(cellLocal.x, 1.0 - cellLocal.x),
                       min(cellLocal.y, 1.0 - cellLocal.y));
    float borderFactor = smoothstep(0.0, 0.05, border);
    base.rgb *= mix(1.0, borderFactor * 0.85 + 0.15, u_intensity * 0.5);

    // Beat brightness.
    base.rgb *= 1.0 + u_beat * 0.3 + u_energy * 0.1;

    gl_FragColor = base * v_fragmentColor;
}
