#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord;
    float t = u_time * 0.3;
    
    // Sky gradient (dark purple to hot pink horizon)
    vec3 sky = mix(vec3(0.05, 0.0, 0.15), vec3(0.4, 0.0, 0.3), uv.y);
    
    // Sun
    float sunY = 0.35 + sin(t * 0.2) * 0.05;
    float sunDist = length(vec2(uv.x - 0.5, (uv.y - sunY) * 1.5));
    float sun = smoothstep(0.15, 0.12, sunDist);
    // Sun stripes
    float stripe = step(0.5, fract(uv.y * 30.0 - t * 2.0));
    sun *= mix(1.0, stripe, smoothstep(0.0, 0.12, sunDist));
    vec3 sunCol = mix(vec3(1.0, 0.8, 0.0), vec3(1.0, 0.2, 0.4), smoothstep(0.0, 0.12, sunDist));
    sky = mix(sky, sunCol, sun);
    
    // Sun glow
    float glow = exp(-sunDist * 5.0) * 0.4;
    sky += vec3(1.0, 0.3, 0.5) * glow;
    
    // Grid floor (perspective)
    if (uv.y < 0.4) {
        float fy = 0.4 - uv.y;
        float perspective = 1.0 / (fy * 8.0 + 0.1);
        float gridX = abs(fract((uv.x - 0.5) * perspective * 2.0 + 0.5) - 0.5);
        float gridZ = abs(fract(perspective * 0.5 - t * 2.0) - 0.5);
        float grid = smoothstep(0.02, 0.0, gridX) + smoothstep(0.02, 0.0, gridZ);
        grid = min(grid, 1.0);
        vec3 gridCol = mix(vec3(0.0, 0.8, 1.0), vec3(1.0, 0.0, 0.8), sin(t + perspective) * 0.5 + 0.5);
        sky = mix(sky, gridCol, grid * 0.8 * (1.0 - fy * 2.0));
        // Floor base color
        sky = mix(sky, vec3(0.02, 0.0, 0.08), (1.0 - grid) * smoothstep(0.4, 0.0, uv.y));
    }
    
    // Horizontal glow line at horizon
    float horizonGlow = exp(-abs(uv.y - 0.4) * 30.0) * 0.6;
    sky += vec3(1.0, 0.2, 0.6) * horizonGlow;
    
    sky *= 0.8 + u_intensity * 0.2;
    gl_FragColor = vec4(sky, 1.0) * v_fragmentColor;
}
