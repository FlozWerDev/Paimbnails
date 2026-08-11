#pragma once

// Turning the current editor selection into a template.

#include <Geode/Geode.hpp>

#include "../AutobuildTypes.hpp"

class EditorUI;
class GameObject;

namespace paimon::autobuild {

// Marker blocks: the classic autobuild hint blocks. 467 is layer 1, 143 and 146
// mark the extra layers so one selection can drive three passes.
constexpr int kMarkerLayer1 = 467;
constexpr int kMarkerLayer2 = 143;
constexpr int kMarkerLayer3 = 146;

int markerLayerOf(int objectId);
inline bool isMarkerId(int objectId) { return markerLayerOf(objectId) != 0; }

std::vector<GameObject*> selectionOf(EditorUI* ui);

geode::Result<Template> capture(EditorUI* ui, Mode mode, Options const& opts);

// Wave template out of loose objects whose dx/dy hold absolute level positions.
// Shared by the editor capture and the .tblib importer.
Template waveFromObjects(std::vector<CapturedObject> objects, float cell);

// Fold another capture of the same kind into an existing template.
geode::Result<> accumulate(Template& target, Template const& sample);

} // namespace paimon::autobuild
