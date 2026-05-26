#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;
uniform vec2 u_cursor; // normalized 0..1 cursor/touch position

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
    vec2 uv = v_texCoord;
    float b3 = u_intensity * 3.0;
    float str = b3 * 0.1;
    
    // Glitch intensity increases near cursor
    float cursorDist = length(v_texCoord - u_cursor);
    float cursorInfluence = 1.0 - smoothstep(0.0, 0.5, cursorDist);
    str *= (0.4 + cursorInfluence * 1.2);
    
    // line + block displacement
    float n1 = noise(vec2(uv.y * 40.0, u_time * 6.0));
    float n2 = noise(vec2(uv.y * 5.0, u_time * 3.5));
    uv.x += (n1 - 0.5) * str * smoothstep(0.92 - b3 * 0.04, 0.95, n1);
    uv.x += (n2 - 0.5) * str * 2.5 * smoothstep(0.93 - b3 * 0.02, 0.97, n2);
    
    vec4 color = texture2D(u_texture, uv);
    
    // chromatic split stronger near cursor
    float n3 = noise(vec2(uv.y * 60.0, u_time * 8.0));
    float cg = smoothstep(0.88 - b3 * 0.03, 0.94, n3);
    float shift = 0.015 * b3 * cg * (0.5 + cursorInfluence);
    color.r = mix(color.r, texture2D(u_texture, uv + vec2(shift, 0.0)).r, cg);
    color.b = mix(color.b, texture2D(u_texture, uv - vec2(shift, 0.0)).b, cg);
    
    // scanline flicker
    float n4 = noise(vec2(u_time * 12.0, uv.y * 200.0));
    color.rgb *= 1.0 - smoothstep(0.65, 0.95, n4) * 0.15 * b3;
    
    gl_FragColor = color * v_fragmentColor;
}
