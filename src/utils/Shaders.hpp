#pragma once
#include <Geode/cocos/platform/CCGL.h>
#include <Geode/cocos/shaders/CCGLProgram.h>
#include <Geode/cocos/shaders/CCShaderCache.h>
#include <Geode/utils/cocos.hpp>
#include <functional>
#include <vector>

// Nota: NO usar 'using namespace' en headers — contamina todo TU.
// Las funciones/clases dentro de Shaders namespace usan tipos cocos2d:: explicitos
// o estan en .cpp donde si se puede usar 'using namespace'.

// shaders comunes que usamos por todos lados
// todo junto aqui pa no copiar/pegar codigo

namespace Shaders {

    // Declared in Shaders.cpp
    cocos2d::CCGLProgram* getOrCreateShader(char const* key, char const* vertexSrc, char const* fragmentSrc);

constexpr auto vertexShaderCell =
R"(
attribute vec4 a_position;
attribute vec4 a_color;
attribute vec2 a_texCoord;

#ifdef GL_ES
varying lowp vec4 v_fragmentColor;
varying mediump vec2 v_texCoord;
#else
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
#endif

void main()
{
    gl_Position = CC_MVPMatrix * a_position;
    v_fragmentColor = a_color;
    v_texCoord = a_texCoord;
})";

constexpr auto fragmentShaderHorizontal = R"(
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
})";

constexpr auto fragmentShaderVertical = R"(
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
})";

// shader de escala de grises
constexpr auto fragmentShaderGrayscale = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    vec3 result = mix(color.rgb, vec3(gray), u_intensity);
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
})";

// shader de sepia
constexpr auto fragmentShaderSepia = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    vec3 sepia;
    sepia.r = dot(color.rgb, vec3(0.393, 0.769, 0.189));
    sepia.g = dot(color.rgb, vec3(0.349, 0.686, 0.168));
    sepia.b = dot(color.rgb, vec3(0.272, 0.534, 0.131));
    vec3 result = mix(color.rgb, sepia, u_intensity);
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
})";

// shader de vineta
constexpr auto fragmentShaderVignette = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    vec2 pos = v_texCoord - 0.5;
    float dist = length(pos);
    float vignette = smoothstep(0.8, 0.3 * (1.0 - u_intensity), dist);
    gl_FragColor = vec4(color.rgb * vignette, color.a) * v_fragmentColor;
})";

// shader de scanlines
constexpr auto fragmentShaderScanlines = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform vec2 u_screenSize;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float scanline = sin(v_texCoord.y * u_screenSize.y * 3.14159 * (1.0 + u_intensity * 2.0)) * 0.5 + 0.5;
    scanline = mix(1.0, scanline, u_intensity * 0.5);
    gl_FragColor = vec4(color.rgb * scanline, color.a) * v_fragmentColor;
})";

// shader de bloom (optimizado: 9-tap cross en lugar de NxN loop)
constexpr auto fragmentShaderBloom = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform vec2 u_screenSize;

void main() {
    vec2 px = u_intensity * 2.0 / u_screenSize;
    vec4 color = texture2D(u_texture, v_texCoord);
    vec3 bloom = vec3(0.0);
    // 9-tap cross pattern: center + 4 cardinal + 4 diagonal
    vec3 s0 = texture2D(u_texture, v_texCoord + vec2(-px.x, 0.0)).rgb;
    vec3 s1 = texture2D(u_texture, v_texCoord + vec2( px.x, 0.0)).rgb;
    vec3 s2 = texture2D(u_texture, v_texCoord + vec2(0.0, -px.y)).rgb;
    vec3 s3 = texture2D(u_texture, v_texCoord + vec2(0.0,  px.y)).rgb;
    vec3 s4 = texture2D(u_texture, v_texCoord + vec2(-px.x, -px.y)).rgb;
    vec3 s5 = texture2D(u_texture, v_texCoord + vec2( px.x, -px.y)).rgb;
    vec3 s6 = texture2D(u_texture, v_texCoord + vec2(-px.x,  px.y)).rgb;
    vec3 s7 = texture2D(u_texture, v_texCoord + vec2( px.x,  px.y)).rgb;
    // extract bright parts from each tap
    float t = 0.75;
    bloom += max(s0 - t, 0.0) + max(s1 - t, 0.0) + max(s2 - t, 0.0) + max(s3 - t, 0.0);
    bloom += (max(s4 - t, 0.0) + max(s5 - t, 0.0) + max(s6 - t, 0.0) + max(s7 - t, 0.0)) * 0.7;
    bloom += max(color.rgb - t, 0.0);
    bloom *= u_intensity * 0.15;
    gl_FragColor = vec4(color.rgb + bloom, color.a) * v_fragmentColor;
})";

// shader de aberracion cromatica (animado, optimizado: 3 texture reads)
constexpr auto fragmentShaderChromatic = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

