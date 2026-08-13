#include "SmartTemplateEngine.hpp"

#include <algorithm>
#include <climits>
#include <unordered_set>

namespace paimon::autobuild {

namespace {

constexpr int kAllSamples = -2;

int rotateDirection(int direction) {
    constexpr int rotated[kNeighbourDirections] = {2, 3, 1, 0, 6, 7, 5, 4};
    return rotated[direction];
}

int flipDirectionX(int direction) {
    constexpr int flipped[kNeighbourDirections] = {0, 1, 3, 2, 7, 6, 5, 4};
    return flipped[direction];
}

template <class Candidate>
bool hasSample(std::vector<Candidate> const& candidates, int sample) {
    return std::any_of(candidates.begin(), candidates.end(), [sample](auto const& candidate) {
        return sample == kAllSamples || candidate.sample == sample;
    });
}

} // namespace

std::uint8_t transformContext(std::uint8_t context, PieceTransform transform) {
    std::uint8_t result = 0;
    int turns = transform.quarterTurns % 4;
    for (int direction = 0; direction < kNeighbourDirections; ++direction) {
        if ((context & (1 << direction)) == 0) continue;
        int mapped = direction;
        for (int turn = 0; turn < turns; ++turn) mapped = rotateDirection(mapped);
        if (transform.flipX) mapped = flipDirectionX(mapped);
        result |= static_cast<std::uint8_t>(1 << mapped);
    }
    return result;
}

int contextDistance(std::uint8_t first, std::uint8_t second) {
    int distance = 0;
    int difference = first ^ second;
    for (int direction = 0; direction < kNeighbourDirections; ++direction) {
        if ((difference & (1 << direction)) != 0) {
            distance += direction < kCardinalDirections ? 4 : 1;
        }
    }
    return distance;
}

SmartTemplateEngine::SmartTemplateEngine(Template const& tpl, RuleSet const& rules,
                                         int preferredSample, bool allowRotation,
                                         bool allowFlip)
  : m_preferredSample(preferredSample) {
    std::vector<int> occurrences(tpl.pieces.size(), 0);
    for (auto const& observation : rules.observations) {
        if (observation.piece >= 0 &&
            observation.piece < static_cast<int>(occurrences.size())) {
            occurrences[observation.piece]++;
        }
    }

    for (auto const& observation : rules.observations) {
        if (observation.piece < 0 ||
            observation.piece >= static_cast<int>(tpl.pieces.size()) ||
            observation.sample < 0 || occurrences[observation.piece] == 0) {
            continue;
        }

        double const weight = static_cast<double>(
            std::max(1, tpl.pieces[observation.piece].weight)) /
            occurrences[observation.piece];
        Candidate const direct{observation.piece, observation.sample, {}, weight};
        m_direct[observation.context].push_back(direct);
        m_hasCandidates = true;

        auto addRemap = [&](PieceTransform transform) {
            auto const mapped = transformContext(observation.context, transform);
            if (mapped == observation.context) return;
            m_remapped[mapped].push_back(
                {observation.piece, observation.sample, transform, weight});
        };

        if (allowRotation) {
            for (unsigned char turns = 1; turns < 4; ++turns) {
                addRemap({turns, false});
            }
        }
        if (allowFlip) {
            int const rotations = allowRotation ? 4 : 1;
            for (unsigned char turns = 0; turns < rotations; ++turns) {
                addRemap({turns, true});
            }
        }
    }
}

SmartTemplateEngine::Resolved const& SmartTemplateEngine::resolve(std::uint8_t context) const {
    auto& resolved = m_resolved[context];
    if (resolved.ready) return resolved;
    resolved.ready = true;

    auto appendBucket = [&](std::vector<Candidate> const& bucket, int sample) {
        size_t const before = resolved.candidates.size();
        for (auto const& candidate : bucket) {
            if (sample == kAllSamples || candidate.sample == sample) {
                resolved.candidates.push_back(&candidate);
            }
        }
        return resolved.candidates.size() != before;
    };

    auto tryExact = [&](auto const& catalog, int sample, SmartMatch match) {
        if (!appendBucket(catalog[context], sample)) return false;
        resolved.match = match;
        return true;
    };

    auto tryCardinals = [&](auto const& catalog, int sample) {
        size_t const before = resolved.candidates.size();
        int const cardinals = context & 0x0f;
        for (int key = 0; key < 256; ++key) {
            if ((key & 0x0f) == cardinals) appendBucket(catalog[key], sample);
        }
        if (resolved.candidates.size() == before) return false;
        resolved.match = SmartMatch::Simplified;
        return true;
    };

    auto tryNearest = [&](int sample) {
        int bestScore = INT_MAX;
        for (int key = 0; key < 256; ++key) {
            int const base = contextDistance(context, static_cast<std::uint8_t>(key)) * 4;
            for (int remapped = 0; remapped < 2; ++remapped) {
                auto const& bucket = remapped ? m_remapped[key] : m_direct[key];
                if (bucket.empty()) continue;
                if (hasSample(bucket, sample)) {
                    bestScore = std::min(bestScore, base + remapped);
                }
            }
        }

        if (bestScore == INT_MAX) return false;
        for (int key = 0; key < 256; ++key) {
            int const base = contextDistance(context, static_cast<std::uint8_t>(key)) * 4;
            for (int remapped = 0; remapped < 2; ++remapped) {
                if (base + remapped == bestScore) {
                    appendBucket(remapped ? m_remapped[key] : m_direct[key], sample);
                }
            }
        }
        resolved.match = SmartMatch::Simplified;
        return !resolved.candidates.empty();
    };

    auto tryLadder = [&](int sample) {
        return tryExact(m_direct, sample, SmartMatch::Exact) ||
               tryExact(m_remapped, sample, SmartMatch::Remapped) ||
               tryCardinals(m_direct, sample) ||
               tryCardinals(m_remapped, sample) || tryNearest(sample);
    };

    bool found = false;
    if (m_preferredSample >= 0) found = tryLadder(m_preferredSample);
    if (!found) found = tryLadder(kAllSamples);

    if (!found) {
        return resolved;
    }

    std::unordered_set<int> pieces;
    resolved.prefix.reserve(resolved.candidates.size());
    for (auto const* candidate : resolved.candidates) {
        resolved.total += std::max(0.0001, candidate->weight);
        resolved.prefix.push_back(resolved.total);
        pieces.insert(candidate->piece);
    }
    resolved.uniquePieces = static_cast<int>(pieces.size());
    return resolved;
}

SmartChoice SmartTemplateEngine::choose(std::uint8_t context, int fallbackPiece,
                                        int avoidPiece, std::mt19937& rng) const {
    auto const& resolved = resolve(context);
    if (resolved.candidates.empty() || resolved.total <= 0.0) {
        return {fallbackPiece, {}, SmartMatch::Original};
    }

    auto draw = [&]() -> Candidate const* {
        std::uniform_real_distribution<double> dist(0.0, resolved.total);
        double const roll = dist(rng);
        auto found = std::lower_bound(resolved.prefix.begin(), resolved.prefix.end(), roll);
        size_t index = static_cast<size_t>(found - resolved.prefix.begin());
        if (index >= resolved.candidates.size()) index = resolved.candidates.size() - 1;
        return resolved.candidates[index];
    };

    Candidate const* chosen = draw();
    if (avoidPiece >= 0 && resolved.uniquePieces > 1 && chosen->piece == avoidPiece) {
        for (int attempt = 0; attempt < 8 && chosen->piece == avoidPiece; ++attempt) {
            chosen = draw();
        }
        if (chosen->piece == avoidPiece) {
            double alternateTotal = 0.0;
            for (auto const* candidate : resolved.candidates) {
                if (candidate->piece != avoidPiece) {
                    alternateTotal += std::max(0.0001, candidate->weight);
                }
            }
            std::uniform_real_distribution<double> dist(0.0, alternateTotal);
            double const roll = dist(rng);
            double accumulated = 0.0;
            for (auto const* candidate : resolved.candidates) {
                if (candidate->piece == avoidPiece) continue;
                accumulated += std::max(0.0001, candidate->weight);
                if (roll <= accumulated) {
                    chosen = candidate;
                    break;
                }
            }
        }
    }

    return {chosen->piece, chosen->transform, resolved.match};
}

} // namespace paimon::autobuild
