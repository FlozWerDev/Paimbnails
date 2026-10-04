#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;
uniform vec2 u_cursor;

void main() {
    vec2 cursorDir = v_texCoord - u_cursor;
    float dist = length(cursorDir);
    float t = mod(u_time, 628.318);

    float pulse = 1.0 + 0.3 * sin(t * 1.8);
    float amount = u_intensity * 0.012 * pulse * (0.5 + dist);

    vec2 offset = cursorDir * amount;
    vec2 perp = vec2(-cursorDir.y, cursorDir.x) / max(dist, 1e-4);
    vec2 oR = offset + perp * amount * 0.4;
    vec2 oB = offset - perp * amount * 0.4;

    vec4 center = texture2D(u_texture, v_texCoord);
    float r = texture2D(u_texture, clamp(v_texCoord + oR, 0.0, 1.0)).r;
    float b = texture2D(u_texture, clamp(v_texCoord - oB, 0.0, 1.0)).b;
    gl_FragColor = vec4(r, center.g, b, center.a) * v_fragmentColor;
}