void main() {
    float pulse = 1.0 + 0.4 * sin(u_time * 1.8) + 0.2 * sin(u_time * 3.1);
    float amount = u_intensity * 0.01 * pulse;
    float angle = u_time * 0.5;
    vec2 dir = vec2(cos(angle), sin(angle));
    vec2 offset = (v_texCoord - 0.5) * amount;
    vec2 oR = offset + dir * amount * 0.3;
    vec2 oB = offset - dir * amount * 0.3;
    vec4 center = texture2D(u_texture, v_texCoord);
    float r = texture2D(u_texture, v_texCoord + oR).r;
    float b = texture2D(u_texture, v_texCoord - oB).b;
    gl_FragColor = vec4(r, center.g, b, center.a) * v_fragmentColor;
})";

// shader de blur radial (centro animado, optimizado: 8 muestras fijas)
constexpr auto fragmentShaderRadialBlur = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

void main() {
    float cx = 0.5 + 0.15 * sin(u_time * 0.7) + 0.08 * cos(u_time * 1.3);
    float cy = 0.5 + 0.12 * cos(u_time * 0.9) + 0.06 * sin(u_time * 1.7);
    vec2 center = vec2(cx, cy);
    vec2 dir = v_texCoord - center;
    float str = u_intensity * 0.05;
    // 8 fixed samples along radial direction
    vec4 c  = texture2D(u_texture, center + dir * (1.0 - str * 0.000));
    c += texture2D(u_texture, center + dir * (1.0 - str * 0.143));
    c += texture2D(u_texture, center + dir * (1.0 - str * 0.286));
    c += texture2D(u_texture, center + dir * (1.0 - str * 0.429));
    c += texture2D(u_texture, center + dir * (1.0 - str * 0.571));
    c += texture2D(u_texture, center + dir * (1.0 - str * 0.714));
    c += texture2D(u_texture, center + dir * (1.0 - str * 0.857));
    c += texture2D(u_texture, center + dir * (1.0 - str * 1.000));
    gl_FragColor = (c * 0.125) * v_fragmentColor;
})";

// shader de glitch (animado fluido, optimizado: 4 noise calls)
constexpr auto fragmentShaderGlitch = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    vec2 uv = v_texCoord;
    float b3 = u_intensity * 3.0;
    float str = b3 * 0.1;
    
    // line + block displacement (2 noise calls)
    float n1 = noise(vec2(uv.y * 40.0, u_time * 6.0));
    float n2 = noise(vec2(uv.y * 5.0, u_time * 3.5));
    uv.x += (n1 - 0.5) * str * smoothstep(0.92 - b3 * 0.04, 0.95, n1);
    uv.x += (n2 - 0.5) * str * 2.5 * smoothstep(0.93 - b3 * 0.02, 0.97, n2);
    
    vec4 color = texture2D(u_texture, uv);
    
    // chromatic split (1 noise call, reuse for flicker)
    float n3 = noise(vec2(uv.y * 60.0, u_time * 8.0));
    float cg = smoothstep(0.88 - b3 * 0.03, 0.94, n3);
    float shift = 0.015 * b3 * cg;
    color.r = mix(color.r, texture2D(u_texture, uv + vec2(shift, 0.0)).r, cg);
    color.b = mix(color.b, texture2D(u_texture, uv - vec2(shift, 0.0)).b, cg);
    
    // scanline flicker (1 noise call)
    float n4 = noise(vec2(u_time * 12.0, uv.y * 200.0));
    color.rgb *= 1.0 - smoothstep(0.65, 0.95, n4) * 0.15 * b3;
    
    gl_FragColor = color * v_fragmentColor;
})";

// shader de pixelado (para GIFs)
constexpr auto fragmentShaderPixelate = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_screenSize;
uniform float u_intensity;

void main() {
    float pixelSize = 2.0 + u_intensity * 15.0;
    vec2 coord = floor(v_texCoord * u_screenSize / pixelSize) * pixelSize / u_screenSize;
    gl_FragColor = texture2D(u_texture, coord) * v_fragmentColor;
})";

// shader de blur simple (para GIFs)
// Dual Kawase Blur, el truco tipico de juegos grandes
// buen balance entre calidad y rendimiento
constexpr auto fragmentShaderBlurSinglePass = R"(
#ifdef GL_ES
precision highp float;
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

    // Dual Kawase sampling pattern - 12 samples ponderados
    vec3 color = texture2D(u_texture, v_texCoord).rgb * 4.0;

    // Diagonales cercanas
    color += texture2D(u_texture, v_texCoord - halfpixel).rgb;
    color += texture2D(u_texture, v_texCoord + halfpixel).rgb;
    color += texture2D(u_texture, v_texCoord + vec2(halfpixel.x, -halfpixel.y)).rgb;
    color += texture2D(u_texture, v_texCoord - vec2(halfpixel.x, -halfpixel.y)).rgb;

    // Cardinales con peso extra
    color += texture2D(u_texture, v_texCoord + vec2(-offset.x, 0.0)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2( offset.x, 0.0)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2(0.0, -offset.y)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2(0.0,  offset.y)).rgb * 2.0;

    // Alpha siempre 1.0 para fondos opacos
    gl_FragColor = vec4(color / 16.0, 1.0) * v_fragmentColor;
})";

