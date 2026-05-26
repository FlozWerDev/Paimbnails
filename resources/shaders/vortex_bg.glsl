#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord * 2.0 - 1.0;
    float t = u_time * 0.5;
    
    float r = length(uv);
    float a = atan(uv.y, uv.x);
    
    // Spiral distortion
    float spiral = a + r * 4.0 - t * 2.0;
    float arms = sin(spiral * 3.0) * 0.5 + 0.5;
    arms = pow(arms, 2.0);
    
    // Color based on angle and radius
    vec3 col1 = vec3(0.1, 0.0, 0.3);  // deep purple
    vec3 col2 = vec3(0.0, 0.4, 0.8);  // blue
    vec3 col3 = vec3(0.8, 0.1, 0.5);  // magenta
    
    float colorMix = sin(a * 2.0 + t) * 0.5 + 0.5;
    vec3 armColor = mix(col2, col3, colorMix);
    
    vec3 col = mix(col1, armColor, arms * smoothstep(1.2, 0.0, r));
    
    // Center glow
    float centerGlow = exp(-r * 3.0);
    col += vec3(0.5, 0.3, 0.8) * centerGlow;
    
    // Outer ring pulses
    float ring = abs(r - 0.6 - sin(t * 1.5) * 0.1);
    ring = smoothstep(0.05, 0.0, ring);
    col += vec3(0.2, 0.5, 1.0) * ring * 0.5;
    
    // Particles orbiting
    for (int i = 0; i < 5; i++) {
        float fi = float(i);
        float pa = t * (1.0 + fi * 0.3) + fi * 1.256;
        float pr = 0.3 + fi * 0.12;
        vec2 pp = vec2(cos(pa), sin(pa)) * pr;
        float pd = length(uv - pp);
        col += vec3(0.4, 0.6, 1.0) * exp(-pd * 20.0) * 0.4;
    }
    
    col *= 0.7 + u_intensity * 0.3;
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
