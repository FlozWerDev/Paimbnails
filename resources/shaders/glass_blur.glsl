// Liquid Glass dual-kawase pass. Downsample: centre x4 + 4 diagonals (/8).
// Upsample: 4 axis taps at 2x + 4 diagonals x2 (/12). Every tap is clamped
// to this capture's rectangle: the shared copy texture holds other regions.
#ifdef GL_ES
precision mediump float;
varying mediump vec2 v_texCoord;
#else
varying vec2 v_texCoord;
#endif
uniform sampler2D u_texture;
uniform vec4 u_step;  // xy: tap offset in uv (half texel * spread)
uniform vec4 u_clamp; // uv min (xy) and max (zw) of the source content
uniform float u_up;   // 0 = downsample, 1 = upsample

vec3 tap(vec2 uv) {
    return texture2D(u_texture, clamp(uv, u_clamp.xy, u_clamp.zw)).rgb;
}

void main() {
    vec2 o = u_step.xy;
    vec2 uv = v_texCoord;
    vec3 sum;
    if (u_up < 0.5) {
        sum = tap(uv) * 4.0;
        sum += tap(uv - o);
        sum += tap(uv + o);
        sum += tap(uv + vec2(o.x, -o.y));
        sum += tap(uv - vec2(o.x, -o.y));
        sum *= 0.125;
    } else {
        sum = tap(uv + vec2(-2.0 * o.x, 0.0));
        sum += tap(uv + vec2(2.0 * o.x, 0.0));
        sum += tap(uv + vec2(0.0, -2.0 * o.y));
        sum += tap(uv + vec2(0.0, 2.0 * o.y));
        sum += tap(uv + o) * 2.0;
        sum += tap(uv - o) * 2.0;
        sum += tap(uv + vec2(o.x, -o.y)) * 2.0;
        sum += tap(uv - vec2(o.x, -o.y)) * 2.0;
        sum *= (1.0 / 12.0);
    }
    gl_FragColor = vec4(sum, 1.0);
}