// shader de posterize
constexpr auto fragmentShaderPosterize = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float levels = mix(32.0, 3.0, u_intensity);
    vec3 result;
    result.r = floor(color.r * levels) / levels;
    result.g = floor(color.g * levels) / levels;
    result.b = floor(color.b * levels) / levels;
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
})";

// shaders pensados para celdas (normalmente post-tint)

constexpr auto fragmentShaderGrayscaleCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    vec3 result = mix(color.rgb, vec3(gray), u_intensity);
    gl_FragColor = vec4(result, color.a);
})";

constexpr auto fragmentShaderInvertCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    vec3 inverted = vec3(1.0) - color.rgb;
    vec3 result = mix(color.rgb, inverted, u_intensity);
    gl_FragColor = vec4(result, color.a);
})";

constexpr auto fragmentShaderBlurCell = R"(
#ifdef GL_ES
precision highp float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

// Blur de alta calidad - 9x9 samples con pesos gaussianos
// Usa offsets fraccionarios para aprovechar linear filtering de la GPU
void main() {
    vec2 texelSize = 1.0 / u_texSize;
    float radius = u_intensity * 4.0 + 0.5;
    vec2 step = texelSize * radius;

    // Pesos gaussianos precalculados (sigma ~2.0)
    // Centro
    vec3 color = texture2D(u_texture, v_texCoord).rgb * 0.1621;

    // Cruz principal (peso alto)
    color += texture2D(u_texture, v_texCoord + vec2(step.x, 0.0)).rgb * 0.1408;
    color += texture2D(u_texture, v_texCoord - vec2(step.x, 0.0)).rgb * 0.1408;
    color += texture2D(u_texture, v_texCoord + vec2(0.0, step.y)).rgb * 0.1408;
    color += texture2D(u_texture, v_texCoord - vec2(0.0, step.y)).rgb * 0.1408;

    // Diagonales cercanas
    color += texture2D(u_texture, v_texCoord + vec2(step.x, step.y) * 0.7071).rgb * 0.0911;
    color += texture2D(u_texture, v_texCoord + vec2(-step.x, step.y) * 0.7071).rgb * 0.0911;
    color += texture2D(u_texture, v_texCoord + vec2(step.x, -step.y) * 0.7071).rgb * 0.0911;
    color += texture2D(u_texture, v_texCoord + vec2(-step.x, -step.y) * 0.7071).rgb * 0.0911;

    // Segunda capa - cruz lejana
    vec2 step2 = step * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2(step2.x, 0.0)).rgb * 0.0215;
    color += texture2D(u_texture, v_texCoord - vec2(step2.x, 0.0)).rgb * 0.0215;
    color += texture2D(u_texture, v_texCoord + vec2(0.0, step2.y)).rgb * 0.0215;
    color += texture2D(u_texture, v_texCoord - vec2(0.0, step2.y)).rgb * 0.0215;

    // Diagonales lejanas
    color += texture2D(u_texture, v_texCoord + step2 * 0.7071).rgb * 0.0108;
    color += texture2D(u_texture, v_texCoord + vec2(-step2.x, step2.y) * 0.7071).rgb * 0.0108;
    color += texture2D(u_texture, v_texCoord + vec2(step2.x, -step2.y) * 0.7071).rgb * 0.0108;
    color += texture2D(u_texture, v_texCoord - step2 * 0.7071).rgb * 0.0108;

    // Puntos intermedios para suavizar (aprovecha linear filtering)
    vec2 step15 = step * 1.5;
    color += texture2D(u_texture, v_texCoord + vec2(step15.x, step.y * 0.5)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(-step15.x, step.y * 0.5)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(step15.x, -step.y * 0.5)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(-step15.x, -step.y * 0.5)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(step.x * 0.5, step15.y)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(-step.x * 0.5, step15.y)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(step.x * 0.5, -step15.y)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(-step.x * 0.5, -step15.y)).rgb * 0.0156;

    gl_FragColor = vec4(color, 1.0) * v_fragmentColor;
})";

