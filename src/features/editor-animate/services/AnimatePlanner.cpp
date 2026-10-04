#include "AnimatePlanner.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <charconv>

namespace paimon::animate {

namespace {

int groupOf(Clip const& clip, Step const& step) {
    return step.frame < clip.frames.size() ? clip.frames[step.frame].group : 0;
}

std::vector<std::string_view> splitOn(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        auto const end = text.find(separator, begin);
        if (end == std::string_view::npos) {
            parts.push_back(text.substr(begin));
            break;
        }
        parts.push_back(text.substr(begin, end - begin));
        begin = end + 1;
    }
    return parts;
}

int parseInt(std::string_view text) {
    int value = 0;
    auto const result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} ? value : 0;
}

std::vector<int> parseGroupList(std::string_view value) {
    std::vector<int> groups;
    for (auto part : splitOn(value, '.')) {
        int const group = parseInt(part);
        if (group > 0 && std::find(groups.begin(), groups.end(), group) == groups.end()) {
            groups.push_back(group);
        }
    }
    return groups;
}

class Emitter {
public:
    Emitter(Clip const& clip, PlanLayout const& layout)
        : m_alpha(clip.settings.visibility == Visibility::Alpha),
          m_fade(clip.settings.fade),
          m_layout(layout) {}

    void visibility(int target, bool show, std::vector<int> groups, bool loose) {
        if (target <= 0) return;
        TriggerSpec spec;
        spec.kind = m_alpha ? TriggerKind::Alpha : TriggerKind::Toggle;
        spec.target = target;
        spec.show = show;
        spec.fade = m_alpha ? m_fade : 0.f;
        spec.groups = std::move(groups);
        place(spec, loose, kLevelStartX);
        m_out.push_back(std::move(spec));
    }

    void spawn(int target, float delay, std::vector<int> groups, bool loose, float looseX) {
        if (target <= 0) return;
        TriggerSpec spec;
        spec.kind = TriggerKind::Spawn;
        spec.target = target;
        spec.delay = std::max(0.f, delay);
        spec.groups = std::move(groups);
        place(spec, loose, looseX);
        m_out.push_back(std::move(spec));
    }

    void stop(int target, int command, std::vector<int> groups) {
        if (target <= 0) return;
        TriggerSpec spec;
        spec.kind = TriggerKind::Stop;
        spec.target = target;
        spec.command = command;
        spec.groups = std::move(groups);
        place(spec, false, 0.f);
        m_out.push_back(std::move(spec));
    }

    std::vector<TriggerSpec> take() { return std::move(m_out); }

private:
    void place(TriggerSpec& spec, bool loose, float looseX) {
        spec.spawnTriggered = !loose;
        if (loose) {
            spec.x = looseX;
            spec.y = m_layout.looseY - static_cast<float>(m_loose++) * kGridStep;
            return;
        }
        spec.x = m_layout.gridX + static_cast<float>(m_grid % kGridColumns) * kGridStep;
        spec.y = m_layout.gridY - static_cast<float>(m_grid / kGridColumns) * kGridStep;
        ++m_grid;
    }

    bool m_alpha;
    float m_fade;
    PlanLayout m_layout;
    std::size_t m_grid = 0;
    std::size_t m_loose = 0;
    std::vector<TriggerSpec> m_out;
};

} // namespace

std::vector<int> frameGroups(Clip const& clip) {
    std::vector<int> groups;
    for (auto const& frame : clip.frames) {
        if (frame.group <= 0) continue;
        if (std::find(groups.begin(), groups.end(), frame.group) == groups.end()) {
            groups.push_back(frame.group);
        }
    }
    return groups;
}

bool needsEndEvent(Clip const& clip, std::vector<Step> const& steps) {
    if (steps.empty() || loops(clip.settings.mode)) return false;
    int const last = groupOf(clip, steps.back());
    int const first = groupOf(clip, steps.front());
    switch (clip.settings.end) {
        case EndAction::HoldLast: return false;
        case EndAction::HideAll: return last > 0;
        case EndAction::ShowFirst: return last != first;
    }
    return false;
}

std::size_t eventGroupsNeeded(Clip const& clip, std::vector<Step> const& steps) {
    return steps.size() + (needsEndEvent(clip, steps) ? 1 : 0);
}

