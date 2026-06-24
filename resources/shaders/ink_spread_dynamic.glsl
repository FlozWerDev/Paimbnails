#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;
uniform vec2 u_cursor;
uniform float u_click;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

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
    vec4 color = texture2D(u_texture, uv);
    
    float dist = length(v_texCoord - u_cursor);
    
    // Ink spread radius grows with time, resets periodically
    float cycle = mod(u_time * 0.4, 3.0);
    float spreadRadius = cycle * 0.4 * (0.5 + u_click * 0.5);
    
    // Organic edge using noise
    float n = noise(v_texCoord * 8.0 + u_time * 0.5);
    float n2 = noise(v_texCoord * 16.0 - u_time * 0.3);
    float organicDist = dist - n * 0.08 - n2 * 0.04;
    
    // Ink mask
    float ink = smoothstep(spreadRadius + 0.02, spreadRadius - 0.02, organicDist);
    ink *= u_intensity * 0.1;
    
    // Ink darkens the image with slight blue tint
    vec3 inkColor = vec3(0.02, 0.02, 0.08);
    color.rgb = mix(color.rgb, inkColor, ink * 0.7);
    
    // Feathered edge with color bleed
    float edge = smoothstep(spreadRadius + 0.04, spreadRadius, organicDist)
               - smoothstep(spreadRadius, spreadRadius - 0.02, organicDist);
    color.rgb += vec3(0.1, 0.05, 0.2) * edge * u_intensity * 0.1;
    
    // Paper texture in ink area
    float paper = noise(v_texCoord * 50.0) * 0.1;
    color.rgb += paper * ink * 0.3;
    
    gl_FragColor = color * v_fragmentColor;
}
