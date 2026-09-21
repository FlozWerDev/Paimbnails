#include "SlotEditorPopup.hpp"

#include "SlotVisuals.hpp"
#include "../services/GmdImporter.hpp"
#include "../services/OfficialSlotStore.hpp"
#include "../services/SlotLevels.hpp"
#include "../services/SlotListRefresh.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJDifficultySprite.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/utils/general.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <random>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::officialslots::ui {

namespace {

// Popup size. Fixed on purpose: every control stays on screen next to the
// live preview, so there is no scroll position to lose while typing.
constexpr float kWidth = 460.f;
constexpr float kHeight = 360.f;

constexpr ccColor3B kOnColor = {255, 255, 255};
constexpr ccColor3B kOffColor = {96, 102, 120};

// Dimmed = not picked. Same trick as the request filter rows: white vs grey
// reads as selected/unselected without rebuilding any sprite.
void tintIcon(CCNode* node, bool on) {
    if (!node) return;
    if (auto* sprite = typeinfo_cast<CCSprite*>(node)) {
        sprite->setColor(on ? kOnColor : kOffColor);
        sprite->setOpacity(on ? 255 : 110);
    } else if (auto* label = typeinfo_cast<CCLabelBMFont*>(node)) {
        label->setColor(on ? kOnColor : kOffColor);
        label->setOpacity(on ? 255 : 110);
    }
    if (auto* children = node->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) tintIcon(child, on);
    }
}

std::string tr(char const* key) {
    return Localization::get().getString(key);
}

void toast(std::string const& text, NotificationIcon icon) {
    PaimonNotify::show(text, icon);
}

// Slider callback wrapper; Slider::create wants a CCNode target with a
// selector, same shape as the config kit's own wrapper.
class StarsSliderCallback : public CCNode {
public:
    std::function<void(double)> m_callback;
    Slider* m_slider = nullptr;

    static StarsSliderCallback* create(std::function<void(double)> cb) {
        auto* ret = new StarsSliderCallback();
        ret->init();
        ret->m_callback = std::move(cb);
        ret->autorelease();
        return ret;
    }

    void onChanged(CCObject*) {
        if (!m_slider || !m_slider->getThumb()) return;
        double value = static_cast<double>(m_slider->getThumb()->getValue()) * 100.0;
        if (m_callback) m_callback(value);
    }
};

std::string officialName(int officialId) {
    if (auto* glm = GameLevelManager::get()) {
        if (auto* main = glm->getMainLevel(officialId, true)) {
            std::string name = main->m_levelName.c_str();
            if (!name.empty()) return name;
        }
    }
    return fmt::format("#{}", officialId);
}

int clampFace(int face) {
    return std::clamp(face, -1, 10);
}

Difficulty difficultyFromFace(int face) {
    return static_cast<Difficulty>(clampFace(face));
}

// Vanilla face value for a downloaded level. Mirrors the community
// leaderboard mapping so an import prefill draws the face the game itself
// would draw: auto first, demon tiers 3-6 to their own faces, then whatever
// the game computed as the average difficulty.
int levelFaceValue(GJGameLevel* level) {
    if (!level) return 0;
    if (level->m_autoLevel) return -1;
    if (level->m_demon.value() > 0) {
        switch (static_cast<int>(level->m_demonDifficulty)) {
            case 3: return 7;
            case 4: return 8;
            case 5: return 9;
            case 6: return 10;
            default: return 6;
        }
    }
    return level->getAverageDifficulty();
}

Tier levelTier(GJGameLevel* level) {
    if (!level) return Tier::None;
    // m_isEpic is a tier, not a flag: 1 epic, 2 legendary, 3 mythic.
    switch (level->m_isEpic) {
        case 1: return Tier::Epic;
        case 2: return Tier::Legendary;
        case 3: return Tier::Mythic;
        default: break;
    }
    return level->m_featured > 0 ? Tier::Featured : Tier::None;
}

} // namespace

