#include "AnimateTypes.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace paimon::animate {

namespace {

float finiteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

} // namespace

void sanitize(ClipSettings& s) {
    s.fps = std::clamp(finiteOr(s.fps, 12.f), kMinFps, kMaxFps);
    s.repeats = std::clamp(s.repeats, 1, kMaxRepeats);
    s.startDelay = std::clamp(finiteOr(s.startDelay, 0.f), 0.f, 60.f);
    s.fade = std::clamp(finiteOr(s.fade, 0.f), 0.f, 2.f);
    s.startX = std::clamp(finiteOr(s.startX, 0.f), 0.f, 1000000.f);
    s.editorLayer = std::clamp(s.editorLayer, 0, 9999);
}

void sanitize(Clip& clip) {
    sanitize(clip.settings);
    clip.color = ((clip.color % kColorTags) + kColorTags) % kColorTags;
    if (clip.name.size() > 32) clip.name.resize(32);
    if (clip.frames.size() > static_cast<std::size_t>(kMaxFrames)) {
        clip.frames.resize(static_cast<std::size_t>(kMaxFrames));
    }
    for (auto& frame : clip.frames) {
        frame.hold = std::clamp(frame.hold, 1, kMaxHold);
        if (frame.group < 0 || frame.group > kMaxGroup) frame.group = 0;
        if (frame.label.size() > 24) frame.label.resize(24);
    }
}

void sanitize(ViewPrefs& p) {
    p.onionBefore = std::clamp(p.onionBefore, 0, 5);
    p.onionAfter = std::clamp(p.onionAfter, 0, 5);
    p.onionOpacity = std::clamp(p.onionOpacity, 10, 230);
    p.dimOpacity = std::clamp(p.dimOpacity, 10, 230);
}

float frameDuration(Clip const& clip, Frame const& frame) {
    return static_cast<float>(std::max(1, frame.hold)) / std::max(kMinFps, clip.settings.fps);
}

bool loops(PlayMode mode) {
    return mode == PlayMode::Loop || mode == PlayMode::PingPong || mode == PlayMode::Reverse;
}

std::vector<Step> buildSequence(Clip const& clip) {
    std::vector<std::size_t> order;
    order.reserve(clip.frames.size());
    for (std::size_t i = 0; i < clip.frames.size(); ++i) {
        if (!clip.frames[i].skip) order.push_back(i);
    }
    if (order.empty()) return {};

    std::vector<std::size_t> indices;
    switch (clip.settings.mode) {
        case PlayMode::Loop:
        case PlayMode::Once:
            indices = order;
            break;
        case PlayMode::Reverse:
            indices.assign(order.rbegin(), order.rend());
            break;
        case PlayMode::PingPong:
            indices = order;
            // both ends are left out on the way back or they would hold twice as long.
            for (std::size_t i = order.size() - 1; i-- > 1;) indices.push_back(order[i]);
            break;
        case PlayMode::Repeat: {
            int const allowed = std::max<int>(1, kMaxSteps / static_cast<int>(order.size()));
            int const times = std::clamp(clip.settings.repeats, 1, allowed);
            for (int t = 0; t < times; ++t) indices.insert(indices.end(), order.begin(), order.end());
            break;
        }
    }
    if (indices.size() > static_cast<std::size_t>(kMaxSteps)) {
        indices.resize(static_cast<std::size_t>(kMaxSteps));
    }

    std::vector<Step> steps;
    steps.reserve(indices.size());
    for (auto index : indices) {
        float const duration = frameDuration(clip, clip.frames[index]);
        if (!steps.empty() && steps.back().frame == index) {
            steps.back().duration += duration;
            continue;
        }
        steps.push_back({index, duration});
    }
    return steps;
}

std::vector<int> parseGroupInput(std::string_view text) {
    std::vector<int> groups;
    auto add = [&](int group) {
        if (group <= 0 || group > kMaxGroup) return;
        if (std::find(groups.begin(), groups.end(), group) != groups.end()) return;
        if (groups.size() < static_cast<std::size_t>(kMaxFrames)) groups.push_back(group);
    };
    auto number = [](std::string_view part, int& out) {
        while (!part.empty() && part.front() == ' ') part.remove_prefix(1);
        while (!part.empty() && part.back() == ' ') part.remove_suffix(1);
        auto const result = std::from_chars(part.data(), part.data() + part.size(), out);
        return !part.empty() && result.ec == std::errc{} && result.ptr == part.data() + part.size();
    };

    std::size_t begin = 0;
    while (begin <= text.size()) {
        auto end = text.find_first_of(",; ", begin);
        if (end == std::string_view::npos) end = text.size();
        auto const part = text.substr(begin, end - begin);
        begin = end + 1;
        if (part.empty()) continue;
        auto const dash = part.find('-');
        int from = 0;
        int to = 0;
        if (dash == std::string_view::npos) {
            if (number(part, from)) add(from);
            continue;
        }
        if (!number(part.substr(0, dash), from) || !number(part.substr(dash + 1), to)) continue;
        int const step = from <= to ? 1 : -1;
        for (int group = from; groups.size() < static_cast<std::size_t>(kMaxFrames); group += step) {
            add(group);
            if (group == to) break;
        }
    }
    return groups;
}

float sequenceDuration(std::vector<Step> const& steps) {
    float total = 0.f;
    for (auto const& step : steps) total += step.duration;
    return total;
}

} // namespace paimon::animate
