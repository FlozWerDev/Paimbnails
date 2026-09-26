#pragma once

#include "../data/ProgressionBadges.hpp"

#include <optional>
#include <string>
#include <vector>

class GJUserScore;

namespace paimon::progression {

struct ProgressDelta {
    int64_t gainedExp = 0;
    int64_t totalExp = 0;
    int fromLevel = 1;
    int toLevel = 1;
    std::vector<BadgeDef const*> newBadges;

    bool leveledUp() const { return toLevel > fromLevel; }
    bool tierChanged() const;
};

class ProgressionService {
public:
    static ProgressionService& get();

    bool enabled() const;

    // Live six from the save; rest from the last synced score.
    BadgeContext ownContext();

    // The local save can't rebuild these parts.
    void rememberOwnScore(GJUserScore* score);

    // Nullopt on first call or when nothing moved.
    std::optional<ProgressDelta> consumeDelta();

    void commitSnapshot();
    bool hasSnapshot() const;

private:
    ProgressionService() = default;

    std::vector<std::string> storedBadges() const;
    void storeBadges(BadgeContext const& ctx);
};

} // namespace paimon::progression