// High quality two-pass Gaussian blur - Horizontal pass
// Usa linear sampling para mejor rendimiento (13-tap con solo 7 samples)
constexpr auto fragmentShaderBlurCellH = R"(
#ifdef GL_ES
precision highp float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    vec2 texelSize = 1.0 / u_texSize;
    float blurRadius = u_intensity * 3.0 + 0.5;

    // Pesos Gaussianos optimizados para linear sampling (sigma ~3.0)
    const float weight0 = 0.227027027;
    const float weight1 = 0.316216216;
    const float weight2 = 0.070270270;

    // Offsets calculados para linear sampling optimo
    const float offset1 = 1.384615385;
    const float offset2 = 3.230769231;

    float dx = texelSize.x * blurRadius;

    vec3 color = texture2D(u_texture, v_texCoord).rgb * weight0;

    color += texture2D(u_texture, v_texCoord + vec2(dx * offset1, 0.0)).rgb * weight1;
    color += texture2D(u_texture, v_texCoord - vec2(dx * offset1, 0.0)).rgb * weight1;
    color += texture2D(u_texture, v_texCoord + vec2(dx * offset2, 0.0)).rgb * weight2;
    color += texture2D(u_texture, v_texCoord - vec2(dx * offset2, 0.0)).rgb * weight2;

    // Alpha siempre 1.0 para fondos opacos
    gl_FragColor = vec4(color, 1.0) * v_fragmentColor;
})";

// High quality two-pass Gaussian blur - Vertical pass
// Usa linear sampling para mejor rendimiento (13-tap con solo 7 samples)
constexpr auto fragmentShaderBlurCellV = R"(
#ifdef GL_ES
precision highp float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    vec2 texelSize = 1.0 / u_texSize;
    float blurRadius = u_intensity * 3.0 + 0.5;

    // Pesos Gaussianos optimizados para linear sampling (sigma ~3.0)
    const float weight0 = 0.227027027;
    const float weight1 = 0.316216216;
    const float weight2 = 0.070270270;

    // Offsets calculados para linear sampling optimo
    const float offset1 = 1.384615385;
    const float offset2 = 3.230769231;

    float dy = texelSize.y * blurRadius;

    vec3 color = texture2D(u_texture, v_texCoord).rgb * weight0;

    color += texture2D(u_texture, v_texCoord + vec2(0.0, dy * offset1)).rgb * weight1;
    color += texture2D(u_texture, v_texCoord - vec2(0.0, dy * offset1)).rgb * weight1;
    color += texture2D(u_texture, v_texCoord + vec2(0.0, dy * offset2)).rgb * weight2;
    color += texture2D(u_texture, v_texCoord - vec2(0.0, dy * offset2)).rgb * weight2;

    // Alpha siempre 1.0 para fondos opacos
    gl_FragColor = vec4(color, 1.0) * v_fragmentColor;
})";

constexpr auto fragmentShaderGlitchCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

float rand(vec2 co) {
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec2 uv = v_texCoord;
    float glitchStrength = u_intensity * 0.1;
    
    float lineNoise = rand(vec2(floor(uv.y * 100.0), u_time));
    if (lineNoise > 0.95 - u_intensity * 0.05) {
        uv.x += (rand(vec2(uv.y, u_time)) - 0.5) * glitchStrength;
    }
    
    vec4 color = texture2D(u_texture, uv);
    
    if (rand(vec2(uv.y, u_time + 1.0)) > 0.98 - u_intensity * 0.02) {
        color.r = texture2D(u_texture, uv + vec2(0.01 * u_intensity, 0.0)).r;
        color.b = texture2D(u_texture, uv - vec2(0.01 * u_intensity, 0.0)).b;
    }
    
    gl_FragColor = color * v_fragmentColor;
})";

constexpr auto fragmentShaderSepiaCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    vec3 sepia = vec3(gray * 1.2, gray * 1.0, gray * 0.8);
    vec3 result = mix(color.rgb, sepia, u_intensity);
    gl_FragColor = vec4(result, color.a);
})";

constexpr auto fragmentShaderSaturationCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity; // saturation: 1.0 = normal
uniform float u_brightness; // brightness: 1.0 = normal

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    
    // saturation
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    vec3 grayColor = vec3(gray);
    vec3 saturated = mix(grayColor, color.rgb, u_intensity);
    
    // brightness
    vec3 finalRGB = saturated * u_brightness;
    
    gl_FragColor = vec4(finalRGB, color.a);
})";

constexpr auto fragmentShaderSharpenCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    vec2 onePixel = vec2(1.0, 1.0) / u_texSize;
    vec4 color = texture2D(u_texture, v_texCoord);
    
    vec4 sum = vec4(0.0);
    sum += texture2D(u_texture, v_texCoord + vec2(0.0, -1.0) * onePixel) * -1.0;
    sum += texture2D(u_texture, v_texCoord + vec2(-1.0, 0.0) * onePixel) * -1.0;
    sum += texture2D(u_texture, v_texCoord + vec2(0.0, 0.0) * onePixel) * 5.0;
    sum += texture2D(u_texture, v_texCoord + vec2(1.0, 0.0) * onePixel) * -1.0;
    sum += texture2D(u_texture, v_texCoord + vec2(0.0, 1.0) * onePixel) * -1.0;
    
    vec3 result = mix(color.rgb, sum.rgb, u_intensity);
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
})";

