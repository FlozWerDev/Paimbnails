#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord;
    vec3 skyTop = vec3(0.08, 0.02, 0.20);
    vec3 skyBot = vec3(1.0, 0.45, 0.20);
    vec3 col = mix(skyBot, skyTop, uv.y);
    float sun = exp(-60.0 * length(uv - vec2(0.5, 0.35)));
    col += vec3(1.0, 0.8, 0.3) * sun;
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
