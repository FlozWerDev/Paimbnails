#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

void main() {
    float s = u_intensity * 0.1;
    
    // barrel distortion
    vec2 uv = v_texCoord * 2.0 - 1.0;
    uv *= 1.0 + dot(uv, uv) * s * 0.5;
    uv = uv * 0.5 + 0.5;
    
    // vignette + clamp
    float vig = smoothstep(0.0, 0.02, uv.x) * smoothstep(1.0, 0.98, uv.x)
              * smoothstep(0.0, 0.02, uv.y) * smoothstep(1.0, 0.98, uv.y);
    uv = clamp(uv, 0.0, 1.0);
    
    // 3 reads: center (g+a), left (r), right (b)
    float subpx = s * 0.003;
    vec4 center = texture2D(u_texture, uv);
    float r = texture2D(u_texture, uv + vec2(subpx, 0.0)).r;
    float b = texture2D(u_texture, uv - vec2(subpx, 0.0)).b;
    
    vec3 col = vec3(r, center.g, b);
    
    // scanlines + flicker + grain combined
    col *= mix(1.0, sin(uv.y * 800.0) * 0.5 + 0.5, s * 0.3);
    col *= 1.0 - 0.03 * s * sin(u_time * 8.0 + sin(u_time * 13.0) * 2.0);
    col += fract(sin(dot(v_texCoord * 500.0 + u_time, vec2(127.1, 311.7))) * 43758.5453) * s * 0.15;
    col *= vig;
    
    gl_FragColor = vec4(col, center.a) * v_fragmentColor;
}
