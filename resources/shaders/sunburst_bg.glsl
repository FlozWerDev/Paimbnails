#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float u_time;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord - 0.5;
    float a = atan(uv.y, uv.x);
    float r = length(uv);
    float rays = 0.5 + 0.5 * sin(a * 18.0 + u_time * 1.3);
    vec3 col = mix(vec3(0.08, 0.02, 0.15), vec3(1.0, 0.65, 0.18), rays);
    col *= (1.0 - r * 1.3) + 0.25;
    gl_FragColor = vec4(col, 1.0) * v_fragmentColor;
}