std::vector<TriggerSpec> planTriggers(
    Clip const& clip,
    std::vector<Step> const& steps,
    PlanGroups const& groups,
    PlanLayout const& layout
) {
    if (steps.empty() || groups.events.size() < eventGroupsNeeded(clip, steps)) return {};

    auto const& s = clip.settings;
    auto const all = frameGroups(clip);
    int const first = groupOf(clip, steps.front());
    int const last = groupOf(clip, steps.back());
    Emitter emit(clip, layout);

    auto tagged = [&](std::initializer_list<int> extra) {
        std::vector<int> list;
        for (int group : extra) {
            if (group > 0) list.push_back(group);
        }
        list.push_back(groups.marker);
        return list;
    };

    // until the entry event runs every frame group is lit at once.
    bool const waits = s.start != StartMode::LevelStart || s.startDelay > 0.f;
    if (waits && s.before != BeforeStart::ShowAll) {
        for (int group : all) {
            if (s.before == BeforeStart::FirstFrame && group == first) continue;
            emit.visibility(group, false, tagged({}), true);
        }
    }

    int const entry = groups.events.front();
    emit.spawn(entry, s.startDelay, tagged({groups.start, groups.chain}), false, 0.f);
    if (s.start == StartMode::LevelStart) {
        emit.spawn(entry, s.startDelay, tagged({groups.chain}), true, kLevelStartX);
    } else if (s.start == StartMode::Position) {
        emit.spawn(entry, s.startDelay, tagged({groups.chain}), true,
            std::max(kLevelStartX, layout.positionX));
    }

    bool const ending = needsEndEvent(clip, steps);
    for (std::size_t k = 0; k < steps.size(); ++k) {
        auto const own = tagged({groups.events[k], groups.chain});
        int const current = groupOf(clip, steps[k]);
        if (k == 0) {
            // re-entering through a loop or the start group may find any frame lit.
            for (int group : all) {
                if (group != current) emit.visibility(group, false, own, false);
            }
            emit.visibility(current, true, own, false);
        } else {
            int const previous = groupOf(clip, steps[k - 1]);
            if (previous != current) {
                emit.visibility(previous, false, own, false);
                emit.visibility(current, true, own, false);
            }
        }

        float const delay = steps[k].duration;
        if (k + 1 < steps.size()) {
            emit.spawn(groups.events[k + 1], delay, own, false, 0.f);
        } else if (loops(s.mode)) {
            emit.spawn(entry, delay, own, false, 0.f);
        } else if (ending) {
            emit.spawn(groups.events[steps.size()], delay, own, false, 0.f);
        }
    }

    if (ending) {
        auto const own = tagged({groups.events[steps.size()], groups.chain});
        emit.visibility(last, false, own, false);
        if (s.end == EndAction::ShowFirst) emit.visibility(first, true, own, false);
    }

    if (s.controls) {
        // control triggers stay out of the chain group so pausing it can't freeze resume.
        auto const stopGroups = tagged({groups.stop});
        emit.stop(groups.chain, 0, stopGroups);
        if (s.stopAction != EndAction::HoldLast) {
            for (int group : all) {
                if (s.stopAction == EndAction::ShowFirst && group == first) continue;
                emit.visibility(group, false, stopGroups, false);
            }
            if (s.stopAction == EndAction::ShowFirst) emit.visibility(first, true, stopGroups, false);
        }
        emit.stop(groups.chain, 1, tagged({groups.pause}));
        emit.stop(groups.chain, 2, tagged({groups.resume}));
    }

    return emit.take();
}

std::string serializeTriggers(std::vector<TriggerSpec> const& triggers, int editorLayer) {
    std::string out;
    out.reserve(triggers.size() * 96);
    for (auto const& spec : triggers) {
        int id = triggerids::Spawn;
        switch (spec.kind) {
            case TriggerKind::Alpha: id = triggerids::Alpha; break;
            case TriggerKind::Toggle: id = triggerids::Toggle; break;
            case TriggerKind::Spawn: id = triggerids::Spawn; break;
            case TriggerKind::Stop: id = triggerids::Stop; break;
        }
        out += fmt::format("1,{},2,{:.2f},3,{:.2f},51,{}", id, spec.x, spec.y, spec.target);
        switch (spec.kind) {
            case TriggerKind::Alpha:
                out += fmt::format(",10,{:.3f},35,{}", spec.fade, spec.show ? 1 : 0);
                break;
            case TriggerKind::Toggle:
                if (spec.show) out += ",56,1";
                break;
            case TriggerKind::Spawn:
                out += fmt::format(",63,{:.4f}", spec.delay);
                break;
            case TriggerKind::Stop:
                if (spec.command != 0) out += fmt::format(",580,{}", spec.command);
                break;
        }
        if (spec.spawnTriggered) out += ",62,1";
        out += ",87,1";
        if (!spec.groups.empty()) {
            out += ",57,";
            for (std::size_t i = 0; i < spec.groups.size(); ++i) {
                if (i > 0) out += '.';
                out += fmt::format("{}", spec.groups[i]);
            }
        }
        if (editorLayer > 0) out += fmt::format(",20,{}", editorLayer);
        out += ';';
    }
    return out;
}

std::string rewriteGroups(std::string_view save, std::vector<int> const& strip, int add) {
    while (!save.empty() && (save.back() == ';' || save.back() == ',')) save.remove_suffix(1);
    auto const tokens = splitOn(save, ',');

    auto rebuilt = [&](std::vector<int> groups) {
        std::erase_if(groups, [&](int group) {
            return std::find(strip.begin(), strip.end(), group) != strip.end();
        });
        if (add > 0 && std::find(groups.begin(), groups.end(), add) == groups.end()) {
            groups.push_back(add);
        }
        std::string value;
        for (std::size_t i = 0; i < groups.size(); ++i) {
            if (i > 0) value += '.';
            value += fmt::format("{}", groups[i]);
        }
        return value;
    };

    std::string out;
    out.reserve(save.size() + 16);
    bool found = false;
    for (std::size_t i = 0; i + 1 < tokens.size(); i += 2) {
        std::string value(tokens[i + 1]);
        if (tokens[i] == "57") {
            found = true;
            value = rebuilt(parseGroupList(tokens[i + 1]));
            if (value.empty()) continue;
        }
        if (!out.empty()) out += ',';
        out += tokens[i];
        out += ',';
        out += value;
    }
    if (!found && add > 0) {
        if (!out.empty()) out += ',';
        out += fmt::format("57,{}", add);
    }
    return out;
}

} // namespace paimon::animate
