#pragma once

// editor-wide events; prefer these over ad-hoc globals.
// features that add hud chrome must hide/show on editoruishowevent.

#include <Geode/loader/Event.hpp>
#include <Geode/binding/EditorUI.hpp>

namespace paimon::editor {

// (editorui*, shown). return true to stop propagation.
class EditorUIShowEvent
    : public geode::Event<EditorUIShowEvent, bool(EditorUI*, bool)>
{
public:
    using Event::Event;
};

} // namespace paimon::editor
