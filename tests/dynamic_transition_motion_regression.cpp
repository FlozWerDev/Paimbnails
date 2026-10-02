#include "../src/features/transitions/services/DynamicTransitionMotion.hpp"
#include <array>
#include <cassert>
#include <limits>

using namespace paimon::transitions::dynamic;

namespace {

bool near(float a, float b) { return std::abs(a - b) < .001f; }

void sameRect(Rect a, Rect b) {
    assert(near(a.x, b.x) && near(a.y, b.y));
    assert(near(a.width, b.width) && near(a.height, b.height));
}

void validRect(Rect rect) {
    assert(std::isfinite(rect.x) && std::isfinite(rect.y));
    assert(std::isfinite(rect.width) && rect.width > 0.f);
    assert(std::isfinite(rect.height) && rect.height > 0.f);
}

void validLayer(Layer const& layer) {
    assert(layer.opacity >= 0.f && layer.opacity <= 1.f);
    assert(layer.brightness >= 0.f && layer.brightness <= 1.f);
    if (layer.opacity == 0.f) return;
    validRect(layer.clip);
    validRect(layer.content);
    assert(layer.radius >= 0.f && layer.radius <= std::min(layer.clip.width, layer.clip.height) / 2.f);
}

void validFrame(Frame const& frame) {
    validLayer(frame.background);
    validLayer(frame.foreground);
    validLayer(frame.button);
    assert(frame.shadow >= 0.f && frame.shadow <= 1.f);
    assert(std::isfinite(frame.shadowBlur) && frame.shadowBlur >= 0.f);
}

}

