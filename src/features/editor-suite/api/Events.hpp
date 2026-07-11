#pragma once

// Suite-wide editor events. Prefer these over ad-hoc globals.
//
// Listen examples (Geode v5):
//   EditorUIShowEvent().listen([](EditorUI* ui, bool shown) -> bool {
//       return false; // propagate
//   });
//
// Modules that add HUD chrome MUST hide/show on EditorUIShowEvent.

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

// Fired when LevelEditorLayer is destroyed / session ends.
class EditorExitEvent
    : public geode::Event<EditorExitEvent, bool()>
{
public:
    using Event::Event;
};

// UI scale changed while in the editor (live). scale==1 when feature off.
class EditorUIScaleEvent
    : public geode::Event<EditorUIScaleEvent, bool(float /*scale*/, bool /*scaleToolbars*/)>
{
public:
    using Event::Event;
};

// Ask Improved Group View (and any listeners) to rebuild.
class GroupViewUpdateEvent
    : public geode::Event<GroupViewUpdateEvent, bool()>
{
public:
    using Event::Event;
};

// Fired after our tabs backend finishes flushToEditor.
class EditorTabsFlushedEvent
    : public geode::Event<EditorTabsFlushedEvent, bool(EditorUI*)>
{
public:
    using Event::Event;
};

} // namespace paimon::editor
