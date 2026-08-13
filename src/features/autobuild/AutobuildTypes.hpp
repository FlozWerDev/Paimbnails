#pragma once

// Data model for the editor autobuilder: what a template stores and the options
// a generation run uses. Everything here is plain data so the solver can run
// without touching the editor.

#include <string>
#include <vector>

namespace paimon::autobuild {

// Wave learns a grid of tiles plus which tile may sit next to which; Stamp
// keeps whole clusters and drops one on each target.
enum class Mode { Wave, Stamp };

// Where the generated objects go.
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

void measurePiece(Piece& piece);

// Wave adjacency of one piece. `open[d]` means the piece was captured with
// nothing on that side, so it is allowed to sit on the border of a fill.
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
    int objectCount() const;
    std::string summary() const;
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

    static Options load();
    void save() const;
};

char const* modeName(Mode mode);
char const* targetName(TargetMode target);

} // namespace paimon::autobuild