constexpr auto fragmentShaderEdgeCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    vec2 onePixel = vec2(1.0, 1.0) / u_texSize;
    
    float kernel[9];
    kernel[0] = -1.0; kernel[1] = -1.0; kernel[2] = -1.0;
    kernel[3] = -1.0; kernel[4] = 8.0; kernel[5] = -1.0;
    kernel[6] = -1.0; kernel[7] = -1.0; kernel[8] = -1.0;
    
    vec4 sum = vec4(0.0);
    int index = 0;
    for (float y = -1.0; y <= 1.0; y++) {
        for (float x = -1.0; x <= 1.0; x++) {
            sum += texture2D(u_texture, v_texCoord + vec2(x, y) * onePixel) * kernel[index];
            index++;
        }
    }
    
    vec4 color = texture2D(u_texture, v_texCoord);
    vec3 result = mix(color.rgb, sum.rgb, u_intensity);
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
})";

constexpr auto fragmentShaderVignetteCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    vec2 uv = v_texCoord - 0.5;
    float dist = length(uv);
    float vignette = smoothstep(0.8, 0.25 * (1.0 - u_intensity * 0.5), dist);
    gl_FragColor = vec4(color.rgb * vignette, color.a);
})";

constexpr auto fragmentShaderPixelateCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    float pixelSize = 2.0 + u_intensity * 15.0;
    vec2 coord = floor(v_texCoord * u_texSize / pixelSize) * pixelSize / u_texSize;
    gl_FragColor = texture2D(u_texture, coord) * v_fragmentColor;
})";

constexpr auto fragmentShaderPosterizeCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    float levels = 10.0 - (u_intensity * 8.0);
    levels = max(2.0, levels);
    vec3 result = floor(color.rgb * levels) / levels;
    gl_FragColor = vec4(result, color.a);
})";

constexpr auto fragmentShaderChromaticCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord;
    float amount = u_intensity * 0.02;
    float r = texture2D(u_texture, uv + vec2(amount, 0.0)).r;
    float g = texture2D(u_texture, uv).g;
    float b = texture2D(u_texture, uv - vec2(amount, 0.0)).b;
    gl_FragColor = vec4(r, g, b, texture2D(u_texture, uv).a) * v_fragmentColor;
})";

constexpr auto fragmentShaderScanlinesCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    float scanline = sin(v_texCoord.y * u_texSize.y * 0.5) * 0.1 * u_intensity;
    color.rgb -= scanline;
    gl_FragColor = color;
})";

constexpr auto fragmentShaderSolarizeCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    float threshold = 0.5;
    vec3 solarized = abs(color.rgb - vec3(threshold)) * 2.0;
    vec3 result = mix(color.rgb, solarized, u_intensity);
    gl_FragColor = vec4(result, color.a);
})";

// simple hue shift for rainbow
constexpr auto fragmentShaderRainbowCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    vec3 hsv = rgb2hsv(color.rgb);
    // shift hue based on time and intensity
    hsv.x = fract(hsv.x + u_time * 0.5 * u_intensity); 
    vec3 rgb = hsv2rgb(hsv);
    // mix with original based on intensity (so it fades in)
    vec3 result = mix(color.rgb, rgb, u_intensity);
    gl_FragColor = vec4(result, color.a);
})";


constexpr auto fragmentShaderAtmosphere = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity; // 0.0 to 1.0

void main() {
    // Kawase-style blur optimizado para atmosfera
    vec2 texelSize = 1.0 / u_texSize;
    float blurAmount = u_intensity * 4.0 + 1.0;

    vec2 offset = blurAmount * texelSize;
    vec2 halfOffset = offset * 0.5;

    // Centro con peso alto
    vec3 color = texture2D(u_texture, v_texCoord).rgb * 4.0;

    // Diagonales cercanas
    color += texture2D(u_texture, v_texCoord + halfOffset).rgb;
    color += texture2D(u_texture, v_texCoord - halfOffset).rgb;
    color += texture2D(u_texture, v_texCoord + vec2(halfOffset.x, -halfOffset.y)).rgb;
    color += texture2D(u_texture, v_texCoord + vec2(-halfOffset.x, halfOffset.y)).rgb;

    // Cardinales
    color += texture2D(u_texture, v_texCoord + vec2(offset.x, 0.0)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord - vec2(offset.x, 0.0)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2(0.0, offset.y)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord - vec2(0.0, offset.y)).rgb * 2.0;

    // Alpha siempre 1.0 para fondos opacos
    gl_FragColor = vec4(color / 16.0, 1.0) * v_fragmentColor;
})";


    constexpr auto fragmentShaderFastBlur = R"(
    #ifdef GL_ES
    precision highp float;
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
    })";

    // Declared in Shaders.cpp
    void applyBlurPass(cocos2d::CCSprite* input, cocos2d::CCRenderTexture* output, cocos2d::CCGLProgram* program, cocos2d::CCSize const& size, float radius);

    // Declared in Shaders.cpp
    float intensityToBlurRadius(float intensity);

    /// Declared in Shaders.cpp
    cocos2d::CCSprite* createBlurredSprite(cocos2d::CCTexture2D* texture, cocos2d::CCSize const& targetSize, float intensity, bool useDirectRadius = false);

    /// Declared in Shaders.cpp
    cocos2d::CCGLProgram* getBlurCellShader();

    /// Declared in Shaders.cpp
    cocos2d::CCGLProgram* getBlurSinglePassShader();

