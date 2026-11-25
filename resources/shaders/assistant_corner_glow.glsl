// Paimon Assistant Claude/GPT Computer-Use style corner & perimeter glow shader.
// Renders soft, calm luminous rounded-corner and edge bloom around the viewport.
#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying lowp vec4 v_fragmentColor;
varying mediump vec2 v_texCoord;
#else
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
#endif

uniform vec2 u_resolution;
uniform float u_time;
uniform vec3 u_effortWeights; // x = Normal, y = High, z = Ultra
uniform float u_working;
uniform float u_fade;

void main() {
    float aspect = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 halfExt = vec2(aspect * 0.5, 0.5);
    vec2 p = (v_texCoord - 0.5) * vec2(aspect, 1.0);

    float cornerR = 0.095;
    vec2 q = abs(p) - (halfExt - vec2(cornerR));
    float sd = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - cornerR;

    float dIn  = max(-sd, 0.0);
    float dOut = max(sd, 0.0);

    vec2 cornerDelta = halfExt - abs(p);
    float distCorner = length(cornerDelta);

    float pulseSpeed = mix(1.0, 2.4, u_working);
    float breath = 0.90 + 0.10 * sin(u_time * pulseSpeed);

    float rim      = exp(-dIn * mix(135.0, 105.0, u_working)) * exp(-dOut * 26.0);
    float edgeHalo = exp(-dIn * mix(28.0, 21.0, u_working)) * exp(-dOut * 8.0);
    float wideEdge = exp(-dIn * mix(11.0, 8.5, u_working));

    float cornerCore  = exp(-distCorner * mix(15.0, 11.5, u_working));
    float cornerBloom = exp(-distCorner * mix(6.2, 4.8, u_working));

    float ang = atan(p.y, p.x);
    float sweepSpeed = mix(0.45, 1.35, u_working);
    float waveA = 0.5 + 0.5 * sin(ang * 2.0 - u_time * sweepSpeed);
    float waveB = 0.5 + 0.5 * cos(ang * 3.0 + u_time * (sweepSpeed * 0.8));

    vec3 cNormA = vec3(0.62, 0.88, 1.00);
    vec3 cNormB = vec3(0.92, 0.96, 1.00);

    vec3 cHighA = vec3(0.45, 0.84, 0.98);
    vec3 cHighB = vec3(0.72, 0.54, 0.98);

    vec3 cUltraA = vec3(1.00, 0.80, 0.38);
    vec3 cUltraB = vec3(0.98, 0.52, 0.58);

    vec3 colA = cNormA * u_effortWeights.x + cHighA * u_effortWeights.y + cUltraA * u_effortWeights.z;
    vec3 colB = cNormB * u_effortWeights.x + cHighB * u_effortWeights.y + cUltraB * u_effortWeights.z;

    vec3 glowCol = mix(colA, colB, waveA * 0.65 + waveB * 0.35);
    vec3 rimCol = mix(glowCol, vec3(1.0), 0.58);

    float intensity =
        rim * (0.42 + 0.20 * u_working)
        + edgeHalo * (0.16 + 0.10 * u_working)
        + wideEdge * (0.04 + 0.04 * u_working)
        + cornerCore * (0.32 + 0.16 * u_working)
        + cornerBloom * (0.18 + 0.10 * u_working);

    intensity *= breath * (0.88 + 0.14 * waveA);

    vec3 finalCol = mix(glowCol, rimCol, clamp(rim + cornerCore * 0.30, 0.0, 1.0));
    float alpha = clamp(intensity, 0.0, 0.85) * u_fade * v_fragmentColor.a;

    gl_FragColor = vec4(finalCol, alpha);
}
