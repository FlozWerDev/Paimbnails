#pragma once

#include "PhysicsWorkspace.hpp"

#include <Geode/Geode.hpp>

#include <cstddef>
#include <vector>

class EditorUI;

namespace paimon::editorphysics {

struct EmitReport {
    std::size_t objects = 0;
    std::size_t keyframes = 0;
    std::size_t triggers = 0;
    std::size_t groups = 0;
    std::size_t assignedObjects = 0;
    std::size_t impacts = 0;
};

geode::Result<EmitReport> emitToEditor(
    EditorUI* ui,
    std::vector<ResolvedBody> const& bodies,
    SimulationTrace const& trace
);

geode::Result<std::size_t> removeLastEmission(EditorUI* ui);

} // namespace paimon::editorphysics
