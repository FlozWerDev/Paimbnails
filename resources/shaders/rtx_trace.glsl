// Trazado en pantalla: altura desde luma+saturacion.
// Direcciones estratificadas con rotacion IGN para bajar varianza.
// Pasos geometricos; altura/emision en sRGB, rebote en lineal.

varying vec2 v_texCoord;

uniform sampler2D u_scene;
uniform vec2  u_texel;
uniform float u_frame;

uniform float u_rayCount;
uniform float u_raySteps;
uniform float u_rayDistance;
uniform float u_stepGrowth;
uniform float u_lightThreshold;
uniform float u_lightRange;
uniform float u_bounceFalloff;
uniform float u_giSaturation;
uniform float u_normalStrength;
uniform float u_thickness;
uniform float u_aoRadius;
uniform float u_aoPower;
uniform float u_reflectStrength;
uniform float u_reflectRoughness;
uniform float u_reflectFresnel;
uniform float u_reflectFade;

const int   kMaxRays  = 16;
const int   kMaxSteps = 32;
const int   kSpecTaps = 4;
const float kTau      = 6.28318531;
const float kPi       = 3.14159265;
const float kF0       = 0.04;
const float kGlassIor = 1.33;
// Mascara de vidrio: luma alta y saturacion baja.
const float kTransLuma0 = 0.55;
const float kTransLuma1 = 0.85;
const float kTransSat0  = 0.05;
const float kTransSat1  = 0.25;

float heightOf(vec3 c) {
    float sat = max(max(c.r, c.g), c.b) - min(min(c.r, c.g), c.b);
    return luma(c) * 0.78 + sat * 0.22;
}

float heightAt(vec2 uv) {
    return heightOf(texture2D(u_scene, uv).rgb);
}

float emissiveOf(vec3 c, float range) {
    return safeSmoothstep(u_lightThreshold, u_lightThreshold + range, luma(c));
}

