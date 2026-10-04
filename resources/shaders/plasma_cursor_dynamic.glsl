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

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float dist = length(v_texCoord - u_cursor);
    float t = mod(u_time, 628.318);

    float radius = 0.08 + u_intensity * 0.02 + u_click * 0.1;
    float plasma = sin(dist * 40.0 - t * 6.0 + sin(t * 2.0) * 3.0);
    plasma += sin(dist * 25.0 + t * 4.0) * 0.7;
    plasma += sin((v_texCoord.x - u_cursor.x) * 30.0 + t * 5.0) * 0.5;
    plasma = plasma * 0.33 * 0.5 + 0.5;

    float mask = smoothstep(radius + 0.1, radius * 0.3, dist);
    float h = t * 0.5 + plasma * 2.0;
    vec3 plasmaColor = 0.5 + 0.5 * sin(vec3(h, h + 2.094, h + 4.189));

    float blend = mask * (0.4 + u_click * 0.4) * u_intensity * 0.12;
    color.rgb = mix(color.rgb, plasmaColor, blend);

    float core = smoothstep(radius * 0.5, 0.0, dist);
    color.rgb += plasmaColor * core * 0.3 * (0.5 + u_click * 0.5);

    gl_FragColor = color * v_fragmentColor;
}