SlotEditorPopup* SlotEditorPopup::create(
    std::optional<std::string> slotId,
    int replacesOfficialId,
    std::function<void()> onSaved
) {
    auto* ret = new SlotEditorPopup();
    if (ret && ret->init(std::move(slotId), replacesOfficialId, std::move(onSaved))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool SlotEditorPopup::init(
    std::optional<std::string> slotId,
    int replacesOfficialId,
    std::function<void()> onSaved
) {
    if (!Popup::init(kWidth, kHeight)) return false;
    paimon::markDynamicPopup(this);
    m_onSaved = std::move(onSaved);

    auto& store = SlotStore::get();
    if (slotId && !slotId->empty()) {
        if (auto existing = store.find(*slotId)) {
            m_draft = *existing;
            m_isNew = false;
        }
    }
    if (m_isNew && isOfficialId(replacesOfficialId)) {
        m_draft.replacesOfficialId = replacesOfficialId;
    }

    if (!m_isNew) {
        this->setTitle(tr("slot.editor.title"));
    } else if (m_draft.replacesOfficialId != 0) {
        this->setTitle(fmt::format(fmt::runtime(tr("slot.editor.replace_title")),
                                   officialName(m_draft.replacesOfficialId)));
    } else {
        this->setTitle(tr("slot.editor.add_title"));
    }

    this->buildDifficultyRow();
    this->buildTierRow();
    this->buildSourceRow();
    this->buildDataRows();
    this->buildStarsRow();
    this->buildPreviewCard();
    this->buildFooter();

    this->restyleSourceChips();
    this->restyleDifficultyRow();
    this->restyleTierRow();
    this->refreshStarsLabel();
    this->refreshPreview();
    this->refreshGmdLabel();
    return true;
}

void SlotEditorPopup::onExit() {
    m_alive = false;
    Popup::onExit();
}

// --- source row (chips + id/import or gmd/browse) ---------------------------

void SlotEditorPopup::buildSourceRow() {
    auto* layer = m_mainLayer;

    m_sourceRow = CCNode::create();
    m_sourceRow->setContentSize({268.f, 114.f});
    m_sourceRow->setPosition({12.f, 96.f});
    m_sourceRow->setID("source-row"_spr);
    layer->addChild(m_sourceRow, 3);

    // Source chips: two text buttons, the inactive one dimmed in place.
    auto* menu = CCMenu::create();
    menu->setPosition({134.f, 100.f});
    menu->setContentSize({268.f, 28.f});
    menu->setID("source-menu"_spr);
    m_sourceRow->addChild(menu, 1);

    struct ChipDef { Source source; char const* key; float width; float x; };
    ChipDef const chips[] = {
        {Source::LevelId, "slot.editor.source_id", 124.f, 64.f},
        {Source::Gmd, "slot.editor.source_gmd", 130.f, 200.f},
    };
    for (auto const& chip : chips) {
        auto* spr = ButtonSprite::create(
            tr(chip.key).c_str(), static_cast<int>(chip.width), true,
            "bigFont.fnt", "GJ_button_01.png", 24.f, 0.5f);
        auto* item = CCMenuItemExt::createSpriteExtra(spr, [this, source = chip.source](CCMenuItemSpriteExtra*) {
            this->setSource(source);
        });
        item->setPosition({chip.x, 14.f});
        menu->addChild(item);
        m_sourceChips.push_back(spr);
    }

    // Level-id input + import button.
    m_idInput = TextInput::create(150.f, tr("slot.editor.level_id_hint").c_str());
    if (m_idInput) {
        m_idInput->setCommonFilter(geode::CommonFilter::Uint);
        m_idInput->setMaxCharCount(10);
        m_idInput->setPosition({77.f, 62.f});
        m_idInput->setID("id-input"_spr);
        if (m_draft.source == Source::LevelId && m_draft.levelId > 0) {
            m_idInput->setString(fmt::format("{}", m_draft.levelId));
        }
        m_idInput->setCallback([this](std::string const& text) {
            auto parsed = numFromString<int>(text);
            m_draft.levelId = (parsed && parsed.unwrap() > 0) ? parsed.unwrap() : 0;
        });
        m_sourceRow->addChild(m_idInput, 1);
    }

    if (auto* dlSpr = CCSprite::createWithSpriteFrameName("GJ_downloadBtn_001.png")) {
        dlSpr->setScale(0.7f);
        auto* importBtn = CCMenuItemExt::createSpriteExtra(dlSpr, [this](CCMenuItemSpriteExtra*) {
            this->onImportById(nullptr);
        });
        auto* importMenu = CCMenu::create();
        importMenu->setPosition({196.f, 62.f});
        importMenu->setContentSize({60.f, 30.f});
        importMenu->setID("import-menu"_spr);
        importMenu->addChild(importBtn);
        m_sourceRow->addChild(importMenu, 1);
    }

    // .gmd filename + browse button.
    m_gmdLabel = CCLabelBMFont::create("", "bigFont.fnt");
    if (m_gmdLabel) {
        m_gmdLabel->setScale(0.4f);
        m_gmdLabel->setAnchorPoint({0.f, 0.5f});
        m_gmdLabel->setPosition({4.f, 24.f});
        m_gmdLabel->setID("gmd-label"_spr);
        m_gmdLabel->limitLabelWidth(140.f, 0.4f, 0.1f);
        m_sourceRow->addChild(m_gmdLabel, 1);
    }

    auto* browseSpr = ButtonSprite::create(
        tr("slot.editor.browse").c_str(), 110, true,
        "bigFont.fnt", "GJ_button_01.png", 24.f, 0.5f);
    auto* browseBtn = CCMenuItemExt::createSpriteExtra(browseSpr, [this](CCMenuItemSpriteExtra*) {
        this->onBrowseGmd(nullptr);
    });
    auto* browseMenu = CCMenu::create();
    browseMenu->setPosition({208.f, 24.f});
    browseMenu->setContentSize({120.f, 30.f});
    browseMenu->setID("browse-menu"_spr);
    browseMenu->addChild(browseBtn);
    m_sourceRow->addChild(browseMenu, 1);
}

void SlotEditorPopup::restyleSourceChips() {
    for (size_t i = 0; i < m_sourceChips.size(); i++) {
        Source const chipSource = (i == 0) ? Source::LevelId : Source::Gmd;
        tintIcon(m_sourceChips[i], m_draft.source == chipSource);
    }
    // The rows stay mounted (no focus loss); the inactive one is dimmed and
    // its buttons stop responding.
    bool const isId = m_draft.source == Source::LevelId;
    if (m_idInput) {
        m_idInput->setVisible(isId);
        m_idInput->setEnabled(isId);
    }
    if (m_gmdLabel) {
        m_gmdLabel->setVisible(!isId);
        m_gmdLabel->setOpacity(!isId ? 255 : 110);
    }
    if (auto* importMenu = typeinfo_cast<CCMenu*>(m_sourceRow->getChildByID("import-menu"_spr))) {
        importMenu->setVisible(isId);
        importMenu->setEnabled(isId);
    }
    if (auto* browseMenu = typeinfo_cast<CCMenu*>(m_sourceRow->getChildByID("browse-menu"_spr))) {
        browseMenu->setVisible(!isId);
        browseMenu->setEnabled(!isId);
    }
}

void SlotEditorPopup::setSource(Source source) {
    if (m_draft.source == source) return;
    m_draft.source = source;
    this->restyleSourceChips();
}

void SlotEditorPopup::refreshGmdLabel() {
    if (!m_gmdLabel) return;
    std::string text = tr("slot.editor.gmd_hint");
    if (!m_pendingGmd.empty()) {
        text = geode::utils::string::pathToString(m_pendingGmd.filename());
    } else if (!m_draft.gmdFile.empty()) {
        text = m_draft.gmdFile;
    }
    m_gmdLabel->setString(text.c_str());
    m_gmdLabel->limitLabelWidth(140.f, 0.4f, 0.1f);
}

// --- name/author ------------------------------------------------------------

void SlotEditorPopup::buildDataRows() {
    auto* layer = m_mainLayer;

    m_nameInput = TextInput::create(268.f, tr("slot.editor.name").c_str());
    if (m_nameInput) {
        m_nameInput->setCommonFilter(geode::CommonFilter::Any);
        if (auto* inner = m_nameInput->getInputNode()) {
            inner->m_allowedChars = geode::getCommonFilterAllowedChars(geode::CommonFilter::Any);
        }
        m_nameInput->setMaxCharCount(60);
        m_nameInput->setPosition({146.f, 140.f});
        m_nameInput->setID("name-input"_spr);
        if (!m_draft.name.empty()) m_nameInput->setString(m_draft.name);
        m_nameInput->setCallback([this](std::string const& text) {
            m_draft.name = text;
            this->refreshPreview();
        });
        layer->addChild(m_nameInput, 3);
    }

    m_authorInput = TextInput::create(268.f, tr("slot.editor.author").c_str());
    if (m_authorInput) {
        m_authorInput->setCommonFilter(geode::CommonFilter::Any);
        if (auto* inner = m_authorInput->getInputNode()) {
            inner->m_allowedChars = geode::getCommonFilterAllowedChars(geode::CommonFilter::Any);
        }
        m_authorInput->setMaxCharCount(40);
        m_authorInput->setPosition({146.f, 110.f});
        m_authorInput->setID("author-input"_spr);
        if (!m_draft.author.empty()) m_authorInput->setString(m_draft.author);
        m_authorInput->setCallback([this](std::string const& text) {
            m_draft.author = text;
            this->refreshPreview();
        });
        layer->addChild(m_authorInput, 3);
    }
}

// --- difficulty + tier face rows --------------------------------------------

void SlotEditorPopup::buildDifficultyRow() {
    auto* layer = m_mainLayer;

    auto* title = CCLabelBMFont::create(tr("slot.editor.difficulty").c_str(), "goldFont.fnt");
    if (title) {
        title->setScale(0.45f);
        title->setAnchorPoint({0.f, 0.5f});
        title->setPosition({12.f, 318.f});
        title->setID("difficulty-title"_spr);
        layer->addChild(title, 3);
    }

    // Two rows of six vanilla faces. Positions are computed from the real
    // sprite size so a texture pack never breaks the layout.
    auto const& diffs = allDifficulties();
    std::vector<CCNode*> faces;
    for (auto diff : diffs) {
        auto* face = GJDifficultySprite::create(difficultyFace(diff), GJDifficultyName::Short);
        if (!face) continue;
        face->updateFeatureState(GJFeatureState::None);
        faces.push_back(face);
    }
    if (faces.empty()) return;

    float const baseW = faces.front()->getContentSize().width;
    float const baseH = faces.front()->getContentSize().height;
    constexpr float kGap = 6.f;
    constexpr size_t kPerRow = 6;
    float const availW = kWidth - 24.f - 90.f; // room past the title
    float scale = std::min(0.6f, 30.f / std::max(1.f, baseH));
    scale = std::min(scale, (availW - kGap * (kPerRow - 1)) / (kPerRow * std::max(1.f, baseW)));

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kWidth, kHeight});
    menu->setID("difficulty-menu"_spr);
    layer->addChild(menu, 3);

    float const rowW = kPerRow * baseW * scale + kGap * (kPerRow - 1);
    float const startX = 100.f + (availW - rowW) / 2.f;
    float const ys[] = {300.f, 268.f};
    for (size_t i = 0; i < faces.size(); i++) {
        size_t const row = i / kPerRow;
        size_t const col = i % kPerRow;
        faces[i]->setScale(scale);
        Difficulty const diff = diffs[i];
        auto* item = CCMenuItemExt::createSpriteExtra(faces[i], [this, diff](CCMenuItemSpriteExtra*) {
            this->setDifficulty(diff);
        });
        item->setPosition({startX + col * (baseW * scale + kGap) + baseW * scale / 2.f, ys[row]});
        item->setContentSize({baseW * scale + kGap, 34.f});
        menu->addChild(item);
    }
    m_difficultyFaces = std::move(faces);
}

