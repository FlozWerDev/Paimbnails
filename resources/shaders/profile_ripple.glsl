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
    vec2 center = (u_cursor.x < -0.5) ? vec2(0.5) : u_cursor;
    float dist = distance(v_texCoord, center);

    float wave = sin(dist * 26.0 - u_time * 5.0) * 0.5 + 0.5;
    float fade = smoothstep(0.55, 0.0, dist);
    float strength = u_intensity * 0.018 * fade * wave;

    vec2 dir = normalize(v_texCoord - center + 0.0001);
    vec4 color = texture2D(u_texture, v_texCoord + dir * strength);

    color.rgb += vec3(0.15, 0.25, 0.45) * wave * fade * u_intensity * 0.1 * color.a;
    gl_FragColor = color * v_fragmentColor;
}
