#pragma once
#include "../../../ui/PaimonPopup.hpp"

// the form stays mounted while the adjacent catalogue previews the draft's placement.

#include "../OfficialSlots.hpp"

#include <Geode/Geode.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace paimon::officialslots::ui {

class SlotEditorPopup : public PaimonPopup {
public:
    // replacesofficialid turns the form into "replace official n" mode (0 appends).
    // onsaved runs after save/hide so the caller can redraw its own list.
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

    // builders run once; selections restyle in place so typing in a
    // textinput never loses focus to a rebuild.
    void buildSourceRow();
    void buildDataRows();
    void buildDifficultyRow();
    void buildTierRow();
    void buildStarsRow();
    void buildPreviewCard();
    void buildFooter();
    void buildPositionRow(cocos2d::CCMenu* menu);
    void rebuildPositionList(bool revealSelection = false);
    std::string pageKey() const;

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
    void setSlotPosition(std::size_t pos);
    void refreshPositionLabel();

    void onImportById(cocos2d::CCObject*);
    void onBrowseGmd(cocos2d::CCObject*);
    void onSurprise(cocos2d::CCObject*);
    void onTest(cocos2d::CCObject*);
    void onSave(cocos2d::CCObject*);
    void onHideOfficial(cocos2d::CCObject*);

    void prefillFromLevel(GJGameLevel* level);
    // also arms the pending import.
    void prefillFromGmd(std::filesystem::path const& path);

    // persist the draft. returns the stored slot id, empty on failure (toast
    // already shown). imports a pending .gmd and discards the replaced file.
    std::string saveDraft();

    void showSpinner(bool show);

    Slot m_draft;
    bool m_isNew = true;
    std::function<void()> m_onSaved;

    // 1-based visible spot, applied on save; untouched edits must not relocate a spotless slot.
    std::size_t m_position = 1;
    std::size_t m_positionMax = 1;
    bool m_positionDirty = false;

    // picked .gmd waits here until save/test imports it; browsing never touches the store.
    std::filesystem::path m_pendingGmd;

    geode::TextInput* m_idInput = nullptr;
    geode::TextInput* m_nameInput = nullptr;
    geode::TextInput* m_authorInput = nullptr;
    cocos2d::CCLabelBMFont* m_gmdLabel = nullptr;
    cocos2d::CCLabelBMFont* m_starsLabel = nullptr;
    cocos2d::CCLabelBMFont* m_positionLabel = nullptr;
    cocos2d::CCNode* m_previewBox = nullptr;
    cocos2d::CCNode* m_sourceRow = nullptr;
    cocos2d::CCNode* m_tierRowBox = nullptr;
    geode::LoadingSpinner* m_spinner = nullptr;
    geode::ScrollLayer* m_positionScroll = nullptr;
    Slider* m_starsSlider = nullptr;
    std::uint64_t m_importRequest = 0;

    std::vector<cocos2d::CCNode*> m_sourceChips;
    std::vector<cocos2d::CCNode*> m_difficultyFaces;
    std::vector<cocos2d::CCNode*> m_tierFaces;
    cocos2d::CCNode* m_coinsChip = nullptr;

    std::atomic<bool> m_alive{true};
};

} // namespace paimon::officialslots::ui
