#pragma once

#include "../data/VersusCards.hpp"

#include <cstdint>
#include <vector>

namespace paimon::versus {

// pcg32. one server seed per match before the countdown, so both clients
// build the same milestone list with no round trip.
class VersusRng {
public:
    explicit VersusRng(uint64_t seed);

    uint32_t next();
    uint32_t below(uint32_t bound);
    float unit();

private:
    uint64_t m_state = 0;
    uint64_t m_inc = 0;
};

inline constexpr int kMinMilestones = 6;
inline constexpr int kMaxMilestones = 9;
inline constexpr float kMilestoneGap = 6.f;

std::vector<float> rollMilestones(uint64_t seed);

// the card a milestone deals. each side rolls its own and announces it, so
// the deficit shift uses local numbers with no agreement needed.
CardId rollCard(uint64_t seed, int milestone, uint8_t modeMask, float deficit, bool catchUp);

} // namespace paimon::versus
