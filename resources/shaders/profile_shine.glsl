#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);

    // diagonal band sweeping across the face.
    float band = v_texCoord.x + v_texCoord.y;
    float pos = fract(u_time * 0.5);
    float d = abs(band * 0.5 - pos);
    float shine = smoothstep(0.14, 0.0, d);

    color.rgb += vec3(1.0) * shine * u_intensity * 0.6 * color.a;
    gl_FragColor = color * v_fragmentColor;
}
