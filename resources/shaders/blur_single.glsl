// dual Kawase 12-tap single pass for GIFs; keep in sync with fragmentShaderBlurSinglePass inline.
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_screenSize;
uniform float u_intensity;

void main() {
    vec2 texelSize = 1.0 / u_screenSize;
    float blurAmount = u_intensity * 3.0 + 1.0;

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
