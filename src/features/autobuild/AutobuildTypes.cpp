#include "AutobuildTypes.hpp"

#include <Geode/loader/Mod.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <limits>

using namespace geode::prelude;

namespace paimon::autobuild {

namespace {

constexpr char const* kCaptureMode  = "autobuild-capture-mode";
constexpr char const* kTarget       = "autobuild-target";
constexpr char const* kLayer2       = "autobuild-layer2";
constexpr char const* kLayer3       = "autobuild-layer3";
constexpr char const* kRemove       = "autobuild-remove-markers";
constexpr char const* kColors       = "autobuild-copy-colors";
constexpr char const* kGaps         = "autobuild-allow-gaps";
constexpr char const* kStrict       = "autobuild-strict-rules";
constexpr char const* kSmart        = "autobuild-smart-templates";
constexpr char const* kRotate       = "autobuild-rotate-variants";
constexpr char const* kFlip         = "autobuild-flip-variants";
constexpr char const* kRepeats      = "autobuild-avoid-repeats";
constexpr char const* kOverlap      = "autobuild-avoid-overlap";
constexpr char const* kSeed         = "autobuild-seed";
constexpr char const* kShiftColors  = "autobuild-shift-colors";
constexpr char const* kShiftGroups  = "autobuild-shift-groups";
constexpr char const* kShiftLayers  = "autobuild-shift-layers";
constexpr char const* kShiftZ       = "autobuild-shift-z";
constexpr char const* kAddGroup     = "autobuild-add-group";
constexpr char const* kMaxObjects   = "autobuild-max-objects";
constexpr char const* kCaptureCell  = "autobuild-capture-cell";
constexpr char const* kClusterRad   = "autobuild-cluster-radius";
constexpr char const* kBacktracks   = "autobuild-backtracks";

} // namespace

void measurePiece(Piece& piece) {
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

int Template::objectCount() const {
    int total = 0;
    for (auto const& piece : pieces) total += static_cast<int>(piece.objects.size());
    return total;
}

std::string Template::summary() const {
    return fmt::format("{} - {} piezas - {} objetos - {} muestra{}",
                       modeName(mode), pieces.size(), objectCount(), samples,
                       samples == 1 ? "" : "s");
}

Options Options::load() {
    auto* mod = Mod::get();
    Options o;
    int target = mod->getSavedValue<int>(kTarget, 0);
    o.target        = static_cast<TargetMode>(std::clamp(target, 0, 2));
    o.captureMode   = mod->getSavedValue<int>(kCaptureMode, 0) == 1 ? Mode::Stamp : Mode::Wave;
    o.layer2Markers = mod->getSavedValue<bool>(kLayer2, o.layer2Markers);
    o.layer3Markers = mod->getSavedValue<bool>(kLayer3, o.layer3Markers);
    o.removeMarkers = mod->getSavedValue<bool>(kRemove, o.removeMarkers);
    o.copyColors    = mod->getSavedValue<bool>(kColors, o.copyColors);
    o.allowGaps     = mod->getSavedValue<bool>(kGaps, o.allowGaps);
    o.strictRules   = mod->getSavedValue<bool>(kStrict, o.strictRules);
    o.smartTemplates = mod->getSavedValue<bool>(kSmart, o.smartTemplates);
    o.rotateVariants = mod->getSavedValue<bool>(kRotate, o.rotateVariants);
    o.flipVariants  = mod->getSavedValue<bool>(kFlip, o.flipVariants);
    o.avoidRepeats  = mod->getSavedValue<bool>(kRepeats, o.avoidRepeats);
    o.avoidOverlap  = mod->getSavedValue<bool>(kOverlap, o.avoidOverlap);
    o.seed          = mod->getSavedValue<int>(kSeed, o.seed);
    o.shiftColors   = mod->getSavedValue<int>(kShiftColors, 0);
    o.shiftGroups   = mod->getSavedValue<int>(kShiftGroups, 0);
    o.shiftLayers   = mod->getSavedValue<int>(kShiftLayers, 0);
    o.shiftZOrder   = mod->getSavedValue<int>(kShiftZ, 0);
    o.addGroup      = std::clamp(mod->getSavedValue<int>(kAddGroup, 0), 0, 9999);
    o.maxObjects    = mod->getSavedValue<int>(kMaxObjects, o.maxObjects);
    o.captureCell   = mod->getSavedValue<float>(kCaptureCell, o.captureCell);
    o.clusterRadius = mod->getSavedValue<float>(kClusterRad, o.clusterRadius);
    o.backtracks    = mod->getSavedValue<int>(kBacktracks, o.backtracks);

    o.captureCell   = std::clamp(o.captureCell, 5.f, 300.f);
    o.clusterRadius = std::clamp(o.clusterRadius, 15.f, 480.f);
    o.maxObjects    = std::clamp(o.maxObjects, 500, 200000);
    o.backtracks    = std::clamp(o.backtracks, 0, 20000);
    return o;
}

void Options::save() const {
    auto* mod = Mod::get();
    mod->setSavedValue(kCaptureMode, captureMode == Mode::Stamp ? 1 : 0);
    mod->setSavedValue(kTarget, static_cast<int>(target));
    mod->setSavedValue(kLayer2, layer2Markers);
    mod->setSavedValue(kLayer3, layer3Markers);
    mod->setSavedValue(kRemove, removeMarkers);
    mod->setSavedValue(kColors, copyColors);
    mod->setSavedValue(kGaps, allowGaps);
    mod->setSavedValue(kStrict, strictRules);
    mod->setSavedValue(kSmart, smartTemplates);
    mod->setSavedValue(kRotate, rotateVariants);
    mod->setSavedValue(kFlip, flipVariants);
    mod->setSavedValue(kRepeats, avoidRepeats);
    mod->setSavedValue(kOverlap, avoidOverlap);
    mod->setSavedValue(kSeed, seed);
    mod->setSavedValue(kShiftColors, shiftColors);
    mod->setSavedValue(kShiftGroups, shiftGroups);
    mod->setSavedValue(kShiftLayers, shiftLayers);
    mod->setSavedValue(kShiftZ, shiftZOrder);
    mod->setSavedValue(kAddGroup, addGroup);
    mod->setSavedValue(kMaxObjects, maxObjects);
    mod->setSavedValue(kCaptureCell, captureCell);
    mod->setSavedValue(kClusterRad, clusterRadius);
    mod->setSavedValue(kBacktracks, backtracks);
}

char const* modeName(Mode mode) {
    return mode == Mode::Wave ? "Onda" : "Sellos";
}

char const* targetName(TargetMode target) {
    switch (target) {
        case TargetMode::Markers:   return "Marcadores";
        case TargetMode::Selection: return "Seleccion";
        case TargetMode::Area:      return "Area";
    }
    return "Marcadores";
}

} // namespace paimon::autobuild
