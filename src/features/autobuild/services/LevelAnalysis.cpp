#include "LevelAnalysis.hpp"

#include "PieceGrid.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <unordered_map>

namespace paimon::autobuild {

namespace {

constexpr float kGridStep = 30.f;
constexpr float kGridPhase = 15.f;
constexpr float kAlignTolerance = 1.5f;
constexpr size_t kMaxMotifInstances = 256;

enum class Depth : unsigned char { Back, Play, Front };

bool behindLayer(int z) {
    return z == kZLayerB1 || z == kZLayerB2 || z == kZLayerB3 || z == kZLayerB4;
}

bool frontLayer(int z) {
    return z == kZLayerT1 || z == kZLayerT2 || z == kZLayerT3;
}

bool backgroundChannel(int channel) {
    return channel == kChannelBG || channel == kChannelLBG || channel == kChannel3DL;
}

// Levels that never touch the Z layer dropdown still push their backdrop back
// with the order field and paint it with a background channel, so both count.
Depth depthOf(LevelObject const& object) {
    if (behindLayer(object.zLayer)) return Depth::Back;
    if (frontLayer(object.zLayer)) return Depth::Front;
    if (backgroundChannel(object.mainColor)) return Depth::Back;
    if (object.zOrder <= -8) return Depth::Back;
    if (object.zOrder >= 8) return Depth::Front;
    return Depth::Play;
}

bool alignedTo(float value, float step, float phase) {
    float const shifted = value - phase;
    float const rest = shifted - std::round(shifted / step) * step;
    return std::abs(rest) <= kAlignTolerance;
}

bool onGrid(LevelObject const& object) {
    return alignedTo(object.x, kGridStep, kGridPhase) &&
           alignedTo(object.y, kGridStep, kGridPhase);
}

bool axisAligned(float rotation) {
    float const rest = rotation - std::round(rotation / 90.f) * 90.f;
    return std::abs(rest) <= 1.f;
}

std::uint64_t packCell(int x, int y) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
           static_cast<std::uint32_t>(y);
}

struct Classified {
    ObjectKind kind = ObjectKind::Unknown;
    Depth depth = Depth::Play;
    bool trigger = false;
};

std::vector<Classified> classifyObjects(LevelData const& data) {
    std::vector<Classified> out;
    out.reserve(data.objects.size());
    for (auto const& object : data.objects) {
        Classified entry;
        entry.kind = kindOf(object.id);
        entry.depth = depthOf(object);
        entry.trigger = entry.kind == ObjectKind::Trigger || looksLikeTrigger(object);
        if (entry.trigger) entry.kind = ObjectKind::Trigger;
        out.push_back(entry);
    }
    return out;
}

// Objects group by proximity, but only within the same depth band: a backdrop
// shape drawn across a platform must not swallow the platform.
std::vector<std::vector<int>> segment(LevelData const& data,
                                      std::vector<Classified> const& kinds,
                                      AnalysisOptions const& opts) {
    float const radius = std::max(10.f, opts.linkRadius);
    std::vector<std::vector<int>> regions;
    std::vector<char> taken(data.objects.size(), 0);

    auto bucketKey = [&](int index) {
        auto const& object = data.objects[index];
        return packCell(static_cast<int>(std::floor(object.x / radius)),
                        static_cast<int>(std::floor(object.y / radius)));
    };

    // One bucket grid per band keeps the neighbour walk linear and stops the
    // bands from linking through each other.
    for (int band = 0; band < 4; ++band) {
        std::unordered_map<std::uint64_t, std::vector<int>> buckets;
        std::vector<int> members;
        for (size_t i = 0; i < data.objects.size(); ++i) {
            int objectBand = kinds[i].trigger ? 3 : static_cast<int>(kinds[i].depth);
            if (!opts.splitByDepth && objectBand != 3) objectBand = 1;
            if (objectBand != band) continue;
            members.push_back(static_cast<int>(i));
            buckets[bucketKey(static_cast<int>(i))].push_back(static_cast<int>(i));
        }
        if (members.empty()) continue;

        std::vector<int> queue;
        for (int start : members) {
            if (taken[start]) continue;
            taken[start] = 1;
            queue.assign(1, start);

            std::vector<int> region;
            while (!queue.empty()) {
                int index = queue.back();
                queue.pop_back();
                region.push_back(index);

                auto const& origin = data.objects[index];
                int bx = static_cast<int>(std::floor(origin.x / radius));
                int by = static_cast<int>(std::floor(origin.y / radius));
                for (int ox = -1; ox <= 1; ++ox) {
                    for (int oy = -1; oy <= 1; ++oy) {
                        auto bucket = buckets.find(packCell(bx + ox, by + oy));
                        if (bucket == buckets.end()) continue;
                        for (int candidate : bucket->second) {
                            if (taken[candidate]) continue;
                            auto const& other = data.objects[candidate];
                            if (std::abs(other.x - origin.x) > radius) continue;
                            if (std::abs(other.y - origin.y) > radius) continue;
                            taken[candidate] = 1;
                            queue.push_back(candidate);
                        }
                    }
                }
            }
            std::sort(region.begin(), region.end());
            regions.push_back(std::move(region));
            if (static_cast<int>(regions.size()) >= opts.maxRegions) return regions;
        }
    }
    return regions;
}

RegionMetrics measureRegion(LevelData const& data, std::vector<Classified> const& kinds,
                            std::vector<int> const& objects) {
    RegionMetrics metrics;
    metrics.objects = static_cast<int>(objects.size());
    metrics.minX = std::numeric_limits<float>::max();
    metrics.minY = std::numeric_limits<float>::max();
    metrics.maxX = std::numeric_limits<float>::lowest();
    metrics.maxY = std::numeric_limits<float>::lowest();

    int aligned = 0;
    int rotated = 0;
    int behind = 0;
    int front = 0;
    int tinted = 0;
    int lowDetail = 0;
    float scaleSum = 0.f;
    for (int index : objects) {
        auto const& object = data.objects[index];
        auto const& entry = kinds[index];

        metrics.minX = std::min(metrics.minX, object.x);
        metrics.minY = std::min(metrics.minY, object.y);
        metrics.maxX = std::max(metrics.maxX, object.x);
        metrics.maxY = std::max(metrics.maxY, object.y);

        switch (entry.kind) {
            case ObjectKind::Solid:
            case ObjectKind::Slope:       metrics.solids++; break;
            case ObjectKind::Hazard:      metrics.hazards++; break;
            case ObjectKind::Portal:
            case ObjectKind::Pad:
            case ObjectKind::Orb:
            case ObjectKind::Collectible: metrics.gameplay++; break;
            case ObjectKind::Trigger:     metrics.triggers++; break;
            default: break;
        }
        if (entry.kind != ObjectKind::Unknown) metrics.known++;

        if (onGrid(object)) ++aligned;
        if (!axisAligned(object.rotation)) ++rotated;
        if (entry.depth == Depth::Back) ++behind;
        if (entry.depth == Depth::Front) ++front;
        if (backgroundChannel(object.mainColor) || backgroundChannel(object.detailColor)) ++tinted;
        if (object.highDetail) ++lowDetail;
        scaleSum += (std::abs(object.scaleX) + std::abs(object.scaleY)) / 2.f;
    }

    float const total = static_cast<float>(std::max(1, metrics.objects));
    metrics.gridAligned = aligned / total;
    metrics.rotated = rotated / total;
    metrics.behind = behind / total;
    metrics.front = front / total;
    metrics.bgTinted = tinted / total;
    metrics.lowDetail = lowDetail / total;
    metrics.meanScale = scaleSum / total;

    float const cells = (metrics.width() / kGridStep + 1.f) * (metrics.height() / kGridStep + 1.f);
    metrics.density = std::min(1.f, metrics.objects / std::max(1.f, cells));
    return metrics;
}

void scoreRegion(RegionMetrics& metrics) {
    float const total = static_cast<float>(std::max(1, metrics.objects));
    float const playable = (metrics.solids + metrics.hazards + metrics.gameplay) / total;
    float const play = std::clamp(1.f - metrics.behind - metrics.front, 0.f, 1.f);

    // How much the shape looks like built geometry, before asking where it sits.
    float shape = 0.f;
    shape += 0.34f * metrics.gridAligned;
    shape += 0.30f * std::min(1.f, playable * 2.f);
    shape += 0.18f * (1.f - metrics.rotated);
    shape += 0.18f * std::clamp(1.f - std::abs(metrics.meanScale - 1.f), 0.f, 1.f);
    // Depth is not one signal among many: a shape the author pushed behind the
    // player is backdrop no matter how much it looks like a platform.
    metrics.structureScore = shape * (0.35f + 0.65f * play);

    float background = 0.f;
    background += 0.45f * metrics.behind;
    background += 0.15f * metrics.bgTinted;
    background += 0.12f * std::clamp((metrics.meanScale - 1.f) / 3.f, 0.f, 1.f);
    background += 0.08f * metrics.rotated;
    background += 0.08f * (1.f - metrics.gridAligned);
    background += 0.08f * metrics.lowDetail;
    background += 0.08f * (1.f - metrics.density);
    if (playable == 0.f) background += 0.10f;
    metrics.backgroundScore = background;
}

RegionKind decideKind(RegionMetrics const& metrics) {
    float const total = static_cast<float>(std::max(1, metrics.objects));
    if (metrics.triggers / total > 0.7f) return RegionKind::Logic;
    if (metrics.front > 0.6f) return RegionKind::Foreground;
    // Only gameplay furniture overrides an explicit background layer: nobody
    // puts a spike or a portal in the backdrop.
    if (metrics.behind >= 0.8f && metrics.hazards + metrics.gameplay == 0) {
        return RegionKind::Background;
    }
    if (metrics.hazards > 0 && metrics.hazards / total >= 0.6f) return RegionKind::Hazard;
    if (metrics.backgroundScore > metrics.structureScore) return RegionKind::Background;
    if (metrics.solids + metrics.hazards + metrics.gameplay > 0 &&
        metrics.structureScore >= 0.5f) {
        return RegionKind::Structure;
    }
    return RegionKind::Decoration;
}

std::string regionSignature(LevelData const& data, std::vector<int> const& objects,
                            float minX, float minY) {
    std::vector<std::string> parts;
    parts.reserve(objects.size());
    char buffer[128];
    for (int index : objects) {
        auto const& object = data.objects[index];
        std::snprintf(buffer, sizeof(buffer), "%d:%.1f:%.1f:%.1f:%.2f:%.2f:%d%d:%d:%d",
                      object.id, object.x - minX, object.y - minY, object.rotation,
                      object.scaleX, object.scaleY, object.flipX ? 1 : 0,
                      object.flipY ? 1 : 0, object.mainColor, object.detailColor);
        parts.emplace_back(buffer);
    }
    std::sort(parts.begin(), parts.end());
    std::string out;
    out.reserve(parts.size() * 40);
    for (auto const& part : parts) {
        out += part;
        out += ';';
    }
    return out;
}

void findFamilies(LevelData const& data, std::vector<Region>& regions) {
    std::unordered_map<std::string, int> bySignature;
    std::vector<int> counts;
    for (auto& region : regions) {
        auto signature = regionSignature(data, region.objects, region.metrics.minX,
                                         region.metrics.minY);
        auto found = bySignature.find(signature);
        if (found == bySignature.end()) {
            int const family = static_cast<int>(counts.size());
            bySignature.emplace(std::move(signature), family);
            counts.push_back(1);
            region.family = family;
        } else {
            region.family = found->second;
            counts[found->second]++;
        }
    }
    for (auto& region : regions) {
        region.repeats = region.family >= 0 ? counts[region.family] : 1;
    }
}

// One connected blob: reusable shapes only emerge from each object's neighbourhood.
void mineMotifs(LevelData const& data, LevelReport& report, AnalysisOptions const& opts) {
    float const radius = std::max(30.f, opts.motifRadius);
    size_t const parentCount = report.regions.size();
    int nextFamily = 0;
    for (auto const& region : report.regions) nextFamily = std::max(nextFamily, region.family + 1);

    struct Instance {
        int parent = 0;
        std::vector<int> objects;
    };
    std::unordered_map<std::string, std::vector<Instance>> byShape;

    for (size_t r = 0; r < parentCount; ++r) {
        auto const& region = report.regions[r];
        if (region.kind == RegionKind::Logic) continue;
        if (static_cast<int>(region.objects.size()) < opts.minMotifObjects * 3) continue;

        std::unordered_map<std::uint64_t, std::vector<int>> buckets;
        for (int index : region.objects) {
            auto const& object = data.objects[index];
            buckets[packCell(static_cast<int>(std::floor(object.x / radius)),
                             static_cast<int>(std::floor(object.y / radius)))].push_back(index);
        }

        for (int seed : region.objects) {
            auto const& origin = data.objects[seed];
            std::vector<int> neighbours;
            int const bx = static_cast<int>(std::floor(origin.x / radius));
            int const by = static_cast<int>(std::floor(origin.y / radius));
            for (int ox = -1; ox <= 1 && static_cast<int>(neighbours.size()) <= 48; ++ox) {
                for (int oy = -1; oy <= 1; ++oy) {
                    auto bucket = buckets.find(packCell(bx + ox, by + oy));
                    if (bucket == buckets.end()) continue;
                    for (int candidate : bucket->second) {
                        auto const& other = data.objects[candidate];
                        if (std::abs(other.x - origin.x) > radius) continue;
                        if (std::abs(other.y - origin.y) > radius) continue;
                        neighbours.push_back(candidate);
                    }
                }
            }
            if (static_cast<int>(neighbours.size()) < opts.minMotifObjects ||
                neighbours.size() > 48) {
                continue;
            }
            std::sort(neighbours.begin(), neighbours.end());

            float minX = std::numeric_limits<float>::max();
            float minY = std::numeric_limits<float>::max();
            for (int index : neighbours) {
                minX = std::min(minX, data.objects[index].x);
                minY = std::min(minY, data.objects[index].y);
            }
            byShape[regionSignature(data, neighbours, minX, minY)]
                .push_back({static_cast<int>(r), std::move(neighbours)});
        }
    }

    // Rare ids weigh more so motifs aren't buried under the most-used tile.
    std::unordered_map<int, int> idCount;
    for (auto const& object : data.objects) idCount[object.id]++;
    double const corpus = static_cast<double>(std::max<size_t>(1, data.objects.size()));
    auto rarity = [&](int id) {
        auto found = idCount.find(id);
        int const seen = found == idCount.end() ? 1 : std::max(1, found->second);
        return std::log(corpus / seen) + 0.05;
    };

    struct Motif {
        std::string const* shape = nullptr;
        std::vector<Instance> const* instances = nullptr;
        double weight = 0.0;
    };
    std::vector<Motif> motifs;
    for (auto const& [shape, instances] : byShape) {
        if (static_cast<int>(instances.size()) < opts.minMotifRepeats) continue;
        double value = 0.0;
        for (int index : instances.front().objects) value += rarity(data.objects[index].id);
        motifs.push_back({&shape, &instances, value * static_cast<double>(instances.size())});
    }
    std::sort(motifs.begin(), motifs.end(), [](Motif const& a, Motif const& b) {
        if (a.weight != b.weight) return a.weight > b.weight;
        return *a.shape < *b.shape;
    });

    std::vector<char> covered(data.objects.size(), 0);
    int kept = 0;
    for (auto const& motif : motifs) {
        if (kept >= opts.maxMotifs) break;
        // A window shifted by one cell over the same tiling is not a second
        // building block, so drop a shape whose first instance is already there.
        int already = 0;
        for (int index : motif.instances->front().objects) already += covered[index] ? 1 : 0;
        if (already * 10 >= static_cast<int>(motif.instances->front().objects.size()) * 7) {
            continue;
        }

        // Instances of one motif may not overlap each other, but two different
        // motifs are allowed to: a pillar standing on a tiled floor is both.
        std::vector<char> claimed(data.objects.size(), 0);
        std::vector<size_t> accepted;
        for (auto const& instance : *motif.instances) {
            if (accepted.size() >= kMaxMotifInstances) break;
            if (static_cast<int>(report.regions.size()) >= opts.maxRegions) break;
            bool free = true;
            for (int index : instance.objects) {
                if (claimed[index]) { free = false; break; }
            }
            if (!free) continue;
            for (int index : instance.objects) claimed[index] = 1;
            accepted.push_back(report.regions.size());

            Region derived;
            derived.motif = true;
            derived.parent = instance.parent;
            derived.family = nextFamily;
            derived.objects = instance.objects;
            derived.kind = report.regions[instance.parent].kind;
            report.regions.push_back(std::move(derived));
        }
        if (static_cast<int>(accepted.size()) < opts.minMotifRepeats) {
            // Overlap ate the repeats, so it was never a reusable shape.
            if (!accepted.empty()) report.regions.resize(accepted.front());
            continue;
        }
        for (size_t index : accepted) {
            report.regions[index].repeats = static_cast<int>(accepted.size());
            for (int object : report.regions[index].objects) covered[object] = 1;
        }
        ++nextFamily;
        ++kept;
    }
}

void buildPalette(LevelData const& data, LevelReport& report) {
    auto channels = parseColorChannels(data.colors);
    std::unordered_map<int, size_t> byId;
    for (auto const& channel : channels) {
        PaletteEntry entry;
        entry.channel = channel;
        byId.emplace(channel.id, report.palette.size());
        report.palette.push_back(entry);
    }

    auto touch = [&](int channelId, bool detail, RegionKind kind) {
        if (channelId <= 0) return;
        auto found = byId.find(channelId);
        if (found == byId.end()) {
            PaletteEntry entry;
            entry.channel.id = channelId;
            found = byId.emplace(channelId, report.palette.size()).first;
            report.palette.push_back(entry);
        }
        auto& entry = report.palette[found->second];
        if (detail) entry.detailUses++;
        else entry.mainUses++;
        entry.usesByKind[static_cast<int>(kind)]++;
    };

    for (auto const& region : report.regions) {
        for (int index : region.objects) {
            auto const& object = data.objects[index];
            touch(object.mainColor, false, region.kind);
            touch(object.detailColor, true, region.kind);
        }
    }

    for (auto& entry : report.palette) {
        int best = 0;
        for (int kind = 1; kind < kRegionKinds; ++kind) {
            if (entry.usesByKind[kind] > entry.usesByKind[best]) best = kind;
        }
        entry.role = static_cast<RegionKind>(best);
    }
    std::sort(report.palette.begin(), report.palette.end(),
              [](PaletteEntry const& a, PaletteEntry const& b) {
                  if (a.uses() != b.uses()) return a.uses() > b.uses();
                  return a.channel.id < b.channel.id;
              });
}

char glyphFor(ObjectKind kind, Depth depth) {
    switch (kind) {
        case ObjectKind::Hazard:      return '^';
        case ObjectKind::Solid:
        case ObjectKind::Slope:       return '#';
        case ObjectKind::Portal:      return 'P';
        case ObjectKind::Pad:         return 'p';
        case ObjectKind::Orb:         return 'o';
        case ObjectKind::Collectible: return 'c';
        case ObjectKind::Trigger:     return 't';
        case ObjectKind::Text:        return 'T';
        default: break;
    }
    if (depth == Depth::Back) return ':';
    if (depth == Depth::Front) return '+';
    return '*';
}

int glyphRank(char glyph) {
    switch (glyph) {
        case '^': return 6;
        case 'P': return 5;
        case 'p': return 5;
        case 'o': return 5;
        case 'c': return 4;
        case '#': return 3;
        case 'T': return 2;
        case '*': return 2;
        case '+': return 1;
        case ':': return 1;
        case 't': return 0;
        default:  return -1;
    }
}

std::string renderGrid(std::vector<char> const& cells, int width, int height) {
    std::string out;
    out.reserve(static_cast<size_t>(width + 1) * height);
    for (int row = height - 1; row >= 0; --row) {
        for (int column = 0; column < width; ++column) {
            char const glyph = cells[static_cast<size_t>(row) * width + column];
            out += glyph == 0 ? ' ' : glyph;
        }
        out += '\n';
    }
    return out;
}

// Bucket points into a width x height character grid, keeping the most telling
// glyph when several objects share a cell.
struct SketchCanvas {
    std::vector<char> cells;
    int width = 0;
    int height = 0;
    float minX = 0.f;
    float minY = 0.f;
    float spanX = 1.f;
    float spanY = 1.f;

