#include "GifMotionPlanner.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace paimon::gifimport {

namespace {

// Below this a silhouette isn't worth a group plus its triggers:
// repeating it per frame costs less than tracking it.
constexpr std::size_t kMinTrackedCells = 8;
constexpr int kMinTrackedFrames = 3;
constexpr std::size_t kMaxTrackedGroups = 24;
constexpr std::size_t kMaxOpenChains = 12;
constexpr std::size_t kMaxOffsetsPerChain = 4;
constexpr int kSearchRadius = 2;
constexpr float kMatchRatio = 0.6f;

struct Component {
    std::vector<int> positions;
    std::vector<std::int32_t> colors;
    float centerX = 0.f;
    float centerY = 0.f;
    bool taken = false;
};

// Reference pose is frame 0; each frame stores its offset against it. `alive`
// unchecks cells some frame drops, so the moving silhouette ends up as what
// every frame shares and the rest stays on the grid, painted as usual.
struct Chain {
    std::vector<int> positions;
    std::vector<std::int32_t> colors;
    std::vector<std::uint8_t> alive;
    std::vector<MotionKey> keys;
    float refCenterX = 0.f;
    float refCenterY = 0.f;
    std::size_t aliveCount = 0;
    bool open = true;
};

std::vector<std::uint8_t> dynamicCells(
    std::vector<GridFrame> const& frames,
    std::size_t cells
) {
    std::vector<std::uint8_t> dynamic(cells, 0);
    for (std::size_t position = 0; position < cells; ++position) {
        auto const first = frames.front().cells[position];
        for (std::size_t frame = 1; frame < frames.size(); ++frame) {
            if (frames[frame].cells[position] == first) continue;
            dynamic[position] = 1;
            break;
        }
    }
    return dynamic;
}

// The most-covering color is the background. Its blobs fit anywhere, so a
// chain following one would jump senselessly and waste a group.
std::int32_t dominantColor(GridFrame const& frame) {
    std::int32_t highest = 0;
    for (auto cell : frame.cells) highest = std::max(highest, cell);
    std::vector<int> tally(static_cast<std::size_t>(highest) + 2, 0);
    for (auto cell : frame.cells) ++tally[static_cast<std::size_t>(cell + 1)];
    std::int32_t dominant = -1;
    int best = 0;
    for (std::size_t i = 0; i < tally.size(); ++i) {
        if (tally[i] <= best) continue;
        dominant = static_cast<std::int32_t>(i) - 1;
        best = tally[i];
    }
    return dominant;
}

std::vector<Component> componentsOf(
    GridFrame const& frame,
    std::vector<std::uint8_t> const& dynamic,
    std::vector<int>& owner,
    int width,
    int height
) {
    auto const dominant = dominantColor(frame);
    owner.assign(dynamic.size(), -1);
    std::vector<Component> components;
    std::vector<std::uint8_t> visited(dynamic.size(), 0);
    std::vector<int> stack;
    for (int start = 0; start < width * height; ++start) {
        auto const index = static_cast<std::size_t>(start);
        if (visited[index] || !dynamic[index] || frame.cells[index] < 0) continue;
        if (frame.cells[index] == dominant) continue;

        Component component;
        double sumX = 0.0;
        double sumY = 0.0;
        stack.push_back(start);
        visited[index] = 1;
        while (!stack.empty()) {
            int const position = stack.back();
            stack.pop_back();
            int const x = position % width;
            int const y = position / width;
            component.positions.push_back(position);
            sumX += x;
            sumY += y;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int const nx = x + dx;
                    int const ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                    auto const neighbour = static_cast<std::size_t>(ny * width + nx);
                    if (visited[neighbour] || !dynamic[neighbour]) continue;
                    if (frame.cells[neighbour] != frame.cells[index]) continue;
                    visited[neighbour] = 1;
                    stack.push_back(ny * width + nx);
                }
            }
        }
        // a blob eating half the grid is background, and background doesn't
        // move: tracking it only spends a group and its triggers.
        if (component.positions.size() < kMinTrackedCells) continue;
        if (component.positions.size() * 5 > dynamic.size() * 2) continue;