void SlotEditorPopup::restyleDifficultyRow() {
    auto const& diffs = allDifficulties();
    for (size_t i = 0; i < m_difficultyFaces.size() && i < diffs.size(); i++) {
        tintIcon(m_difficultyFaces[i], diffs[i] == m_draft.difficulty);
    }
}

void SlotEditorPopup::setDifficulty(Difficulty difficulty) {
    if (m_draft.difficulty == difficulty) return;
    m_draft.difficulty = difficulty;
    this->restyleDifficultyRow();
    this->buildTierRow();
    this->restyleTierRow();
    this->refreshPreview();
}

void SlotEditorPopup::buildTierRow() {
    auto* layer = m_mainLayer;

    if (!m_tierRowBox) {
        m_tierRowBox = CCNode::create();
        m_tierRowBox->setContentSize({kWidth, 34.f});
        m_tierRowBox->setPosition({0.f, 0.f});
        m_tierRowBox->setID("tier-row"_spr);
        layer->addChild(m_tierRowBox, 3);

        auto* title = CCLabelBMFont::create(tr("slot.editor.tier").c_str(), "goldFont.fnt");
        if (title) {
            title->setScale(0.45f);
            title->setAnchorPoint({0.f, 0.5f});
            title->setPosition({12.f, 236.f});
            title->setID("tier-title"_spr);
            layer->addChild(title, 3);
        }
    }
    m_tierRowBox->removeAllChildren();
    m_tierFaces.clear();

    std::vector<std::pair<Tier, GJFeatureState>> const states = {
        {Tier::None, GJFeatureState::None},
        {Tier::Featured, GJFeatureState::Featured},
        {Tier::Epic, GJFeatureState::Epic},
        {Tier::Legendary, GJFeatureState::Legendary},
        {Tier::Mythic, GJFeatureState::Mythic},
    };

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kWidth, kHeight});
    menu->setID("tier-menu"_spr);
    m_tierRowBox->addChild(menu, 1);

    std::vector<CCNode*> faces;
    std::vector<Tier> faceTiers;
    for (auto [tier, state] : states) {
        auto* face = GJDifficultySprite::create(
            difficultyFace(m_draft.difficulty), GJDifficultyName::Short);
        if (!face) continue;
        face->updateFeatureState(state);
        faces.push_back(face);
        faceTiers.push_back(tier);
        m_tierFaces.push_back(face);
    }
    if (faces.empty()) return;

    // The tier row shows the current difficulty face under each rate glow, so
    // rebuilding it here is what keeps the glow preview honest.
    float const baseW = faces.front()->getContentSize().width;
    float const baseH = faces.front()->getContentSize().height;
    constexpr float kGap = 10.f;
    float const availW = kWidth - 24.f - 90.f;
    float scale = std::min(0.6f, 30.f / std::max(1.f, baseH));
    scale = std::min(scale, (availW - kGap * (faces.size() - 1)) / (faces.size() * std::max(1.f, baseW)));

    float const rowW = faces.size() * baseW * scale + kGap * (faces.size() - 1);
    float const startX = 100.f + (availW - rowW) / 2.f;
    for (size_t i = 0; i < faces.size(); i++) {
        faces[i]->setScale(scale);
        Tier const tier = faceTiers[i];
        auto* item = CCMenuItemExt::createSpriteExtra(faces[i], [this, tier](CCMenuItemSpriteExtra*) {
            this->setTier(tier);
        });
        item->setPosition({startX + i * (baseW * scale + kGap) + baseW * scale / 2.f, 236.f});
        item->setContentSize({baseW * scale + kGap, 34.f});
        menu->addChild(item);
    }
}