    SketchCanvas(int w, int h, float x0, float y0, float x1, float y1)
      : cells(static_cast<size_t>(std::max(1, w)) * std::max(1, h), 0),
        width(std::max(1, w)), height(std::max(1, h)), minX(x0), minY(y0),
        spanX(std::max(1.f, x1 - x0)), spanY(std::max(1.f, y1 - y0)) {}

    void plot(float x, float y, char glyph) {
        int const column = std::clamp(static_cast<int>((x - minX) / spanX * (width - 1) + 0.5f),
                                      0, width - 1);
        int const row = std::clamp(static_cast<int>((y - minY) / spanY * (height - 1) + 0.5f),
                                   0, height - 1);
        auto& cell = cells[static_cast<size_t>(row) * width + column];
        if (glyphRank(glyph) > glyphRank(cell)) cell = glyph;
    }

    std::string render() const { return renderGrid(cells, width, height); }
};

} // namespace

char const* regionKindName(RegionKind kind) {
    switch (kind) {
        case RegionKind::Structure:  return "estructura";
        case RegionKind::Hazard:     return "pinchos";
        case RegionKind::Decoration: return "decoracion";
        case RegionKind::Background: return "fondo";
        case RegionKind::Foreground: return "primer plano";
        case RegionKind::Logic:      return "triggers";
    }
    return "decoracion";
}

