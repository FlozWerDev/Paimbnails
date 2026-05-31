// beat_tunnel.glsl — depth-tunnel reactive overlay. Bass pushes the tunnel
// outward, treble adds chromatic shimmer, beat punches a flash through the
// center.
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
    vec2 uv = v_texCoord - 0.5;
    uv.x *= 1.78;
    float r = length(uv) + 0.0001;
    float a = atan(uv.y, uv.x);

    // Tunnel depth: faster perspective with bass.
    float speed = 0.6 + u_intensity * 0.5 + u_bass * 0.6;
    float depth = u_time * speed + 0.5 / r;

    // Concentric rings spinning.
    float rings = 0.5 + 0.5 * sin(depth * 12.0 + a * 6.0);
    rings = pow(rings, 3.0);

    // Treble adds high-frequency chromatic shimmer.
    float shimmer = 0.5 + 0.5 * sin(depth * 32.0 + a * 18.0 + u_time * 6.0);
    shimmer *= u_treble * 0.6;

    // Color: rotate hue with depth.
    float h = depth * 0.6 + u_time * 0.18;
    vec3 col = 0.5 + 0.5 * sin(vec3(h, h + 2.094, h + 4.189));
    col *= rings * (0.6 + u_mid * 0.6) + shimmer;

    // Bass-driven warp + center punch on beat.
    float vignette = smoothstep(1.4, 0.0, r);
    float beatFlash = exp(-r * 9.0) * u_beat * 1.5;
    col += vec3(1.0, 0.9, 0.8) * beatFlash;

    col *= vignette * (0.7 + u_energy * 0.6);

    float alpha = clamp(length(col) * 0.7, 0.0, 1.0);
    gl_FragColor = vec4(col, alpha) * v_fragmentColor;
}