int main() {
    Config config;
    Rect button{12.f, 70.f, 40.f, 30.f};
    Rect screen{0.f, 0.f, 568.f, 320.f};
    auto entry = evaluate(config, button, 568.f, 320.f, 0.f, false);
    sameRect(entry.foreground.clip, button);
    assert(entry.button.opacity == 1.f && entry.foreground.opacity == 0.f);
    sameRect(entry.background.content, screen);
    assert(entry.background.brightness == 1.f);
    auto opened = evaluate(config, button, 568.f, 320.f, 1.f, false);
    sameRect(opened.foreground.clip, screen);
    assert(opened.foreground.radius == 0.f && opened.foreground.opacity == 1.f && opened.button.opacity == 0.f);
    sameRect(evaluate(config, button, 568.f, 320.f, 0.f, true).foreground.clip, screen);
    auto returned = evaluate(config, button, 568.f, 320.f, 1.f, true);
    sameRect(returned.foreground.clip, button);
    assert(returned.foreground.opacity == 0.f && returned.button.opacity == 1.f);
    sameRect(returned.background.content, screen);
    assert(returned.background.brightness == 1.f);

    for (bool backwards : {false, true}) {
        for (int step = 0; step <= 100; ++step) {
            auto frame = evaluate(config, button, 568.f, 320.f, step / 100.f, backwards);
            float alpha = frame.foreground.opacity + (1.f - frame.foreground.opacity) * frame.button.opacity;
            assert(near(alpha, 1.f));
        }
    }

    for (int style = 0; style < kStyleCount; ++style) {
        for (int curve = 0; curve < kCurveCount; ++curve) {
            assert(ease(0.f, static_cast<Curve>(curve), .25f) == 0.f);
            assert(ease(1.f, static_cast<Curve>(curve), .25f) == 1.f);
            for (auto size : std::array<std::array<float, 2>, 5>{{{568.f, 320.f},
                    {320.f, 568.f}, {2048.f, 320.f}, {196.f, 110.f}, {8.f, 8.f}}}) {
                config.style = static_cast<Style>(style);
                config.curve = static_cast<Curve>(curve);
                config.spring = .25f;
                for (int origin = 0; origin < kOriginCount; ++origin) {
                    config.origin = static_cast<Origin>(origin);
                    Rect start = origin == 0 ? normalizeOrigin(button, size[0], size[1]) :
                        fallbackOrigin(size[0], size[1], config.origin);
                    for (bool panel : {false, true}) {
                        for (bool backwards : {false, true}) {
                            for (int step = 0; step <= 100; ++step)
                                validFrame(evaluate(config, start, size[0], size[1], step / 100.f, backwards, panel));
                        }
                        auto end = evaluate(config, start, size[0], size[1], 1.f, false, panel);
                        sameRect(end.foreground.clip, {0.f, 0.f, size[0], size[1]});
                        assert(end.foreground.opacity == 1.f && end.foreground.radius == 0.f);
                        sameRect(evaluate(config, start, size[0], size[1], 0.f, true, panel).foreground.clip,
                            end.foreground.clip);
                    }
                }
            }
        }
    }

    config = Config{};
    config.style = Style::Push;
    sameRect(evaluate(config, button, 568.f, 320.f, .5f, false, true).background.content, screen);
    config = reducedMotionConfig(config);
    for (int step = 0; step <= 100; ++step) {
        auto frame = evaluate(config, button, 568.f, 320.f, step / 100.f, false);
        sameRect(frame.foreground.clip, screen);
        sameRect(frame.foreground.content, screen);
        sameRect(frame.background.content, screen);
        assert(frame.shadow == 0.f && frame.background.brightness == 1.f);
    }

    float springPeak = 0.f;
    float previous = 0.f;
    for (int step = 0; step <= 1000; ++step) {
        float progress = step / 1000.f;
        springPeak = std::max(springPeak, ease(progress, Curve::Spring, .25f));
        float value = ease(progress, Curve::Spring, 0.f);
        assert(value >= previous && value <= 1.f);
        previous = value;
    }
    assert(springPeak > 1.05f && springPeak < 1.3f);

    assert(matchPreset(Config{}) == 0);
    Config custom;
    custom.enabled = false;
    custom.animateBack = false;
    custom.animateOtherMods = true;
    custom.syncSmoothUI = false;
    custom.origin = Origin::Right;
    custom.panelStyle = PanelStyle::Reveal;
    custom.quality = .5f;
    for (size_t i = 0; i < kPresets.size(); ++i) {
        auto preset = applyPreset(custom, static_cast<int>(i));
        assert(matchPreset(preset) == static_cast<int>(i));
        assert(!preset.enabled && !preset.animateBack && preset.animateOtherMods && !preset.syncSmoothUI);
        assert(preset.origin == Origin::Right && preset.panelStyle == PanelStyle::Reveal && preset.quality == .5f);
        preset.duration += .03f;
        assert(matchPreset(preset) == -1);
    }
    assert(matchPreset(applyPreset(Config{}, -1)) == 0);
    assert(matchPreset(applyPreset(Config{}, static_cast<int>(kPresets.size()))) == 0);
    for (int panel = 1; panel < kPanelStyleCount; ++panel) {
        custom.panelStyle = static_cast<PanelStyle>(panel);
        assert(static_cast<int>(panelConfig(custom).style) == panel - 1);
    }

    float nan = std::numeric_limits<float>::quiet_NaN();
    float inf = std::numeric_limits<float>::infinity();
    Config invalid;
    invalid.duration = nan;
    invalid.backDuration = -inf;
    invalid.cornerRadius = inf;
    invalid.backgroundScale = -10.f;
    invalid.dim = 10.f;
    invalid.spring = nan;
    invalid.buttonBlend = -1.f;
    invalid.shadow = inf;
    invalid.quality = inf;
    invalid.style = static_cast<Style>(99);
    invalid.curve = static_cast<Curve>(-1);
    invalid.origin = static_cast<Origin>(99);
    invalid.reducedMotion = static_cast<ReducedMotion>(99);
    invalid.panelStyle = static_cast<PanelStyle>(-1);
    auto safe = sanitize(invalid);
    assert(safe.style == Style::App && safe.curve == Curve::Smooth);
    assert(safe.origin == Origin::Button && safe.reducedMotion == ReducedMotion::Fade);
    assert(safe.panelStyle == PanelStyle::Match && safe.shadow == .5f);
    assert(safe.duration >= .12f && safe.duration <= 1.5f);
    assert(safe.backDuration >= .12f && safe.backDuration <= 1.5f);
    assert(safe.cornerRadius >= 0.f && safe.cornerRadius <= 48.f);
    assert(safe.backgroundScale >= .85f && safe.backgroundScale <= 1.f);
    assert(safe.dim >= 0.f && safe.dim <= .65f);
    assert(safe.buttonBlend == 0.f && safe.quality >= .5f && safe.quality <= 1.f);
    validFrame(evaluate(safe, {nan, inf, -1.f, inf}, 568.f, 320.f, nan, false));
    sameRect(evaluate(safe, button, 568.f, 320.f, -1.f, false).foreground.clip,
        evaluate(safe, button, 568.f, 320.f, 0.f, false).foreground.clip);
    sameRect(evaluate(safe, button, 568.f, 320.f, 2.f, false).foreground.clip,
        evaluate(safe, button, 568.f, 320.f, 1.f, false).foreground.clip);

    assert(frameStep(nan, false) == 0.f && frameStep(inf, true) == 0.f);
    assert(frameStep(-1.f, false) == 0.f);
    assert(near(frameStep(.01f, true), .01f));
    assert(near(frameStep(1.f, true), 1.f / 60.f));
    assert(near(frameStep(1.f, false), .1f));

    for (int origin = 0; origin < kOriginCount; ++origin) {
        auto rect = fallbackOrigin(568.f, 320.f, static_cast<Origin>(origin));
        assert(rect.x >= 0.f && rect.y >= 0.f);
        assert(rect.x + rect.width <= 568.f && rect.y + rect.height <= 320.f);
    }
}
