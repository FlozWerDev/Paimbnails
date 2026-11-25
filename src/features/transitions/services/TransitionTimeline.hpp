#pragma once
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

namespace paimon::transitions {
// inspect ieee exponent bits independently of compiler floating-point assumptions.
inline bool finite(float value) {
    return (std::bit_cast<std::uint32_t>(value) & 0x7f800000u) != 0x7f800000u;
}
inline float bounded(float value, float fallback, float low, float high) {
    return finite(value) ? std::clamp(value, low, high) : fallback;
}
struct TimelineClip {
    std::size_t command;
    float start;
    float duration;
};
struct Timeline {
    std::vector<TimelineClip> clips;
    float duration = 0.f;
};
// spawn eats the next n commands as a parallel group. delays are relative
// to the group start; followers wait for its last end.
template<class Commands, class IsSpawn>
Timeline compileTimeline(Commands const& commands, IsSpawn isSpawn) {
    Timeline result;
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < commands.size(); ++i)
        if (commands[i].enabled) order.push_back(i);
    for (std::size_t k = 0; k < order.size();) {
        auto const& command = commands[order[k]];
        float groupStart = result.duration;
        std::size_t end = k + 1;
        if (isSpawn(command)) {
            groupStart += bounded(command.delay, 0.f, 0.f, 30.f);
            end = std::min(order.size(), k + 1 + static_cast<std::size_t>(std::clamp(command.spawnCount, 0, 16)));
            ++k;
        }
        result.duration = std::max(result.duration, groupStart);
        for (; k < end; ++k) {
            auto i = order[k];
            if (isSpawn(commands[i])) continue; // nested groups are deliberately not recursive
            float start = commands[i].startTime >= 0.f
                ? bounded(commands[i].startTime, 0.f, 0.f, 30.f)
                : groupStart + bounded(commands[i].delay, 0.f, 0.f, 30.f);
            float duration = bounded(commands[i].duration, .3f, .001f, 30.f);
            result.clips.push_back({i, start, duration});
            result.duration = std::max(result.duration, start + duration);
        }
    }
    return result;
}
inline std::size_t frameAt(std::vector<int> const& endsMs, double elapsedMs) {
    auto it = std::upper_bound(endsMs.begin(), endsMs.end(), elapsedMs);
    return endsMs.empty() ? 0 : std::min<std::size_t>(it - endsMs.begin(), endsMs.size() - 1);
}
} // namespace paimon::transitions
