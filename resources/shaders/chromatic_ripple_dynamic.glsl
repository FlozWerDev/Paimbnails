// expanding rings split RGB along the radial direction from the cursor
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
    vec2 delta = v_texCoord - u_cursor;
    float dist = length(delta);
    float t = mod(u_time, 628.318);
    vec2 dir = delta / max(dist, 1e-4);

    float wave = sin(dist * 28.0 - t * 5.0);
    float fade = smoothstep(0.8, 0.0, dist);
    float amt = (u_intensity * 0.01 + u_click * 0.02) * wave * fade;

    float r = texture2D(u_texture, clamp(v_texCoord + dir * amt * 1.4, 0.0, 1.0)).r;
    float g = texture2D(u_texture, clamp(v_texCoord + dir * amt, 0.0, 1.0)).g;
    float b = texture2D(u_texture, clamp(v_texCoord + dir * amt * 0.6, 0.0, 1.0)).b;
    vec4 color = vec4(r, g, b, texture2D(u_texture, v_texCoord).a);

    float crest = smoothstep(0.6, 1.0, wave) * fade;
    color.rgb += vec3(0.4, 0.6, 1.0) * crest * u_intensity * 0.05;

    gl_FragColor = color * v_fragmentColor;
}
