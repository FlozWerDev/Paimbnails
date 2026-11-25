#pragma once
#include "TransitionTypes.hpp"
#include "TransitionTimeline.hpp"
#include <numeric>

namespace paimon::transitions {
inline void insertTransitionCommand(std::vector<TransitionCommand>& commands, int at, TransitionCommand command) {
    at = std::clamp(at, 0, static_cast<int>(commands.size()));
    for (auto& existing : commands)
        if (existing.targetObj >= at) ++existing.targetObj;
    if (command.targetObj >= at) ++command.targetObj;
    commands.insert(commands.begin() + at, std::move(command));
}

// reordering preserves object references, including references from earlier tracks.
inline void moveTransitionCommand(std::vector<TransitionCommand>& commands, int from, int to) {
    if (from < 0 || to < 0 || from >= static_cast<int>(commands.size()) || to >= static_cast<int>(commands.size())) return;
    std::vector<int> order(commands.size());
    std::iota(order.begin(), order.end(), 0);
    auto value = std::move(commands[from]);
    auto original = order[from];
    commands.erase(commands.begin() + from); order.erase(order.begin() + from);
    commands.insert(commands.begin() + to, std::move(value)); order.insert(order.begin() + to, original);
    std::vector<int> remap(order.size());
    for (std::size_t i = 0; i < order.size(); ++i) remap[order[i]] = static_cast<int>(i);
    for (auto& command : commands)
        if (command.targetObj >= 0 && command.targetObj < static_cast<int>(remap.size())) command.targetObj = remap[command.targetObj];
}

inline void eraseTransitionCommand(std::vector<TransitionCommand>& commands, int at) {
    if (at < 0 || at >= static_cast<int>(commands.size())) return;
    std::vector<int> remap(commands.size(), -1);
    std::vector<TransitionCommand> kept;
    for (int i = 0; i < static_cast<int>(commands.size()); ++i) {
        if (i == at || commands[i].targetObj == at) continue;
        remap[i] = static_cast<int>(kept.size());
        kept.push_back(commands[i]);
    }
    for (auto& command : kept)
        if (command.targetObj >= 0 && command.targetObj < static_cast<int>(remap.size())) command.targetObj = remap[command.targetObj];
    commands = std::move(kept);
}

inline void makeTransitionTimelineAbsolute(std::vector<TransitionCommand>& commands) {
    auto timeline = compileTimeline(commands, [](auto const& c) { return c.action == CommandAction::Spawn; });
    for (auto const& clip : timeline.clips) {
        auto& command = commands[clip.command];
        if (command.action == CommandAction::Image && command.startTime < 0.f) {
            if (command.fromVal == 0.f && command.toVal == 0.f) command.toVal = 255.f;
            for (auto const& trigger : commands)
                if (trigger.enabled && trigger.targetObj == static_cast<int>(clip.command))
                    command.duration = std::max(command.duration, timeline.duration - clip.start);
        }
        command.startTime = clip.start;
    }
    for (auto& c : commands) { c.delay = 0.f; if (c.startTime < 0.f) c.startTime = 0.f; }
    for (int i = static_cast<int>(commands.size()) - 1; i >= 0; --i)
        if (commands[i].action == CommandAction::Spawn) eraseTransitionCommand(commands, i);
}
} // namespace paimon::transitions