std::string LevelReport::summary() const {
    char buffer[192];
    std::snprintf(buffer, sizeof(buffer),
                  "%d objetos - %d estructuras - %d fondos - %d decoraciones - %d triggers",
                  objectCount, counts[static_cast<int>(RegionKind::Structure)],
                  counts[static_cast<int>(RegionKind::Background)],
                  counts[static_cast<int>(RegionKind::Decoration)],
                  counts[static_cast<int>(RegionKind::Logic)]);
    return buffer;
}

LevelReport analyzeLevel(LevelData const& data, AnalysisOptions const& opts) {
    LevelReport report;
    report.objectCount = static_cast<int>(data.objects.size());
    report.truncated = data.truncated;
    report.colors = data.colors;
    if (data.objects.empty()) return report;

    auto const kinds = classifyObjects(data);
    auto groups = segment(data, kinds, opts);

    report.regions.reserve(groups.size());
    for (auto& objects : groups) {
        if (static_cast<int>(objects.size()) < opts.minRegionObjects) continue;
        Region region;
        region.objects = std::move(objects);
        region.metrics = measureRegion(data, kinds, region.objects);
        scoreRegion(region.metrics);
        region.kind = decideKind(region.metrics);
        float const top = std::max(region.metrics.structureScore, region.metrics.backgroundScore);
        region.confidence = top <= 0.f ? 0.f
            : std::abs(region.metrics.structureScore - region.metrics.backgroundScore) / top;
        report.regions.push_back(std::move(region));
    }
    findFamilies(data, report.regions);

    for (auto const& region : report.regions) {
        report.counts[static_cast<int>(region.kind)]++;
        report.objectsByKind[static_cast<int>(region.kind)] += region.metrics.objects;
    }

    if (opts.findMotifs) {
        size_t const before = report.regions.size();
        mineMotifs(data, report, opts);
        for (size_t i = before; i < report.regions.size(); ++i) {
            auto& derived = report.regions[i];
            derived.metrics = measureRegion(data, kinds, derived.objects);
            scoreRegion(derived.metrics);
            derived.confidence = report.regions[derived.parent].confidence;
        }
    }

    float maxX = 0.f;
    std::vector<float> solidRows;
    for (size_t i = 0; i < data.objects.size(); ++i) {
        auto const& object = data.objects[i];
        maxX = std::max(maxX, object.x);
        if (kinds[i].kind == ObjectKind::Solid || kinds[i].kind == ObjectKind::Slope) {
            solidRows.push_back(object.y);
        }
        auto const kind = kinds[i].kind;
        if (kind == ObjectKind::Portal || kind == ObjectKind::Pad || kind == ObjectKind::Orb ||
            kind == ObjectKind::Collectible) {
            report.beats.push_back({object.x, object.y, object.id, kind});
        }
    }
    report.lengthX = maxX;
    if (!solidRows.empty()) {
        auto const pick = solidRows.begin() + solidRows.size() / 10;
        std::nth_element(solidRows.begin(), pick, solidRows.end());
        report.groundY = *pick;
    }
    std::sort(report.beats.begin(), report.beats.end(),
              [](GameplayBeat const& a, GameplayBeat const& b) { return a.x < b.x; });

    buildPalette(data, report);
    return report;
}

