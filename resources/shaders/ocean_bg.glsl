#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord;
    float t = u_time * 0.4;
    
    // Deep ocean base
    vec3 deep = vec3(0.01, 0.04, 0.12);
    vec3 mid = vec3(0.02, 0.12, 0.28);
    vec3 surface = vec3(0.05, 0.25, 0.45);
    
    // Layered waves
    float wave1 = sin(uv.x * 3.0 + t * 0.8 + uv.y * 2.0) * 0.5 + 0.5;
    float wave2 = sin(uv.x * 5.0 - t * 1.2 + uv.y * 3.0) * 0.5 + 0.5;
    float wave3 = sin(uv.x * 8.0 + t * 0.5 - uv.y * 4.0) * 0.5 + 0.5;
    
    float depth = uv.y * 0.7 + wave1 * 0.15 + wave2 * 0.1;
    vec3 col = mix(deep, mid, smoothstep(0.0, 0.5, depth));
    col = mix(col, surface, smoothstep(0.5, 1.0, depth));
    
    // Caustics
    float c1 = sin(uv.x * 20.0 + t * 2.0) * sin(uv.y * 20.0 + t * 1.5);
    float c2 = sin(uv.x * 15.0 - t * 1.8) * sin(uv.y * 18.0 - t * 2.2);
    float caustic = max(c1, c2);
    caustic = smoothstep(0.3, 0.8, caustic) * (1.0 - uv.y) * 0.3;
    col += vec3(0.1, 0.3, 0.4) * caustic;
    
    // Light rays from surface
    float ray = smoothstep(0.7, 1.0, uv.y);
    float rayPattern = sin(uv.x * 6.0 + t * 0.3) * 0.5 + 0.5;
    rayPattern *= sin(uv.x * 10.0 - t * 0.5) * 0.5 + 0.5;
    col += vec3(0.1, 0.2, 0.3) * ray * rayPattern * 0.5;
    
    // Bubbles (small bright dots rising)
    float bubbleY = fract(uv.y * 3.0 - t * 0.4);
    float bubbleX = sin(uv.x * 30.0 + floor(uv.y * 3.0 - t * 0.4) * 7.0);
    float bubble = smoothstep(0.95, 1.0, bubbleX) * smoothstep(0.0, 0.1, bubbleY) * smoothstep(1.0, 0.8, bubbleY);
    col += vec3(0.3, 0.5, 0.7) * bubble * 2.0;
    
    col *= 0.8 + u_intensity * 0.2;
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