// ═══════════════════════════════════════════════════════════
//  PAIMON BLUR — Dual Kawase multi-pass (downsample + upsample)
// ═══════════════════════════════════════════════════════════

// Dual Kawase downsample: center(×4) + 4 diagonal half-pixel(×1) = ÷8
constexpr auto fragmentShaderPaimonBlurDown = R"(
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
})";

// Dual Kawase upsample: 4 cardinal(×2) + 4 diagonal(×1) = ÷12
constexpr auto fragmentShaderPaimonBlurUp = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_halfpixel;

void main() {
    vec3 sum = vec3(0.0);
    // cardinal samples (weight 2)
    sum += texture2D(u_texture, v_texCoord + vec2(-u_halfpixel.x * 2.0, 0.0)).rgb * 2.0;
    sum += texture2D(u_texture, v_texCoord + vec2( u_halfpixel.x * 2.0, 0.0)).rgb * 2.0;
    sum += texture2D(u_texture, v_texCoord + vec2(0.0, -u_halfpixel.y * 2.0)).rgb * 2.0;
    sum += texture2D(u_texture, v_texCoord + vec2(0.0,  u_halfpixel.y * 2.0)).rgb * 2.0;
    // diagonal samples (weight 1)
    sum += texture2D(u_texture, v_texCoord + u_halfpixel).rgb;
    sum += texture2D(u_texture, v_texCoord - u_halfpixel).rgb;
    sum += texture2D(u_texture, v_texCoord + vec2(u_halfpixel.x, -u_halfpixel.y)).rgb;
    sum += texture2D(u_texture, v_texCoord - vec2(u_halfpixel.x, -u_halfpixel.y)).rgb;
    gl_FragColor = vec4(sum / 12.0, 1.0) * v_fragmentColor;
})";

// PaimonBlur real-time single-pass for GIFs — lightweight Dual Kawase 9-tap
constexpr auto fragmentShaderPaimonBlurRT = R"(
#ifdef GL_ES
precision highp float;
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
})";

    /// Declared in Shaders.cpp — multi-pass Dual Kawase blur for static thumbnails
    cocos2d::CCSprite* createPaimonBlurSprite(cocos2d::CCTexture2D* texture, cocos2d::CCSize const& targetSize, float intensity);

    /// Declared in Shaders.cpp — real-time PaimonBlur shader for GIFs
    cocos2d::CCGLProgram* getPaimonBlurShader();

// ═══════════════════════════════════════════════════════════
//  NUEVOS EFECTOS ANIMADOS UNICOS
// ═══════════════════════════════════════════════════════════

// Rain — gotas de lluvia (optimizado: loop desenrollado, 1 texture read)
constexpr auto fragmentShaderRain = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

float rHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float rainLayer(vec2 uv, float speed, float density, float layer) {
    vec2 r = uv * vec2(density, 1.0);
    r.y += u_time * speed + rHash(vec2(floor(r.x), layer)) * 100.0;
    float drop = smoothstep(0.0, 0.02, fract(r.y * 0.1) - 0.97);
    float mask = smoothstep(0.45, 0.5, abs(fract(r.x) - 0.5));
    return drop * (1.0 - mask);
}

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float str = u_intensity * 0.15;
    // 3 layers unrolled
    float rain = rainLayer(v_texCoord, 4.0, 80.0, 0.0)
               + rainLayer(v_texCoord, 6.0, 120.0, 1.0) * 0.75
               + rainLayer(v_texCoord, 8.0, 160.0, 2.0) * 0.5;
    color.rgb += vec3(0.7, 0.8, 1.0) * rain * str * str;
    color.rgb *= 1.0 - str * 0.2;
    gl_FragColor = color * v_fragmentColor;
})";

// Matrix — digital rain (optimizado: 1 texture read, 2 hash calls)
constexpr auto fragmentShaderMatrix = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

float mHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float str = u_intensity * 0.2;
    float cols = 30.0 + u_intensity * 10.0;
    vec2 cell = floor(v_texCoord * vec2(cols, cols * 2.0));
    
    float spd = 2.0 + mHash(vec2(cell.x, 0.0)) * 4.0;
    float fall = fract(cell.y / (cols * 2.0) - u_time * spd * 0.1);
    float trail = smoothstep(0.0, 0.4, fall) * smoothstep(1.0, 0.5, fall);
    float head = smoothstep(0.38, 0.42, fall);
    float flick = step(0.3, mHash(cell + floor(u_time * 8.0)));
    
    float glow = (trail * 0.6 + head) * flick;
    color.rgb = mix(color.rgb, color.rgb * 0.7 + vec3(0.1, 1.0, 0.3) * glow * str, str);
    gl_FragColor = color * v_fragmentColor;
})";

