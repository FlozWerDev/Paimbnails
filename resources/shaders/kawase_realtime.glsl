// PaimonBlur real-time single-pass for GIFs — lightweight Dual Kawase 9-tap.
// Este shader se usa en `getPaimonBlurShader()` (clave `paimonblur-rt-v2`).
// Debe mantenerse idéntico a `fragmentShaderPaimonBlurRT` del inline hasta
// que la Fase 5 elimine el literal.
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
    float blurAmount = u_intensity * 4.0 + 1.5;
    vec2 hp = (blurAmount * 0.5) * texelSize;

    // Optimized 9-tap Kawase single-pass (5 texture reads total)
    vec3 color = texture2D(u_texture, v_texCoord).rgb * 4.0;
    color += texture2D(u_texture, v_texCoord + hp).rgb;
    color += texture2D(u_texture, v_texCoord - hp).rgb;
    color += texture2D(u_texture, v_texCoord + vec2(hp.x, -hp.y)).rgb;
    color += texture2D(u_texture, v_texCoord - vec2(hp.x, -hp.y)).rgb;

    gl_FragColor = vec4(color / 8.0, 1.0) * v_fragmentColor;
}
