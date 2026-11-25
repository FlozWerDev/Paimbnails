#pragma once
#include "TransitionTypes.hpp"
#include "TransitionTimeline.hpp"
#include <optional>

namespace paimon::transitions {
struct TransitionPoint { float x = 0.f, y = 0.f; };
struct TransitionVisual {
    TransitionPoint position;
    float opacity = 255.f, scale = 1.f, rotation = 0.f, skew = 0.f;
    int r = 255, g = 255, b = 255;
    bool visible = false;
    float mediaElapsed = 0.f;
};
struct TransitionFrame {
    TransitionVisual sceneA, sceneB, wash;
    std::vector<TransitionVisual> objects; // indexed by command; references never change on disabling
};

inline float transitionEase(TransitionEasing easing, float t) {
    t = bounded(t, 0.f, 0.f, 1.f);
    constexpr float pi = 3.14159265f;
    switch (easing) {
        case TransitionEasing::EaseIn: return t * t * t;
        case TransitionEasing::EaseOut: return 1.f - std::pow(1.f - t, 3.f);
        case TransitionEasing::EaseInOut: return t < .5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) / 2.f;
        case TransitionEasing::Sine: return -(std::cos(pi * t) - 1.f) / 2.f;
        case TransitionEasing::Back: { float u = t - 1.f; return 1.f + 2.70158f * u * u * u + 1.70158f * u * u; }
        case TransitionEasing::Bounce:
            if (t < 1.f / 2.75f) return 7.5625f * t * t;
            if (t < 2.f / 2.75f) { float p = t - 1.5f / 2.75f; return 7.5625f * p * p + .75f; }
            if (t < 2.5f / 2.75f) { float p = t - 2.25f / 2.75f; return 7.5625f * p * p + .9375f; }
            { float p = t - 2.625f / 2.75f; return 7.5625f * p * p + .984375f; }
        case TransitionEasing::Elastic:
            if (t == 0.f || t == 1.f) return t;
            return std::pow(2.f, -10.f * t) * std::sin((t * 10.f - .75f) * (2.f * pi / 3.f)) + 1.f;
        default: return t;
    }
}

