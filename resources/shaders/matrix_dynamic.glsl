#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;
uniform vec2 u_cursor; // normalized 0..1 cursor/touch position

float mHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float str = u_intensity * 0.2;
    float cols = 30.0 + u_intensity * 10.0;
    vec2 cell = floor(v_texCoord * vec2(cols, cols * 2.0));
    
    float spd = 2.0 + mHash(vec2(cell.x, 0.0)) * 4.0;
    float fall = fract(cell.y / (cols * 2.0) - u_time * spd * 0.1);
    float trail = smoothstep(0.0, 0.4, fall) * smoothstep(1.0, 0.5, fall);
    float head = smoothstep(0.38, 0.42, fall);
    float flick = step(0.3, mHash(cell + floor(u_time * 8.0)));
    
    // Matrix rain intensifies near cursor
    float cursorDist = length(v_texCoord - u_cursor);
    float cursorBoost = 1.0 + (1.0 - smoothstep(0.0, 0.4, cursorDist)) * 1.5;
    
    float glow = (trail * 0.6 + head) * flick * cursorBoost;
    color.rgb = mix(color.rgb, color.rgb * 0.7 + vec3(0.1, 1.0, 0.3) * glow * str, str);
    gl_FragColor = color * v_fragmentColor;
}
