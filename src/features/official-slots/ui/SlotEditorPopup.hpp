#pragma once

// Graphical editor for one cosmetic official slot: fixed 440x320 two-column
// popup, no scroll. Everything here is paint over a local unrated stand-in.

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
    // replacesOfficialId turns the form into "replace official N" mode (0 appends).
    // onSaved runs after save/hide so the caller can redraw its own list.
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

    // Builders run once; selections restyle in place so typing in a
    // TextInput never loses focus to a rebuild.
    void buildSourceRow();
    void buildDataRows();
    void buildDifficultyRow();
    void buildTierRow();
    void buildStarsRow();
    void buildPreviewCard();
    void buildFooter();

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

    void prefillFromLevel(GJGameLevel* level);
    // Also arms the pending import.
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