// the editor and the real transition share this sampler. It supports random
// access for scrubbing; only cursor positions need history between samples.
class TransitionEvaluator {
public:
    void configure(std::vector<TransitionCommand> commands, float minimumDuration = 0.f) {
        m_commands = std::move(commands);
        m_timeline = compileTimeline(m_commands, [](auto const& c) { return c.action == CommandAction::Spawn; });
        m_timeline.duration = std::max(m_timeline.duration, bounded(minimumDuration, 0.f, 0.f, 30.f));
        std::stable_sort(m_timeline.clips.begin(), m_timeline.clips.end(), [](auto const& a, auto const& b) { return a.start < b.start; });
        m_followPositions.assign(m_commands.size(), std::nullopt);
        m_previousTime = -1.f;
    }
    Timeline const& timeline() const { return m_timeline; }
    TransitionFrame sample(float time, TransitionPoint screen, TransitionPoint cursor, float cutPoint = -1.f) {
        time = bounded(time, 0.f, 0.f, 60.f);
        if (time < m_previousTime) m_followPositions.assign(m_commands.size(), std::nullopt);
        m_previousTime = time;
        TransitionFrame frame;
        frame.sceneA.visible = frame.sceneB.visible = true;
        frame.sceneA.position = frame.sceneB.position = {screen.x / 2.f, screen.y / 2.f};
        frame.sceneB.opacity = 0.f;
        if (cutPoint >= 0.f && time >= m_timeline.duration * cutPoint) {
            frame.sceneA.opacity = 0.f; frame.sceneB.opacity = 255.f;
        }
        frame.wash.opacity = 0.f;
        frame.objects.resize(m_commands.size());
        // establish object lifetimes and base alpha before applying any triggers.
        for (auto const& clip : m_timeline.clips) {
            auto const& c = m_commands[clip.command];
            if (c.action != CommandAction::Image) continue;
            auto& object = frame.objects[clip.command];
            object.position = {c.fromX * screen.x, c.fromY * screen.y};
            object.visible = time >= clip.start && time < clip.start + clip.duration;
            if (c.startTime < 0.f) {
                for (auto const& trigger : m_commands)
                    if (trigger.enabled && trigger.targetObj == static_cast<int>(clip.command)) object.visible = time >= clip.start;
            }
            float progress = std::clamp((time - clip.start) / clip.duration, 0.f, 1.f);
            object.opacity = c.fromVal + (c.toVal - c.fromVal) * transitionEase(c.easing, progress);
            if (c.startTime < 0.f && c.fromVal == 0.f && c.toVal == 0.f) object.opacity = 255.f * progress;
            object.mediaElapsed = std::clamp(time - clip.start, 0.f, clip.duration);
            if (c.solidObject) { object.r = c.r; object.g = c.g; object.b = c.b; }
        }
        for (auto const& clip : m_timeline.clips) {
            auto const& c = m_commands[clip.command];
            if (c.action == CommandAction::Image || time < clip.start) continue;
            float elapsed = std::min(time - clip.start, clip.duration);
            float progress = std::clamp(elapsed / clip.duration, 0.f, 1.f);
            auto easing = c.action == CommandAction::EaseIn ? TransitionEasing::EaseIn :
                c.action == CommandAction::EaseOut ? TransitionEasing::EaseOut :
                c.action == CommandAction::Bounce ? TransitionEasing::Bounce : c.easing;
            float e = transitionEase(easing, progress);
            float value = c.fromVal + (c.toVal - c.fromVal) * e;
            if (c.action == CommandAction::Overlay) {
                frame.wash.visible = true; frame.wash.opacity = value;
                frame.wash.r = c.r; frame.wash.g = c.g; frame.wash.b = c.b;
                continue;
            }
            if (c.action == CommandAction::FollowCursor && time <= clip.start + clip.duration)
                m_followPositions[clip.command] = cursor;
            auto apply = [&](TransitionVisual& visual, bool object) {
                switch (c.action) {
                    case CommandAction::FadeIn: case CommandAction::FadeOut:
                    case CommandAction::EaseIn: case CommandAction::EaseOut: case CommandAction::Bounce: visual.opacity = value; break;
                    case CommandAction::Move:
                        visual.position = {(c.fromX + (c.toX - c.fromX) * e) * (object ? screen.x : 1.f),
                            (c.fromY + (c.toY - c.fromY) * e) * (object ? screen.y : 1.f)}; break;
                    case CommandAction::Scale: visual.scale = std::clamp(value, .01f, 10.f); break;
                    case CommandAction::Rotate: visual.rotation = value; break;
                    case CommandAction::Skew: visual.skew = std::clamp(value, -60.f, 60.f); break;
                    case CommandAction::Color: visual.r = c.r; visual.g = c.g; visual.b = c.b; break;
                    case CommandAction::FollowCursor:
                        if (m_followPositions[clip.command]) visual.position = *m_followPositions[clip.command];
                        break;
                    case CommandAction::Shake: {
                        float amplitude = c.intensity * std::sin(progress * 3.14159265f);
                        visual.position.x += amplitude * std::sin(elapsed * 91.f);
                        visual.position.y += amplitude * std::sin(elapsed * 113.f); break;
                    }
                    default: break;
                }
            };
            if (c.targetObj >= 0) {
                if (c.targetObj < static_cast<int>(m_commands.size()) && m_commands[c.targetObj].enabled &&
                    m_commands[c.targetObj].action == CommandAction::Image) apply(frame.objects[c.targetObj], true);
            } else {
                if (c.target != "to") apply(frame.sceneA, false);
                if (c.target != "from") apply(frame.sceneB, false);
            }
        }
        return frame;
    }
private:
    std::vector<TransitionCommand> m_commands;
    Timeline m_timeline;
    std::vector<std::optional<TransitionPoint>> m_followPositions;
    float m_previousTime = -1.f;
};
} // namespace paimon::transitions