// Neon Pulse — pulsing neon glow (optimizado: 4 texture reads, precomputed lum)
constexpr auto fragmentShaderNeonPulse = R"(
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
    float str = u_intensity * 0.15;
    
    // edge detection: 4 neighbors, skip center lum (not needed for gradient)
    const vec3 lw = vec3(0.299, 0.587, 0.114);
    float px = 1.0 / 512.0;
    float lL = dot(texture2D(u_texture, v_texCoord + vec2(-px, 0.0)).rgb, lw);
    float lR = dot(texture2D(u_texture, v_texCoord + vec2( px, 0.0)).rgb, lw);
    float lU = dot(texture2D(u_texture, v_texCoord + vec2(0.0,  px)).rgb, lw);
    float lD = dot(texture2D(u_texture, v_texCoord + vec2(0.0, -px)).rgb, lw);
    float edge = smoothstep(0.02, 0.15, abs(lL - lR) + abs(lU - lD));
    
    // hue cycle + pulse in one pass
    float h = (u_time * 0.5 + v_texCoord.x * 0.3 + v_texCoord.y * 0.2) * 6.2832;
    vec3 neon = 0.5 + 0.5 * sin(vec3(h, h + 2.094, h + 4.189));
    float pulse = 0.7 + 0.3 * sin(u_time * 3.0);
    
    color.rgb += neon * edge * pulse * str * 3.0;
    gl_FragColor = color * v_fragmentColor;
})";

// Wave Distortion — underwater wavy effect
constexpr auto fragmentShaderWaveDistortion = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

void main() {
    float strength = u_intensity * 0.02;
    vec2 uv = v_texCoord;
    
    // multiple sine waves for organic distortion
    uv.x += sin(uv.y * 15.0 + u_time * 2.0) * strength;
    uv.y += cos(uv.x * 12.0 + u_time * 1.7) * strength * 0.8;
    uv.x += sin(uv.y * 25.0 + u_time * 3.3) * strength * 0.4;
    uv.y += cos(uv.x * 20.0 + u_time * 2.5) * strength * 0.3;
    
    // slight color shift for underwater feel
    vec4 color = texture2D(u_texture, uv);
    float caustic = sin(uv.x * 30.0 + u_time * 4.0) * sin(uv.y * 30.0 + u_time * 3.0);
    caustic = smoothstep(0.3, 1.0, caustic) * strength * 2.0;
    color.rgb += vec3(0.1, 0.3, 0.5) * caustic;
    
    // subtle blue tint
    color.rgb = mix(color.rgb, color.rgb * vec3(0.9, 0.95, 1.1), u_intensity * 0.05);
    
    gl_FragColor = color * v_fragmentColor;
})";

// CRT — old TV effect (optimizado: 3 texture reads total)
constexpr auto fragmentShaderCRT = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

void main() {
    float s = u_intensity * 0.1;
    
    // barrel distortion
    vec2 uv = v_texCoord * 2.0 - 1.0;
    uv *= 1.0 + dot(uv, uv) * s * 0.5;
    uv = uv * 0.5 + 0.5;
    
    // vignette + clamp
    float vig = smoothstep(0.0, 0.02, uv.x) * smoothstep(1.0, 0.98, uv.x)
              * smoothstep(0.0, 0.02, uv.y) * smoothstep(1.0, 0.98, uv.y);
    uv = clamp(uv, 0.0, 1.0);
    
    // 3 reads: center (g+a), left (r), right (b)
    float subpx = s * 0.003;
    vec4 center = texture2D(u_texture, uv);
    float r = texture2D(u_texture, uv + vec2(subpx, 0.0)).r;
    float b = texture2D(u_texture, uv - vec2(subpx, 0.0)).b;
    
    vec3 col = vec3(r, center.g, b);
    
    // scanlines + flicker + grain combined
    col *= mix(1.0, sin(uv.y * 800.0) * 0.5 + 0.5, s * 0.3);
    col *= 1.0 - 0.03 * s * sin(u_time * 8.0 + sin(u_time * 13.0) * 2.0);
    col += fract(sin(dot(v_texCoord * 500.0 + u_time, vec2(127.1, 311.7))) * 43758.5453) * s * 0.15;
    col *= vig;
    
    gl_FragColor = vec4(col, center.a) * v_fragmentColor;
})";

// ShaderBgSprite — CCSprite que re-aplica uniforms cada frame.
// Sin esto, los uniforms custom se pierden al cambiar de shader activo.

class ShaderBgSprite : public cocos2d::CCSprite {
public:
    float m_shaderIntensity = 0.5f;
    float m_screenW = 0.f;
    float m_screenH = 0.f;
    float m_shaderTime = 0.f;

