// final composite; inverse pairs cancel with no other effects.

varying vec2 v_texCoord;

uniform sampler2D u_scene;
uniform sampler2D u_gi;
uniform sampler2D u_bloom;
uniform sampler2D u_rays;
uniform sampler2D u_adapt;
uniform vec2  u_texel;
uniform vec2  u_giTexel;
uniform float u_time;

uniform float u_mix;
uniform float u_giStrength;
uniform float u_aoStrength;
uniform float u_bloomStrength;
uniform float u_rayStrength;

uniform float u_tonemap;
uniform float u_exposure;
uniform float u_adaptKey;
uniform float u_contrast;
uniform float u_saturation;
uniform float u_temperature;
uniform float u_tint;
uniform float u_gammaV;

uniform float u_ca;
uniform float u_vignette;
uniform float u_grain;
uniform float u_sharpen;

// cas eases at extremes to avoid halos.
vec3 sharpenCAS(vec2 uv, vec3 c, float amount) {
    vec3 n = texture2D(u_scene, uv + vec2(0.0,  u_texel.y)).rgb;
    vec3 s = texture2D(u_scene, uv - vec2(0.0,  u_texel.y)).rgb;
    vec3 e = texture2D(u_scene, uv + vec2(u_texel.x, 0.0)).rgb;
    vec3 w = texture2D(u_scene, uv - vec2(u_texel.x, 0.0)).rgb;

    vec3 mn = min(min(min(n, s), min(e, w)), c);
    vec3 mx = max(max(max(n, s), max(e, w)), c);
    vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, 0.0001), 0.0, 1.0));
    // clamp amount to keep the denominator alive.
    vec3 k = -amp * min(amount, 3.0) * 0.2;

    return ((c + (n + s + e + w) * k) / max(1.0 + 4.0 * k, vec3(0.2))) - c;
}

// bilateral gi upsample avoids edge bleed.
vec4 giUpsample(vec2 uv, vec3 guideC) {
    float guideL = luma(guideC);
    vec2 o = max(u_giTexel, vec2(0.0000001)) * 0.5;
    vec4 sum = vec4(0.0);
    float wsum = 0.0;
    for (int k = 0; k < 4; k++) {
        vec2 d = uv + o * vec2(float(k - (k / 2) * 2) * 2.0 - 1.0,
                               float(k / 2) * 2.0 - 1.0);
        vec3 gc = texture2D(u_scene, d).rgb;
        float wgt = exp(-(abs(luma(gc) - guideL) * 28.0 + distance(gc, guideC) * 14.0));
        sum += texture2D(u_gi, d) * wgt;
        wsum += wgt;
    }
    return sum / max(wsum, 0.0001);
}

void main() {
    vec2 uv = v_texCoord;
    vec3 original = texture2D(u_scene, uv).rgb;

    vec3 shown = original;
    if (u_ca > 0.0) {
        vec2 d = uv - 0.5;
        vec2 off = d * dot(d, d) * u_ca * 0.24;
        shown.r = texture2D(u_scene, uv + off).r;
        shown.b = texture2D(u_scene, uv - off).b;
    }

    vec3 lin = toLinear(shown);
    vec3 hdr = tonemapInverse(lin, u_tonemap);

    vec4 traced = giUpsample(uv, original);

    // ao skips emitters, corners only.
    float aoMask = 1.0 - safeSmoothstep(0.35, 1.0, luma(lin));
    hdr *= mix(1.0, clamp(traced.a, 0.0, 1.0), clamp(u_aoStrength * aoMask, 0.0, 1.0));
    // soft >1 rolloff kills fireflies.
    hdr += softClampHi(traced.rgb) * u_giStrength;
    hdr += texture2D(u_bloom, uv).rgb * u_bloomStrength;
    hdr += texture2D(u_rays, uv).rgb * u_rayStrength;

    // clamp exp2 against inf/nan.
    float ev = exp2(clamp(u_exposure, -8.0, 8.0));
    if (u_adaptKey > 0.0) {
        ev *= clamp(u_adaptKey / max(texture2D(u_adapt, vec2(0.5)).r, 0.0005), 0.35, 3.0);
    }
    hdr *= ev;
    hdr = clamp(hdr, vec3(0.0), vec3(64.0));

    vec3 col = toDisplay(clamp(tonemapApply(hdr, u_tonemap), 0.0, 1.0));

    col.r *= 1.0 + u_temperature * 0.25;
    col.b *= 1.0 - u_temperature * 0.25;
    col.g *= 1.0 + u_tint * 0.15;

    col = (col - 0.5) * u_contrast + 0.5;
    col = mix(vec3(luma(col)), col, u_saturation);
    col = pow(max(col, 0.0), vec3(1.0 / max(u_gammaV, 0.05)));

    if (u_sharpen > 0.0) col += sharpenCAS(uv, original, u_sharpen);

    if (u_vignette > 0.0) {
        vec2 vd = (uv - 0.5) * 2.0 * vec2(u_texel.y / max(u_texel.x, 0.000001), 1.0);
        // inverted smoothstep fails on angle.
        col *= 1.0 - safeSmoothstep(0.30, 1.55, length(vd)) * min(u_vignette, 1.6) * 0.55;
    }

    if (u_grain > 0.0) {
        float fr = floor(u_time * 60.0);
        vec2 goff = vec2(halton(mod(fr, 1024.0) + 1.0, 2.0),
                         halton(mod(fr, 1024.0) + 1.0, 3.0)) * 1024.0;
        float resp = 1.0 - abs(luma(col) * 2.0 - 1.0);
        col += (hash12(gl_FragCoord.xy + goff) - 0.5) * u_grain * 0.09 * resp;
    }

    col = mix(original, col, clamp(u_mix, 0.0, 1.0));

    // 1-lsb dither kills gradient banding.
    col += (hash12(gl_FragCoord.xy + 0.5) - hash12(gl_FragCoord.yx + 7.3)) * 0.0039;

    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
