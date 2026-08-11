#include "CursorTransitionFX.hpp"

#include <algorithm>
#include <array>
#include <cmath>

using namespace cocos2d;

namespace paimon::cursorfx {
namespace {

constexpr float kPi = 3.14159265358979323846f;

constexpr std::array<char const*, kTransitionEffectCount> kEffectNames = {
    "Instantanea", "Disolver", "Aparecer", "Fundido cruzado", "Zoom", "Zoom inverso",
    "Pop", "Pulso", "Giro horario", "Giro antihorario", "Volteo horizontal",
    "Volteo vertical", "Deslizar izquierda", "Deslizar derecha", "Deslizar arriba",
    "Deslizar abajo", "Diagonal", "Balanceo", "Pendulo", "Comprimir", "Estirar",
    "Orbita horaria", "Orbita antihoraria", "Golpe", "Glitch", "Explosion"
};

constexpr std::array<char const*, kTransitionEffectCount> kEffectDescriptions = {
    "Cambia de estado sin animacion.",
    "Oculta el anterior y revela el nuevo en dos tiempos.",
    "El nuevo estado aparece suavemente.",
    "Mezcla el estado anterior con el nuevo.",
    "El nuevo estado crece desde el centro.",
    "El nuevo estado llega desde un tamano mayor.",
    "Entrada corta con rebote de escala.",
    "Una pulsacion rapida entre ambos estados.",
    "Gira hacia la derecha durante el cambio.",
    "Gira hacia la izquierda durante el cambio.",
    "Cierra y abre el cursor por el eje horizontal.",
    "Cierra y abre el cursor por el eje vertical.",
    "El nuevo estado entra desde la izquierda.",
    "El nuevo estado entra desde la derecha.",
    "El nuevo estado entra desde arriba.",
    "El nuevo estado entra desde abajo.",
    "Desplazamiento corto en diagonal.",
    "Inclina el cursor como un pequeno balanceo.",
    "Oscila al cambiar de estado.",
    "Aplasta el cursor antes de recuperar su forma.",
    "Estira el cursor en la direccion del cambio.",
    "El nuevo estado describe una orbita corta.",
    "La misma orbita en sentido contrario.",
    "Cambio seco con impacto y retroceso.",
    "Pequenos saltos digitales antes de estabilizarse.",
    "El estado nuevo aparece con una expansion rapida."
};

constexpr std::array<char const*, kTransitionEasingCount> kEasingNames = {
    "Lineal", "Suave", "Acelerar", "Frenar", "Suave doble", "Rebote atras", "Elastica", "Rebote"
};

constexpr std::array<char const*, kTransitionEasingCount> kEasingDescriptions = {
    "Velocidad constante.",
    "Entrada y salida naturales.",
    "Empieza despacio y acelera.",
    "Empieza rapido y frena al final.",
    "Acelera y vuelve a frenar.",
    "Se pasa un poco y vuelve.",
    "Oscila antes de asentarse.",
    "Termina con pequenos rebotes."
};

constexpr TransitionPreset kPresets[] = {
    {"Suave",              {TransitionEffect::CrossFade,          TransitionEasing::Smooth,    0.16f, 0.80f}},
    {"Instantanea",        {TransitionEffect::Instant,            TransitionEasing::Linear,    0.04f, 1.00f}},
    {"Fundido corto",      {TransitionEffect::CrossFade,          TransitionEasing::EaseOut,   0.10f, 0.75f}},
    {"Fundido lento",      {TransitionEffect::Dissolve,           TransitionEasing::Smooth,    0.28f, 0.80f}},
    {"Aparicion",          {TransitionEffect::Fade,               TransitionEasing::EaseOut,   0.18f, 0.80f}},
    {"Zoom limpio",        {TransitionEffect::ZoomIn,             TransitionEasing::EaseOut,   0.18f, 0.90f}},
    {"Zoom inverso",       {TransitionEffect::ZoomOut,            TransitionEasing::EaseOut,   0.20f, 0.85f}},
    {"Pop rapido",         {TransitionEffect::Pop,                TransitionEasing::Back,      0.14f, 0.85f}},
    {"Pop elastico",       {TransitionEffect::Pop,                TransitionEasing::Elastic,   0.28f, 1.00f}},
    {"Pulso suave",        {TransitionEffect::Pulse,              TransitionEasing::Smooth,    0.18f, 0.65f}},
    {"Rebote",             {TransitionEffect::ZoomIn,             TransitionEasing::Bounce,    0.28f, 0.90f}},
    {"Giro corto",         {TransitionEffect::SpinClockwise,      TransitionEasing::EaseOut,   0.18f, 0.65f}},
    {"Espiral",            {TransitionEffect::SpinClockwise,      TransitionEasing::Back,      0.28f, 1.20f}},
    {"Espiral inversa",    {TransitionEffect::SpinCounterClockwise, TransitionEasing::Back,    0.28f, 1.20f}},
    {"Volteo X",           {TransitionEffect::FlipHorizontal,     TransitionEasing::Smooth,    0.20f, 1.00f}},
    {"Volteo Y",           {TransitionEffect::FlipVertical,       TransitionEasing::Smooth,    0.20f, 1.00f}},
    {"Desde la izquierda", {TransitionEffect::SlideLeft,          TransitionEasing::EaseOut,   0.18f, 0.85f}},
    {"Desde la derecha",   {TransitionEffect::SlideRight,         TransitionEasing::EaseOut,   0.18f, 0.85f}},
    {"Desde arriba",       {TransitionEffect::SlideUp,            TransitionEasing::EaseOut,   0.18f, 0.85f}},
    {"Desde abajo",        {TransitionEffect::SlideDown,          TransitionEasing::EaseOut,   0.18f, 0.85f}},
    {"Diagonal",           {TransitionEffect::Diagonal,           TransitionEasing::Smooth,    0.20f, 0.90f}},
    {"Balanceo",           {TransitionEffect::Swing,              TransitionEasing::Back,      0.24f, 0.85f}},
    {"Pendulo",            {TransitionEffect::Pendulum,           TransitionEasing::Smooth,    0.30f, 1.00f}},
    {"Comprimir",          {TransitionEffect::Squash,             TransitionEasing::Back,      0.18f, 0.90f}},
    {"Estirar",            {TransitionEffect::Stretch,            TransitionEasing::Back,      0.18f, 0.90f}},
    {"Orbita",             {TransitionEffect::OrbitClockwise,     TransitionEasing::EaseOut,   0.28f, 1.00f}},
    {"Orbita inversa",     {TransitionEffect::OrbitCounterClockwise, TransitionEasing::EaseOut, 0.28f, 1.00f}},
    {"Impacto",            {TransitionEffect::Snap,               TransitionEasing::Back,      0.12f, 1.10f}},
    {"Glitch corto",       {TransitionEffect::Glitch,             TransitionEasing::Linear,    0.16f, 0.85f}},
    {"Explosion",          {TransitionEffect::Burst,              TransitionEasing::EaseOut,   0.22f, 1.00f}},
};

float bounceOut(float t) {
    constexpr float n = 7.5625f;
    constexpr float d = 2.75f;
    if (t < 1.f / d) return n * t * t;
    if (t < 2.f / d) {
        t -= 1.5f / d;
        return n * t * t + 0.75f;
    }
    if (t < 2.5f / d) {
        t -= 2.25f / d;
        return n * t * t + 0.9375f;
    }
    t -= 2.625f / d;
    return n * t * t + 0.984375f;
}

float ease(TransitionEasing easing, float t) {
    t = std::clamp(t, 0.f, 1.f);
    switch (easing) {
        case TransitionEasing::Linear:    return t;
        case TransitionEasing::Smooth:    return t * t * (3.f - 2.f * t);
        case TransitionEasing::EaseIn:    return t * t * t;
        case TransitionEasing::EaseOut:   return 1.f - std::pow(1.f - t, 3.f);
        case TransitionEasing::EaseInOut:
            return t < 0.5f ? 4.f * t * t * t
                            : 1.f - std::pow(-2.f * t + 2.f, 3.f) / 2.f;
        case TransitionEasing::Back: {
            constexpr float c1 = 1.70158f;
            constexpr float c3 = c1 + 1.f;
            float u = t - 1.f;
            return 1.f + c3 * u * u * u + c1 * u * u;
        }
        case TransitionEasing::Elastic:
            if (t <= 0.f || t >= 1.f) return t;
            return std::pow(2.f, -10.f * t) *
                std::sin((t * 10.f - 0.75f) * (2.f * kPi / 3.f)) + 1.f;
        case TransitionEasing::Bounce: return bounceOut(t);
        case TransitionEasing::Count:  break;
    }
    return t;
}

float fadeIn(float t) {
    return std::clamp(t * 1.35f, 0.f, 1.f);
}

float fadeOut(float t) {
    return std::clamp(1.f - t * 1.35f, 0.f, 1.f);
}

bool nearlyEqual(float a, float b) {
    return std::abs(a - b) < 0.001f;
}

} // namespace

char const* transitionEffectName(TransitionEffect effect) {
    int index = static_cast<int>(effect);
    return index >= 0 && index < kTransitionEffectCount ? kEffectNames[index] : "Instantanea";
}

char const* transitionEffectDesc(TransitionEffect effect) {
    int index = static_cast<int>(effect);
    return index >= 0 && index < kTransitionEffectCount
        ? kEffectDescriptions[index] : kEffectDescriptions[0];
}

char const* transitionEasingName(TransitionEasing easing) {
    int index = static_cast<int>(easing);
    return index >= 0 && index < kTransitionEasingCount ? kEasingNames[index] : "Suave";
}

char const* transitionEasingDesc(TransitionEasing easing) {
    int index = static_cast<int>(easing);
    return index >= 0 && index < kTransitionEasingCount
        ? kEasingDescriptions[index] : kEasingDescriptions[1];
}

int transitionPresetCount() {
    return static_cast<int>(std::size(kPresets));
}

TransitionPreset const& transitionPresetAt(int index) {
    return kPresets[std::clamp(index, 0, transitionPresetCount() - 1)];
}

int findTransitionPreset(TransitionSettings const& settings) {
    for (int i = 0; i < transitionPresetCount(); ++i) {
        auto const& preset = kPresets[i].settings;
        if (preset.effect == settings.effect && preset.easing == settings.easing &&
            nearlyEqual(preset.duration, settings.duration) &&
            nearlyEqual(preset.intensity, settings.intensity)) {
            return i;
        }
    }
    return -1;
}

TransitionFrame sampleTransition(
    TransitionSettings const& settings, float progress, bool incoming) {
    TransitionFrame frame;
    float raw = std::clamp(progress, 0.f, 1.f);
    float t = ease(settings.easing, raw);
    float timed = std::clamp(t, 0.f, 1.f);
    float phase = incoming ? 1.f - t : t;
    float intensity = std::clamp(
        settings.intensity, kTransitionIntensityMin, kTransitionIntensityMax);
    float direction = incoming ? -1.f : 1.f;
    float opacity = incoming ? fadeIn(raw) : fadeOut(raw);

    switch (settings.effect) {
        case TransitionEffect::Instant:
            frame.opacity = incoming ? 1.f : 0.f;
            break;

        case TransitionEffect::Dissolve:
            frame.opacity = incoming
                ? std::clamp((timed - 0.42f) / 0.58f, 0.f, 1.f)
                : std::clamp(1.f - timed / 0.58f, 0.f, 1.f);
            break;

        case TransitionEffect::Fade:
            frame.opacity = incoming ? std::clamp(t, 0.f, 1.f) : 0.f;
            break;

        case TransitionEffect::CrossFade:
            frame.opacity = incoming ? std::clamp(t, 0.f, 1.f)
                                     : std::clamp(1.f - t, 0.f, 1.f);
            break;

        case TransitionEffect::ZoomIn:
            frame.scaleX = frame.scaleY = 1.f - phase * 0.62f * intensity;
            frame.opacity = opacity;
            break;

        case TransitionEffect::ZoomOut:
            frame.scaleX = frame.scaleY = 1.f + phase * 0.72f * intensity;
            frame.opacity = opacity;
            break;

        case TransitionEffect::Pop:
            frame.scaleX = frame.scaleY = 1.f - phase * 0.45f * intensity;
            frame.rotation = direction * phase * 8.f * intensity;
            frame.opacity = opacity;
            break;

        case TransitionEffect::Pulse: {
            float pulse = std::sin(timed * kPi) * 0.24f * intensity;
            frame.scaleX = frame.scaleY = incoming ? 1.f + pulse : 1.f - pulse * 0.55f;
            frame.opacity = opacity;
            break;
        }

        case TransitionEffect::SpinClockwise:
        case TransitionEffect::SpinCounterClockwise: {
            float spin = settings.effect == TransitionEffect::SpinClockwise ? 1.f : -1.f;
            frame.rotation = spin * direction * phase * 180.f * intensity;
            frame.scaleX = frame.scaleY = 1.f - phase * 0.35f * intensity;
            frame.opacity = opacity;
            break;
        }

        case TransitionEffect::FlipHorizontal:
            frame.scaleX = std::max(0.04f, 1.f - phase * 0.96f * intensity);
            frame.opacity = opacity;
            break;

        case TransitionEffect::FlipVertical:
            frame.scaleY = std::max(0.04f, 1.f - phase * 0.96f * intensity);
            frame.opacity = opacity;
            break;

        case TransitionEffect::SlideLeft:
        case TransitionEffect::SlideRight:
        case TransitionEffect::SlideUp:
        case TransitionEffect::SlideDown: {
            float distance = 30.f * intensity;
            CCPoint axis;
            if (settings.effect == TransitionEffect::SlideLeft)  axis = ccp(-1.f, 0.f);
            if (settings.effect == TransitionEffect::SlideRight) axis = ccp(1.f, 0.f);
            if (settings.effect == TransitionEffect::SlideUp)    axis = ccp(0.f, 1.f);
            if (settings.effect == TransitionEffect::SlideDown)  axis = ccp(0.f, -1.f);
            float amount = incoming ? phase : phase * -0.35f;
            frame.offset = ccp(axis.x * distance * amount, axis.y * distance * amount);
            frame.opacity = opacity;
            break;
        }

        case TransitionEffect::Diagonal: {
            float amount = incoming ? phase : -phase * 0.35f;
            frame.offset = ccp(-25.f * intensity * amount, 20.f * intensity * amount);
            frame.rotation = direction * phase * 15.f * intensity;
            frame.opacity = opacity;
            break;
        }

        case TransitionEffect::Swing:
            frame.rotation = direction * phase * 42.f * intensity;
            frame.offset.y = phase * 5.f * intensity;
            frame.opacity = opacity;
            break;

        case TransitionEffect::Pendulum: {
            float wave = std::sin(timed * kPi * 3.f) * (1.f - timed);
            frame.rotation = (incoming ? wave : -wave) * 34.f * intensity;
            frame.offset.x = wave * 4.f * intensity;
            frame.opacity = opacity;
            break;
        }

        case TransitionEffect::Squash:
            frame.scaleX = 1.f + phase * 0.48f * intensity;
            frame.scaleY = std::max(0.12f, 1.f - phase * 0.58f * intensity);
            frame.opacity = opacity;
            break;

        case TransitionEffect::Stretch:
            frame.scaleX = std::max(0.12f, 1.f - phase * 0.40f * intensity);
            frame.scaleY = 1.f + phase * 0.58f * intensity;
            frame.opacity = opacity;
            break;

        case TransitionEffect::OrbitClockwise:
        case TransitionEffect::OrbitCounterClockwise: {
            float spin = settings.effect == TransitionEffect::OrbitClockwise ? 1.f : -1.f;
            float angle = spin * phase * kPi * 1.35f;
            float radius = phase * 22.f * intensity;
            frame.offset = ccp(std::cos(angle) * radius, std::sin(angle) * radius);
            frame.rotation = spin * direction * phase * 100.f * intensity;
            frame.scaleX = frame.scaleY = 1.f - phase * 0.22f * intensity;
            frame.opacity = opacity;
            break;
        }

        case TransitionEffect::Snap:
            frame.offset.x = direction * phase * 13.f * intensity;
            frame.scaleX = 1.f + phase * 0.25f * intensity;
            frame.scaleY = 1.f - phase * 0.18f * intensity;
            frame.rotation = direction * phase * 10.f * intensity;
            frame.opacity = opacity;
            break;

        case TransitionEffect::Glitch: {
            float envelope = std::sin(timed * kPi);
            float jitterX = std::sin(raw * 79.f) * 8.f * envelope * intensity;
            float jitterY = std::sin(raw * 113.f) * 3.f * envelope * intensity;
            frame.offset = ccp(incoming ? jitterX : -jitterX, jitterY);
            frame.skewX = std::sin(raw * 61.f) * 12.f * envelope * intensity;
            frame.opacity = std::clamp(opacity + std::sin(raw * 47.f) * 0.18f * envelope, 0.f, 1.f);
            break;
        }

        case TransitionEffect::Burst: {
            float burst = std::sin(timed * kPi) * intensity;
            frame.scaleX = frame.scaleY = incoming ? 1.f + burst * 0.42f
                                                   : 1.f - burst * 0.25f;
            frame.rotation = direction * burst * 24.f;
            frame.opacity = opacity;
            break;
        }

        case TransitionEffect::Count:
            break;
    }

    frame.scaleX = std::max(0.02f, frame.scaleX);
    frame.scaleY = std::max(0.02f, frame.scaleY);
    frame.opacity = std::clamp(frame.opacity, 0.f, 1.f);
    return frame;
}

void applyTransitionFrame(
    CCSprite* sprite, CCPoint const& position, CCPoint const& baseScale,
    int baseOpacity, TransitionFrame const& frame) {
    if (!sprite) return;
    sprite->setPosition(position + frame.offset);
    sprite->setScaleX(baseScale.x * frame.scaleX);
    sprite->setScaleY(baseScale.y * frame.scaleY);
    sprite->setRotation(frame.rotation);
    sprite->setSkewX(frame.skewX);
    sprite->setSkewY(frame.skewY);
    sprite->setOpacity(static_cast<GLubyte>(std::clamp(
        static_cast<int>(std::round(baseOpacity * frame.opacity)), 0, 255)));
}

} // namespace paimon::cursorfx
