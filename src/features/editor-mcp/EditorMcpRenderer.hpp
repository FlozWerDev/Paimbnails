#pragma once

#include "EditorMcpTypes.hpp"

namespace paimon::editormcp {

// Must be called on the main/GL thread while LevelEditorLayer is active.
RenderResult renderEditor1To1(RenderRequest const& req);

} // namespace paimon::editormcp
