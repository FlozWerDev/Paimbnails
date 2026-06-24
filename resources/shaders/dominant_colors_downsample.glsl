// GPU-accelerated color extraction pre-pass.
// Renders the source texture to a tiny FBO (e.g. 32x32) while converting
// each pixel from sRGB to CIE LAB and encoding L/a/b into the RGB channels.
// The CPU then reads back only 1024 pixels instead of sampling thousands
// from the full-resolution image.
//
// Output encoding (per pixel):
//   R = L / 100.0          (luminosity, [0..1])
//   G = (a + 128.0) / 255.0  (green-red axis, [0..1])
//   B = (b + 128.0) / 255.0  (blue-yellow axis, [0..1])
//   A = saturation mask (0.0 = filtered out, 1.0 = valid sample)
//
// The saturation mask filters out UI elements (pure black/white) and
// very desaturated pixels, matching the CPU-side isLikelyUIOrObject() logic.

#ifdef GL_ES
precision mediump float;
#endif

varying vec2 v_texCoord;
varying vec4 v_fragmentColor;
uniform sampler2D u_texture;

// sRGB gamma → linear
float srgbToLinear(float c) {
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

// Linear RGB → XYZ (D65)
vec3 rgbToXYZ(vec3 rgb) {
    float R = srgbToLinear(rgb.r);
    float G = srgbToLinear(rgb.g);
    float B = srgbToLinear(rgb.b);
    
    float X = R * 0.4124564 + G * 0.3575761 + B * 0.1804375;
    float Y = R * 0.2126729 + G * 0.7151522 + B * 0.0721750;
    float Z = R * 0.0193339 + G * 0.1191920 + B * 0.9503041;
    
    return vec3(X, Y, Z);
}

// XYZ → LAB helper
float labF(float t) {
    const float delta = 6.0 / 29.0;
    const float delta3 = delta * delta * delta;
    return t > delta3 ? pow(t, 1.0 / 3.0) : (t / (3.0 * delta * delta) + 4.0 / 29.0);
}

// XYZ → CIE LAB
vec3 xyzToLAB(vec3 xyz) {
    // D65 white point
    const vec3 Wn = vec3(0.95047, 1.0, 1.08883);
    
    vec3 f = vec3(
        labF(xyz.x / Wn.x),
        labF(xyz.y / Wn.y),
        labF(xyz.z / Wn.z)
    );
    
    float L = 116.0 * f.y - 16.0;
    float a = 500.0 * (f.x - f.y);
    float b = 200.0 * (f.y - f.z);
    
    return vec3(L, a, b);
}

// RGB → HSV (for saturation filtering)
float getSaturation(vec3 rgb) {
    float cmax = max(rgb.r, max(rgb.g, rgb.b));
    float cmin = min(rgb.r, min(rgb.g, rgb.b));
    float d = cmax - cmin;
    return cmax == 0.0 ? 0.0 : d / cmax;
}

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec3 rgb = texColor.rgb;
    
    // Filter: UI elements (near-black or near-white)
    float minC = min(rgb.r, min(rgb.g, rgb.b));
    float maxC = max(rgb.r, max(rgb.g, rgb.b));
    
    // Thresholds matching CPU-side constants (15/255 ≈ 0.059, 240/255 ≈ 0.941)
    bool isBlack = maxC < 0.059;
    bool isWhite = minC > 0.941;
    
    // Filter: low saturation or very dark
    float sat = getSaturation(rgb);
    float val = maxC;
    bool isDesaturated = sat < 0.08 || val < 0.12;
    
    if (isBlack || isWhite || isDesaturated) {
        // Mark as invalid sample
        gl_FragColor = vec4(0.0, 0.5, 0.5, 0.0);
        return;
    }
    
    // Convert to LAB
    vec3 xyz = rgbToXYZ(rgb);
    vec3 lab = xyzToLAB(xyz);
    
    // Encode LAB into [0,1] range for RGBA8 storage
    float encodedL = lab.x / 100.0;                    // L: [0,100] → [0,1]
    float encodedA = (lab.y + 128.0) / 255.0;         // a: [-128,127] → [0,1]
    float encodedB = (lab.z + 128.0) / 255.0;         // b: [-128,127] → [0,1]
    
    gl_FragColor = vec4(
        clamp(encodedL, 0.0, 1.0),
        clamp(encodedA, 0.0, 1.0),
        clamp(encodedB, 0.0, 1.0),
        1.0  // valid sample
    );
}