// IGN con offset por frame: baja discrepancia temporal.
float ign(vec2 p, float frame) {
    p += 5.588238 * mod(frame, 64.0);
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

bool outside(vec2 p) {
    return p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0;
}

void main() {
    vec2 uv = v_texCoord;

    // Clamp defensivo: un JSON a mano no debe meter NaN/Inf.
    float steps = clamp(u_raySteps, 1.0, float(kMaxSteps));
    float dist  = max(u_rayDistance, 0.001);
    float range = max(u_lightRange, 0.0001);
    float aoR   = clamp(u_aoRadius / dist, 0.0, 1.0);
    float fadeR = max(u_reflectFade, 0.01);
    float thick = max(u_thickness, 0.001);
    float rayN  = max(u_rayCount, 1.0);
    float fall  = max(u_bounceFalloff, 0.0);

    vec3 c0 = texture2D(u_scene, uv).rgb;
    if (!all(equal(c0, c0))) c0 = vec3(0.0);
    float h0 = heightOf(c0);
    float hL = heightAt(uv - vec2(u_texel.x, 0.0));
    float hR = heightAt(uv + vec2(u_texel.x, 0.0));
    float hD = heightAt(uv - vec2(0.0, u_texel.y));
    float hU = heightAt(uv + vec2(0.0, u_texel.y));

    vec3 n = normalize(vec3((hL - hR) * u_normalStrength,
                            (hD - hU) * u_normalStrength,
                            1.0));
    float slopeMag = (abs(hR - hL) + abs(hU - hD)) * 0.5;

    // 4 streams IGN separados para no correlacionar difuso y especular.
    vec2 frag = gl_FragCoord.xy;
    float rotA   = ign(frag, u_frame);
    float rPhase = ign(frag + 11.31, u_frame + 57.0);
    float specU  = ign(frag + vec2(43.17, 17.29), u_frame + 113.0);
    float specV  = ign(frag.yx + vec2(29.53, 13.37), u_frame + 171.0);

    float g = clamp(u_stepGrowth, 1.0001, 2.0);
    float invSpan = 1.0 / max(pow(g, steps) - 1.0, 0.0001);
    float gStart = pow(g, rPhase);

    vec3  gi   = vec3(0.0);
    float occ  = 0.0;
    float wsum = 0.0;

    for (int i = 0; i < kMaxRays; i++) {
        if (float(i) >= u_rayCount) break;

        // Estratificado con jitter: reparte el giro sin reagrupar rayos.
        float j = hash12(frag + vec2(float(i) * 12.91, float(i) * 7.73)
                         + fract(u_frame * 0.159) * 43.7);
        float a = ((float(i) + j) / rayN + rotA) * kTau;
        vec2 dirPx = vec2(cos(a), sin(a));

        // Sin luz trasera; peso 0 salta la marcha.
        float w = max(dot(dirPx, n.xy) * 0.5 + 0.5, 0.0);
        wsum += w;
        if (w <= 0.0001) continue;

        // Marcha isotropa en pixeles para no depender del aspecto.
        vec2 dirUV = dirPx * u_texel;
        dirUV /= max(length(dirUV), 0.0000001);

        float gPow = gStart;

        for (int s = 0; s < kMaxSteps; s++) {
            if (float(s) >= steps) break;

            float t = dist * (gPow - 1.0) * invSpan;
            gPow *= g;

            vec2 p = uv + dirUV * t;
            if (outside(p)) break;

            vec3 c = texture2D(u_scene, p).rgb;
            // Tolerancia conica con sesgo por pendiente contra el acne.
            float tN = t / dist;
            float tol = h0 + slopeMag * thick * 0.5
                      + n.z * t * thick * (0.75 + 0.5 * tN);
            if (heightOf(c) > tol) {
                float f = exp(-tN * fall);
                vec3 lit = toLinear(c);
                vec3 tinted = mix(vec3(luma(lit)), lit, clamp(u_giSaturation, 0.0, 2.0));
                gi  += tinted * emissiveOf(c, range) * f * w;
                occ += (1.0 - safeSmoothstep(0.0, aoR, tN)) * w;
                break;
            }
        }
    }

    float inv = 1.0 / max(wsum, 0.0001);
    gi  *= inv;
    occ *= inv;
    float ao = 1.0 - pow(clamp(occ, 0.0, 1.0), max(u_aoPower, 0.05));

    vec3 refl = vec3(0.0);
    vec3 trans = vec3(0.0);
    float rStr = clamp(u_reflectStrength, 0.0, 4.0);
    if (rStr > 0.001) {
        // GGX con Smith y Schlick; 4 taps solo si hay reflejo.
        vec3 vDir = vec3(0.0, 0.0, 1.0);
        float ndv = clamp(n.z, 0.0, 1.0);
        float rough = clamp(u_reflectRoughness, 0.0, 1.0);
        float alpha = max(rough * rough, 0.001);
        float alpha2 = alpha * alpha;
        float fresAmt = clamp(u_reflectFresnel, 0.0, 1.0);

        // Tangente con fallback a +X si la normal es plana.
        vec3 tDir = vec3(-n.y, n.x, 0.0);
        float tLen = length(tDir);
        if (tLen > 0.0001) {
            tDir /= tLen;
        } else {
            tDir = vec3(1.0, 0.0, 0.0);
        }
        vec3 bDir = cross(n, tDir);

        // Reflejo con menos pasos; reparto recalculado al alcance.
        float reflSteps = max(4.0, steps * 0.6);
        float reflSpan = 1.0 / max(pow(g, reflSteps) - 1.0, 0.0001);

        vec3 specAcc = vec3(0.0);
        for (int k = 0; k < kSpecTaps; k++) {
            // Estratos 2x2 con Cranley-Patterson, sin tocar el difuso.
            float kx = mod(float(k), 2.0);
            float ky = floor(float(k) * 0.5);
            float u1 = fract((kx + specU) * 0.5);
            float u2 = fract((ky + specV) * 0.5);

            float phi = u1 * kTau;
            float cosT = sqrt(max((1.0 - u2) / max(1.0 + (alpha2 - 1.0) * u2, 0.0001), 0.0));
            float sinT = sqrt(max(1.0 - cosT * cosT, 0.0));
            vec3 h = tDir * (sinT * cos(phi)) + bDir * (sinT * sin(phi)) + n * cosT;

            float vdh = clamp(dot(vDir, h), 0.0, 1.0);
            float ndh = clamp(dot(n, h), 0.0, 1.0);
            if (ndh <= 0.0001 || vdh <= 0.0001) continue;

            vec3 lDir = 2.0 * vdh * h - vDir;
            float ndl = dot(n, lDir);
            if (ndl <= 0.0) continue;
            float ndlc = clamp(ndl, 0.0, 1.0);

            // D se cancela con la pdf para acotar energia.
            float ggxD = alpha2 / max(kPi * pow(ndh * ndh * (alpha2 - 1.0) + 1.0, 2.0), 0.0000001);
            float kk = alpha * 0.5;
            float gV = ndv / max(ndv * (1.0 - kk) + kk, 0.0001);
            float gL = ndlc / max(ndlc * (1.0 - kk) + kk, 0.0001);
            float ggxG = gV * gL;
            float fres = kF0 + (1.0 - kF0) * pow(max(1.0 - vdh, 0.0), 5.0);
            float fMix = mix(1.0, fres, fresAmt);
            float pdf = ggxD * ndh / max(4.0 * vdh, 0.0001);
            float fNdotL = ggxD * ggxG * fMix / max(4.0 * ndv, 0.0001);
            float thru = clamp(fNdotL / max(pdf, 0.0001), 0.0, 1.0);
            if (thru <= 0.0001) continue;

            vec2 rd = lDir.xy;
            float rl = length(rd);
            if (rl <= 0.0001) {
                // En espejo plano se reutiliza el azimut muestreado.
                rd = vec2(cos(phi), sin(phi));
            } else {
                rd /= max(rl, 0.0000001);
            }

            float w = max(dot(rd, n.xy) * 0.5 + 0.5, 0.0);
            if (w <= 0.0001) continue;

            vec2 rdUV = rd * u_texel;
            rdUV /= max(length(rdUV), 0.0000001);
            float gPow = pow(g, u2);
            for (int s = 0; s < kMaxSteps; s++) {
                if (float(s) >= reflSteps) break;

                float t = dist * (gPow - 1.0) * reflSpan;
                gPow *= g;

                vec2 p = uv + rdUV * t;
                if (outside(p)) break;

                vec3 c = texture2D(u_scene, p).rgb;
                float tN = t / dist;
                float tol = h0 + slopeMag * thick * 0.5
                          + n.z * t * thick * (0.75 + 0.5 * tN);
                if (heightOf(c) > tol) {
                    specAcc += toLinear(c) * (1.0 - safeSmoothstep(0.0, fadeR, t)) * thru * w;
                    break;
                }
            }
        }

        // Promedio acotado; empaqueta (gi+refl+trans, AO) para el composite.
        refl = specAcc * (1.0 / float(kSpecTaps)) * rStr;

        float sat0 = max(max(c0.r, c0.g), c0.b) - min(min(c0.r, c0.g), c0.b);
        float transMask = safeSmoothstep(kTransLuma0, kTransLuma1, luma(c0))
                        * (1.0 - safeSmoothstep(kTransSat0, kTransSat1, sat0));
        float fPix = mix(1.0, kF0 + (1.0 - kF0) * pow(max(1.0 - ndv, 0.0), 5.0), fresAmt);
        float transK = clamp(transMask * (1.0 - fPix), 0.0, 1.0);
        if (transK > 0.001) {
            // Sin TIR posible (eta < 1); fallbacks estables sin shimmer.
            vec3 refr3 = refract(-vDir, n, 1.0 / kGlassIor);
            vec2 refrD = refr3.xy;
            float refrL = length(refrD);
            float gradL = length(n.xy);
            vec2 marchD;
            if (refrL > 0.0001) {
                marchD = refrD / refrL;
            } else if (gradL > 0.0001) {
                marchD = -n.xy / gradL;
            } else {
                marchD = vec2(1.0, 0.0);
            }
            vec2 refrUV = marchD * u_texel;
            refrUV /= max(length(refrUV), 0.0000001);

            vec3 transHit = vec3(0.0);
            float gPowT = pow(g, rPhase);
            for (int s = 0; s < kMaxSteps; s++) {
                if (float(s) >= reflSteps) break;

                float t = dist * (gPowT - 1.0) * reflSpan;
                gPowT *= g;

                vec2 p = uv + refrUV * t;
                if (outside(p)) break;

                vec3 c = texture2D(u_scene, p).rgb;
                float tN = t / dist;
                float tol = h0 + slopeMag * thick * 0.5
                          + n.z * t * thick * (0.75 + 0.5 * tN);
                if (heightOf(c) > tol) {
                    transHit = toLinear(c) * (1.0 - safeSmoothstep(0.0, fadeR, t));
                    break;
                }
            }
            trans = transHit * transK * rStr;
            // El difuso cede a la transmision para que los pesos sumen <= 1.
            gi *= (1.0 - transK);
        }
    }

    // Clamp final contra entradas degeneradas.
    gl_FragColor = vec4(min(gi + refl + trans, vec3(8.0)), clamp(ao, 0.0, 1.0));
}