        std::sort(component.positions.begin(), component.positions.end());
        component.colors.reserve(component.positions.size());
        for (int position : component.positions) {
            component.colors.push_back(frame.cells[static_cast<std::size_t>(position)]);
        }
        auto const count = static_cast<double>(component.positions.size());
        component.centerX = static_cast<float>(sumX / count);
        component.centerY = static_cast<float>(sumY / count);
        components.push_back(std::move(component));
    }
    std::sort(components.begin(), components.end(),
              [](Component const& left, Component const& right) {
                  return left.positions.size() > right.positions.size();
              });
    for (std::size_t i = 0; i < components.size(); ++i) {
        for (int position : components[i].positions) {
            owner[static_cast<std::size_t>(position)] = static_cast<int>(i);
        }
    }
    return components;
}

std::size_t scoreOffset(
    Chain const& chain,
    GridFrame const& frame,
    int width,
    int height,
    int offsetX,
    int offsetY,
    std::size_t stride,
    std::vector<std::uint8_t>* hit
) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < chain.positions.size(); i += stride) {
        if (hit) (*hit)[i] = 0;
        if (!chain.alive[i]) continue;
        int const x = chain.positions[i] % width + offsetX;
        int const y = chain.positions[i] / width + offsetY;
        if (x < 0 || y < 0 || x >= width || y >= height) continue;
        if (frame.cells[static_cast<std::size_t>(y * width + x)] != chain.colors[i]) continue;
        if (hit) (*hit)[i] = 1;
        ++count;
    }
    return count;
}

Chain startChain(Component const& component) {
    Chain chain;
    chain.positions = component.positions;
    chain.colors = component.colors;
    chain.alive.assign(chain.positions.size(), 1);
    chain.aliveCount = chain.positions.size();
    chain.refCenterX = component.centerX;
    chain.refCenterY = component.centerY;
    chain.keys.push_back({0, 0, 0});
    return chain;
}

bool chainMoves(Chain const& chain, int limit) {
    bool moves = false;
    for (std::size_t i = 1; i < chain.keys.size(); ++i) {
        int const stepX = chain.keys[i].x - chain.keys[i - 1].x;
        int const stepY = chain.keys[i].y - chain.keys[i - 1].y;
        if (std::abs(stepX) > limit || std::abs(stepY) > limit) return false;
        moves = moves || stepX != 0 || stepY != 0;
    }
    return moves;
}

} // namespace

