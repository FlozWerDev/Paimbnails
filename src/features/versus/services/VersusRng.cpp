#include "VersusRng.hpp"

#include <algorithm>
#include <numeric>

namespace paimon::versus {

namespace {

constexpr uint64_t kMul = 6364136223846793005ULL;

// milestones live between these two, so nobody gets a card off the start line
// and nobody gets one they cannot reach.
constexpr float kFirstMilestone = 8.f;
constexpr float kLastMilestone = 94.f;

} // namespace

VersusRng::VersusRng(uint64_t seed) {
    m_state = 0;
    m_inc = (seed << 1u) | 1u;
    next();
    m_state += seed;
    next();
}

uint32_t VersusRng::next() {
    uint64_t const old = m_state;
    m_state = old * kMul + m_inc;
    uint32_t const xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
    uint32_t const rot = static_cast<uint32_t>(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
}

uint32_t VersusRng::below(uint32_t bound) {
    if (bound == 0) return 0;
    uint32_t const threshold = (0u - bound) % bound;
    while (true) {
        uint32_t const r = next();
        if (r >= threshold) return r % bound;
    }
}

float VersusRng::unit() {
    return static_cast<float>(next() >> 8) / static_cast<float>(1u << 24);
}

std::vector<float> rollMilestones(uint64_t seed) {
    VersusRng rng(seed);
    int const count = kMinMilestones + static_cast<int>(rng.below(kMaxMilestones - kMinMilestones + 1));

    float const span = kLastMilestone - kFirstMilestone;
    float const slack = span - kMilestoneGap * static_cast<float>(count - 1);

    // hand each gap a share of the slack, then walk them out from the first
    // milestone: every pair ends up at least kmilestonegap apart by construction.
    std::vector<float> shares(count);
    float total = 0.f;
    for (auto& share : shares) {
        share = rng.unit();
        total += share;
    }
    if (total <= 0.f) {
        std::fill(shares.begin(), shares.end(), 1.f);
        total = static_cast<float>(count);
    }

    std::vector<float> out;
    out.reserve(count);
    float cursor = kFirstMilestone;
    for (int i = 0; i < count; i++) {
        cursor += shares[i] / total * slack;
        out.push_back(cursor);
        cursor += kMilestoneGap;
    }
    return out;
}

CardId rollCard(uint64_t seed, int milestone, uint8_t modeMask, float deficit, bool catchUp) {
    VersusRng rng(seed ^ (static_cast<uint64_t>(milestone + 1) * 0x9E3779B97F4A7C15ULL));

    auto const weights = rarityWeights(deficit, catchUp);
    int const total = std::accumulate(weights.begin(), weights.end(), 0);
    int roll = static_cast<int>(rng.below(static_cast<uint32_t>(total)));

    Rarity picked = Rarity::Common;
    for (size_t i = 0; i < weights.size(); i++) {
        roll -= weights[i];
        if (roll < 0) {
            picked = static_cast<Rarity>(i);
            break;
        }
    }

    // a rarity can end up empty for this mode (skull and beacon are platformer
    // only), so walk down until something is playable.
    for (int step = 0; step < 4; step++) {
        std::vector<CardDef const*> pool;
        for (auto const* def : cardsOfRarity(picked)) {
            if (def->modes & modeMask) pool.push_back(def);
        }
        if (!pool.empty()) {
            return pool[rng.below(static_cast<uint32_t>(pool.size()))]->id;
        }
        picked = static_cast<Rarity>((static_cast<int>(picked) + 3) % 4);
    }
    return CardId::Fog;
}

} // namespace paimon::versus
