#pragma once

#include "../AutobuildTypes.hpp"

#include <cstdint>
#include <vector>

namespace paimon::autobuild {

struct PrefabObservation {
    int piece = -1;
    int sample = -1;
    std::uint8_t context = 0;
};

struct RuleSet {
    std::vector<Links> pieces;
    std::vector<int> pieceOf;
    std::vector<int> sampleOf;
    std::vector<unsigned char> borders;
    std::vector<PrefabObservation> observations;
    bool emptySide[kNeighbourDirections] = {};
};

RuleSet inferRules(Template const& tpl);

} // namespace paimon::autobuild
