// Liquid Glass UI material (v5). Draws one plate (panel, button, well, or
// contour-masked UI sprite button/tab/chrome) by refracting the framebuffer
// copy taken right before it (GlassCapture).
// A convex squircle bezel, h(x) = (1 - (1 - x)^4)^(1/4), bends a straight
// down ray (Snell, glass IOR 1.5, split per channel at the rim). Frost
// mixes in the blurred chain, then vibrancy, tint, a specular rim and a
// soft drop shadow. v_local is the plate-local position in node units.
// Output is premultiplied: blend with GL_ONE, GL_ONE_MINUS_SRC_ALPHA.
// u_state.y = 1 draws the simulated material (no copy bound).
// u_shape.w = 0 draws a rounded-box/capsule/circle glass plate.
// u_shape.w = 1 draws a recessed well (list frames, insets, inputs, grooves):
//   no copy, a shade, an inner shadow under the lit edges and a hairline on
//   the far ones.
// u_shape.w = 2 draws a contour-masked Liquid Glass UI sprite using u_spriteTex
//   (unit 2): refracts the backdrop inside the sprite's exact silhouette,
//   builds a curved bezel & specular rim along the contour, and according to
//   u_spriteOpts.w preserves baked inner icons (0), baked tab labels (1),
//   or renders pure frosted panel-matching list chrome (2).
// u_shadow.w > 0 draws a vector line icon on a button (see glyphDistance).
#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#define GLASS_P highp
#else
precision mediump float;
#define GLASS_P mediump
#endif
varying lowp vec4 v_fragmentColor;
#else
#define GLASS_P
varying vec4 v_fragmentColor;
#endif
varying GLASS_P vec2 v_local;

uniform sampler2D u_texture;   // sharp framebuffer copy (unit 0)
uniform sampler2D u_blurTex;   // blurred copy (unit 1)
uniform sampler2D u_spriteTex; // UI button sprite atlas/texture (unit 2, mode 2)
uniform vec2 u_size;           // plate size, node units
uniform vec4 u_shape;          // radius, bevel width, framebuffer px per unit, mode (0 glass, 1 well, 2 sprite)
uniform vec2 u_fbOrigin;       // framebuffer px of local (0, 0)
uniform vec2 u_fbAxisX;        // framebuffer px per local unit along x
uniform vec2 u_fbAxisY;        // framebuffer px per local unit along y
uniform vec4 u_sharpMap;       // xy: framebuffer px of texel 0, zw: 1 / texture size
uniform vec4 u_sharpClamp;     // uv min (xy) and max (zw) of this capture
uniform vec4 u_blurMap;        // xy: region origin, zw: framebuffer px -> uv
uniform vec4 u_blurClamp;      // uv min (xy) and max (zw) of the blurred content
uniform vec4 u_tint;           // rgb tint, amount
uniform vec4 u_material;       // frost, bezel thickness (units), dispersion, vibrancy
uniform vec4 u_light;          // light direction xy, rim glow, specular
uniform vec4 u_shadow;         // y offset (units), softness (units), strength, icon
uniform vec4 u_state;          // press glow, simulated, brightness, well shade
uniform vec4 u_spriteMap0;     // xy: uv origin at local (0,0), zw: uv axisX across [0,1]
uniform vec4 u_spriteMap1;     // xy: uv axisY across [0,1], zw: unused
uniform vec4 u_spriteOpts;     // x: coloredSprite (0/1), y: iconStrength, z: premultipliedAlpha (0/1), w: style (0 iconBtn, 1 tab, 2 chrome)

