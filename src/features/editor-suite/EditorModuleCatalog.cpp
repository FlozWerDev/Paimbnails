#include "EditorModuleCatalog.hpp"

namespace paimon::editor {

std::vector<EditorModuleInfo> const& getEditorModuleCatalog() {
    static std::vector<EditorModuleInfo> const kCatalog = {
        // Master
        {"editor-suite-enable", "Editor Suite", "Master switch for all new editor-mod features.", "Master", false, true},

        // Existing (legacy, not suite-gated)
        {"editor-rotate-enable", "Canvas Rotate", "Rotate the editor canvas (Alt+RMB).", "Existente", false, false},
        {"editor-color-picker-enable", "Color Picker", "Eyedropper to pick colors in the editor.", "Existente", false, true},
        {"editor-history-enable", "Undo Color/Groups/Layers", "Simple undo for color, groups and layers.", "Existente", false, true},
        {"editorMusicEnable", "Editor Music", "Your music library inside the editor.", "Existente", false, false},
        {"collab-enabled", "Collab Editor", "Real-time collaborative editing.", "Existente", false, true},
        {"input-scroll-enable", "Input Scroll", "Scroll wheel over numeric inputs.", "Existente", false, true},
        {"editor-filters-enable", "My Levels Filters", "Filter created levels by length/song/etc.", "Existente", false, true},

        // PR1 — Navigation / UX
        {"editor-mod-scroll-zoom", "Smooth Scroll & Zoom", "Better pan/zoom with zoom-to-cursor (Ctrl+wheel).", "Navegacion", true, true},
        {"editor-mod-zoom-text", "Zoom Level Text", "Flash current zoom when zooming.", "Navegacion", true, true},
        {"editor-mod-grid-control", "Grid Controls", "Change grid size with +/- and input.", "Navegacion", true, true},
        {"editor-mod-hide-ui", "Hide UI Toggle", "Button next to undo to hide editor UI.", "Interfaz", true, true},
        {"editor-mod-length-display", "Length Display", "Show level length (time) in the editor.", "Interfaz", true, false},
        {"editor-mod-object-tooltips", "Object Tooltips", "Object name tooltips on the build tab.", "Interfaz", true, true},
        {"editor-mod-repeating-buttons", "Repeating Buttons", "Hold editor buttons to repeat them.", "Interfaz", true, true},
        {"editor-mod-camera-tools", "Camera Tools", "Center camera on selection / move selection to camera.", "Objetos", true, true},
        {"editor-mod-single-deselect", "Single Deselect", "Hold Alt and click to deselect one object.", "Objetos", true, true},
        {"editor-mod-copy-string", "Copy Object String", "Copy object string to system clipboard on copy.", "Objetos", true, true},
        {"editor-mod-paste-warnings", "Paste Warnings", "Warn before paste state / paste color.", "Objetos", true, true},
        {"editor-mod-fixes-pack", "Editor Fixes Pack", "Slider fix, centered buttons, link controls, NaN crash fix.", "Fixes", true, true},
        {"editor-mod-ldm-count", "LDM Object Count", "Show high-detail object count in editor pause.", "Interfaz", true, true},
        {"editor-mod-negate-input", "Negate Input", "Press N to negate the focused number input.", "Interfaz", true, true},

        // PR2 — Deep editing
        {"editor-mod-scale-rotate", "Improved Scale/Rotate", "Numeric inputs and Shift-snap on scale/rotate controls.", "Objetos", true, true},
        {"editor-mod-type-in-layer", "Type-in Layer", "Type layer number, next-free layer, clearer lock control.", "Objetos", true, true},
        {"editor-mod-next-free-offset", "Next Free Offset", "Start Next Free group ID from a configurable offset.", "Objetos", true, true},
        {"editor-mod-start-pos", "Start Pos Tools", "Cycle start positions and playtest from a chosen one.", "Playtest", true, true},
        {"editor-mod-auto-build-helper", "Auto Build Helper", "Auto-run Build Helper after paste when toggled.", "Objetos", true, false},
        {"editor-mod-object-summary", "Object Summary", "Count objects by ID in editor pause.", "Objetos", true, true},
        {"editor-mod-multi-text", "Multi Text Edit", "Apply text edits to all selected text objects.", "Objetos", true, true},
        {"editor-mod-ui-scale", "Editor UI Scale", "Scale the editor HUD for smaller/larger screens.", "Interfaz", true, false},
        {"editor-mod-button-rows", "Button Rows Bypass", "Allow more button rows / buttons per row in editor options.", "Interfaz", true, false},
        {"editor-mod-move-keybinds", "Move Fractions Keybinds", "Ctrl+Alt+WASD half-block and Alt+WASD quarter moves.", "Objetos", true, true},
        {"editor-mod-extra-keybinds", "Extra Editor Keybinds", "Keys for edit object/group, copy/paste state, rotate 45, restart playtest.", "Objetos", true, true},
        {"editor-mod-group-view", "Improved Group View", "Scrollable list of all groups on multi-select group panel.", "Objetos", true, true},

        // PR3 — Advanced
        {"editor-mod-object-search", "Object Search", "Search objects by ID/name and jump to the create button.", "Objetos", true, true},
        {"editor-mod-view-panel", "View Toggles Panel", "Quick toggles for grid, ground, hitboxes, LDM hide, effect lines.", "Interfaz", true, true},
        {"editor-mod-trigger-lines", "Trigger Indicators", "Draw lines from selected triggers to their target groups.", "Visual", true, false},
        {"editor-mod-playtest-confirm", "Playtest Confirmation", "Ask before starting playtest.", "Playtest", true, false},
        {"editor-mod-hide-level-ids", "Hide Level IDs", "Hide IDs on Edit Level screen; hold Shift to show.", "Interfaz", true, false},
        {"editor-mod-duration-info", "Duration HUD", "Show duration of selected trigger in the editor HUD.", "Objetos", true, true},
        {"editor-mod-live-colors", "Live Color Strip", "Show active level color channels at the bottom of the editor.", "Interfaz", true, false},
        {"editor-mod-native-tabs", "Native Editor Tabs", "View/Search/Favs as Paimon side tabs (no duplicate floating View).", "Interfaz", true, true},
        {"editor-mod-object-favorites", "Object Favorites", "Right-click build-bar objects to favorite; Favs button lists them.", "Objetos", true, true},
        {"editor-mod-edit-mixed", "Edit Mixed Values", "Show L1/L2/Z ranges and type a value to set all selected objects.", "Objetos", true, true},
        {"editor-mod-duration-drag", "Duration Drag", "Shift+drag on duration triggers to change duration.", "Objetos", true, true},
        {"editor-mod-dash-portal-lines", "Dash/Portal Guides", "Guide lines for selected dash orbs and portals.", "Visual", true, false},
        {"editor-mod-hide-trigger-ui", "Hide Trigger UI on Slider", "Dim popup while dragging sliders.", "Interfaz", true, false},
        {"editor-mod-hsv-preview", "HSV Preview Swatch", "Color swatch on HSV widget.", "Interfaz", true, true},

        // PR4 — Gaps filled (Tinker / BetterEdit parity push)
        {"editor-mod-scrollable-objects", "Scrollable Object Bar", "Mouse wheel over the build bar changes object pages.", "Interfaz", true, true},
        {"editor-mod-preview-object-colors", "Preview Object Colors", "Tint create-tab icons with the level object color.", "Interfaz", true, false},
        {"editor-mod-reference-image", "Reference Image", "Import a translucent reference image into the editor.", "Interfaz", true, false},
        {"editor-mod-group-summary", "Group Summary", "List all used group IDs (pause menu / Ctrl+Shift+U).", "Objetos", true, true},

        // PR5 — Remaining Tinker / BetterEdit parity
        {"editor-mod-quick-save", "Quick Save Backup", "Save a level-string backup from the undo menu.", "Guardado", true, true},
        {"editor-mod-auto-save", "Auto Save Backup", "Periodic level-string backups while editing.", "Guardado", true, true},
        {"editor-mod-backups", "Backups Browser", "List/restore backups from the editor pause menu.", "Guardado", true, true},
        {"editor-mod-better-color-menu", "Better Color Menu", "Extra channel strip on the color select popup.", "Interfaz", true, true},
        {"editor-mod-better-font-select", "Better Font Select", "Scrollable font picker grid.", "Interfaz", true, true},
        {"editor-mod-move-menu", "Quarter Move Buttons", "1/4 block arrows on the edit bar, sorted by step size.", "Objetos", true, false},
        {"editor-mod-quick-extras", "Quick Extras", "One-click Edit Special from the undo menu.", "Objetos", true, true},
        {"editor-mod-relocate-build-tools", "Relocate Build Tools", "Build Helper / Align / Loop on the editor HUD.", "Objetos", true, true},
        {"editor-mod-grid-colors", "Custom Grid Color", "Recolor the editor grid (solid or gradient).", "Visual", true, false},
        {"editor-mod-toolbar-colors", "Toolbar Color Tint", "Tint bottom toolbar panels.", "Visual", true, false},
        {"editor-mod-joystick-nav", "Joystick / Alt+WASD Pan", "Hold Alt + WASD to pan the canvas.", "Navegacion", true, false},

        // PR6 — Tinker/BE-inspired hardening
        {"editor-mod-old-color-triggers", "Old Color Trigger Look", "Use classic tint-button textures for color triggers.", "Visual", true, false},
        {"editor-mod-select-direction", "Select All Left/Right", "Ctrl+Shift+Arrows select all objects left/right of camera.", "Objetos", true, true},
        {"editor-mod-editor-percentage", "Editor Percentage", "Show camera X position as % of level length.", "Interfaz", true, false},
        {"editor-mod-next-free-color", "Next Free Color Offset", "Offset field for next free color channel in Color Select.", "Objetos", true, true},
        {"editor-mod-rotate-lock-pos", "Lock Pos While Rotating", "Keep selection center fixed while using the rotate control.", "Objetos", true, false},
        {"editor-mod-back-to-content", "Back To Content", "Button to center camera on selection or level content.", "Navegacion", true, true},
        {"editor-mod-paste-object-string", "Paste Object String", "Ctrl+Shift+P pastes an object string from the system clipboard.", "Objetos", true, true},

        // PR7 — Tinker fixes parity
        {"editor-mod-wave-ignore-damage", "Wave Ignore Damage Fix", "Wave stops treating slopes as D blocks when ignore damage is on.", "Fixes", true, true},
    };
    return kCatalog;
}

std::vector<EditorModuleInfo> getModulesLayerEntries() {
    // Skip the suite master row as a separate list entry is optional;
    // include everything with a real bool key for the Modules layer.
    return getEditorModuleCatalog();
}

} // namespace paimon::editor
