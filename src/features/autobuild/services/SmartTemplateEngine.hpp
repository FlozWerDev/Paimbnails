#pragma once

#include "RuleInference.hpp"

#include <array>
#include <cstdint>
#include <random>
#include <vector>

namespace paimon::autobuild {

enum class SmartMatch { Original, Exact, Remapped, Simplified };

struct SmartChoice {
    int piece = -1;
    PieceTransform transform;
    SmartMatch match = SmartMatch::Original;
};

std::uint8_t transformContext(std::uint8_t context, PieceTransform transform);
int contextDistance(std::uint8_t first, std::uint8_t second);

class SmartTemplateEngine {
public:
    SmartTemplateEngine(Template const& tpl, RuleSet const& rules, int preferredSample,
                        bool allowRotation, bool allowFlip);

    bool empty() const { return !m_hasCandidates; }
    SmartChoice choose(std::uint8_t context, int fallbackPiece, int avoidPiece,
                       std::mt19937& rng) const;

private:
    struct Candidate {
        int piece = -1;
        int sample = -1;
        PieceTransform transform;
        double weight = 1.0;
    };

    struct Resolved {
        bool ready = false;
        SmartMatch match = SmartMatch::Original;
        std::vector<Candidate const*> candidates;
        std::vector<double> prefix;
        double total = 0.0;
        int uniquePieces = 0;
    };

    Resolved const& resolve(std::uint8_t context) const;

    int m_preferredSample = -1;
    bool m_hasCandidates = false;
    std::array<std::vector<Candidate>, 256> m_direct;
    std::array<std::vector<Candidate>, 256> m_remapped;
    mutable std::array<Resolved, 256> m_resolved;
};

} // namespace paimon::autobuild