std::vector<TemplateSuggestion> suggestTemplates(LevelData const& data,
                                                 LevelReport const& report,
                                                 int maxSuggestions) {
    std::unordered_map<int, TemplateSuggestion> byFamily;
    for (size_t i = 0; i < report.regions.size(); ++i) {
        auto const& region = report.regions[i];
        if (region.kind == RegionKind::Logic) continue;
        if (region.metrics.objects < 2) continue;

        auto& suggestion = byFamily[region.family];
        if (suggestion.regions.empty()) {
            suggestion.kind = region.kind;
            suggestion.repeats = region.repeats;
            suggestion.objects = region.metrics.objects;
            // A shape that spans several cells and sits on the grid tiles well;
            // a loose ornament is better dropped whole.
            bool const tileable = region.metrics.gridAligned >= 0.6f &&
                                  region.metrics.objects >= 4 &&
                                  (region.metrics.width() >= 60.f ||
                                   region.metrics.height() >= 60.f);
            suggestion.mode = tileable ? Mode::Wave : Mode::Stamp;
        }
        suggestion.regions.push_back(static_cast<int>(i));
    }

    std::vector<TemplateSuggestion> out;
    out.reserve(byFamily.size());
    for (auto& [family, suggestion] : byFamily) {
        (void)family;
        // Repeats are what make a shape a building block; size decides between
        // two shapes that repeat the same number of times.
        suggestion.score = static_cast<float>(suggestion.repeats) * 4.f +
                           std::log2(static_cast<float>(suggestion.objects) + 1.f);
        if (suggestion.kind == RegionKind::Structure) suggestion.score += 6.f;
        if (suggestion.kind == RegionKind::Background) suggestion.score += 2.f;
        out.push_back(std::move(suggestion));
    }
    std::sort(out.begin(), out.end(), [](TemplateSuggestion const& a, TemplateSuggestion const& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.objects > b.objects;
    });
    if (static_cast<int>(out.size()) > maxSuggestions) out.resize(maxSuggestions);

    int counters[kRegionKinds] = {};
    char buffer[96];
    for (auto& suggestion : out) {
        int const index = ++counters[static_cast<int>(suggestion.kind)];
        if (suggestion.repeats > 1) {
            std::snprintf(buffer, sizeof(buffer), "%s %d (x%d)",
                          regionKindName(suggestion.kind), index, suggestion.repeats);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%s %d",
                          regionKindName(suggestion.kind), index);
        }
        suggestion.name = buffer;
        suggestion.name[0] = static_cast<char>(std::toupper(
            static_cast<unsigned char>(suggestion.name[0])));
    }
    (void)data;
    return out;
}