MotionAnalysis analyzeMotion(
    std::vector<GridFrame> const& frames,
    int width,
    int height
) {
    MotionAnalysis analysis;
    int const frameCount = static_cast<int>(frames.size());
    if (frameCount < kMinTrackedFrames || width <= 0 || height <= 0) return analysis;

    auto const cells = static_cast<std::size_t>(width) * height;
    auto const dynamic = dynamicCells(frames, cells);
    int const words = (frameCount + 63) / 64;

    std::vector<Chain> chains;
    std::vector<std::uint8_t> hit;
    std::vector<int> owner;
    for (int frame = 0; frame < frameCount; ++frame) {
        auto components = componentsOf(
            frames[static_cast<std::size_t>(frame)], dynamic, owner, width, height);

        for (auto& chain : chains) {
            if (!chain.open) continue;
            auto const& current = frames[static_cast<std::size_t>(frame)];
            auto const& last = chain.keys.back();
            int velocityX = 0;
            int velocityY = 0;
            if (chain.keys.size() > 1) {
                auto const& previous = chain.keys[chain.keys.size() - 2];
                velocityX = last.x - previous.x;
                velocityY = last.y - previous.y;
            }

            // search starts where it should be if it kept course, plus neighbors:
            // a cell-and-quarter drift never lands on a blob center, and without
            // this sweep the chain stuck to its old spot.
            std::vector<std::pair<int, int>> offsets;
            for (int dy = -kSearchRadius; dy <= kSearchRadius; ++dy) {
                for (int dx = -kSearchRadius; dx <= kSearchRadius; ++dx) {
                    offsets.emplace_back(last.x + velocityX + dx, last.y + velocityY + dy);
                }
            }
            // long jumps miss the sweep, so similar-size blobs propose their own shift.
            std::size_t const swept = offsets.size();
            std::vector<int> sources(swept, -1);
            for (std::size_t i = 0; i < components.size(); ++i) {
                auto const& component = components[i];
                if (component.taken) continue;
                double const ratio = static_cast<double>(component.positions.size()) /
                    static_cast<double>(chain.aliveCount);
                if (ratio < 0.4 || ratio > 2.5) continue;
                offsets.emplace_back(
                    static_cast<int>(std::lround(component.centerX - chain.refCenterX)),
                    static_cast<int>(std::lround(component.centerY - chain.refCenterY)));
                sources.push_back(static_cast<int>(i));
                if (offsets.size() - swept >= kMaxOffsetsPerChain) break;
            }

            std::size_t const stride = std::max<std::size_t>(
                1, chain.positions.size() / 256);
            std::size_t bestCount = 0;
            int bestTravel = 0;
            int bestX = last.x;
            int bestY = last.y;
            int bestComponent = -1;
            for (std::size_t i = 0; i < offsets.size(); ++i) {
                auto const count = scoreOffset(
                    chain, current, width, height,
                    offsets[i].first, offsets[i].second, stride, nullptr);
                int const travel = std::abs(offsets[i].first - last.x) +
                    std::abs(offsets[i].second - last.y);
                if (count < bestCount) continue;
                if (count == bestCount && bestCount > 0 && travel >= bestTravel) continue;
                bestCount = count;
                bestTravel = travel;
                bestX = offsets[i].first;
                bestY = offsets[i].second;
                bestComponent = sources[i];
            }

            hit.assign(chain.positions.size(), 0);
            auto const matched = scoreOffset(
                chain, current, width, height, bestX, bestY, 1, &hit);
            if (matched < kMinTrackedCells ||
                matched < static_cast<std::size_t>(chain.aliveCount * kMatchRatio)) {
                chain.open = false;
                continue;
            }
            chain.alive = hit;
            chain.aliveCount = matched;
            chain.keys.push_back({frame, bestX, bestY});
            if (bestComponent >= 0) {
                components[static_cast<std::size_t>(bestComponent)].taken = true;
            }
            for (std::size_t i = 0; i < chain.positions.size(); ++i) {
                if (!chain.alive[i]) continue;
                int const x = chain.positions[i] % width + bestX;
                int const y = chain.positions[i] / width + bestY;
                int const found = owner[static_cast<std::size_t>(y * width + x)];
                if (found >= 0) components[static_cast<std::size_t>(found)].taken = true;
            }
        }

        if (frame > 0) continue;
        for (auto& component : components) {
            if (chains.size() >= kMaxOpenChains) break;
            chains.push_back(startChain(component));
        }
    }

    std::vector<MotionGroup> merged;
    std::vector<std::vector<std::int64_t>> signatures;
    for (auto const& chain : chains) {
        // only what every frame shares is tracked. The fill the silhouette
        // leaves on the grid holds for the whole animation; a missing frame
        // would show background there instead.
        if (static_cast<int>(chain.keys.size()) != frameCount) continue;
        if (chain.aliveCount < kMinTrackedCells) continue;
        // untracked leftovers get eaten by the background, so track only while
        // that's the edge and not half the drawing.
        if (chain.aliveCount * 5 < chain.positions.size() * 4) continue;
        if (!chainMoves(chain, std::max(width, height) / 3)) continue;

        std::vector<std::int64_t> signature;
        signature.reserve(chain.keys.size() * 3);
        for (auto const& key : chain.keys) {
            signature.push_back(key.frame);
            signature.push_back(key.x);
            signature.push_back(key.y);
        }
        auto const match = std::find(signatures.begin(), signatures.end(), signature);
        std::size_t index = static_cast<std::size_t>(match - signatures.begin());
        if (match == signatures.end()) {
            signatures.push_back(std::move(signature));
            merged.emplace_back();
            auto& group = merged.back();
            group.keys = chain.keys;
            group.mask.assign(static_cast<std::size_t>(words), 0);
            for (auto const& key : chain.keys) {
                group.mask[static_cast<std::size_t>(key.frame / 64)] |=
                    std::uint64_t{1} << (key.frame % 64);
            }
        }
        for (std::size_t i = 0; i < chain.positions.size(); ++i) {
            if (!chain.alive[i]) continue;
            merged[index].positions.push_back(chain.positions[i]);
            merged[index].colors.push_back(chain.colors[i]);
        }
    }

    std::vector<std::size_t> order(merged.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        return merged[left].positions.size() * merged[left].keys.size() >
            merged[right].positions.size() * merged[right].keys.size();
    });
    if (order.size() > kMaxTrackedGroups) order.resize(kMaxTrackedGroups);
    if (order.empty()) return analysis;

    std::vector<std::vector<std::uint8_t>> covered(
        static_cast<std::size_t>(frameCount), std::vector<std::uint8_t>(cells, 0));
    std::vector<std::uint8_t> touched(cells, 0);
    for (auto index : order) {
        for (auto const& key : merged[index].keys) {
            for (int position : merged[index].positions) {
                int const x = position % width + key.x;
                int const y = position / width + key.y;
                auto const cell = static_cast<std::size_t>(y * width + x);
                covered[static_cast<std::size_t>(key.frame)][cell] = 1;
                touched[cell] = 1;
            }
        }
        analysis.groups.push_back(std::move(merged[index]));
    }

    // background under the silhouette rebuilds from each cell's color in clear
    // frames: uniform again everywhere, painted by fixed objects instead of
    // one track per frame.
    analysis.residual = frames;
    std::int32_t highest = 0;
    for (auto const& frame : frames) {
        for (auto cell : frame.cells) highest = std::max(highest, cell);
    }
    std::vector<int> tally(static_cast<std::size_t>(highest) + 2, 0);
    std::vector<std::int32_t> seen;
    for (std::size_t position = 0; position < cells; ++position) {
        if (!touched[position]) continue;
        seen.clear();
        for (int frame = 0; frame < frameCount; ++frame) {
            if (covered[static_cast<std::size_t>(frame)][position]) continue;
            auto const color = frames[static_cast<std::size_t>(frame)].cells[position];
            auto& count = tally[static_cast<std::size_t>(color + 1)];
            if (count == 0) seen.push_back(color);
            ++count;
        }
        std::int32_t plate = -1;
        int best = 0;
        for (auto color : seen) {
            int const count = tally[static_cast<std::size_t>(color + 1)];
            tally[static_cast<std::size_t>(color + 1)] = 0;
            if (count <= best) continue;
            plate = color;
            best = count;
        }

        // fill lands in every frame, not just under the silhouette: that's what
        // stops the cell changing so a fixed object paints it. The cost is the
        // silhouette edge, reading as background on shifted frames; plan
        // comparison decides whether that trade pays.
        for (auto& frame : analysis.residual) frame.cells[position] = plate;
    }
    return analysis;
}

