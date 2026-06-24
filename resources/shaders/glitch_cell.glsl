#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

float rand(vec2 co) {
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec2 uv = v_texCoord;
    float glitchStrength = u_intensity * 0.1;
    
    float lineNoise = rand(vec2(floor(uv.y * 100.0), u_time));
    if (lineNoise > 0.95 - u_intensity * 0.05) {
        uv.x += (rand(vec2(uv.y, u_time)) - 0.5) * glitchStrength;
    }
    
    vec4 color = texture2D(u_texture, uv);
    
    if (rand(vec2(uv.y, u_time + 1.0)) > 0.98 - u_intensity * 0.02) {
        color.r = texture2D(u_texture, uv + vec2(0.01 * u_intensity, 0.0)).r;
        color.b = texture2D(u_texture, uv - vec2(0.01 * u_intensity, 0.0)).b;
    }
    
    gl_FragColor = color * v_fragmentColor;
}