float roundedBox(vec2 p, vec2 halfSize, float radius) {
    vec2 q = abs(p) - halfSize + vec2(radius);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

// Outward normal of the rounded box.
vec2 boxNormal(vec2 p, vec2 halfSize, float radius) {
    vec2 q = abs(p) - halfSize + vec2(radius);
    vec2 side = vec2(p.x < 0.0 ? -1.0 : 1.0, p.y < 0.0 ? -1.0 : 1.0);
    if (q.x > 0.0 && q.y > 0.0) return normalize(q) * side;
    if (q.x > q.y) return vec2(side.x, 0.0);
    return vec2(0.0, side.y);
}

float segment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

// Signed distance to a triangle (after Inigo Quilez).
float triangle(vec2 p, vec2 p0, vec2 p1, vec2 p2) {
    vec2 e0 = p1 - p0;
    vec2 e1 = p2 - p1;
    vec2 e2 = p0 - p2;
    vec2 v0 = p - p0;
    vec2 v1 = p - p1;
    vec2 v2 = p - p2;
    vec2 q0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    vec2 q1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    vec2 q2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    float s = sign(e0.x * e2.y - e0.y * e2.x);
    vec2 d = min(min(vec2(dot(q0, q0), s * (v0.x * e0.y - v0.y * e0.x)),
                     vec2(dot(q1, q1), s * (v1.x * e1.y - v1.y * e1.x))),
                     vec2(dot(q2, q2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}

// Line icons for utility buttons, in units of the button radius:
// 1 close, 2 refresh, 3 chevron left, 4 chevron right, 5 check, 6 info,
// 7 plus, 8 minus, 9 play, 10 stop, 11 download.
float glyphDistance(vec2 g, float icon) {
    float stroke = 0.075;
    if (icon < 1.5) {
        float a = 0.3;
        return min(segment(g, vec2(-a, -a), vec2(a, a)), segment(g, vec2(-a, a), vec2(a, -a))) - stroke;
    }
    if (icon < 2.5) {
        // Open ring with the gap at the top right and the head on its lower end.
        float r = 0.4;
        float gapCenter = 0.785398;
        float gapHalf = 0.6;
        float rel = abs(mod(atan(g.y, g.x) - gapCenter + 3.141593, 6.283185) - 3.141593);
        float ring;
        if (rel > gapHalf) {
            ring = abs(length(g) - r);
        } else {
            vec2 e0 = r * vec2(cos(gapCenter - gapHalf), sin(gapCenter - gapHalf));
            vec2 e1 = r * vec2(cos(gapCenter + gapHalf), sin(gapCenter + gapHalf));
            ring = min(length(g - e0), length(g - e1));
        }
        float end = gapCenter - gapHalf;
        vec2 radial = vec2(cos(end), sin(end));
        vec2 along = vec2(-radial.y, radial.x);
        vec2 c = radial * r;
        float head = triangle(g, c + along * 0.22, c + radial * 0.19 - along * 0.03,
                              c - radial * 0.19 - along * 0.03);
        return min(ring - stroke, head - 0.01);
    }
    if (icon < 4.5) {
        // Chevrons point left; the right one is mirrored.
        vec2 q = vec2(icon < 3.5 ? g.x : -g.x, g.y);
        vec2 apex = vec2(-0.14, 0.0);
        return min(segment(q, apex, vec2(0.14, 0.36)), segment(q, apex, vec2(0.14, -0.36))) - stroke;
    }
    if (icon < 5.5) {
        // Checkmark.
        float a = segment(g, vec2(-0.34, -0.02), vec2(-0.08, -0.28));
        float b = segment(g, vec2(-0.08, -0.28), vec2(0.36, 0.28));
        return min(a, b) - stroke * 1.1;
    }
    if (icon < 6.5) {
        // Info "i".
        float stem = segment(g, vec2(0.0, -0.34), vec2(0.0, 0.07)) - stroke;
        float dotD = length(g - vec2(0.0, 0.32)) - stroke * 1.2;
        return min(stem, dotD);
    }
    if (icon < 7.5) {
        // Plus "+".
        float h = segment(g, vec2(-0.33, 0.0), vec2(0.33, 0.0));
        float v = segment(g, vec2(0.0, -0.33), vec2(0.0, 0.33));
        return min(h, v) - stroke;
    }
    if (icon < 8.5) {
        // Minus "-".
        return segment(g, vec2(-0.33, 0.0), vec2(0.33, 0.0)) - stroke;
    }
    if (icon < 9.5) {
        // Play triangle.
        return triangle(g, vec2(-0.20, -0.30), vec2(-0.20, 0.30), vec2(0.34, 0.0)) - 0.035;
    }
    if (icon < 10.5) {
        // Stop rounded square.
        return roundedBox(g, vec2(0.26, 0.26), 0.06);
    }
    // Download arrow + tray.
    float shaft = segment(g, vec2(0.0, 0.34), vec2(0.0, -0.14));
    float leftArm = segment(g, vec2(-0.22, 0.04), vec2(0.0, -0.18));
    float rightArm = segment(g, vec2(0.22, 0.04), vec2(0.0, -0.18));
    float tray = segment(g, vec2(-0.30, -0.34), vec2(0.30, -0.34));
    return min(min(shaft, min(leftArm, rightArm)), tray) - stroke * 0.92;
}

// Recessed well, premultiplied: a tinted shade, an inner shadow under the
// edges that face the light (the lip blocks it) and a hairline where the
// far edges catch it.
vec4 well(vec2 p, vec2 halfSize, float radius, float d, float onePx) {
    vec2 n = boxNormal(p, halfSize, radius);
    float depth = max(-d, 0.0);
    float facing = dot(n, u_light.xy);
    float lip = exp(-depth / max(u_shadow.y, onePx));
    float shade = clamp(u_shadow.z * lip * (0.35 + 0.65 * max(facing, 0.0)), 0.0, 1.0);
    float edgeLine = exp(-depth / (1.25 * onePx));
    float catchLight = pow(max(-facing, 0.0), 1.5);
    float hairline = clamp(edgeLine * (0.1 + 0.4 * catchLight) * (0.4 + 0.6 * u_light.z), 0.0, 1.0);
    float fill = clamp(u_state.w, 0.0, 1.0);
    vec3 rgb = u_tint.rgb * (0.1 * fill);
    float alpha = fill;
    rgb *= 1.0 - shade;
    alpha = shade + alpha * (1.0 - shade);
    rgb = vec3(hairline) + rgb * (1.0 - hairline);
    alpha = hairline + alpha * (1.0 - hairline);
    return vec4(rgb, alpha);
}

vec3 backdrop(vec2 local, float frost) {
    vec2 fb = u_fbOrigin + local.x * u_fbAxisX + local.y * u_fbAxisY;
    vec2 blurUv = clamp((fb - u_blurMap.xy) * u_blurMap.zw, u_blurClamp.xy, u_blurClamp.zw);
    vec3 soft = texture2D(u_blurTex, blurUv).rgb;
    if (frost >= 0.999) return soft;
    vec2 sharpUv = clamp((fb - u_sharpMap.xy) * u_sharpMap.zw, u_sharpClamp.xy, u_sharpClamp.zw);
    return mix(texture2D(u_texture, sharpUv).rgb, soft, frost);
}

// Inward shift (node units) of a vertical ray refracted by the bezel at
// fraction x (0 = outer edge, 1 = flat top). A convex surface keeps the
// sampled point inside the shape, so the copy needs no extra margin.
float lensShift(float x, float thickness, float bevel, float ior) {
    float e = 1.0 - x;
    float e2 = e * e;
    float base = max(1.0 - e2 * e2, 1.0e-4);
    float height = pow(base, 0.25);
    float slope = e2 * e * pow(base, -0.75) * thickness / bevel;
    float incidence = atan(slope);
    float refracted = asin(sin(incidence) / ior);
    return height * thickness * tan(incidence - refracted);
}

vec4 sampleSprite(vec2 t) {
    if (t.x < 0.0 || t.x > 1.0 || t.y < 0.0 || t.y > 1.0) return vec4(0.0);
    vec2 uv = u_spriteMap0.xy + t.x * u_spriteMap0.zw + t.y * u_spriteMap1.xy;
    return texture2D(u_spriteTex, uv);
}

vec3 decodeSpriteRgb(vec4 sp) {
    return u_spriteOpts.z > 0.5 ? clamp(sp.rgb / max(sp.a, 0.01), 0.0, 1.0) : sp.rgb;
}

float spriteLuma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

float spriteSat(vec3 c) {
    float hi = max(c.r, max(c.g, c.b));
    float lo = min(c.r, min(c.g, c.b));
    return (hi - lo) / max(hi, 1.0e-3);
}

// Probe an interior-perimeter point for button body fill color estimation.
// Returns vec4(rgb, valid) where valid is 1.0 for saturated/midtone body pixels
// and 0.0 for transparent, white highlight ring, or black border pixels.
vec4 probeBodySample(vec2 p) {
    vec4 s = sampleSprite(p);
    if (s.a <= 0.5) return vec4(0.0);
    vec3 rgb = decodeSpriteRgb(s);
    float luma = spriteLuma(rgb);
    float sat = spriteSat(rgb);
    if (luma < 0.16) return vec4(0.0);
    if (luma > 0.88 && sat < 0.16) return vec4(0.0);
    return vec4(rgb, 1.0);
}

// Contour-masked Liquid Glass for baked GD UI sprite buttons, tabs, and list chrome.
// Refracts the live backdrop inside the sprite's silhouette, builds a smooth
// glass bezel & specular rim along its contour, and keeps baked icons or
// baked tab labels crisp and legible.
vec4 renderSpriteGlass(float onePx) {
    vec2 invSize = 1.0 / max(u_size, vec2(1.0));
    vec2 t = v_local * invSize;
    vec4 sp = sampleSprite(t);

    // Soft drop shadow cast just below the sprite silhouette.
    vec2 shOff = vec2(0.0, u_shadow.x * invSize.y);
    float shR = max(u_shadow.y * 0.45, onePx);
    vec2 shStep = vec2(shR) * invSize;
    float shA = sampleSprite(t + shOff).a * 0.4
              + sampleSprite(t + shOff + vec2(shStep.x, 0.0)).a * 0.15
              + sampleSprite(t + shOff - vec2(shStep.x, 0.0)).a * 0.15
              + sampleSprite(t + shOff + vec2(0.0, shStep.y)).a * 0.15
              + sampleSprite(t + shOff - vec2(0.0, shStep.y)).a * 0.15;
    float shadow = u_shadow.z * shA;
    if (sp.a <= 0.02) {
        if (shadow <= 0.003) discard;
        return vec4(0.0, 0.0, 0.0, shadow) * v_fragmentColor.a;
    }

    float bevel = max(u_shape.y, 2.0 * onePx);
    vec2 rEdge = vec2(max(1.75 * onePx, 0.025 * min(u_size.x, u_size.y))) * invSize;
    vec2 rBevel = vec2(bevel * 0.65) * invSize;

    // 8-direction alpha samples for smooth contour normal and bezel profile.
    float aE  = sampleSprite(t + vec2( rEdge.x, 0.0)).a;
    float aW  = sampleSprite(t + vec2(-rEdge.x, 0.0)).a;
    float aN  = sampleSprite(t + vec2(0.0,  rEdge.y)).a;
    float aS  = sampleSprite(t + vec2(0.0, -rEdge.y)).a;
    float aNE = sampleSprite(t + vec2( rBevel.x,  rBevel.y) * 0.7071).a;
    float aNW = sampleSprite(t + vec2(-rBevel.x,  rBevel.y) * 0.7071).a;
    float aSE = sampleSprite(t + vec2( rBevel.x, -rBevel.y) * 0.7071).a;
    float aSW = sampleSprite(t + vec2(-rBevel.x, -rBevel.y) * 0.7071).a;

    vec2 grad = vec2((aW - aE) + 0.55 * (aNW + aSW - aNE - aSE),
                     (aS - aN) + 0.55 * (aSW + aSE - aNW - aNE));
    float gLen = length(grad);
    vec2 fallbackN = normalize(t - vec2(0.5) + vec2(1.0e-4, 0.0));
    vec2 n = gLen > 0.02 ? (grad / gLen) : fallbackN;

    float rimOuter = clamp(1.0 - 0.25 * (aE + aW + aN + aS), 0.0, 1.0);
    float rimBezel = clamp(1.0 - 0.125 * (aE + aW + aN + aS + aNE + aNW + aSE + aSW), 0.0, 1.0);
    float x = clamp(1.0 - rimBezel, 0.0, 1.0);

    vec3 srgb = decodeSpriteRgb(sp);
    float sLuma = spriteLuma(srgb);
    float sSat = spriteSat(srgb);

    vec3 glass;
    if (u_state.y > 0.5) {
        float shade = clamp(t.y, 0.0, 1.0);
        vec3 baseTint = u_spriteOpts.x > 0.5 ? mix(u_tint.rgb, srgb, 0.45) : u_tint.rgb;
        glass = mix(baseTint * 0.34, baseTint * 0.68, shade);
    } else {
        float frost = u_material.x;
        float thickness = u_material.y;
        if (x < 1.0 && thickness > 0.0) {
            float shiftG = lensShift(x, thickness, bevel, 1.5);
            if (u_material.z > 0.0) {
                float split = 0.05 * u_material.z;
                float shiftR = lensShift(x, thickness, bevel, 1.5 - split);
                float shiftB = lensShift(x, thickness, bevel, 1.5 + split);
                glass = vec3(backdrop(v_local - n * shiftR, frost).r,
                             backdrop(v_local - n * shiftG, frost).g,
                             backdrop(v_local - n * shiftB, frost).b);
            } else {
                glass = backdrop(v_local - n * shiftG, frost);
            }
        } else {
            glass = backdrop(v_local, frost);
        }
        float luma = dot(glass, vec3(0.299, 0.587, 0.114));
        glass = clamp(mix(vec3(luma), glass, u_material.w), 0.0, 1.0);
        float tintShare = u_spriteOpts.w > 1.5
            ? u_tint.a
            : (u_spriteOpts.w > 0.5 ? u_tint.a * 0.88 : u_tint.a * (u_spriteOpts.x > 0.5 ? 0.38 : 0.88));
        glass = mix(glass, u_tint.rgb, tintShare);
    }

    vec3 artColor = srgb;
    float artMix = 0.0;

    if (u_spriteOpts.w <= 1.5) {
        // Both icon buttons and tabs keep outer border clean and free of thick black outlines.
        float interior = 1.0 - smoothstep(0.22, 0.56, rimBezel);
        float whiteGlyph = smoothstep(0.56, 0.84, sLuma) * (1.0 - smoothstep(0.24, 0.58, sSat));
        float darkLine = 1.0 - smoothstep(0.08, 0.25, sLuma);

        if (u_spriteOpts.w > 0.5) {
            // Mode Tab: suppress brown/flat tab fill completely; keep crisp white label & inner outline.
            float whiteText = whiteGlyph * interior;
            float darkInk = darkLine * interior;
            artColor = mix(vec3(0.06, 0.08, 0.14), srgb, smoothstep(0.22, 0.60, sLuma));
            artMix = clamp(max(whiteText * 0.94, darkInk * 0.62) * u_spriteOpts.y, 0.0, 0.95);
        } else {
            // Mode IconButton: estimate body fill from perimeter ring so inner colored or white
            // icons stand out clearly while body fill & outer rings yield to Liquid Glass.
            vec4 b0 = probeBodySample(vec2(0.15, 0.50));
            vec4 b1 = probeBodySample(vec2(0.85, 0.50));
            vec4 b2 = probeBodySample(vec2(0.50, 0.15));
            vec4 b3 = probeBodySample(vec2(0.50, 0.85));

            vec3 bodyRef = vec3(0.0);
            float bestScore = 1.0e6;
            float hasBody = 0.0;
            if (b0.a > 0.5 && b1.a > 0.5) {
                float d = length(b0.rgb - b1.rgb);
                if (d < bestScore) { bestScore = d; bodyRef = 0.5 * (b0.rgb + b1.rgb); hasBody = 1.0; }
            }
            if (b0.a > 0.5 && b2.a > 0.5) {
                float d = length(b0.rgb - b2.rgb);
                if (d < bestScore) { bestScore = d; bodyRef = 0.5 * (b0.rgb + b2.rgb); hasBody = 1.0; }
            }
            if (b0.a > 0.5 && b3.a > 0.5) {
                float d = length(b0.rgb - b3.rgb);
                if (d < bestScore) { bestScore = d; bodyRef = 0.5 * (b0.rgb + b3.rgb); hasBody = 1.0; }
            }
            if (b1.a > 0.5 && b2.a > 0.5) {
                float d = length(b1.rgb - b2.rgb);
                if (d < bestScore) { bestScore = d; bodyRef = 0.5 * (b1.rgb + b2.rgb); hasBody = 1.0; }
            }
            if (b1.a > 0.5 && b3.a > 0.5) {
                float d = length(b1.rgb - b3.rgb);
                if (d < bestScore) { bestScore = d; bodyRef = 0.5 * (b1.rgb + b3.rgb); hasBody = 1.0; }
            }
            if (b2.a > 0.5 && b3.a > 0.5) {
                float d = length(b2.rgb - b3.rgb);
                if (d < bestScore) { bestScore = d; bodyRef = 0.5 * (b2.rgb + b3.rgb); hasBody = 1.0; }
            }
            if (hasBody < 0.5) {
                if (b0.a > 0.5) { bodyRef = b0.rgb; hasBody = 1.0; }
                else if (b1.a > 0.5) { bodyRef = b1.rgb; hasBody = 1.0; }
                else if (b2.a > 0.5) { bodyRef = b2.rgb; hasBody = 1.0; }
                else if (b3.a > 0.5) { bodyRef = b3.rgb; hasBody = 1.0; }
            }

            float diffMask = 0.0;
            if (hasBody > 0.5) {
                float refLuma = spriteLuma(bodyRef);
                vec3 chromaS = srgb - vec3(sLuma);
                vec3 chromaR = bodyRef - vec3(refLuma);
                float bodyDist = length(chromaS - chromaR) * 1.65 + 0.45 * abs(sLuma - refLuma);
                diffMask = smoothstep(0.16, 0.36, bodyDist);
            }

            // Exclude outer cartoon rings/borders so the glass specular rim draws cleanly.
            float innerZone = 1.0 - smoothstep(0.14, 0.42, rimBezel);
            float outerRing = smoothstep(0.10, 0.42, rimBezel);
            float iconMask = max(innerZone * diffMask, interior * max(whiteGlyph, darkLine * 0.85));

            float bodyShare = (u_spriteOpts.x > 0.5 ? 0.24 : 0.0) * (1.0 - outerRing * 0.90) * (1.0 - darkLine * 0.70);
            artMix = clamp(max(bodyShare, iconMask * 0.93) * u_spriteOpts.y, 0.0, 0.94);
        }
    }

    vec3 color = mix(glass, artColor, artMix);
    color *= u_state.z;
    color += vec3(0.10) * u_state.x;
    color += vec3(0.07) * rimBezel * rimBezel * u_light.z;

    float facing = dot(n, u_light.xy);
    float lit = pow(max(facing, 0.0), 1.5) + 0.6 * pow(max(-facing, 0.0), 1.5);
    float edgeGlow = rimBezel * rimBezel;
    color += vec3((rimOuter * 0.82 + edgeGlow * 0.24) * lit * u_light.w + rimOuter * 0.12 * u_light.z);
    color = clamp(color, 0.0, 1.0);

    float body = u_state.y > 0.5 ? 0.88 : 1.0;
    float alpha = sp.a * body;
    return vec4(color * alpha, alpha + shadow * (1.0 - alpha)) * v_fragmentColor.a;
}

void main() {
    vec2 halfSize = u_size * 0.5;
    vec2 p = v_local - halfSize;
    float onePx = 1.0 / max(u_shape.z, 1.0e-3);

    if (u_shape.w > 1.5) {
        gl_FragColor = renderSpriteGlass(onePx);
        return;
    }

    float radius = min(u_shape.x, min(halfSize.x, halfSize.y));
    float d = roundedBox(p, halfSize, radius);
    float cover = clamp(0.5 - d / onePx, 0.0, 1.0);

    if (u_shape.w > 0.5) {
        if (cover <= 0.0) discard;
        gl_FragColor = well(p, halfSize, radius, d, onePx) * cover * v_fragmentColor.a;
        return;
    }

    // Soft drop shadow, slightly below the plate.
    float shadowD = roundedBox(p + vec2(0.0, u_shadow.x), halfSize, radius);
    float fall = clamp(shadowD / max(u_shadow.y, onePx), 0.0, 1.0);
    float shadow = u_shadow.z * (1.0 - fall) * (1.0 - fall);
    if (cover <= 0.0) {
        if (shadow <= 0.003) discard;
        gl_FragColor = vec4(0.0, 0.0, 0.0, shadow) * v_fragmentColor.a;
        return;
    }

    vec2 n = boxNormal(p, halfSize, radius);

    float depth = max(-d, 0.0);
    float bevel = max(u_shape.y, onePx);
    float x = clamp(depth / bevel, 0.0, 1.0);
    float rimBand = 1.0 - x;

    vec3 color;
    if (u_state.y > 0.5) {
        float shade = clamp(v_local.y / max(u_size.y, 1.0), 0.0, 1.0);
        color = mix(u_tint.rgb * 0.34, u_tint.rgb * 0.68, shade);
    } else {
        float frost = u_material.x;
        float thickness = u_material.y;
        if (x < 1.0 && thickness > 0.0) {
            float shiftG = lensShift(x, thickness, bevel, 1.5);
            if (u_material.z > 0.0) {
                float split = 0.05 * u_material.z;
                float shiftR = lensShift(x, thickness, bevel, 1.5 - split);
                float shiftB = lensShift(x, thickness, bevel, 1.5 + split);
                color = vec3(backdrop(v_local - n * shiftR, frost).r,
                             backdrop(v_local - n * shiftG, frost).g,
                             backdrop(v_local - n * shiftB, frost).b);
            } else {
                color = backdrop(v_local - n * shiftG, frost);
            }
        } else {
            color = backdrop(v_local, frost);
        }
        // Vibrancy keeps colours alive through the frost, then the tint.
        float luma = dot(color, vec3(0.299, 0.587, 0.114));
        color = clamp(mix(vec3(luma), color, u_material.w), 0.0, 1.0);
        color = mix(color, u_tint.rgb, u_tint.a);
    }

    color *= u_state.z;
    color += vec3(0.10) * u_state.x;
    // Light scattered inside the bezel brightens the rim a little.
    color += vec3(0.07) * rimBand * rimBand * rimBand * u_light.z;

    // Specular rim: strongest where the edge faces the light, with a
    // weaker reflection on the opposite side, as on real glass.
    float facing = dot(n, u_light.xy);
    float lit = pow(max(facing, 0.0), 1.5) + 0.6 * pow(max(-facing, 0.0), 1.5);
    float edgeLine = exp(-depth / (1.25 * onePx));
    float edgeGlow = rimBand * rimBand * rimBand;
    color += vec3((edgeLine * 0.85 + edgeGlow * 0.25) * lit * u_light.w + edgeLine * 0.10 * u_light.z);

    // Line icon of a utility button, white over a faint dark halo so it
    // reads on light and dark backdrops alike.
    if (u_shadow.w > 0.5) {
        float unit = max(min(halfSize.x, halfSize.y), onePx);
        float gd = glyphDistance(p / unit, u_shadow.w) * unit;
        float ink = clamp(0.5 - gd / onePx, 0.0, 1.0);
        float halo = (1.0 - ink) * exp(-max(gd, 0.0) / (2.0 * onePx + 0.05 * unit));
        color = mix(color, color * 0.5, halo * 0.5);
        color = mix(color, vec3(min(u_state.z, 1.0)), ink * 0.95);
    }
    color = clamp(color, 0.0, 1.0);

    float body = u_state.y > 0.5 ? 0.86 : 1.0;
    float alpha = cover * body;
    gl_FragColor = vec4(color * alpha, alpha + shadow * (1.0 - alpha)) * v_fragmentColor.a;
}