std::size_t motionMoveCount(
    std::vector<MotionTrack> const& tracks,
    int frames,
    bool loop
) {
    std::size_t count = 0;
    for (auto const& track : tracks) {
        int const last = loop ? frames - 1 : frames - 2;
        for (int frame = 0; frame <= last; ++frame) {
            int const next = frame + 1 >= frames ? 0 : frame + 1;
            auto const* from = keyAt(track, frame);
            auto const* to = keyAt(track, next);
            if (from && to) {
                if (from->x != to->x || from->y != to->y) ++count;
            } else if (from && loop && (from->x != 0 || from->y != 0)) {
                ++count;
            }
        }
    }
    return count;
}

std::size_t motionTriggerCount(
    std::vector<MotionTrack> const& tracks,
    int frames,
    bool loop
) {
    std::size_t count = motionMoveCount(tracks, frames, loop);
    for (auto const& track : tracks) {
        if (!visibleAt(track.mask, 0)) ++count;
        for (int frame = 1; frame < frames; ++frame) {
            if (visibleAt(track.mask, frame) != visibleAt(track.mask, frame - 1)) ++count;
        }
        if (loop && visibleAt(track.mask, frames - 1) != visibleAt(track.mask, 0)) ++count;
    }
    return count;
}

} // namespace paimon::gifimport
