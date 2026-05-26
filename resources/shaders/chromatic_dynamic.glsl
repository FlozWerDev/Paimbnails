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
    // Cursor drives the chromatic direction and strength
    vec2 cursorDir = v_texCoord - u_cursor;
    float dist = length(cursorDir);
    float pulse = 1.0 + 0.3 * sin(u_time * 1.8);
    float amount = u_intensity * 0.012 * pulse * (0.5 + dist);
    
    vec2 offset = cursorDir * amount;
    vec2 perpDir = vec2(-cursorDir.y, cursorDir.x);
    vec2 oR = offset + normalize(perpDir + 0.001) * amount * 0.4;
    vec2 oB = offset - normalize(perpDir + 0.001) * amount * 0.4;
    
    vec4 center = texture2D(u_texture, v_texCoord);
    float r = texture2D(u_texture, v_texCoord + oR).r;
    float b = texture2D(u_texture, v_texCoord - oB).b;
    gl_FragColor = vec4(r, center.g, b, center.a) * v_fragmentColor;
}
