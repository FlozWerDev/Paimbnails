// sRGB to LAB downsample for CPU readback; A=0 drops the pixel.

#ifdef GL_ES
precision mediump float;
#endif

varying vec2 v_texCoord;
varying vec4 v_fragmentColor;
uniform sampler2D u_texture;

float srgbToLinear(float c) {
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

vec3 rgbToXYZ(vec3 rgb) {
    float R = srgbToLinear(rgb.r);
    float G = srgbToLinear(rgb.g);
    float B = srgbToLinear(rgb.b);
    
    float X = R * 0.4124564 + G * 0.3575761 + B * 0.1804375;
    float Y = R * 0.2126729 + G * 0.7151522 + B * 0.0721750;
    float Z = R * 0.0193339 + G * 0.1191920 + B * 0.9503041;
    
    return vec3(X, Y, Z);
}

float labF(float t) {
    const float delta = 6.0 / 29.0;
    const float delta3 = delta * delta * delta;
    return t > delta3 ? pow(t, 1.0 / 3.0) : (t / (3.0 * delta * delta) + 4.0 / 29.0);
}

vec3 xyzToLAB(vec3 xyz) {
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

float getSaturation(vec3 rgb) {
    float cmax = max(rgb.r, max(rgb.g, rgb.b));
    float cmin = min(rgb.r, min(rgb.g, rgb.b));
    float d = cmax - cmin;
    return cmax == 0.0 ? 0.0 : d / cmax;
}

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec3 rgb = texColor.rgb;
    
    float minC = min(rgb.r, min(rgb.g, rgb.b));
    float maxC = max(rgb.r, max(rgb.g, rgb.b));
    
    // thresholds mirror CPU-side 15/255 and 240/255.
    bool isBlack = maxC < 0.059;
    bool isWhite = minC > 0.941;
    
    float sat = getSaturation(rgb);
    float val = maxC;
    bool isDesaturated = sat < 0.08 || val < 0.12;
    
    if (isBlack || isWhite || isDesaturated) {
        gl_FragColor = vec4(0.0, 0.5, 0.5, 0.0);
        return;
    }
    
    vec3 xyz = rgbToXYZ(rgb);
    vec3 lab = xyzToLAB(xyz);
    
    float encodedL = lab.x / 100.0;
    float encodedA = (lab.y + 128.0) / 255.0;
    float encodedB = (lab.z + 128.0) / 255.0;
    
    gl_FragColor = vec4(
        clamp(encodedL, 0.0, 1.0),
        clamp(encodedA, 0.0, 1.0),
        clamp(encodedB, 0.0, 1.0),
        1.0
    );
}
