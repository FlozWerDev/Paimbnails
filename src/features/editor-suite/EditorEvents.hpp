#pragma once

// Editor-wide events; prefer these over ad-hoc globals.
// Features that add HUD chrome MUST hide/show on EditorUIShowEvent.

#include <Geode/loader/Event.hpp>
#include <Geode/binding/EditorUI.hpp>

namespace paimon::editor {

// (EditorUI*, shown). Return true to stop propagation.
class EditorUIShowEvent
    : public geode::Event<EditorUIShowEvent, bool(EditorUI*, bool)>
{
public:
    using Event::Event;
};

} // namespace paimon::editor