Template templateFrom(LevelData const& data, LevelReport const& report,
                      TemplateSuggestion const& suggestion, float cell) {
    float const cellSize = cell > 0.f ? cell : 30.f;
    Template tpl;
    tpl.name = suggestion.name;
    tpl.colors = report.colors;
    tpl.cell = cellSize;
    if (suggestion.regions.empty()) return tpl;

    // Every region of a family is the same shape, so one of them carries all the
    // information; the rest only raise how often it was seen.
    auto const& region = report.regions[suggestion.regions.front()];
    std::vector<CapturedObject> objects;
    objects.reserve(region.objects.size());
    for (int index : region.objects) {
        auto const& object = data.objects[index];
        // Triggers keep their target groups, so copying them around would fire
        // effects the builder never asked for.
        if (looksLikeTrigger(object) || isMarkerObject(object.id)) continue;
        CapturedObject captured;
        captured.objectId = object.id;
        captured.dx = object.x;
        captured.dy = object.y;
        captured.save = object.save;
        objects.push_back(std::move(captured));
    }
    if (objects.empty()) return tpl;

    tpl.samples = std::max(1, suggestion.repeats);
    if (suggestion.mode == Mode::Wave) {
        auto colors = std::move(tpl.colors);
        auto name = std::move(tpl.name);
        tpl = waveFromObjects(std::move(objects), cellSize);
        tpl.name = std::move(name);
        tpl.colors = std::move(colors);
        tpl.samples = std::max(1, suggestion.repeats);
        for (auto& piece : tpl.pieces) piece.weight *= tpl.samples;
        return tpl;
    }

    tpl.mode = Mode::Stamp;
    Piece piece;
    piece.weight = std::max(1, suggestion.repeats);
    piece.objects = std::move(objects);
    centerPiece(piece);
    tpl.pieces.push_back(std::move(piece));
    return tpl;
}

