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
    
    // Dark city sky
    vec3 col = mix(vec3(0.02, 0.01, 0.05), vec3(0.05, 0.02, 0.12), uv.y);
    
    // Buildings silhouette
    float buildingX = uv.x * 20.0;
    float buildingID = floor(buildingX);
    float buildingFrac = fract(buildingX);
    float buildingHeight = 0.2 + fract(sin(buildingID * 43.7) * 17.3) * 0.4;
    float building = step(uv.y, buildingHeight) * step(0.05, buildingFrac) * step(buildingFrac, 0.95);
    
    // Windows
    float winX = fract(buildingX * 3.0);
    float winY = fract(uv.y * 30.0);
    float window = step(0.2, winX) * step(winX, 0.8) * step(0.2, winY) * step(winY, 0.7);
    float windowOn = step(0.4, fract(sin(floor(buildingX * 3.0) * 13.0 + floor(uv.y * 30.0) * 7.0 + t * 0.1) * 43.0));
    
    vec3 windowColor = mix(vec3(1.0, 0.8, 0.3), vec3(0.3, 0.8, 1.0), 
                           fract(sin(buildingID * 7.0) * 43.0));
    col = mix(col, vec3(0.01, 0.01, 0.02), building);
    col += windowColor * window * windowOn * building * 0.8;
    
    // Neon signs (horizontal lines on buildings)
    float neonY = abs(fract(uv.y * 8.0 + buildingID * 0.3) - 0.5);
    float neon = smoothstep(0.02, 0.0, neonY) * building;
    float neonPulse = sin(t * 3.0 + buildingID * 2.0) * 0.5 + 0.5;
    vec3 neonCol = mix(vec3(1.0, 0.0, 0.5), vec3(0.0, 1.0, 0.8), fract(buildingID * 0.37));
    col += neonCol * neon * neonPulse * 0.6;
    
    // Rain
    float rainX = fract(uv.x * 80.0 + sin(uv.y * 5.0) * 0.1);
    float rainY = fract(uv.y * 4.0 - t * 8.0 + fract(sin(floor(uv.x * 80.0) * 7.0) * 43.0));
    float rain = smoothstep(0.48, 0.5, rainX) * smoothstep(0.0, 0.3, rainY) * smoothstep(1.0, 0.7, rainY);
    rain *= step(buildingHeight, uv.y); // only above buildings
    col += vec3(0.3, 0.4, 0.6) * rain * 0.3;
    
    // Reflection on ground
    if (uv.y < 0.05) {
        float reflY = 0.05 - uv.y;
        col += vec3(0.2, 0.1, 0.3) * (1.0 - reflY * 20.0) * 0.5;
        // Neon reflection
        float reflNeon = sin(uv.x * 40.0 + t * 2.0) * 0.5 + 0.5;
        col += neonCol * reflNeon * 0.2 * (1.0 - reflY * 20.0);
    }
    
    col *= 0.8 + u_intensity * 0.2;
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
