// beat_grid.glsl — neon grid pulsing with bass + lines glowing on beat.
// Designed to overlay any background — outputs near-zero alpha on grid gaps.
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;

uniform float u_time;
uniform float u_intensity;
uniform float u_bass;
uniform float u_mid;
uniform float u_treble;
uniform float u_beat;
uniform float u_energy;

void main() {
    vec2 uv = v_texCoord;

    // Grid density grows with intensity setting.
    float density = mix(12.0, 32.0, u_intensity);

    // Cell-local UV [0,1) inside each cell.
    vec2 cellUV = fract(uv * density);
    float lineW = mix(0.04, 0.10, u_bass) + u_beat * 0.08;

    float vert  = smoothstep(0.0, lineW, cellUV.x) * smoothstep(1.0, 1.0 - lineW, cellUV.x);
    float horiz = smoothstep(0.0, lineW, cellUV.y) * smoothstep(1.0, 1.0 - lineW, cellUV.y);
    float line  = clamp(1.0 - vert * horiz, 0.0, 1.0);

    // Per-cell flicker from mid frequencies.
    vec2 cell = floor(uv * density);
    float flicker = 0.5 + 0.5 * sin(u_time * 4.0 + cell.x * 12.7 + cell.y * 7.3);
    flicker = mix(0.4, 1.0, flicker) * (0.5 + u_mid * 1.2);

    // Color shifts slightly with treble.
    vec3 colA = vec3(0.20, 0.95, 0.85);
    vec3 colB = vec3(0.95, 0.30, 1.00);
    vec3 col  = mix(colA, colB, smoothstep(0.0, 1.0, u_treble));
    col *= 1.0 + u_beat * 0.8 + u_energy * 0.3;

    float alpha = line * flicker * 0.85;
    gl_FragColor = vec4(col * alpha, alpha) * v_fragmentColor;
}