void SlotEditorPopup::restyleTierRow() {
    auto const& tiers = allTiers();
    for (size_t i = 0; i < m_tierFaces.size() && i < tiers.size(); i++) {
        tintIcon(m_tierFaces[i], tiers[i] == m_draft.tier);
    }
}

void SlotEditorPopup::setTier(Tier tier) {
    if (m_draft.tier == tier) return;
    m_draft.tier = tier;
    this->restyleTierRow();
    this->refreshPreview();
}

// --- stars stepper + slider, coins chip --------------------------------------

void SlotEditorPopup::buildStarsRow() {
    auto* layer = m_mainLayer;

    auto* title = CCLabelBMFont::create(tr("slot.editor.stars").c_str(), "goldFont.fnt");
    if (title) {
        title->setScale(0.45f);
        title->setAnchorPoint({0.f, 0.5f});
        title->setPosition({12.f, 66.f});
        title->setID("stars-title"_spr);
        layer->addChild(title, 3);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kWidth, kHeight});
    menu->setID("stars-menu"_spr);
    layer->addChild(menu, 3);

    auto* minusSpr = ButtonSprite::create("-", 30, true, "bigFont.fnt", "GJ_button_01.png", 22.f, 0.6f);
    auto* minus = CCMenuItemExt::createSpriteExtra(minusSpr, [this](CCMenuItemSpriteExtra*) {
        this->setStars(m_draft.stars - 1);
    });
    minus->setPosition({110.f, 66.f});
    menu->addChild(minus);

    m_starsLabel = CCLabelBMFont::create("0", "bigFont.fnt");
    if (m_starsLabel) {
        m_starsLabel->setScale(0.55f);
        m_starsLabel->setPosition({140.f, 66.f});
        m_starsLabel->setID("stars-label"_spr);
        layer->addChild(m_starsLabel, 3);
    }

    auto* plusSpr = ButtonSprite::create("+", 30, true, "bigFont.fnt", "GJ_button_01.png", 22.f, 0.6f);
    auto* plus = CCMenuItemExt::createSpriteExtra(plusSpr, [this](CCMenuItemSpriteExtra*) {
        this->setStars(m_draft.stars + 1);
    });
    plus->setPosition({170.f, 66.f});
    menu->addChild(plus);

    auto* cb = StarsSliderCallback::create([this](double value) {
        this->setStars(static_cast<int>(std::lround(value)));
    });
    auto* slider = Slider::create(cb, menu_selector(StarsSliderCallback::onChanged), 0.55f);
    cb->m_slider = slider;
    slider->setPosition({265.f, 66.f});
    slider->setValue(m_draft.stars / 100.f);
    slider->setUserObject(cb); // keep the wrapper alive with the slider
    slider->setID("stars-slider"_spr);
    layer->addChild(slider, 3);
    m_starsSlider = slider;

    auto* coinsTitle = CCLabelBMFont::create(tr("slot.editor.coins").c_str(), "goldFont.fnt");
    if (coinsTitle) {
        coinsTitle->setScale(0.45f);
        coinsTitle->setAnchorPoint({0.f, 0.5f});
        coinsTitle->setPosition({352.f, 66.f});
        coinsTitle->setID("coins-title"_spr);
        layer->addChild(coinsTitle, 3);
    }

    if (auto* coins = createCoinRow(0.45f)) {
        auto* chip = CCMenuItemExt::createSpriteExtra(coins, [this](CCMenuItemSpriteExtra*) {
            m_draft.coins = !m_draft.coins;
            tintIcon(m_coinsChip, m_draft.coins);
            this->refreshPreview();
        });
        chip->setPosition({420.f, 66.f});
        chip->setContentSize({44.f, 26.f});
        menu->addChild(chip);
        m_coinsChip = coins;
        tintIcon(m_coinsChip, m_draft.coins);
    }
}

