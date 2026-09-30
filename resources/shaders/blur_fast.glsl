// fixed 3.5px blur for profilethumbs backgrounds.
// keep in sync with fragmentshaderfastblur inline.
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;

void main() {
    vec2 texelSize = 1.0 / u_texSize;
    float blurAmount = 3.5;

    vec2 halfpixel = (blurAmount * 0.5) * texelSize;
    vec2 offset = blurAmount * texelSize;

    vec3 color = texture2D(u_texture, v_texCoord).rgb * 4.0;

    color += texture2D(u_texture, v_texCoord - halfpixel).rgb;
    color += texture2D(u_texture, v_texCoord + halfpixel).rgb;
    color += texture2D(u_texture, v_texCoord + vec2(halfpixel.x, -halfpixel.y)).rgb;
    color += texture2D(u_texture, v_texCoord - vec2(halfpixel.x, -halfpixel.y)).rgb;

    color += texture2D(u_texture, v_texCoord + vec2(-offset.x, 0.0)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2( offset.x, 0.0)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2(0.0, -offset.y)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2(0.0,  offset.y)).rgb * 2.0;

    gl_FragColor = vec4(color / 16.0, 1.0) * v_fragmentColor;
}
