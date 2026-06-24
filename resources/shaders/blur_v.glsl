// Gaussian vertical pass. Idéntico a `fragmentShaderVertical` del inline.
// Ver blur_h.glsl para contexto del uniform `u_screenSize`.
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
    vec2 direction = vec2(0.0, texOffset.y);

    // 9-tap Gaussian via linear-sampling optimization
    // Precomputed weights for sigma=3.0, scaled dynamically
    float scale = sigma / 3.0;
    scale = min(scale, 2.5); // evita artefactos cuadrados con radius extremo
    float dy = direction.y * scale;

    vec3 result = texture2D(u_texture, v_texCoord).rgb * 0.227027027;
    result += texture2D(u_texture, v_texCoord + vec2(0.0, dy * 1.384615385)).rgb * 0.316216216;
    result += texture2D(u_texture, v_texCoord - vec2(0.0, dy * 1.384615385)).rgb * 0.316216216;
    result += texture2D(u_texture, v_texCoord + vec2(0.0, dy * 3.230769231)).rgb * 0.070270270;
    result += texture2D(u_texture, v_texCoord - vec2(0.0, dy * 3.230769231)).rgb * 0.070270270;

    gl_FragColor = vec4(result, 1.0) * v_fragmentColor;
}
