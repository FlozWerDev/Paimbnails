#ifdef GL_ES
precision mediump float;
#endif

varying vec2 v_texCoord;
varying vec4 v_fragmentColor;

uniform sampler2D u_texture;

uniform int stopAt;
uniform float stops[24];
uniform vec4 colors[24];

uniform vec2 startPoint;
uniform vec2 endPoint;

uniform vec2 uvMin;
uniform vec2 uvMax;

uniform int u_animType;
uniform float u_animSpeed;
uniform float u_animIntensity;
uniform float u_animDirection;

// Custom animation (u_animType == 6): a stack of up to 4 movements applied in
// order. u_customLayers[i] = (motion, wave, amount, speed) and u_customPhase[i]
// shifts the layer inside its own cycle. The numbers are the enum values in
// GradientAnimationManager.hpp, so don't renumber the branches below.
uniform int u_customCount;
uniform vec4 u_customLayers[4];
uniform float u_customPhase[4];

// Shape of a layer's value over time, in [-1, 1] (Bounce stays in [0, 1]).
float animWave(int shape, float t)
{
    if (shape == 1) return 1.0 - 4.0 * abs(fract(t + 0.25) - 0.5);
    if (shape == 2) return fract(t) * 2.0 - 1.0;
    if (shape == 3) return fract(t) < 0.5 ? 1.0 : -1.0;
    if (shape == 4) return abs(sin(t * 3.14159265));
    if (shape == 5) {
        float cell = floor(t);
        float a = fract(sin(cell * 12.9898) * 43758.5453);
        float b = fract(sin((cell + 1.0) * 12.9898) * 43758.5453);
        float f = fract(t);
        return mix(a, b, f * f * (3.0 - 2.0 * f)) * 2.0 - 1.0;
    }
    return sin(t * 6.28318531);
}

// How a layer pushes the gradient. `value` is the animWave output.
vec2 animMotion(vec2 uv, int motion, float value, float amount)
{
    vec2 center = vec2(0.5);

    if (motion == 1) {
        uv.y += value * 0.5 * amount;
    }
    else if (motion == 2) {
        uv = (uv - center) / max(1.0 + value * 0.6 * amount, 0.15) + center;
    }
    else if (motion == 3) {
        float angle = value * 3.14159265 * amount;
        float cosine = cos(angle);
        float sine = sin(angle);
        uv = mat2(cosine, -sine, sine, cosine) * (uv - center) + center;
    }
    else if (motion == 4) {
        float angle = value * 3.14159265;
        uv += vec2(cos(angle), sin(angle)) * 0.35 * amount;
    }
    else if (motion == 5) {
        uv.x += sin((uv.y * 3.0 + value) * 3.14159265) * 0.25 * amount;
    }
    else if (motion == 6) {
        uv.y += sin((uv.x * 3.0 + value) * 3.14159265) * 0.25 * amount;
    }
    else if (motion == 7) {
        float angle = value * 3.14159265 * amount * length(uv - center) * 2.0;
        float cosine = cos(angle);
        float sine = sin(angle);
        uv = mat2(cosine, -sine, sine, cosine) * (uv - center) + center;
    }
    else {
        uv.x += value * 0.5 * amount;
    }

    return uv;
}

vec2 animateGradient(vec2 uv)
{
    if (u_animType == 0 || u_animIntensity <= 0.001) return uv;

    float time = CC_Time.y * u_animSpeed * u_animDirection;

    if (u_animType == 6) {
        for (int i = 0; i < 4; ++i) {
            if (i >= u_customCount) break;

            vec4 layer = u_customLayers[i];
            float value = animWave(int(layer.y), time * layer.w + u_customPhase[i]);
            uv = animMotion(uv, int(layer.x), value, layer.z * u_animIntensity);
        }

        return uv;
    }

    if (u_animType == 1) {
        uv.x += sin(time * 1.5) * 0.3 * u_animIntensity;
    }
    else if (u_animType == 2) {
        float scale = 1.0 + sin(time * 2.0) * 0.25 * u_animIntensity;
        uv = (uv - 0.5) * scale + 0.5;
    }
    else if (u_animType == 3) {
        float angle = time * 0.8 * u_animIntensity;
        float cosine = cos(angle);
        float sine = sin(angle);
        uv = mat2(cosine, -sine, sine, cosine) * (uv - 0.5) + 0.5;
    }
    else if (u_animType == 4) {
        uv += vec2(cos(time), sin(time)) * 0.22 * u_animIntensity;
    }
    else if (u_animType == 5) {
        float angle = sin(time * 1.5) * 0.8 * u_animIntensity;
        float cosine = cos(angle);
        float sine = sin(angle);
        uv = mat2(cosine, -sine, sine, cosine) * (uv - 0.5) + 0.5;
    }

    return uv;
}

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);

    if (stopAt <= 1) {
        gl_FragColor = texColor * colors[0] * v_fragmentColor;
        return;
    }

    vec2 dir = endPoint - startPoint;

    float len = length(dir);
    if (len < 1e-6) {
        gl_FragColor = texColor * colors[0] * v_fragmentColor;
        return;
    }

    vec2 unit = dir / len;
    vec2 uv = animateGradient((v_texCoord - uvMin) / (uvMax - uvMin));
    float proj = dot(uv - startPoint, unit);
    float t = clamp(proj / len, 0.0, 1.0);

    if (t <= stops[0]) {
        gl_FragColor = texColor * colors[0] * v_fragmentColor;
        return;
    }

    for (int i = 0; i < 23; ++i) {
        if (i >= stopAt - 1) break;
        float a = stops[i];
        float b = stops[i + 1];
        if (t <= b) {
            float localT = (t - a) / (b - a);
            gl_FragColor = texColor * mix(colors[i], colors[i + 1], localT) * v_fragmentColor;
            return;
        }
    }

    gl_FragColor = texColor * colors[stopAt - 1] * v_fragmentColor;
}