void SlotEditorPopup::setStars(int stars) {
    stars = std::clamp(stars, kMinStars, kMaxStars);
    bool const changed = m_draft.stars != stars;
    m_draft.stars = stars;
    if (m_starsSlider && changed) m_starsSlider->setValue(stars / 100.f);
    this->refreshStarsLabel();
    if (changed) this->refreshPreview();
}

void SlotEditorPopup::refreshStarsLabel() {
    if (!m_starsLabel) return;
    m_starsLabel->setString(fmt::format("{}", m_draft.stars).c_str());
}

// --- live preview -------------------------------------------------------------

void SlotEditorPopup::buildPreviewCard() {
    auto* layer = m_mainLayer;

    auto* title = CCLabelBMFont::create(tr("slot.editor.preview").c_str(), "goldFont.fnt");
    if (title) {
        title->setScale(0.45f);
        title->setAnchorPoint({0.f, 0.5f});
        title->setPosition({292.f, 200.f});
        title->setID("preview-title"_spr);
        layer->addChild(title, 3);
    }

    if (auto* bg = createCardBackground({156.f, 112.f})) {
        bg->setPosition({292.f, 86.f});
        bg->setID("preview-bg"_spr);
        layer->addChild(bg, 1);
    }

    m_previewBox = CCNode::create();
    m_previewBox->setContentSize({156.f, 112.f});
    m_previewBox->setPosition({292.f, 86.f});
    m_previewBox->setID("preview-box"_spr);
    layer->addChild(m_previewBox, 2);

    auto* note = CCLabelBMFont::create(
        tr("slot.editor.cosmetic_note").c_str(), "chatFont.fnt",
        150.f / 0.32f, kCCTextAlignmentCenter);
    if (note) {
        note->setScale(0.32f);
        note->setPosition({370.f, 66.f});
        note->setColor({170, 180, 200});
        note->setOpacity(220);
        note->setID("cosmetic-note"_spr);
        layer->addChild(note, 3);
    }
}

