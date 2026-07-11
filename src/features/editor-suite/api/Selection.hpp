#pragma once

// Selection helpers re-exported with a stable API name for modules/API users.

#include "../EditorHelpers.hpp"

namespace paimon::editor::selection {

using paimon::editor::getSelectedObjects;
using paimon::editor::selectionCenter;
using paimon::editor::focusCameraOnPoint;
using paimon::editor::focusCameraOnSelection;
using paimon::editor::moveSelectionToCamera;

} // namespace paimon::editor::selection
