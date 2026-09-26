// Dual Kawase upsample: 4 cardinal(×2) + 4 diagonal(×1) = ÷12.
// keep weights in sync with fragmentShaderPaimonBlurUp.
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_halfpixel;

void main() {
    vec3 sum = vec3(0.0);
    sum += texture2D(u_texture, v_texCoord + vec2(-u_halfpixel.x * 2.0, 0.0)).rgb * 2.0;
    sum += texture2D(u_texture, v_texCoord + vec2( u_halfpixel.x * 2.0, 0.0)).rgb * 2.0;
    sum += texture2D(u_texture, v_texCoord + vec2(0.0, -u_halfpixel.y * 2.0)).rgb * 2.0;
    sum += texture2D(u_texture, v_texCoord + vec2(0.0,  u_halfpixel.y * 2.0)).rgb * 2.0;
    sum += texture2D(u_texture, v_texCoord + u_halfpixel).rgb;
    sum += texture2D(u_texture, v_texCoord - u_halfpixel).rgb;
    sum += texture2D(u_texture, v_texCoord + vec2(u_halfpixel.x, -u_halfpixel.y)).rgb;
    sum += texture2D(u_texture, v_texCoord - vec2(u_halfpixel.x, -u_halfpixel.y)).rgb;
    gl_FragColor = vec4(sum / 12.0, 1.0) * v_fragmentColor;
}
