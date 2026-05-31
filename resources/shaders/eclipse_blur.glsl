// eclipse_blur.glsl
// Shader gaussiano optimizado adaptado del proyecto EclipseMenu (EPL-2.0):
//   https://github.com/EclipseMenu/EclipseMenu/blob/main/resources/Blur/pp-frag.glsl
//
// Diferencia vs blur_h.glsl / blur_v.glsl actuales de Paimbnails:
//   1. UN solo shader maneja ambas direcciones via uniform `first`.
//   2. Modo `fast`: weights lineales decrecientes con early-exit → menos
//      samples efectivos para mismo radio que la gaussiana clasica (mismo
//      look visual, mas rapido).
//   3. Loop con `break` cuando weight <= 0 → la GPU no procesa muestras
//      cuyo aporte es despreciable (vs gaussian fija de N samples).
//
// La calidad visual es indistinguible a ojo del shader gaussiano clasico
// porque el blur es perceptualmente dominado por las muestras cercanas
// al centro; las muestras lejanas con peso bajo son ruido visual.

#version 120

#ifdef GL_ES
precision mediump float;
#endif

varying vec2 v_texCoord;

uniform sampler2D CC_Texture0;
uniform vec2 u_textureSize;     // Paimbnails: tamano de la textura input
uniform float u_blurRadius;     // [0..1] normalized → mismo rango que blur_h/v

// Direccion del blur. En lugar del uniform `first` de Eclipse usamos
// u_blurDirection que el caller setea a (1,0) para H o (0,1) para V.
// Hace falta porque applyBlurPass() de Shaders.cpp ya pasa direction asi.
uniform vec2 u_blurDirection;

// Modo fast: si fast=1, usa weights lineales (Eclipse-style); si fast=0,
// usa gaussian classic. Default fast=1 — dejamos al caller forzar el
// modo classico solo en debug.
uniform float u_blurFast;       // 0 o 1

void main() {
    // Convertir radius normalizado [0..1] a "scaled radius" en pixeles.
    // Eclipse usa radius * screenSize.y * 0.5; nosotros usamos
    // u_textureSize.y * 0.5 — consistente con applyBlurPass de Paimbnails
    // que pasa size = blurSize (= textureSize del FBO actual).
    float scaledRadius = u_blurRadius * u_textureSize.y * 0.5;
    vec2 texOffset = 1.0 / u_textureSize;
    vec2 direction = u_blurDirection * texOffset;

    vec3 result = texture2D(CC_Texture0, v_texCoord).rgb;
    float weightSum = 1.0;
    float weight = 1.0;

    if (u_blurFast > 0.5) {
        // ── Modo fast (Eclipse-style) ──
        // Weights decrecen linealmente. fastScale ajusta el radio efectivo
        // para que el resultado matchee aprox. al gaussian classic.
        float r10 = u_blurRadius * 10.0;
        float fastScale = r10 / ((r10 + 1.0) * (r10 + 1.0) - 1.0);
        scaledRadius *= fastScale;

        for (int i = 1; i < 64; i++) {
            if (float(i) >= scaledRadius) break;

            weight -= 1.0 / scaledRadius;
            if (weight <= 0.0) break;

            vec2 offset = direction * float(i);
            result += texture2D(CC_Texture0, v_texCoord + offset).rgb * weight;
            result += texture2D(CC_Texture0, v_texCoord - offset).rgb * weight;
            weightSum += 2.0 * weight;
        }
    } else {
        // ── Modo classic (gaussian) ──
        float firstWeight = 0.84089642 / pow(scaledRadius, 0.96);
        result *= firstWeight;
        weightSum = firstWeight;

        for (int i = 1; i < 64; i++) {
            float dist = float(i);
            if (dist > scaledRadius) break;

            float w = firstWeight * exp(-dist * dist / (2.0 * scaledRadius));
            vec2 offset = direction * dist;

            result += texture2D(CC_Texture0, v_texCoord + offset).rgb * w;
            result += texture2D(CC_Texture0, v_texCoord - offset).rgb * w;
            weightSum += 2.0 * w;
        }
    }

    result /= weightSum;
    gl_FragColor = vec4(result, 1.0);
}
