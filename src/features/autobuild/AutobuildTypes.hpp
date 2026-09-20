#pragma once

// Plain data so the solver runs without touching the editor.

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace paimon::autobuild {

// Wave learns tiles; Stamp drops whole clusters.
enum class Mode { Wave, Stamp };

enum class TargetMode { Markers, Selection, Area };

struct PieceTransform {
    unsigned char quarterTurns = 0;
    bool flipX = false;

    bool identity() const { return quarterTurns == 0 && !flipX; }
};

constexpr int kCardinalDirections = 4;
constexpr int kNeighbourDirections = 8;
constexpr int kUpDirection = 0;
constexpr int kDownDirection = 1;
constexpr int kRightDirection = 2;
constexpr int kLeftDirection = 3;
constexpr int kDirectionX[kNeighbourDirections] = {0, 0, 1, -1, 1, -1, 1, -1};
constexpr int kDirectionY[kNeighbourDirections] = {1, -1, 0, 0, 1, -1, -1, 1};
constexpr int kOppositeDirection[kNeighbourDirections] = {1, 0, 3, 2, 5, 4, 7, 6};
constexpr int kMaxTemplateGrids = 2048;
constexpr int kMaxTemplateGridCells = 120000;

struct CapturedObject {
    int objectId = 0;
    float dx = 0.f;
    float dy = 0.f;
    std::string save;  // GD save string of the original object
};

struct Piece {
    int weight = 1;    // how many times the shape was observed
    std::vector<CapturedObject> objects;
    float width = 0.f;
    float height = 0.f;
};

inline void measurePiece(Piece& piece) {
    if (piece.objects.empty()) {
        piece.width = 0.f;
        piece.height = 0.f;
        return;
    }

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (auto const& obj : piece.objects) {
        minX = std::min(minX, obj.dx);
        minY = std::min(minY, obj.dy);
        maxX = std::max(maxX, obj.dx);
        maxY = std::max(maxY, obj.dy);
    }
    piece.width = maxX - minX;
    piece.height = maxY - minY;
}

// Wave adjacency; open[d] allows sitting on fill border.
struct Links {
    std::vector<int> side[kNeighbourDirections];
    bool open[kNeighbourDirections] = {};
};

struct SampleCell {
    int x = 0;
    int y = 0;
    int piece = -1;
};

struct SampleGrid {
    int width = 0;
    int height = 0;
    std::vector<SampleCell> cells;
};

struct Template {
    std::string name = "Sin nombre";
    Mode mode = Mode::Wave;
    float cell = 30.f;
    int samples = 1;
    std::vector<Piece> pieces;
    std::vector<Links> links;  // wave only, parallel to pieces
    std::vector<SampleGrid> grids;  // original wave layouts, with piece ids remapped
    std::string colors;        // kS38 body captured with the sample
    std::string file;          // file name on disk, empty until saved

    bool valid() const { return !pieces.empty(); }
    std::string summary() const;

    int objectCount() const {
        int total = 0;
        for (auto const& piece : pieces) total += static_cast<int>(piece.objects.size());
        return total;
    }
};

struct Options {
    Mode captureMode = Mode::Wave;
    TargetMode target = TargetMode::Markers;
    bool layer2Markers = true;
    bool layer3Markers = true;
    bool removeMarkers = true;
    bool copyColors = true;
    bool allowGaps = false;
    bool strictRules = true;  // wave: only reuse neighbour pairs the sample showed
    bool smartTemplates = true;
    bool rotateVariants = true;
    bool flipVariants = true;
    bool avoidRepeats = true;
    bool avoidOverlap = false;  // stamp: skip a piece that would land on another
    int seed = 0;               // 0 = a new random seed on every run
    int shiftColors = 0;
    int shiftGroups = 0;
    int shiftLayers = 0;
    int shiftZOrder = 0;
    int addGroup = 0;  // extra group on every generated object, 0 = none
    int maxObjects = 40000;
    float captureCell = 30.f;
    float clusterRadius = 60.f;
    int backtracks = 1200;
    // 0 fiel, 1 mezcla, 2 atrevido.
    int invention = 0;
    // Seeds tried per build, best wins; 1 = classic.
    int refineTries = 3;

    static Options load();
    void save() const;
};

char const* modeName(Mode mode);
char const* targetName(TargetMode target);

} // namespace paimon::autobuild
