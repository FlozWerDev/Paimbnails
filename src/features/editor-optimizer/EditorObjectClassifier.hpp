#pragma once

#include "StructureOptimizerCore.hpp"

#include <Geode/Geode.hpp>
#include <vector>

class EditorUI;
class GameObject;
class LevelEditorLayer;

namespace paimon::optimizer {

struct CapturedScene {
    std::vector<geode::WeakRef<GameObject>> objects;
    std::vector<ObjectRecord> records;
    bool usedSelection = false;
    std::size_t selectionCount = 0;
    std::size_t levelCount = 0;
};

struct ApplyReport {
    std::size_t removedObjects = 0;
    std::size_t createdObjects = 0;
    std::size_t netSaved = 0;
};

// Classifies an object ID's safe stretch axes using frame metadata + rendered sprite symmetry.
StretchCapability classifyObjectID(int objectID, GameObject* sampleObject = nullptr);

// Captures either the current editor selection or the full level into pure ObjectRecord data.
CapturedScene captureEditorScene(
    LevelEditorLayer* editor,
    EditorUI* ui,
    bool selectedOnly
);

// Applies an OptimizationPlan atomically to the editor with undo and collab support.
geode::Result<ApplyReport> applyOptimizationPlan(
    LevelEditorLayer* editor,
    EditorUI* ui,
    CapturedScene const& scene,
    OptimizationPlan const& plan
);

// Config persistence helpers.
OptimizerOptions loadOptimizerOptions();
void saveOptimizerOptions(OptimizerOptions const& options);

} // namespace paimon::optimizer
