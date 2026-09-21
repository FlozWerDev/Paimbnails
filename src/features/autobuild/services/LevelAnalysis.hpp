#pragma once

// Reading a whole level and deciding what each part of it is.
//
// No GD object database here, so regions read like a builder reads a screenshot.

#include <string>
#include <vector>

#include "../AutobuildTypes.hpp"
#include "LevelParse.hpp"
#include "ObjectTaxonomy.hpp"

namespace paimon::autobuild {

enum class RegionKind : unsigned char {
    Structure,   // geometry the player rides: blocks, slopes, the hazards on them
    Hazard,      // a cluster that is mostly spikes and saws
    Decoration,  // deco sitting at playfield depth, attached to a structure
    Background,  // behind the player: backdrops, parallax shapes, big fills
    Foreground,  // in front of the player: T layers, overlays
    Logic,       // triggers
};

constexpr int kRegionKinds = 6;

char const* regionKindName(RegionKind kind);

struct RegionMetrics {
    int objects = 0;
    int solids = 0;
    int hazards = 0;
    int gameplay = 0;   // portals, pads, orbs, collectibles
    int triggers = 0;
    int known = 0;      // objects the taxonomy could name
    float minX = 0.f;
    float minY = 0.f;
    float maxX = 0.f;
    float maxY = 0.f;
    float gridAligned = 0.f;    // fraction sitting on GD's 30 grid
    float rotated = 0.f;        // fraction turned off the 90 axis
    float meanScale = 1.f;
    float behind = 0.f;         // fraction on a B layer or painted by a BG channel
    float front = 0.f;
    float bgTinted = 0.f;
    float lowDetail = 0.f;      // fraction the author already flagged as LDM
    float density = 0.f;        // objects per grid cell of the bounding box
    float structureScore = 0.f;
    float backgroundScore = 0.f;

    float width() const { return maxX - minX; }
    float height() const { return maxY - minY; }
};

struct Region {
    RegionKind kind = RegionKind::Decoration;
    RegionMetrics metrics;
    std::vector<int> objects;  // indices into LevelData::objects
    int family = -1;           // regions with the same shape share a family
    int repeats = 1;
    float confidence = 0.f;
    // A motif is a shape that repeats *inside* a bigger region, found by the
    // neighbourhood pass. It overlaps its parent on purpose.
    bool motif = false;
    int parent = -1;
};

struct PaletteEntry {
    ColorChannel channel;
    int mainUses = 0;
    int detailUses = 0;
    int usesByKind[kRegionKinds] = {};
    RegionKind role = RegionKind::Decoration;

    int uses() const { return mainUses + detailUses; }
};

// Everything that changes how the level plays, in the order the player meets it.
struct GameplayBeat {
    float x = 0.f;
    float y = 0.f;
    int objectId = 0;
    ObjectKind kind = ObjectKind::Unknown;
};

struct LevelReport {
    int levelId = 0;
    std::string name;
    int objectCount = 0;
    bool truncated = false;
    float lengthX = 0.f;
    float groundY = 0.f;
    std::string colors;
    std::vector<Region> regions;
    std::vector<PaletteEntry> palette;
    std::vector<GameplayBeat> beats;
    int counts[kRegionKinds] = {};
    int objectsByKind[kRegionKinds] = {};

    std::string summary() const;
};

struct AnalysisOptions {
    float cell = 30.f;
    float linkRadius = 46.f;    // two objects closer than this belong together
    float motifRadius = 78.f;   // how much of its surroundings a motif covers
    int minRegionObjects = 2;
    int minMotifObjects = 4;
    int minMotifRepeats = 3;
    int maxRegions = 8000;
    int maxMotifs = 12;
    bool splitByDepth = true;
    bool findMotifs = true;
};

LevelReport analyzeLevel(LevelData const& data, AnalysisOptions const& opts = {});

// One template offer: a recurring shape, or a big one-off worth keeping.
struct TemplateSuggestion {
    std::string name;
    RegionKind kind = RegionKind::Structure;
    Mode mode = Mode::Wave;
    int repeats = 1;
    int objects = 0;
    float score = 0.f;
    std::vector<int> regions;  // indices into LevelReport::regions
};

std::vector<TemplateSuggestion> suggestTemplates(LevelData const& data,
                                                 LevelReport const& report,
                                                 int maxSuggestions = 24);

Template templateFrom(LevelData const& data, LevelReport const& report,
                      TemplateSuggestion const& suggestion, float cell);

// ASCII pictures, so a change in the classifier is visible instead of being a
// number that moved. Used by the regression test and the analysis log.
std::string sketchObjects(LevelData const& data, std::vector<int> const& objects,
                          int width, int height);
std::string sketchRegion(LevelData const& data, Region const& region,
                         int width, int height);
std::string sketchTemplate(Template const& tpl, int width, int height);

} // namespace paimon::autobuild
