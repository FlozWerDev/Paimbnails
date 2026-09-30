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

void validFrame(Frame const& frame) {
    assert(std::isfinite(frame.rect.x) && std::isfinite(frame.rect.y));
    assert(std::isfinite(frame.rect.width) && frame.rect.width > 0.f);
    assert(std::isfinite(frame.rect.height) && frame.rect.height > 0.f);
    assert(frame.radius >= 0.f && frame.radius <= std::min(frame.rect.width, frame.rect.height) / 2.f);
    assert(frame.opacity >= 0.f && frame.opacity <= 1.f);
    assert(frame.buttonOpacity >= 0.f && frame.buttonOpacity <= 1.f);
    assert(frame.backgroundBrightness >= 0.f && frame.backgroundBrightness <= 1.f);
}

}

int main() {
    Config config;
    Rect button{12.f, 70.f, 40.f, 30.f};
    auto entry = evaluate(config, button, 568.f, 320.f, 0.f, false);
    sameRect(entry.rect, button);
    assert(entry.buttonOpacity == 1.f && entry.opacity == 0.f);
    assert(entry.backgroundScale == 1.f && entry.backgroundBrightness == 1.f);
    auto opened = evaluate(config, button, 568.f, 320.f, 1.f, false);
    sameRect(opened.rect, {0.f, 0.f, 568.f, 320.f});
    assert(opened.radius == 0.f && opened.opacity == 1.f && opened.buttonOpacity == 0.f);
    sameRect(evaluate(config, button, 568.f, 320.f, 0.f, true).rect, opened.rect);
    auto returned = evaluate(config, button, 568.f, 320.f, 1.f, true);
    sameRect(returned.rect, button);
    assert(returned.opacity == 0.f && returned.buttonOpacity == 1.f);
    assert(returned.backgroundScale == 1.f && returned.backgroundBrightness == 1.f);

    for (auto style : {Style::App, Style::Card, Style::Slide, Style::Zoom}) {
        for (auto curve : {Curve::Smooth, Curve::Spring, Curve::Soft, Curve::Linear}) {
            for (auto size : std::array<std::array<float, 2>, 4>{{{568.f, 320.f},
                    {320.f, 568.f}, {2048.f, 320.f}, {8.f, 8.f}}}) {
                config.style = style;
                config.curve = curve;
                config.spring = .12f;
                Rect origin = normalizeOrigin(button, size[0], size[1]);
                for (bool backwards : {false, true}) {
                    for (int step = 0; step <= 1000; ++step)
                        validFrame(evaluate(config, origin, size[0], size[1], step / 1000.f, backwards));
                }
                sameRect(evaluate(config, origin, size[0], size[1], 1.f, false).rect,
                    {0.f, 0.f, size[0], size[1]});
                sameRect(evaluate(config, origin, size[0], size[1], 0.f, true).rect,
                    {0.f, 0.f, size[0], size[1]});
            }
        }
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
    invalid.quality = inf;
    invalid.style = static_cast<Style>(99);
    invalid.curve = static_cast<Curve>(-1);
    invalid.origin = static_cast<Origin>(99);
    invalid.reducedMotion = static_cast<ReducedMotion>(99);
    auto safe = sanitize(invalid);
    assert(safe.style == Style::App && safe.curve == Curve::Smooth);
    assert(safe.origin == Origin::Button && safe.reducedMotion == ReducedMotion::Fade);
    assert(safe.duration >= .12f && safe.duration <= 1.5f);
    assert(safe.backDuration >= .12f && safe.backDuration <= 1.5f);
    assert(safe.cornerRadius >= 0.f && safe.cornerRadius <= 48.f);
    assert(safe.backgroundScale >= .85f && safe.backgroundScale <= 1.f);
    assert(safe.dim >= 0.f && safe.dim <= .65f);
    assert(safe.buttonBlend == 0.f && safe.quality >= .5f && safe.quality <= 1.f);
    validFrame(evaluate(safe, {nan, inf, -1.f, inf}, 568.f, 320.f, nan, false));
    sameRect(evaluate(safe, button, 568.f, 320.f, -1.f, false).rect,
        evaluate(safe, button, 568.f, 320.f, 0.f, false).rect);
    sameRect(evaluate(safe, button, 568.f, 320.f, 2.f, false).rect,
        evaluate(safe, button, 568.f, 320.f, 1.f, false).rect);

    for (auto origin : {Origin::Button, Origin::Center, Origin::Bottom, Origin::Left}) {
        auto rect = fallbackOrigin(568.f, 320.f, origin);
        assert(rect.x >= 0.f && rect.y >= 0.f);
        assert(rect.x + rect.width <= 568.f && rect.y + rect.height <= 320.f);
    }
}
