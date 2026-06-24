// Fast blur de intensidad fija (3.5 px) para fondos de ProfileThumbs.
// Idéntico a `fragmentShaderFastBlur` del inline. Se usa como fallback en
// `ProfileThumbs` cuando el realtime shader principal no está disponible.
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;

void main() {
    vec2 texelSize = 1.0 / u_texSize;
    float blurAmount = 3.5; // Intensidad fija para fondos

    vec2 halfpixel = (blurAmount * 0.5) * texelSize;
    vec2 offset = blurAmount * texelSize;

    // Dual Kawase Blur - metodo profesional de juegos AAA
    vec3 color = texture2D(u_texture, v_texCoord).rgb * 4.0;

    // Diagonales cercanas
    color += texture2D(u_texture, v_texCoord - halfpixel).rgb;
    color += texture2D(u_texture, v_texCoord + halfpixel).rgb;
    color += texture2D(u_texture, v_texCoord + vec2(halfpixel.x, -halfpixel.y)).rgb;
    color += texture2D(u_texture, v_texCoord - vec2(halfpixel.x, -halfpixel.y)).rgb;

    // Cardinales con peso extra para suavidad
    color += texture2D(u_texture, v_texCoord + vec2(-offset.x, 0.0)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2( offset.x, 0.0)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2(0.0, -offset.y)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2(0.0,  offset.y)).rgb * 2.0;

    // Alpha siempre 1.0 para fondos opacos
    gl_FragColor = vec4(color / 16.0, 1.0) * v_fragmentColor;
}
