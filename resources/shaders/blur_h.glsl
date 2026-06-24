// Gaussian horizontal pass (13-tap linear-sampling, sigma tuned por radius).
// Idéntico a `fragmentShaderHorizontal` del inline. Se usa en H+V Gaussian
// blur (createBlurredSprite, createPopupBlurredSprite, ProgressiveBlurJob).
// El uniform `u_screenSize` es el tamaño del RT destino, no de la textura.
#ifdef GL_ES
precision mediump float;
#endif

varying vec4 v_fragmentColor;
varying vec2 v_texCoord;

uniform sampler2D u_texture;
uniform vec2 u_screenSize;
uniform float u_radius;

void main() {
    float sigma = max(u_radius * u_screenSize.y * 0.15, 0.5);
    vec2 texOffset = 1.0 / u_screenSize;
    vec2 direction = vec2(texOffset.x, 0.0);

    // 9-tap Gaussian via linear-sampling optimization
    // Precomputed weights for sigma=3.0, scaled dynamically
    float scale = sigma / 3.0;
    scale = min(scale, 2.5); // evita artefactos cuadrados con radius extremo
    float dx = direction.x * scale;

    vec3 result = texture2D(u_texture, v_texCoord).rgb * 0.227027027;
    result += texture2D(u_texture, v_texCoord + vec2(dx * 1.384615385, 0.0)).rgb * 0.316216216;
    result += texture2D(u_texture, v_texCoord - vec2(dx * 1.384615385, 0.0)).rgb * 0.316216216;
    result += texture2D(u_texture, v_texCoord + vec2(dx * 3.230769231, 0.0)).rgb * 0.070270270;
    result += texture2D(u_texture, v_texCoord - vec2(dx * 3.230769231, 0.0)).rgb * 0.070270270;

    gl_FragColor = vec4(result, 1.0) * v_fragmentColor;
}
