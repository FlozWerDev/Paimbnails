// voronoi facets refract the image like cut glass, brighter near the cursor
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

vec2 hash2(vec2 p) {
    return fract(sin(vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)))) * 43758.5453);
}

void main() {
    float t = mod(u_time, 628.318);
    float scale = 7.0 + u_intensity * 1.5;
    vec2 g = v_texCoord * scale;
    vec2 cell = floor(g);
    vec2 f = fract(g);

    float minD = 10.0;
    vec2 minOff = vec2(0.0);
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 nb = vec2(float(x), float(y));
            vec2 pt = hash2(cell + nb);
            pt = 0.5 + 0.4 * sin(t * 0.6 + 6.2832 * pt);
            vec2 d = nb + pt - f;
            float dd = dot(d, d);
            if (dd < minD) { minD = dd; minOff = d; }
        }
    }

    float facet = smoothstep(0.0, 0.5, minD);
    vec2 refr = minOff / scale * (u_intensity * 0.3 + 0.4);
    vec4 color = texture2D(u_texture, clamp(v_texCoord + refr, 0.0, 1.0));

    float near = smoothstep(0.5, 0.0, length(v_texCoord - u_cursor));
    color.rgb += vec3(0.6, 0.8, 1.0) * (1.0 - facet) * (0.3 + near + u_click * 0.5) * u_intensity * 0.08;

    gl_FragColor = color * v_fragmentColor;
}