    static ShaderBgSprite* createWithTexture(cocos2d::CCTexture2D* tex) {
        auto ret = new ShaderBgSprite();
        if (ret && ret->initWithTexture(tex)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void draw() override {
        auto* shader = getShaderProgram();
        if (shader) {
            shader->use();
            shader->setUniformsForBuiltins();

            GLint loc;
            loc = shader->getUniformLocationForName("u_intensity");
            if (loc != -1) shader->setUniformLocationWith1f(loc, m_shaderIntensity);

            loc = shader->getUniformLocationForName("u_screenSize");
            if (loc != -1) shader->setUniformLocationWith2f(loc, m_screenW, m_screenH);

            loc = shader->getUniformLocationForName("u_time");
            if (loc != -1) shader->setUniformLocationWith1f(loc, m_shaderTime);

            loc = shader->getUniformLocationForName("u_texSize");
            if (loc != -1) {
                auto* t = getTexture();
                float tw = t ? static_cast<float>(t->getPixelsWide()) : 1.f;
                float th = t ? static_cast<float>(t->getPixelsHigh()) : 1.f;
                shader->setUniformLocationWith2f(loc, tw, th);
            }
        }
        CCSprite::draw();
    }

    void updateShaderTime(float dt) {
        m_shaderTime += dt;
    }
};

// Declared in Shaders.cpp
cocos2d::CCGLProgram* getBgShaderProgram(std::string const& shaderName);

/// Pre-compila todos los shaders de LevelInfoLayer durante el idle del menu
/// para evitar stutter la primera vez que se entra a un nivel.
/// DEBE llamarse desde el hilo principal (contexto GL requerido).
void prewarmLevelInfoShaders();

/// Motor de blur progresivo que reparte las pasadas FBO entre frames
/// para evitar freezes. Hereda de CCObject para lifecycle con Ref<>.
class ProgressiveBlurJob : public cocos2d::CCObject {
public:
    using CompletionCallback = std::function<void(cocos2d::CCSprite* result)>;

    /// Crea un blur Gaussiano progresivo (2-4 pasadas repartidas entre frames)
    static ProgressiveBlurJob* createGaussian(
        cocos2d::CCTexture2D* texture,
        cocos2d::CCSize const& targetSize,
        float intensity,
        CompletionCallback onComplete);

    /// Crea un blur Dual Kawase progresivo (2-16 pasadas repartidas entre frames)
    static ProgressiveBlurJob* createPaimonBlur(
        cocos2d::CCTexture2D* texture,
        cocos2d::CCSize const& targetSize,
        float intensity,
        CompletionCallback onComplete);

    void start();    ///< Registra tick con CCScheduler
    void cancel();   ///< Detiene procesamiento y libera recursos
    bool isDone() const { return m_done; }
    bool isCancelled() const { return m_cancelled; }

    ~ProgressiveBlurJob() override;

private:
    enum class Phase : uint8_t {
        Setup,
        Downsample,
        Upsample,
        GaussianH1,
        GaussianV1,
        GaussianH2,
        GaussianV2,
        Complete
    };

    enum class BlurType : uint8_t { Gaussian, PaimonBlur };

    struct MipLevel {
        geode::Ref<cocos2d::CCRenderTexture> rt;
        cocos2d::CCSize size;
    };

    bool initGaussian(cocos2d::CCTexture2D* texture, cocos2d::CCSize const& targetSize,
                      float intensity, CompletionCallback onComplete);
    bool initPaimonBlur(cocos2d::CCTexture2D* texture, cocos2d::CCSize const& targetSize,
                        float intensity, CompletionCallback onComplete);

    void tick(float dt);
    void tickPaimonBlur();
    void tickGaussian();
    void finish(cocos2d::CCSprite* result);
    cocos2d::CCSprite* capSourceTexture(cocos2d::CCTexture2D* texture,
                                        cocos2d::CCSize const& targetSize);

    BlurType m_blurType = BlurType::Gaussian;
    Phase m_phase = Phase::Setup;
    bool m_done = false;
    bool m_cancelled = false;
    bool m_started = false;

    CompletionCallback m_onComplete;
    cocos2d::CCSize m_targetSize;
    float m_intensity = 5.0f;

    // Gaussian state
    geode::Ref<cocos2d::CCRenderTexture> m_rtA;
    geode::Ref<cocos2d::CCRenderTexture> m_rtB;
    cocos2d::CCGLProgram* m_blurH = nullptr;
    cocos2d::CCGLProgram* m_blurV = nullptr;
    float m_radius = 0.1f;

    // PaimonBlur state
    cocos2d::CCGLProgram* m_blurDown = nullptr;
    cocos2d::CCGLProgram* m_blurUp = nullptr;
    int m_totalPasses = 4;
    int m_currentPass = 0;
    std::vector<MipLevel> m_mips;
    geode::Ref<cocos2d::CCSprite> m_currentSprite;
    cocos2d::CCSize m_currentSize;

    // Source texture retenida
    geode::Ref<cocos2d::CCTexture2D> m_sourceTexture;
};

}
