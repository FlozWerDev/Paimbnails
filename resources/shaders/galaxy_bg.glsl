#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    vec2 uv = v_texCoord * 2.0 - 1.0;
    float t = u_time * 0.15;
    
    // Dark space background
    vec3 col = vec3(0.01, 0.005, 0.03);
    
    // Galaxy spiral
    float r = length(uv);
    float a = atan(uv.y, uv.x);
    
    float spiral1 = sin(a * 2.0 + r * 6.0 - t * 1.5) * 0.5 + 0.5;
    float spiral2 = sin(a * 2.0 + r * 6.0 - t * 1.5 + 3.14) * 0.5 + 0.5;
    float spiralMask = exp(-r * 2.5) * smoothstep(0.0, 0.3, r);
    
    vec3 arm1Col = vec3(0.3, 0.1, 0.5) * pow(spiral1, 3.0) * spiralMask;
    vec3 arm2Col = vec3(0.1, 0.2, 0.6) * pow(spiral2, 3.0) * spiralMask;
    col += arm1Col + arm2Col;
    
    // Nebula clouds
    float n1 = noise(uv * 3.0 + t * 0.5);
    float n2 = noise(uv * 6.0 - t * 0.3);
    float nebula = n1 * n2 * spiralMask * 2.0;
    col += vec3(0.4, 0.1, 0.3) * nebula * 0.5;
    
    // Core glow
    float core = exp(-r * 8.0);
    col += vec3(1.0, 0.8, 0.5) * core * 0.6;
    col += vec3(0.5, 0.3, 0.7) * exp(-r * 4.0) * 0.3;
    
    // Stars (multiple layers)
    for (int layer = 0; layer < 3; layer++) {
        float fl = float(layer);
        vec2 starUV = v_texCoord * (50.0 + fl * 30.0);
        vec2 starID = floor(starUV);
        vec2 starF = fract(starUV) - 0.5;
        float starBright = hash(starID + fl * 100.0);
        float starSize = hash(starID + fl * 200.0 + 50.0);
        float star = smoothstep(0.05 * starSize, 0.0, length(starF));
        star *= step(0.85 - fl * 0.1, starBright);
        float twinkle = sin(t * 5.0 + starBright * 20.0) * 0.3 + 0.7;
        col += vec3(0.8 + starBright * 0.2) * star * twinkle * (0.3 + fl * 0.2);
    }
    
    col *= 0.7 + u_intensity * 0.3;
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
