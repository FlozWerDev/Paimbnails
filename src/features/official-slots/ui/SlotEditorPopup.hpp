#pragma once

// Graphical editor for one cosmetic official slot.
//
// Layout is a fixed 440x320 two-column popup, no scroll layer: every control
// stays visible so the live preview on the right always reflects the draft.
// The left column holds the source chips (level id + import, or .gmd browse),
// the name/author inputs, the star stepper+slider and the coins chip; the
// bottom strip holds the 12 vanilla difficulty faces and the 5 rate-tier
// faces; the footer holds Test/Save (plus Hide-official in replace mode).
//
// Everything here is paint. Saving writes a Slot to the store, which only
// describes how a page is drawn; the playable stand-in built from it is local
// and unrated (see services/SlotLevels.hpp), so stars and orbs are never
// granted no matter what the preview shows.

#include "../OfficialSlots.hpp"

#include <Geode/Geode.hpp>

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace paimon::officialslots::ui {

class SlotEditorPopup : public geode::Popup {
public:
    // Edit the stored slot, or pass nullopt to create one. replacesOfficialId
    // turns the form into "replace official N" mode (0 appends after the
    // vanilla pages). onSaved runs after a successful save or hide so the
    // caller (manager, hooks) can redraw its own list.
    static SlotEditorPopup* create(
        std::optional<std::string> slotId,
        int replacesOfficialId,
        std::function<void()> onSaved = nullptr
    );

protected:
    bool init(
        std::optional<std::string> slotId,
        int replacesOfficialId,
        std::function<void()> onSaved
    );
    void onExit() override;

    // Form builders. Each builds once; selections restyle in place so typing
    // in a TextInput never loses focus to a rebuild.
    void buildSourceRow();
    void buildDataRows();
    void buildDifficultyRow();
    void buildTierRow();
    void buildStarsRow();
    void buildPreviewCard();
    void buildFooter();

    // Restyle helpers for the single-select face rows.
    void restyleSourceChips();
    void restyleDifficultyRow();
    void restyleTierRow();
    void refreshStarsLabel();
    void refreshPreview();
    void refreshGmdLabel();

    void setSource(Source source);
    void setDifficulty(Difficulty difficulty);
    void setTier(Tier tier);
    void setStars(int stars);

    void onImportById(cocos2d::CCObject*);
    void onBrowseGmd(cocos2d::CCObject*);
    void onSurprise(cocos2d::CCObject*);
    void onTest(cocos2d::CCObject*);
    void onSave(cocos2d::CCObject*);
    void onHideOfficial(cocos2d::CCObject*);
    void onStarsStep(cocos2d::CCObject* sender);

    // Fill the draft from a downloaded level (import by id).
    void prefillFromLevel(GJGameLevel* level);
    // Fill the draft from a picked .gmd file (also arms the pending import).
    void prefillFromGmd(std::filesystem::path const& path);

    // Persist the draft. Returns the stored slot id, empty on failure (toast
    // already shown). Imports a pending .gmd and discards the replaced file.
    std::string saveDraft();

    void showSpinner(bool show);

    Slot m_draft;
    bool m_isNew = true;
    std::function<void()> m_onSaved;

    // A picked .gmd waits here until Save (or Test) imports it into our folder,
    // so browsing never touches the store until the user commits.
    std::filesystem::path m_pendingGmd;

    geode::TextInput* m_idInput = nullptr;
    geode::TextInput* m_nameInput = nullptr;
    geode::TextInput* m_authorInput = nullptr;
    cocos2d::CCLabelBMFont* m_gmdLabel = nullptr;
    cocos2d::CCLabelBMFont* m_starsLabel = nullptr;
    cocos2d::CCNode* m_previewBox = nullptr;
    cocos2d::CCNode* m_sourceRow = nullptr;
    cocos2d::CCNode* m_tierRowBox = nullptr;
    geode::LoadingSpinner* m_spinner = nullptr;
    Slider* m_starsSlider = nullptr;

    std::vector<cocos2d::CCNode*> m_sourceChips;
    std::vector<cocos2d::CCNode*> m_difficultyFaces;
    std::vector<cocos2d::CCNode*> m_tierFaces;
    cocos2d::CCNode* m_coinsChip = nullptr;

    std::atomic<bool> m_alive{true};
};

} // namespace paimon::officialslots::ui
