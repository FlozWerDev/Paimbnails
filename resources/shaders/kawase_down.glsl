// Dual Kawase downsample: center(×4) + 4 diagonal half-pixel(×1) = ÷8.
// Idéntico a `fragmentShaderPaimonBlurDown` del header — no cambies pesos
// sin actualizar ambos lados durante la ventana de migración.
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_halfpixel;

void main() {
    vec3 sum = texture2D(u_texture, v_texCoord).rgb * 4.0;
    sum += texture2D(u_texture, v_texCoord - u_halfpixel).rgb;
    sum += texture2D(u_texture, v_texCoord + u_halfpixel).rgb;
    sum += texture2D(u_texture, v_texCoord + vec2(u_halfpixel.x, -u_halfpixel.y)).rgb;
    sum += texture2D(u_texture, v_texCoord - vec2(u_halfpixel.x, -u_halfpixel.y)).rgb;
    gl_FragColor = vec4(sum / 8.0, 1.0) * v_fragmentColor;
}
