#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;
uniform vec2 u_cursor; // normalized 0..1 cursor/touch position

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float str = u_intensity * 0.15;
    
    // edge detection: 4 neighbors
    const vec3 lw = vec3(0.299, 0.587, 0.114);
    float px = 1.0 / 512.0;
    float lL = dot(texture2D(u_texture, v_texCoord + vec2(-px, 0.0)).rgb, lw);
    float lR = dot(texture2D(u_texture, v_texCoord + vec2( px, 0.0)).rgb, lw);
    float lU = dot(texture2D(u_texture, v_texCoord + vec2(0.0,  px)).rgb, lw);
    float lD = dot(texture2D(u_texture, v_texCoord + vec2(0.0, -px)).rgb, lw);
    float edge = smoothstep(0.02, 0.15, abs(lL - lR) + abs(lU - lD));
    
    // Neon glow radiates from cursor position
    float cursorDist = length(v_texCoord - u_cursor);
    float cursorGlow = 1.0 - smoothstep(0.0, 0.6, cursorDist);
    
    // hue cycle centered on cursor
    float h = (u_time * 0.5 + cursorDist * 2.0 + v_texCoord.x * 0.1) * 6.2832;
    vec3 neon = 0.5 + 0.5 * sin(vec3(h, h + 2.094, h + 4.189));
    float pulse = 0.7 + 0.3 * sin(u_time * 3.0 - cursorDist * 8.0);
    
    color.rgb += neon * edge * pulse * str * (1.5 + cursorGlow * 2.5);
    gl_FragColor = color * v_fragmentColor;
}