void SlotEditorPopup::refreshPreview() {
    if (!m_previewBox) return;
    m_previewBox->removeAllChildren();

    constexpr float kCX = 78.f;

    if (auto* face = createDifficultyBadge(m_draft.difficulty, m_draft.tier, 0.85f)) {
        face->setPosition({kCX, 80.f});
        m_previewBox->addChild(face);
    }
    if (auto* stars = createStarBadge(m_draft.stars, 0.8f)) {
        stars->setPosition({kCX, 48.f});
        m_previewBox->addChild(stars);
    }
    if (m_draft.coins) {
        if (auto* coins = createCoinRow(0.45f)) {
            coins->setPosition({kCX, 30.f});
            m_previewBox->addChild(coins);
        }
    }

    std::string name = m_draft.name.empty() ? tr("slot.level.unnamed") : m_draft.name;
    auto* nameLbl = CCLabelBMFont::create(name.c_str(), "bigFont.fnt");
    if (nameLbl) {
        nameLbl->limitLabelWidth(140.f, 0.4f, 0.1f);
        nameLbl->setPosition({kCX, 14.f});
        m_previewBox->addChild(nameLbl);
    }
}

// --- footer --------------------------------------------------------------------

void SlotEditorPopup::buildFooter() {
    auto* layer = m_mainLayer;

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kWidth, kHeight});
    menu->setID("footer-menu"_spr);
    layer->addChild(menu, 3);

    float x = kWidth - 12.f;
    auto addButton = [&](char const* key, int width, auto onPress) {
        auto* spr = ButtonSprite::create(
            tr(key).c_str(), width, true,
            "bigFont.fnt", "GJ_button_01.png", 26.f, 0.55f);
        auto* item = CCMenuItemExt::createSpriteExtra(spr, onPress);
        x -= width / 2.f;
        item->setPosition({x, 26.f});
        x -= width / 2.f + 8.f;
        menu->addChild(item);
    };

    addButton("slot.editor.save", 90, [this](CCMenuItemSpriteExtra*) {
        this->onSave(nullptr);
    });
    addButton("slot.editor.surprise", 110, [this](CCMenuItemSpriteExtra*) {
        this->onSurprise(nullptr);
    });
    addButton("slot.editor.test", 85, [this](CCMenuItemSpriteExtra*) {
        this->onTest(nullptr);
    });
    if (m_draft.replacesOfficialId != 0) {
        addButton("slot.editor.hide_official", 130, [this](CCMenuItemSpriteExtra*) {
            this->onHideOfficial(nullptr);
        });
    }
}

// --- actions ----------------------------------------------------------------------

