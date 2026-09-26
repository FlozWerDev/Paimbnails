// two-way blur ported from EclipseMenu (EPL-2.0); fast mode uses linear weights.

#version 120

#ifdef GL_ES
precision mediump float;
#endif

varying vec2 v_texCoord;

uniform sampler2D CC_Texture0;
uniform vec2 u_textureSize;
uniform float u_blurRadius;

// u_blurDirection is (1,0) for H, (0,1) for V.
uniform vec2 u_blurDirection;

// u_blurFast: 1 linear weights, 0 classic gaussian.
uniform float u_blurFast;

void main() {
    float scaledRadius = u_blurRadius * u_textureSize.y * 0.5;
    vec2 texOffset = 1.0 / u_textureSize;
    vec2 direction = u_blurDirection * texOffset;

    vec3 result = texture2D(CC_Texture0, v_texCoord).rgb;
    float weightSum = 1.0;
    float weight = 1.0;

    if (u_blurFast > 0.5) {
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
