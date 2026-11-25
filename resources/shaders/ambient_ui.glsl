// Dynamic Ambient UI lighting & real-time background-adaptive palette shader.
// 1) Intelligently maps vanilla GD color families onto the active scene's extracted
//    palette (like a live real-time texture pack) while preserving ink outlines,
//    white glyphs/highlights, bevel depth, and contrast.
// 2) Applies smooth scene-matched directional sunlight, chromatic ambient grading,
//    soft interior rim highlight, and clean directional drop shadow.
#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#define AMB_P highp
#else
precision mediump float;
#define AMB_P mediump
#endif
varying lowp vec4 v_fragmentColor;
varying mediump vec2 v_texCoord;
#else
#define AMB_P
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
#endif

varying AMB_P vec2 v_local;

uniform sampler2D u_texture;
uniform vec2 u_size;          // host size in node units
uniform vec4 u_uvBounds;      // uMin, vMin, uMax, vMax
uniform vec4 u_uvAxes;        // dUdx, dVdx, dUdy, dVdy across full [0..1] local range
uniform vec4 u_light;         // dirX, dirY (toward light), directionality, proximityBoost
uniform vec4 u_keyColor;      // r, g, b, overall intensity
uniform vec4 u_localColor;    // local scene r, g, b, animation timeSeconds
uniform vec4 u_shadowColor;   // r, g, b, drop shadow opacity
uniform vec4 u_palPrimary;    // r, g, b, paletteStrength (0..1)
uniform vec3 u_palSecondary;  // secondary contrasting scene color
uniform vec3 u_palSurface;    // muted background/panel surface tone
uniform vec3 u_palAccent;     // highlight/gold accent tone
uniform vec4 u_params;        // tintStrength, rimStrength, kindCode (0=btn, 0.5=asset, 1=panel), passMode
uniform vec4 u_mode;          // premultipliedAlpha (0/1), isNineSlice (0/1), pressGlow, disabledFactor

vec4 sampleClamped(vec2 uv) {
    if (uv.x < u_uvBounds.x || uv.x > u_uvBounds.z ||
        uv.y < u_uvBounds.y || uv.y > u_uvBounds.w) {
        return vec4(0.0);
    }
    return texture2D(u_texture, uv);
}

vec3 decodeRgb(vec4 texel) {
    if (u_mode.x > 0.5) {
        return clamp(texel.rgb / max(texel.a, 0.004), 0.0, 1.0);
    }
    return texel.rgb;
}

