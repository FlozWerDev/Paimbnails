#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord - 0.5;
    float r = length(uv);
    float rings = 0.5 + 0.5 * sin(r * 60.0 - u_time * 3.0);
    vec3 col = mix(vec3(0.02, 0.03, 0.08), vec3(0.10, 0.80, 1.0), rings);
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
