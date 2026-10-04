// a horizontal scan bar sweeps the cursor row, tearing and color-shifting it
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

void main() {
    vec2 uv = v_texCoord;
    float t = mod(u_time, 300.0);

    float scanY = fract(t * 0.25);
    float barDist = abs(uv.y - scanY);
    float bar = smoothstep(0.05, 0.0, barDist);

    float cursorBand = smoothstep(0.08, 0.0, abs(uv.y - u_cursor.y));
    float active = max(bar, cursorBand * (0.4 + u_click * 0.6));

    float block = hash(vec2(floor(uv.y * 60.0), floor(t * 20.0)));
    float tear = (block - 0.5) * active * (u_intensity * 0.04 + 0.02);
    uv.x = clamp(uv.x + tear, 0.0, 1.0);

    float sh = active * (u_intensity * 0.01 + u_click * 0.015);
    vec4 color;
    color.r = texture2D(u_texture, clamp(uv + vec2(sh, 0.0), 0.0, 1.0)).r;
    color.g = texture2D(u_texture, uv).g;
    color.b = texture2D(u_texture, clamp(uv - vec2(sh, 0.0), 0.0, 1.0)).b;
    color.a = texture2D(u_texture, uv).a;

    color.rgb += vec3(0.1, 0.9, 0.8) * step(0.8, block) * active * u_intensity * 0.08;

    gl_FragColor = color * v_fragmentColor;
}