float lumaOf(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

float rec601Of(vec3 c) {
    return dot(c, vec3(0.30, 0.59, 0.11));
}

// Returns vec3(hue06, sat, rec601Lum) in [0..6, 0..1, 0..1].
vec3 rgbHueSatLum(vec3 c) {
    float maxC = max(max(c.r, c.g), c.b);
    float minC = min(min(c.r, c.g), c.b);
    float delta = maxC - minC;
    float sat = (maxC > 1.0e-4) ? (delta / maxC) : 0.0;
    float h = 0.0;
    if (delta > 1.0e-4) {
        if (maxC == c.r) h = mod((c.g - c.b) / delta + 6.0, 6.0);
        else if (maxC == c.g) h = (c.b - c.r) / delta + 2.0;
        else h = (c.r - c.g) / delta + 4.0;
    }
    return vec3(h, sat, rec601Of(c));
}

// Preserves Texture-Studio style internal shading & saturated glossy highlight.
vec3 luminanceTint(vec3 tone, float lum, float pivot) {
    float lumFactor = pow(clamp(lum / max(pivot, 0.2), 0.0, 1.4), 0.92);
    vec3 shaded = tone * min(lumFactor, 1.0);
    float specT = smoothstep(min(pivot + 0.12, 0.86), 0.98, lum);
    vec3 brightTone = clamp(tone * 1.32 + vec3(0.20), 0.0, 1.0);
    vec3 highlight = mix(tone, brightTone, 0.54);
    return clamp(mix(shaded, highlight, specT), 0.0, 1.0);
}

vec3 selectRoleTone(float hue06, float sat, float lum, bool isPanel) {
    vec3 pri = clamp(u_palPrimary.rgb, 0.0, 1.0);
    vec3 sec = clamp(u_palSecondary, 0.0, 1.0);
    vec3 surf = clamp(u_palSurface, 0.0, 1.0);
    vec3 acc = clamp(u_palAccent, 0.0, 1.0);
    vec3 deep = mix(sec * 0.96, mix(pri, acc, 0.22), 0.34);
    vec3 lightPop = mix(acc, pri, 0.38);

    // Panels and their corner/frame trim always stay cohesive inside the panel tone family.
    if (isPanel) {
        vec3 trimTone = mix(surf * 1.18, mix(acc, pri, 0.38), smoothstep(0.42, 0.84, lum));
        return mix(surf, trimTone, smoothstep(0.36, 0.78, sat));
    }
    // Warm brown panel textures appearing on buttons/assets -> panel surface.
    if (hue06 >= 0.28 && hue06 <= 0.98 && sat < 0.74 && lum < 0.64) {
        return surf;
    }
    // Red / delete / warm buttons adapt smoothly toward primary + warm crimson harmony.
    if (hue06 >= 5.65 || hue06 < 0.45) {
        vec3 crimsonAnchor = vec3(0.94, 0.32, 0.42);
        return mix(pri, mix(crimsonAnchor, acc, 0.44), 0.42);
    }
    // Gold / yellow highlights & trim -> radiant accent harmonized with primary.
    if (hue06 < 1.25) {
        return mix(acc, pri, 0.24);
    }
    // GD Green primary UI -> scene dominant primary (slightly lifted in highlights).
    if (hue06 < 2.75) {
        return mix(pri, lightPop, smoothstep(0.55, 0.90, lum) * 0.24);
    }
    // GD Cyan secondary accents -> crisp scene secondary.
    if (hue06 < 3.45) {
        return mix(sec, acc, 0.16);
    }
    // GD Blue buttons -> lush vibrant cool companion (avoids dull grey wash).
    if (hue06 < 4.45) {
        return mix(deep, acc, smoothstep(0.55, 0.88, lum) * 0.22);
    }
    // Purple / magenta -> radiant accent mix.
    return lightPop;
}

float roundedBoxDist(vec2 p, vec2 halfSize, float radius) {
    vec2 q = abs(p) - halfSize + vec2(radius);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

vec2 boxNormal(vec2 p, vec2 halfSize, float radius) {
    vec2 q = abs(p) - halfSize + vec2(radius);
    vec2 side = vec2(p.x < 0.0 ? -1.0 : 1.0, p.y < 0.0 ? -1.0 : 1.0);
    if (q.x > 0.0 && q.y > 0.0) return normalize(q) * side;
    if (q.x > q.y) return vec2(side.x, 0.0);
    return vec2(0.0, side.y);
}

void main() {
    float passMode = u_params.w;

    // Pass 1: clean soft directional drop shadow.
    if (passMode > 0.5) {
        if (u_mode.y > 0.5) {
            vec2 halfSize = u_size * 0.5;
            vec2 p = (v_local - vec2(0.5)) * u_size;
            float radius = min(8.0, min(halfSize.x, halfSize.y) * 0.45);
            float d = roundedBoxDist(p, halfSize, radius);
            float soft = max(4.0, min(u_size.x, u_size.y) * 0.10);
            float fall = clamp(1.0 - max(d, 0.0) / soft, 0.0, 1.0);
            float a = fall * fall * u_shadowColor.a * v_fragmentColor.a;
            if (a <= 0.004) discard;
            vec3 shCol = mix(vec3(0.02, 0.02, 0.05), u_shadowColor.rgb * 0.25, 0.65);
            gl_FragColor = vec4(shCol * a, a);
            return;
        }

        vec2 stepX = u_uvAxes.xy * (1.6 / max(u_size.x, 12.0));
        vec2 stepY = u_uvAxes.zw * (1.6 / max(u_size.y, 12.0));
        float a = sampleClamped(v_texCoord).a * 0.40
                + sampleClamped(v_texCoord + stepX).a * 0.15
                + sampleClamped(v_texCoord - stepX).a * 0.15
                + sampleClamped(v_texCoord + stepY).a * 0.15
                + sampleClamped(v_texCoord - stepY).a * 0.15;
        a *= u_shadowColor.a * v_fragmentColor.a;
        if (a <= 0.004) discard;
        vec3 shCol = mix(vec3(0.02, 0.02, 0.05), u_shadowColor.rgb * 0.25, 0.65);
        gl_FragColor = vec4(shCol * a, a);
        return;
    }

    // Pass 0: adaptive palette + lit artwork.
    vec4 rawTex = (u_mode.y > 0.5) ? texture2D(u_texture, v_texCoord) : sampleClamped(v_texCoord);
    float alpha = rawTex.a * v_fragmentColor.a;
    if (alpha <= 0.004) discard;

    vec3 baseRgb = decodeRgb(rawTex) * v_fragmentColor.rgb;
    float kindCode = u_params.z; // 0 = button, 0.25 = button overlay, 0.5 = asset, 1 = panel
    bool isPanel = kindCode > 0.75;
    bool isAsset = (kindCode > 0.37 && kindCode <= 0.75);
    bool isOverlay = (kindCode > 0.12 && kindCode <= 0.37);

    // Preserve crisp dark GD ink outlines (`baseLuma` near 0 stays dark & sharp).
    float baseLuma = lumaOf(baseRgb);
    float inkMask = smoothstep(0.05, 0.24, baseLuma);

    // Real-time Texture Pack palette adaptation from background colors.
    float palAmt = clamp(u_palPrimary.a, 0.0, 1.0);
    if (palAmt > 0.004) {
        vec3 hsl = rgbHueSatLum(baseRgb);
        float chromaMask = smoothstep(0.08, 0.30, hsl.y);
        float whiteKeep = (1.0 - smoothstep(0.08, 0.24, hsl.y)) * smoothstep(0.60, 0.86, hsl.z);
        float neutralMid = (1.0 - chromaMask) * (1.0 - whiteKeep) * smoothstep(0.16, 0.34, hsl.z);

        vec3 roleTone = selectRoleTone(hsl.x, hsl.y, hsl.z, isPanel);
        vec3 neutralTone = mix(clamp(u_palSurface, 0.0, 1.0) * 1.35 + vec3(0.10), clamp(u_palSecondary, 0.0, 1.0), 0.32);

        float pivot = isPanel ? 0.54 : 0.62;
        vec3 chromaPainted = luminanceTint(roleTone, hsl.z, pivot);
        vec3 neutralPainted = luminanceTint(neutralTone, hsl.z, 0.58);

        float roleScale = isPanel ? 0.90 : (isAsset ? 0.46 : (isOverlay ? 0.22 : 1.00));
        float paintW = palAmt * roleScale * inkMask * (1.0 - whiteKeep);
        vec3 targetRgb = mix(baseRgb, chromaPainted, clamp(paintW * chromaMask, 0.0, 1.0));
        targetRgb = mix(targetRgb, neutralPainted, clamp(paintW * neutralMid * 0.38, 0.0, 1.0));
        baseRgb = targetRgb;
    }

    float intensity = clamp(u_keyColor.a, 0.0, 1.5);
    float tintStrength = u_params.x;
    float rimStrength = u_params.y;
    float dirAmt = clamp(u_light.z, 0.0, 1.0);
    float proxBoost = clamp(u_light.w, 0.75, 1.35);

    // Smooth directional coordinate across the UI element toward u_light.xy.
    vec2 centered = v_local - vec2(0.5);
    float proj = dot(centered, u_light.xy) * 1.35;
    float lightSide = smoothstep(-0.78, 0.78, proj);

    // Re-evaluate inkMask cleanly after palette adaptation so outlines stay crisp.
    float litBaseLuma = lumaOf(baseRgb);
    float finalInkMask = smoothstep(0.05, 0.24, litBaseLuma);

    // Silhouette outer rim (strictly smoothed and kept off dark borders).
    vec2 nOuter = normalize(centered + vec2(1.0e-4, 0.0));
    float rimBand = 0.0;

    if (u_mode.y > 0.5) {
        vec2 halfSize = u_size * 0.5;
        vec2 p = centered * u_size;
        float radius = min(8.0, min(halfSize.x, halfSize.y) * 0.45);
        float d = roundedBoxDist(p, halfSize, radius);
        nOuter = boxNormal(p, halfSize, radius);
        float edgeW = isPanel ? 2.2 : 2.6;
        rimBand = smoothstep(0.0, -0.9, d) * clamp(1.0 - max(-d - 0.8, 0.0) / edgeW, 0.0, 1.0);
    } else {
        vec2 stepX = u_uvAxes.xy * (1.4 / max(u_size.x, 12.0));
        vec2 stepY = u_uvAxes.zw * (1.4 / max(u_size.y, 12.0));
        float aE = sampleClamped(v_texCoord + stepX).a;
        float aW = sampleClamped(v_texCoord - stepX).a;
        float aN = sampleClamped(v_texCoord + stepY).a;
        float aS = sampleClamped(v_texCoord - stepY).a;

        vec2 gradA = vec2(aW - aE, aS - aN);
        float gLen = length(gradA);
        if (gLen > 0.04) {
            nOuter = gradA / gLen;
        }
        float minAlpha = min(min(aE, aW), min(aN, aS));
        rimBand = smoothstep(0.55, 0.95, rawTex.a) * clamp((1.0 - minAlpha) * 1.15, 0.0, 1.0);
    }

    float facingOut = dot(nOuter, u_light.xy);
    float rimLit = pow(max(facingOut, 0.0), 1.45) * rimBand * finalInkMask;
    float rimDark = pow(max(-facingOut, 0.0), 1.45) * rimBand * finalInkMask;

    float surfaceScale = (isPanel ? 0.44 : (isOverlay ? 0.72 : (isAsset ? 0.62 : 0.94))) * intensity * proxBoost;
    float edgeScale = (isPanel ? 0.46 : (isOverlay ? 0.62 : (isAsset ? 0.44 : 0.88))) * intensity * rimStrength;

    vec3 keyCol = clamp(u_keyColor.rgb, 0.0, 1.0);
    vec3 localCol = clamp(u_localColor.rgb, 0.0, 1.0);
    vec3 effKeyCol = mix(keyCol, localCol, 0.52);
    vec3 secCol = clamp(u_palSecondary, 0.0, 1.0);
    vec3 accCol = clamp(u_palAccent, 0.0, 1.0);
    vec3 shCol = clamp(u_shadowColor.rgb, 0.0, 1.0);

    // Local shadow pocket response: areas where background light is lacking settle deeper into chromatic shade.
    float localLuma = lumaOf(localCol);
    float lightLack = 1.0 - smoothstep(0.14, 0.64, localLuma);

    // Ambient atmosphere color grading: harmonizes shadow mood with spatial local & global key light.
    vec3 shadowMood = mix(shCol * 1.02 + vec3(0.04), secCol * 0.78, 0.26);
    vec3 lightMood = mix(effKeyCol * 1.02 + vec3(0.04), vec3(1.0), 0.12);
    float sideCurve = smoothstep(0.0, 1.0, lightSide);
    vec3 moodTint = mix(vec3(1.0), mix(shadowMood, lightMood, 0.18 + sideCurve * 0.82), tintStrength * 0.76);
    vec3 lit = mix(baseRgb, baseRgb * moodTint, clamp(intensity * surfaceScale * 0.62, 0.0, 0.82));

    // Sculpted directional shade on the unlit half + subtle recessing in light-starved backdrop zones.
    float awayCurve = pow(1.0 - lightSide, 1.15);
    float awayWeight = (0.14 + 0.24 * dirAmt) * awayCurve * surfaceScale * (0.35 + 0.65 * finalInkMask);
    float pocketShade = lightLack * (0.10 + 0.08 * (1.0 - lightSide)) * min(intensity, 1.2) * (0.40 + 0.60 * finalInkMask);
    lit *= clamp(1.0 - awayWeight * 0.62 - pocketShade, 0.42, 1.0);
    lit = mix(lit, lit * mix(vec3(1.0), shadowMood * 0.92 + vec3(0.05), 0.55), clamp(awayCurve * 0.42 + lightLack * 0.24, 0.0, 0.58) * tintStrength);

    // Chromatic key-light glow on the light-facing half (screen-blended, spatial-aware).
    float sunCurve = pow(lightSide, 1.18);
    float keyStrength = (0.14 + 0.24 * dirAmt) * sunCurve * surfaceScale * finalInkMask * (1.0 - lightLack * 0.35);
    vec3 keyGlow = mix(effKeyCol, accCol, 0.24);
    lit += keyGlow * keyStrength * (1.0 - lit * 0.64);

    // Living organic breathing sheen & dual-tone specular contour driven by time.
    float t = u_localColor.a;
    float breath = 0.88 + 0.12 * sin(t * 1.55);
    if (!isPanel) {
        float diagPos = dot(centered, normalize(u_light.xy + vec2(0.35, -0.25)));
        float wavePhase = fract(t * 0.16) * 3.4 - 1.7;
        float sheenBand = exp(-pow((diagPos - wavePhase) * 3.4, 2.0));
        float sheenScale = isOverlay ? 0.085 : (isAsset ? 0.065 : 0.115);
        float sheenAmt = sheenBand * sheenScale * intensity * finalInkMask * (0.35 + 0.65 * tintStrength);
        lit += mix(effKeyCol, vec3(1.0), 0.42) * sheenAmt * (1.0 - lit * 0.52);
    }

    // Smooth specular rim highlight along sun-facing contour + subtle cool bounce rim on shadow edge.
    vec3 rimCol = mix(effKeyCol, vec3(1.0), 0.34);
    vec3 bounceRimCol = mix(secCol, accCol, 0.38);
    lit += rimCol * rimLit * 0.42 * edgeScale * (0.45 + 0.55 * dirAmt) * breath;
    lit += bounceRimCol * rimDark * 0.18 * edgeScale * (0.40 + 0.60 * dirAmt) * (2.0 - breath);
    lit *= (1.0 - rimDark * 0.16 * edgeScale * (0.40 + 0.60 * dirAmt));

    lit += vec3(0.08) * u_mode.z;
    lit *= u_mode.w;
    lit = clamp(lit, 0.0, 1.0);

    gl_FragColor = vec4(lit * alpha, alpha);
}