void SlotEditorPopup::onImportById(CCObject*) {
    int const levelId = m_draft.levelId;
    if (levelId <= 0) {
        toast(tr("slot.editor.invalid_id"), NotificationIcon::Warning);
        return;
    }
    m_draft.source = Source::LevelId;
    this->restyleSourceChips();
    this->showSpinner(true);

    geode::WeakRef<CCNode> weakThis(this);
    SlotDownloads::get().fetch(levelId, [weakThis](GJGameLevel* level) {
        geode::Loader::get()->queueInMainThread([weakThis, level] {
            auto ref = weakThis.lock();
            if (!ref) return;
            auto* self = typeinfo_cast<SlotEditorPopup*>(ref.data());
            if (!self || !self->m_alive.load()) return;
            self->showSpinner(false);
            if (!level) {
                toast(tr("slot.editor.import_failed"), NotificationIcon::Error);
                return;
            }
            self->prefillFromLevel(level);
            toast(tr("slot.editor.imported"), NotificationIcon::Success);
        });
    });
}

void SlotEditorPopup::onBrowseGmd(CCObject*) {
    geode::WeakRef<CCNode> weakThis(this);
    pt::pickGmd([weakThis](Result<std::optional<std::filesystem::path>> res) {
        geode::Loader::get()->queueInMainThread([weakThis, res] {
            auto ref = weakThis.lock();
            if (!ref) return;
            auto* self = typeinfo_cast<SlotEditorPopup*>(ref.data());
            if (!self || !self->m_alive.load()) return;
            if (!res) return;
            auto picked = res.unwrap();
            if (!picked.has_value()) return;
            self->prefillFromGmd(picked.value());
        });
    });
}

void SlotEditorPopup::prefillFromLevel(GJGameLevel* level) {
    if (!level) return;
    m_draft.source = Source::LevelId;
    m_draft.levelId = level->m_levelID.value();
    std::string name = level->m_levelName.c_str();
    std::string author = level->m_creatorName.c_str();
    if (!name.empty()) m_draft.name = name;
    if (!author.empty() && author != "-") m_draft.author = author;
    m_draft.difficulty = difficultyFromFace(levelFaceValue(level));
    m_draft.tier = levelTier(level);
    m_draft.stars = std::clamp(level->m_stars.value(), kMinStars, kMaxStars);
    m_draft.coins = level->m_coins > 0;

    if (m_idInput) m_idInput->setString(fmt::format("{}", m_draft.levelId));
    if (m_nameInput) m_nameInput->setString(m_draft.name);
    if (m_authorInput) m_authorInput->setString(m_draft.author);
    if (m_starsSlider) m_starsSlider->setValue(m_draft.stars / 100.f);
    tintIcon(m_coinsChip, m_draft.coins);

    this->restyleSourceChips();
    this->restyleDifficultyRow();
    this->buildTierRow();
    this->restyleTierRow();
    this->refreshStarsLabel();
    this->refreshPreview();
}

void SlotEditorPopup::prefillFromGmd(std::filesystem::path const& path) {
    auto info = readGmdInfo(path);
    if (!info) {
        toast(tr("slot.editor.import_failed"), NotificationIcon::Error);
        return;
    }
    // The file is only copied into our folder on Save/Test; until then the
    // pending path is just a draft field, so cancelling costs nothing.
    m_pendingGmd = path;
    m_draft.source = Source::Gmd;
    if (!info->name.empty()) {
        m_draft.name = info->name;
        if (m_nameInput) m_nameInput->setString(m_draft.name);
    }
    if (!info->author.empty()) {
        m_draft.author = info->author;
        if (m_authorInput) m_authorInput->setString(m_draft.author);
    }
    this->restyleSourceChips();
    this->refreshGmdLabel();
    this->refreshPreview();
    toast(tr("slot.editor.imported"), NotificationIcon::Success);
}

void SlotEditorPopup::onSurprise(CCObject*) {
    static std::mt19937 rng{std::random_device{}()};

    auto const& diffs = allDifficulties();
    // Easy..ExtremeDemon: the surprise should always draw a real face.
    std::uniform_int_distribution<size_t> diffDist(2, diffs.size() - 1);
    m_draft.difficulty = diffs[diffDist(rng)];

    // Weighted towards unrated tiers so mythic stays special.
    std::array<Tier, 8> const tierBag = {
        Tier::None, Tier::None, Tier::None,
        Tier::Featured, Tier::Featured,
        Tier::Epic, Tier::Legendary, Tier::Mythic,
    };
    std::uniform_int_distribution<size_t> tierDist(0, tierBag.size() - 1);
    m_draft.tier = tierBag[tierDist(rng)];

    // Vanilla-ish star ranges per difficulty, with a little jitter.
    int low = 2, high = 3;
    switch (m_draft.difficulty) {
        case Difficulty::Easy:         low = 2;  high = 3;  break;
        case Difficulty::Normal:       low = 4;  high = 5;  break;
        case Difficulty::Hard:         low = 6;  high = 7;  break;
        case Difficulty::Harder:       low = 8;  high = 9;  break;
        case Difficulty::Insane:       low = 10; high = 12; break;
        default:                       low = 10; high = 14; break; // demons
    }
    std::uniform_int_distribution<int> starDist(low, high);
    m_draft.stars = starDist(rng);
    std::bernoulli_distribution coinDist(0.35);
    m_draft.coins = coinDist(rng);

    if (m_starsSlider) m_starsSlider->setValue(m_draft.stars / 100.f);
    tintIcon(m_coinsChip, m_draft.coins);
    this->restyleDifficultyRow();
    this->buildTierRow();
    this->restyleTierRow();
    this->refreshStarsLabel();
    this->refreshPreview();
}

