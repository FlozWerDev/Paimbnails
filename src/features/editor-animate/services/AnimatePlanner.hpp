#pragma once

// pure planner: frames + playback settings in, gd trigger save string out.

#include "../AnimateTypes.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace paimon::animate {

enum class TriggerKind { Alpha, Toggle, Spawn, Stop };

namespace triggerids {
inline constexpr int Alpha = 1007;
inline constexpr int Toggle = 1049;
inline constexpr int Spawn = 1268;
inline constexpr int Stop = 1616;
} // namespace triggerids

struct TriggerSpec {
    TriggerKind kind = TriggerKind::Spawn;
    float x = 0.f;
    float y = 0.f;
    int target = 0;
    std::vector<int> groups;
    bool spawnTriggered = false;
    bool show = false;
    float delay = 0.f;
    float fade = 0.f;
    int command = 0;
};

struct PlanGroups {
    int marker = 0;
    int chain = 0;
    int start = 0;
    int stop = 0;
    int pause = 0;
    int resume = 0;
    std::vector<int> events;
};

struct PlanLayout {
    float gridX = 0.f;
    float gridY = 0.f;
    float looseY = 0.f;
    float positionX = 0.f;
};

// loose triggers this close to the origin fire on the first ticks of the level.
inline constexpr float kLevelStartX = 15.f;
inline constexpr int kGridColumns = 16;
inline constexpr float kGridStep = 10.f;

std::vector<int> frameGroups(Clip const& clip);
bool needsEndEvent(Clip const& clip, std::vector<Step> const& steps);
std::size_t eventGroupsNeeded(Clip const& clip, std::vector<Step> const& steps);

std::vector<TriggerSpec> planTriggers(
    Clip const& clip,
    std::vector<Step> const& steps,
    PlanGroups const& groups,
    PlanLayout const& layout
);
std::string serializeTriggers(std::vector<TriggerSpec> const& triggers, int editorLayer);

std::string rewriteGroups(std::string_view save, std::vector<int> const& strip, int add);

} // namespace paimon::animate