std::string sketchObjects(LevelData const& data, std::vector<int> const& objects,
                          int width, int height) {
    if (objects.empty()) return {};
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (int index : objects) {
        auto const& object = data.objects[index];
        minX = std::min(minX, object.x);
        minY = std::min(minY, object.y);
        maxX = std::max(maxX, object.x);
        maxY = std::max(maxY, object.y);
    }

    SketchCanvas canvas(width, height, minX, minY, maxX, maxY);
    for (int index : objects) {
        auto const& object = data.objects[index];
        auto kind = kindOf(object.id);
        if (kind == ObjectKind::Unknown && looksLikeTrigger(object)) kind = ObjectKind::Trigger;
        canvas.plot(object.x, object.y, glyphFor(kind, depthOf(object)));
    }
    return canvas.render();
}

std::string sketchRegion(LevelData const& data, Region const& region, int width, int height) {
    return sketchObjects(data, region.objects, width, height);
}

std::string sketchTemplate(Template const& tpl, int width, int height) {
    if (tpl.pieces.empty()) return {};

    std::vector<std::pair<float, float>> points;
    std::vector<char> glyphs;
    auto collect = [&](Piece const& piece, float originX, float originY) {
        for (auto const& object : piece.objects) {
            auto kind = kindOf(object.objectId);
            Depth depth = Depth::Play;
            std::string value;
            if (objectKey(object.save, 24, value)) {
                int const layer = std::atoi(value.c_str());
                if (behindLayer(layer)) depth = Depth::Back;
                else if (frontLayer(layer)) depth = Depth::Front;
            }
            points.emplace_back(originX + object.dx, originY + object.dy);
            glyphs.push_back(glyphFor(kind, depth));
        }
    };

    if (tpl.mode == Mode::Wave && !tpl.grids.empty()) {
        auto const& grid = tpl.grids.front();
        for (auto const& cell : grid.cells) {
            if (cell.piece < 0 || cell.piece >= static_cast<int>(tpl.pieces.size())) continue;
            collect(tpl.pieces[cell.piece], cell.x * tpl.cell, cell.y * tpl.cell);
        }
    } else {
        size_t biggest = 0;
        for (size_t i = 1; i < tpl.pieces.size(); ++i) {
            if (tpl.pieces[i].objects.size() > tpl.pieces[biggest].objects.size()) biggest = i;
        }
        collect(tpl.pieces[biggest], 0.f, 0.f);
    }
    if (points.empty()) return {};

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (auto const& point : points) {
        minX = std::min(minX, point.first);
        minY = std::min(minY, point.second);
        maxX = std::max(maxX, point.first);
        maxY = std::max(maxY, point.second);
    }

    SketchCanvas canvas(width, height, minX, minY, maxX, maxY);
    for (size_t i = 0; i < points.size(); ++i) {
        canvas.plot(points[i].first, points[i].second, glyphs[i]);
    }
    return canvas.render();
}

} // namespace paimon::autobuild
