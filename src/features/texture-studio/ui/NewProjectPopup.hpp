#pragma once
//
// NewProjectPopup.hpp - Dialog shown when the user clicks "+ New Pack" in
// TextureStudioLayer. Walks them through:
//
//   1. Pack name (text input).
//   2. Source mode (auto-detect from GD Resources OR manual file picker).
//   3. Sheet selection (checkboxes from the auto-detect scan).
//   4. Confirmation → SlotStore::createSlot() and close.
//
// On success it invokes a callback with the new slot id, so the parent
// layer can open the project editor immediately.
//
// We deliberately keep this popup read-only with respect to colors and
// brightness — those are set later in the project editor. This screen
// is just "tell me what to base your pack on".
//

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include <functional>
#include <string>

namespace paimon::texture_studio {

class NewProjectPopup : public geode::Popup {
public:
    using SlotCreatedCallback = std::function<void(std::string const& slotId)>;

    static NewProjectPopup* create(SlotCreatedCallback cb);

protected:
    bool init(SlotCreatedCallback cb);

    // Refresh the sheets list (called once on init and when the user
    // toggles auto-detect ↔ manual). On manual mode it stays empty until
    // the user picks files.
    void refreshSheetsList();

    // Build the SheetSelection list from the currently-checked rows.
    void onCreateClicked(cocos2d::CCObject*);

private:
    SlotCreatedCallback m_onCreated;

    // UI references kept as raw pointers because they live in the
    // CCNode tree and Cocos owns their lifetime.
    geode::TextInput* m_nameInput   = nullptr;
    cocos2d::CCNode*  m_sheetsListContainer = nullptr;

    // Per-row state for the sheet checkboxes. We pull this back out at
    // creation time to know which sheets to include.
    struct SheetRow {
        std::string baseName;
        std::string qualitySuffix;
        std::string plistPath;
        std::string pngPath;
        bool checked = true;
    };
    std::vector<SheetRow> m_rows;
};

}  // namespace paimon::texture_studio
