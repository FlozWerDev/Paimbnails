// soft bokeh orbs drift upward and bloom brighter when the cursor passes near
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
    vec4 color = texture2D(u_texture, uv);
    float t = mod(u_time, 300.0);

    float orbs = 0.0;
    vec3 tint = vec3(0.0);
    for (int i = 0; i < 9; i++) {
        float fi = float(i);
        vec2 seed = vec2(hash(vec2(fi, 1.0)), hash(vec2(fi, 2.0)));
        vec2 pos = vec2(seed.x + sin(t * 0.3 + fi) * 0.05, fract(seed.y + t * 0.04 * (0.5 + seed.x)));
        float d = length(uv - pos);
        float r = 0.04 + seed.x * 0.05;
        float orb = smoothstep(r, r * 0.2, d);
        float near = smoothstep(0.4, 0.0, length(pos - u_cursor));
        orb *= 0.3 + near * 0.9 + u_click * 0.4;
        orbs += orb;
        tint += (0.5 + 0.5 * sin(vec3(fi, fi + 2.0, fi + 4.0))) * orb;
    }
    color.rgb += tint * u_intensity * 0.1;

    gl_FragColor = color * v_fragmentColor;
}