std::string SlotEditorPopup::saveDraft() {
    Slot slot = m_draft;
    slot.stars = std::clamp(slot.stars, kMinStars, kMaxStars);

    auto& store = SlotStore::get();
    if (slot.source == Source::LevelId) {
        if (slot.levelId <= 0) {
            toast(tr("slot.editor.invalid_id"), NotificationIcon::Warning);
            return {};
        }
        if (!slot.gmdFile.empty()) {
            store.discardGmd(slot.gmdFile);
            slot.gmdFile.clear();
        }
    } else {
        std::string file = slot.gmdFile;
        if (!m_pendingGmd.empty()) {
            auto imported = store.importGmd(m_pendingGmd);
            if (!imported) {
                toast(tr("slot.editor.import_failed"), NotificationIcon::Error);
                return {};
            }
            file = *imported;
            m_pendingGmd.clear();
        }
        if (file.empty()) {
            toast(tr("slot.editor.pick_gmd_first"), NotificationIcon::Warning);
            return {};
        }
        if (file != slot.gmdFile && !slot.gmdFile.empty()) {
            store.discardGmd(slot.gmdFile);
        }
        slot.gmdFile = file;
    }

    std::string id = slot.id;
    if (m_isNew || id.empty()) {
        slot.id.clear(); // the store assigns a stable uuid
        id = store.add(slot);
    } else {
        slot.id = id;
        if (!store.update(slot)) {
            // Deleted elsewhere while we edited; re-add instead of losing it.
            slot.id.clear();
            id = store.add(slot);
        }
    }
    if (id.empty()) {
        toast(tr("slot.editor.import_failed"), NotificationIcon::Error);
        return {};
    }

    m_draft = slot;
    m_draft.id = id;
    m_isNew = false;
    SlotLevelCache::get().invalidate(id);
    refreshOfficialList();
    if (m_onSaved) m_onSaved();
    return id;
}

void SlotEditorPopup::onTest(CCObject*) {
    // Test plays the saved slot: an unsaved draft has no cache entry for the
    // download callback to find, so commit first. The toast says so.
    std::string const id = this->saveDraft();
    if (id.empty()) return;
    toast(tr("slot.editor.saved"), NotificationIcon::Success);
    if (auto slot = SlotStore::get().find(id)) {
        openSlotLevel(*slot);
    }
}

void SlotEditorPopup::onSave(CCObject*) {
    if (this->saveDraft().empty()) return;
    toast(tr("slot.editor.saved"), NotificationIcon::Success);
    this->keyBackClicked();
}

void SlotEditorPopup::onHideOfficial(CCObject*) {
    int const officialId = m_draft.replacesOfficialId;
    if (!isOfficialId(officialId)) {
        this->keyBackClicked();
        return;
    }
    auto& store = SlotStore::get();
    // Hiding wins over replacing: drop the replacement so no orphan slot
    // lingers behind the hidden page.
    if (auto existing = store.slotForOfficial(officialId)) {
        if (!existing->gmdFile.empty()) store.discardGmd(existing->gmdFile);
        SlotLevelCache::get().invalidate(existing->id);
        store.remove(existing->id);
    }
    store.setOfficialHidden(officialId, true);
    refreshOfficialList();
    toast(tr("slot.official.hidden"), NotificationIcon::Success);
    if (m_onSaved) m_onSaved();
    this->keyBackClicked();
}

void SlotEditorPopup::showSpinner(bool show) {
    if (show) {
        if (m_spinner) return;
        m_spinner = geode::LoadingSpinner::create(24.f);
        if (!m_spinner) return;
        m_spinner->setPosition({kWidth / 2.f, kHeight / 2.f});
        m_spinner->setZOrder(100);
        m_spinner->setID("import-spinner"_spr);
        m_mainLayer->addChild(m_spinner, 100);
    } else {
        if (m_spinner) {
            m_spinner->removeFromParent();
            m_spinner = nullptr;
        }
    }
}

} // namespace paimon::officialslots::ui
