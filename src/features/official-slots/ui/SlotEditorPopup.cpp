#include "../../../framework/compat/NativeUI.hpp"
#include "../../../utils/FilesystemPath.hpp"
#include "SlotEditorPopup.hpp"

#include "SlotVisuals.hpp"
#include "../services/GmdImporter.hpp"
#include "../services/OfficialSlotStore.hpp"
#include "../services/SlotLevels.hpp"
#include "../services/SlotListRefresh.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/utils/general.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <random>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::officialslots::ui {

namespace {

constexpr float kWidth = 560.f;
constexpr float kHeight = 380.f;
constexpr float kFormWidth = 296.f;
constexpr float kCatalogueX = 328.f;
constexpr float kCatalogueWidth = 216.f;
constexpr float kPlacementRowHeight = 28.f;

// only the highlighted row starts a drag; the rest of the catalogue scrolls normally.
class PlacementScrollLayer : public ScrollLayer {
public:
    std::function<void(std::size_t)> onPlace;
    std::size_t selectedIndex = 0;
    std::size_t rowCount = 1;

    static PlacementScrollLayer* create(CCSize size) {
        auto* list = new PlacementScrollLayer({0.f, 0.f, size.width, size.height});
        list->setAnchorPoint({0.f, 0.f});
        list->autorelease();
        list->setStealingTouches(true);
        return list;
    }

    bool isDragging() const { return m_dragging; }

protected:
    explicit PlacementScrollLayer(CCRect rect) : ScrollLayer(rect, true, true) {}

    bool ccTouchBegan(CCTouch* touch, CCEvent* event) override {
        auto const point = this->convertToNodeSpace(touch->getLocation());
        if (!geode::cocos::nodeIsVisible(this) ||
            !CCRect{0.f, 0.f, getContentWidth(), getContentHeight()}.containsPoint(point)) return false;
        auto const contentPoint = m_contentLayer->convertToNodeSpace(touch->getLocation());
        auto const index = static_cast<int>(std::floor(
            (m_contentLayer->getContentHeight() - contentPoint.y) / kPlacementRowHeight));
        m_dragging = index >= 0 && static_cast<std::size_t>(index) == selectedIndex;
        return m_dragging || ScrollLayer::ccTouchBegan(touch, event);
    }

    void ccTouchMoved(CCTouch* touch, CCEvent* event) override {
        if (!m_dragging) {
            ScrollLayer::ccTouchMoved(touch, event);
            return;
        }
        auto const point = this->convertToNodeSpace(touch->getLocation());
        float const height = m_contentLayer->getContentHeight();
        float const viewport = getContentHeight();
        float offset = m_contentLayer->getPositionY();
        if (point.y < 14.f) offset += 12.f;
        else if (point.y > viewport - 14.f) offset -= 12.f;
        m_contentLayer->setPositionY(std::clamp(offset, std::min(0.f, viewport - height), 0.f));
        auto const contentPoint = m_contentLayer->convertToNodeSpace(touch->getLocation());
        int const index = std::clamp(static_cast<int>(std::floor(
            (height - contentPoint.y) / kPlacementRowHeight)), 0, static_cast<int>(rowCount) - 1);
        if (static_cast<std::size_t>(index) != selectedIndex && onPlace) {
            selectedIndex = static_cast<std::size_t>(index);
            onPlace(selectedIndex + 1);
        }
    }

    void ccTouchEnded(CCTouch* touch, CCEvent* event) override {
        if (!m_dragging) ScrollLayer::ccTouchEnded(touch, event);
        m_dragging = false;
    }

    void ccTouchCancelled(CCTouch* touch, CCEvent* event) override {
        if (!m_dragging) ScrollLayer::ccTouchCancelled(touch, event);
        m_dragging = false;
    }

private:
    bool m_dragging = false;
};

ButtonSprite* formButton(std::string const& text, float width, char const* background,
    float height = 22.f) {
    auto* button = ButtonSprite::create(text.c_str(), static_cast<int>(width), true,
        "bigFont.fnt", background, height, 0.42f);
    if (button) {
        auto const size = button->getContentSize();
        button->setScale(std::min({1.f, width / std::max(1.f, size.width),
            height / std::max(1.f, size.height)}));
    }
    return button;
}

CCMenu* makeMenu(CCNode* parent, char const* id, CCSize size) {
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize(size);
    menu->setID(id);
    parent->addChild(menu, 3);
    return menu;
}

constexpr ccColor3B kOnColor = {255, 255, 255};
constexpr ccColor3B kOffColor = {96, 102, 120};

// dimmed = not picked. same trick as the request filter rows: white vs grey
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

// slider callback wrapper; slider::create wants a ccnode target with a
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

int clampFace(int face) {
    return std::clamp(face, -1, 10);
}

Difficulty difficultyFromFace(int face) {
    return static_cast<Difficulty>(clampFace(face));
}

// mirrors the community leaderboard mapping so an import prefill draws the
// face the game itself would draw.
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
    return paimon::compat::averageDifficulty(level);
}

Tier levelTier(GJGameLevel* level) {
    if (!level) return Tier::None;
    // m_isepic is a tier, not a flag: 1 epic, 2 legendary, 3 mythic.
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
    if (!PaimonPopup::init(kWidth, kHeight)) return false;
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

    if (m_draft.replacesOfficialId != 0) {
        this->setTitle(fmt::format(fmt::runtime(tr("slot.editor.replace_title")),
                                   officialName(m_draft.replacesOfficialId)));
    } else if (m_isNew) {
        this->setTitle(tr("slot.editor.add_title"));
    } else {
        this->setTitle(tr("slot.editor.title"));
    }

    this->addInfoButton(tr("slot.editor.title"),
        "Edita como se ve un slot oficial personalizado: elige <cy>fuente</c> "
        "(ID o archivo .gmd), dificultad, tier de rate, estrellas y monedas. "
        "El recuadro verde muestra su posicion entre los demas niveles. "
        "Arrastra el recuadro, toca una fila o usa las flechas para moverlo. "
        "<cg>Guardar</c> aplica los cambios.");

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

void SlotEditorPopup::buildSourceRow() {
    auto* layer = m_mainLayer;

    m_sourceRow = CCNode::create();
    m_sourceRow->setContentSize({kFormWidth, 84.f});
    m_sourceRow->setPosition({16.f, 264.f});
    m_sourceRow->setID("source-row"_spr);
    layer->addChild(m_sourceRow, 3);

    // source chips: two text buttons, the inactive one dimmed in place.
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kFormWidth, 84.f});
    menu->setID("source-menu"_spr);
    m_sourceRow->addChild(menu, 1);

    struct ChipDef { Source source; char const* key; float width; float x; };
    ChipDef const chips[] = {
        {Source::LevelId, "slot.editor.source_id", 142.f, 71.f},
        {Source::Gmd, "slot.editor.source_gmd", 142.f, 225.f},
    };
    for (auto const& chip : chips) {
        auto* spr = formButton(tr(chip.key), chip.width, "GJ_button_01.png");
        auto* item = CCMenuItemExt::createSpriteExtra(spr, [this, source = chip.source](CCMenuItemSpriteExtra*) {
            this->setSource(source);
        });
        item->setPosition({chip.x, 68.f});
        menu->addChild(item);
        m_sourceChips.push_back(spr);
    }

    // source row controls: toggled between online Level ID download and local .gmd file picker.
    m_idInput = TextInput::create(246.f, tr("slot.editor.level_id_hint").c_str());
    if (m_idInput) {
        m_idInput->setCommonFilter(geode::CommonFilter::Uint);
        m_idInput->setMaxCharCount(10);
        m_idInput->setPosition({123.f, 32.f});
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
        dlSpr->setScale(0.65f);
        auto* importBtn = CCMenuItemExt::createSpriteExtra(dlSpr, [this](CCMenuItemSpriteExtra*) {
            this->onImportById(nullptr);
        });
        auto* importMenu = CCMenu::create();
        importMenu->setPosition({0.f, 0.f});
        importMenu->setContentSize({kFormWidth, 84.f});
        importBtn->setPosition({274.f, 32.f});
        importMenu->setID("import-menu"_spr);
        importMenu->addChild(importBtn);
        m_sourceRow->addChild(importMenu, 1);
    }

    m_gmdLabel = CCLabelBMFont::create("", "bigFont.fnt");
    if (m_gmdLabel) {
        m_gmdLabel->setScale(0.4f);
        m_gmdLabel->setAnchorPoint({0.f, 0.5f});
        m_gmdLabel->setPosition({4.f, 32.f});
        m_gmdLabel->setID("gmd-label"_spr);
        m_gmdLabel->limitLabelWidth(180.f, 0.4f, 0.1f);
        m_sourceRow->addChild(m_gmdLabel, 1);
    }

    auto* browseSpr = formButton(tr("slot.editor.browse"), 102.f, "GJ_button_01.png");
    auto* browseBtn = CCMenuItemExt::createSpriteExtra(browseSpr, [this](CCMenuItemSpriteExtra*) {
        this->onBrowseGmd(nullptr);
    });
    auto* browseMenu = CCMenu::create();
    browseMenu->setPosition({0.f, 0.f});
    browseMenu->setContentSize({kFormWidth, 84.f});
    browseBtn->setPosition({245.f, 32.f});
    browseMenu->setID("browse-menu"_spr);
    browseMenu->addChild(browseBtn);
    m_sourceRow->addChild(browseMenu, 1);
}

void SlotEditorPopup::restyleSourceChips() {
    for (size_t i = 0; i < m_sourceChips.size(); i++) {
        Source const chipSource = (i == 0) ? Source::LevelId : Source::Gmd;
        tintIcon(m_sourceChips[i], m_draft.source == chipSource);
    }
    // the rows stay mounted (no focus loss); the inactive one is dimmed and
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
    ++m_importRequest;
    this->showSpinner(false);
    m_draft.source = source;
    this->restyleSourceChips();
}

void SlotEditorPopup::refreshGmdLabel() {
    if (!m_gmdLabel) return;
    std::string text = tr("slot.editor.gmd_hint");
    if (!m_pendingGmd.empty()) {
        text = paimon::paths::toUtf8(m_pendingGmd.filename());
    } else if (!m_draft.gmdFile.empty()) {
        text = m_draft.gmdFile;
    }
    m_gmdLabel->setString(text.c_str());
    m_gmdLabel->limitLabelWidth(180.f, 0.4f, 0.1f);
}

void SlotEditorPopup::buildDataRows() {
    auto* layer = m_mainLayer;

    m_nameInput = TextInput::create(kFormWidth, tr("slot.editor.name").c_str());
    if (m_nameInput) {
        m_nameInput->setCommonFilter(geode::CommonFilter::Any);
        m_nameInput->setMaxCharCount(60);
        m_nameInput->setPosition({164.f, 258.f});
        m_nameInput->setID("name-input"_spr);
        if (!m_draft.name.empty()) m_nameInput->setString(m_draft.name);
        m_nameInput->setCallback([this](std::string const& text) {
            m_draft.name = text;
            this->refreshPreview();
        });
        layer->addChild(m_nameInput, 3);
    }

    m_authorInput = TextInput::create(kFormWidth, tr("slot.editor.author").c_str());
    if (m_authorInput) {
        m_authorInput->setCommonFilter(geode::CommonFilter::Any);
        m_authorInput->setMaxCharCount(40);
        m_authorInput->setPosition({164.f, 226.f});
        m_authorInput->setID("author-input"_spr);
        if (!m_draft.author.empty()) m_authorInput->setString(m_draft.author);
        m_authorInput->setCallback([this](std::string const& text) {
            m_draft.author = text;
            this->refreshPreview();
        });
        layer->addChild(m_authorInput, 3);
    }
}

void SlotEditorPopup::buildDifficultyRow() {
    auto* layer = m_mainLayer;

    auto* title = CCLabelBMFont::create(tr("slot.editor.difficulty").c_str(), "goldFont.fnt");
    if (title) {
        title->setScale(0.45f);
        title->setAnchorPoint({0.f, 0.5f});
        title->setPosition({16.f, 199.f});
        title->setID("difficulty-title"_spr);
        layer->addChild(title, 3);
    }

    // two rows of six vanilla faces. positions are computed from the real
    // sprite size so a texture pack never breaks the layout.
    auto const& diffs = allDifficulties();
    std::vector<CCNode*> faces;
    for (auto diff : diffs) {
        auto* face = createDifficultyBadge(diff, Tier::None, 1.f);
        if (!face) continue;
        face->setTag(static_cast<int>(diff));
        faces.push_back(face);
    }
    if (faces.empty()) return;

    float const baseW = faces.front()->getContentSize().width;
    float const baseH = faces.front()->getContentSize().height;
    constexpr float kGap = 6.f;
    constexpr size_t kPerRow = 6;
    float const availW = kFormWidth;
    float scale = std::min(0.48f, 24.f / std::max(1.f, baseH));
    scale = std::min(scale, (availW - kGap * (kPerRow - 1)) / (kPerRow * std::max(1.f, baseW)));

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kWidth, kHeight});
    menu->setID("difficulty-menu"_spr);
    layer->addChild(menu, 3);

    float const rowW = kPerRow * baseW * scale + kGap * (kPerRow - 1);
    float const startX = 16.f + (availW - rowW) / 2.f;
    float const ys[] = {177.f, 145.f};
    for (size_t i = 0; i < faces.size(); i++) {
        size_t const row = i / kPerRow;
        size_t const col = i % kPerRow;
        faces[i]->setScale(scale);
        Difficulty const diff = static_cast<Difficulty>(faces[i]->getTag());
        auto* item = CCMenuItemExt::createSpriteExtra(faces[i], [this, diff](CCMenuItemSpriteExtra*) {
            this->setDifficulty(diff);
        });
        item->setPosition({startX + col * (baseW * scale + kGap) + baseW * scale / 2.f, ys[row]});
        menu->addChild(item);
    }
    m_difficultyFaces = std::move(faces);
}

void SlotEditorPopup::restyleDifficultyRow() {
    for (auto* face : m_difficultyFaces) {
        tintIcon(face, face->getTag() == static_cast<int>(m_draft.difficulty));
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
            title->setPosition({16.f, 118.f});
            title->setID("tier-title"_spr);
            layer->addChild(title, 3);
        }
    }
    m_tierRowBox->removeAllChildren();
    m_tierFaces.clear();

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kWidth, kHeight});
    menu->setID("tier-menu"_spr);
    m_tierRowBox->addChild(menu, 1);

    std::vector<CCNode*> faces;
    for (auto tier : allTiers()) {
        auto* face = createDifficultyBadge(m_draft.difficulty, tier, 1.f);
        if (!face) continue;
        face->setTag(static_cast<int>(tier));
        faces.push_back(face);
        m_tierFaces.push_back(face);
    }
    if (faces.empty()) return;

    // the tier row shows the current difficulty face under each rate glow, so
    // rebuilding it here is what keeps the glow preview honest.
    float const baseW = faces.front()->getContentSize().width;
    float const baseH = faces.front()->getContentSize().height;
    constexpr float kGap = 10.f;
    float const availW = kFormWidth;
    float scale = std::min(0.46f, 22.f / std::max(1.f, baseH));
    scale = std::min(scale, (availW - kGap * (faces.size() - 1)) / (faces.size() * std::max(1.f, baseW)));

    float const rowW = faces.size() * baseW * scale + kGap * (faces.size() - 1);
    float const startX = 16.f + (availW - rowW) / 2.f;
    for (size_t i = 0; i < faces.size(); i++) {
        faces[i]->setScale(scale);
        Tier const tier = static_cast<Tier>(faces[i]->getTag());
        auto* item = CCMenuItemExt::createSpriteExtra(faces[i], [this, tier](CCMenuItemSpriteExtra*) {
            this->setTier(tier);
        });
        item->setPosition({startX + i * (baseW * scale + kGap) + baseW * scale / 2.f, 92.f});
        menu->addChild(item);
    }
}

void SlotEditorPopup::restyleTierRow() {
    for (auto* face : m_tierFaces) {
        tintIcon(face, face->getTag() == static_cast<int>(m_draft.tier));
    }
}

void SlotEditorPopup::setTier(Tier tier) {
    if (m_draft.tier == tier) return;
    m_draft.tier = tier;
    this->restyleTierRow();
    this->refreshPreview();
}

void SlotEditorPopup::buildStarsRow() {
    auto* layer = m_mainLayer;

    auto* title = CCLabelBMFont::create(tr("slot.editor.stars").c_str(), "goldFont.fnt");
    if (title) {
        title->setScale(0.45f);
        title->setAnchorPoint({0.f, 0.5f});
        title->setPosition({16.f, 54.f});
        title->setID("stars-title"_spr);
        layer->addChild(title, 3);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kWidth, kHeight});
    menu->setID("stars-menu"_spr);
    layer->addChild(menu, 3);

    auto* minusSpr = formButton("-", 24.f, "GJ_button_04.png");
    auto* minus = CCMenuItemExt::createSpriteExtra(minusSpr, [this](CCMenuItemSpriteExtra*) {
        this->setStars(m_draft.stars - 1);
    });
    minus->setPosition({94.f, 54.f});
    menu->addChild(minus);

    m_starsLabel = CCLabelBMFont::create("0", "bigFont.fnt");
    if (m_starsLabel) {
        m_starsLabel->setScale(0.45f);
        m_starsLabel->setPosition({123.f, 54.f});
        m_starsLabel->setID("stars-label"_spr);
        layer->addChild(m_starsLabel, 3);
    }

    auto* plusSpr = formButton("+", 24.f, "GJ_button_04.png");
    auto* plus = CCMenuItemExt::createSpriteExtra(plusSpr, [this](CCMenuItemSpriteExtra*) {
        this->setStars(m_draft.stars + 1);
    });
    plus->setPosition({152.f, 54.f});
    menu->addChild(plus);

    auto* cb = StarsSliderCallback::create([this](double value) {
        this->setStars(static_cast<int>(std::lround(value)));
    });
    auto* slider = Slider::create(cb, menu_selector(StarsSliderCallback::onChanged), 0.32f);
    cb->m_slider = slider;
    slider->setPosition({210.f, 54.f});
    slider->setValue(m_draft.stars / 100.f);
    slider->setUserObject(cb); // keep the wrapper alive with the slider
    slider->setID("stars-slider"_spr);
    layer->addChild(slider, 3);
    m_starsSlider = slider;

    auto* coinsTitle = CCLabelBMFont::create(tr("slot.editor.coins").c_str(), "goldFont.fnt");
    if (coinsTitle) {
        coinsTitle->setScale(0.35f);
        coinsTitle->setAnchorPoint({0.f, 0.5f});
        coinsTitle->setPosition({267.f, 64.f});
        coinsTitle->setID("coins-title"_spr);
        layer->addChild(coinsTitle, 3);
    }

    if (auto* coins = createCoinRow(0.32f)) {
        auto* chip = CCMenuItemExt::createSpriteExtra(coins, [this](CCMenuItemSpriteExtra*) {
            m_draft.coins = !m_draft.coins;
            tintIcon(m_coinsChip, m_draft.coins);
            this->refreshPreview();
        });
        chip->setPosition({288.f, 47.f});
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

void SlotEditorPopup::buildPreviewCard() {
    auto* layer = m_mainLayer;

    auto* title = CCLabelBMFont::create(tr("slot.editor.preview").c_str(), "goldFont.fnt");
    if (title) {
        title->setScale(0.45f);
        title->setAnchorPoint({0.f, 0.5f});
        title->setPosition({kCatalogueX, 350.f});
        title->setID("preview-title"_spr);
        layer->addChild(title, 3);
    }

    if (auto* bg = createCardBackground({kCatalogueWidth, 88.f})) {
        bg->setAnchorPoint({0.f, 0.f});
        bg->setPosition({kCatalogueX, 252.f});
        bg->setID("preview-bg"_spr);
        layer->addChild(bg, 1);
    }

    m_previewBox = CCNode::create();
    m_previewBox->setContentSize({kCatalogueWidth, 88.f});
    m_previewBox->setPosition({kCatalogueX, 252.f});
    m_previewBox->setID("preview-box"_spr);
    layer->addChild(m_previewBox, 2);

    auto* note = CCLabelBMFont::create(
        tr("slot.editor.cosmetic_note").c_str(), "chatFont.fnt",
        kCatalogueWidth / 0.32f, kCCTextAlignmentCenter);
    if (note) {
        note->setScale(0.32f);
        note->setPosition({kCatalogueX + kCatalogueWidth / 2.f, 48.f});
        note->setColor({170, 180, 200});
        note->setOpacity(220);
        note->setID("cosmetic-note"_spr);
        layer->addChild(note, 3);
    }
}

void SlotEditorPopup::refreshPreview() {
    if (!m_previewBox) return;
    m_previewBox->removeAllChildren();

    constexpr float kCX = kCatalogueWidth / 2.f;

    if (auto* face = createDifficultyBadge(m_draft.difficulty, m_draft.tier, 0.65f)) {
        face->setPosition({42.f, 54.f});
        m_previewBox->addChild(face);
    }
    if (auto* stars = createStarBadge(m_draft.stars, 0.8f)) {
        stars->setPosition({110.f, 54.f});
        m_previewBox->addChild(stars);
    }
    if (m_draft.coins) {
        if (auto* coins = createCoinRow(0.38f)) {
            coins->setPosition({174.f, 54.f});
            m_previewBox->addChild(coins);
        }
    }

    std::string name = m_draft.name.empty() ? tr("slot.level.unnamed") : m_draft.name;
    auto* nameLbl = CCLabelBMFont::create(name.c_str(), "bigFont.fnt");
    if (nameLbl) {
        nameLbl->limitLabelWidth(kCatalogueWidth - 16.f, 0.4f, 0.1f);
        nameLbl->setPosition({kCX, 14.f});
        m_previewBox->addChild(nameLbl);
    }
    if (m_positionScroll) this->rebuildPositionList();
}

void SlotEditorPopup::buildFooter() {
    auto* layer = m_mainLayer;

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kWidth, kHeight});
    menu->setID("footer-menu"_spr);
    layer->addChild(menu, 3);

    float x = kWidth - 16.f;
    auto addButton = [&](char const* key, int width, char const* bg, auto onPress) {
        auto* spr = formButton(tr(key), static_cast<float>(width), bg);
        auto* item = CCMenuItemExt::createSpriteExtra(spr, onPress);
        x -= width / 2.f;
        item->setPosition({x, 22.f});
        x -= width / 2.f + 8.f;
        menu->addChild(item);
    };

    addButton("slot.editor.save", 88, "GJ_button_01.png", [this](CCMenuItemSpriteExtra*) {
        this->onSave(nullptr);
    });
    addButton("slot.editor.surprise", 108, "GJ_button_03.png", [this](CCMenuItemSpriteExtra*) {
        this->onSurprise(nullptr);
    });
    addButton("slot.editor.test", 72, "GJ_button_02.png", [this](CCMenuItemSpriteExtra*) {
        this->onTest(nullptr);
    });
    if (m_draft.replacesOfficialId != 0) {
        addButton("slot.editor.hide_official", 184, "GJ_button_06.png", [this](CCMenuItemSpriteExtra*) {
            this->onHideOfficial(nullptr);
        });
    }
    this->buildPositionRow(menu);
}

std::string SlotEditorPopup::pageKey() const {
    return m_draft.replacesOfficialId != 0
        ? SlotStore::officialKey(m_draft.replacesOfficialId)
        : SlotStore::slotKey(m_draft.id);
}

void SlotEditorPopup::buildPositionRow(CCMenu* menu) {
    auto& store = SlotStore::get();
    auto const currentPosition = store.visiblePosition(this->pageKey());
    m_positionMax = std::max(std::size_t{1}, store.visiblePages().size() + (currentPosition == 0 ? 1 : 0));
    m_position = currentPosition != 0 ? currentPosition : m_positionMax;

    if (auto* title = CCLabelBMFont::create(tr("slot.editor.position").c_str(), "goldFont.fnt")) {
        title->setScale(0.4f);
        title->setAnchorPoint({0.f, 0.5f});
        title->setPosition({kCatalogueX, 234.f});
        title->setID("position-title"_spr);
        m_mainLayer->addChild(title, 3);
    }
    auto addStep = [&](char const* text, float x, auto onPress) {
        auto* spr = formButton(text, 24.f, "GJ_button_04.png", 20.f);
        auto* item = CCMenuItemExt::createSpriteExtra(spr, onPress);
        item->setPosition({x, 234.f});
        menu->addChild(item);
    };
    addStep("^", 500.f, [this](CCMenuItemSpriteExtra*) {
        if (m_position > 1) this->setSlotPosition(m_position - 1);
    });

    m_positionLabel = CCLabelBMFont::create("", "bigFont.fnt");
    if (m_positionLabel) {
        m_positionLabel->setScale(0.42f);
        m_positionLabel->setPosition({448.f, 234.f});
        m_positionLabel->setID("position-label"_spr);
        m_mainLayer->addChild(m_positionLabel, 3);
    }

    addStep("v", 530.f, [this](CCMenuItemSpriteExtra*) {
        this->setSlotPosition(m_position + 1);
    });
    if (auto* hint = CCLabelBMFont::create(tr("slot.editor.position_hint").c_str(), "chatFont.fnt")) {
        hint->setPosition({kCatalogueX + kCatalogueWidth / 2.f, 216.f});
        hint->limitLabelWidth(kCatalogueWidth, 0.4f, 0.2f);
        m_mainLayer->addChild(hint, 3);
    }
    auto* panel = paimon::ui::makeInset({kCatalogueWidth, 146.f}, 100);
    panel->setPosition({kCatalogueX, 64.f});
    m_mainLayer->addChild(panel, 1);
    auto* placement = PlacementScrollLayer::create({kCatalogueWidth - 6.f, 140.f});
    placement->onPlace = [this](std::size_t position) { this->setSlotPosition(position); };
    m_positionScroll = placement;
    if (m_positionScroll) {
        m_positionScroll->setPosition({kCatalogueX + 3.f, 67.f});
        m_positionScroll->setID("position-list"_spr);
        m_mainLayer->addChild(m_positionScroll, 3);
        this->rebuildPositionList(true);
    }
    this->refreshPositionLabel();
}

void SlotEditorPopup::rebuildPositionList(bool revealSelection) {
    if (!m_positionScroll) return;
    auto* content = m_positionScroll->m_contentLayer;
    float const previousY = content->getPositionY();
    content->removeAllChildren();
    auto& store = SlotStore::get();
    auto pages = store.visiblePages();
    auto const ownKey = this->pageKey();
    std::erase(pages, ownKey);
    auto const index = std::min(m_position - 1, pages.size());
    pages.insert(pages.begin() + static_cast<std::ptrdiff_t>(index), ownKey);
    constexpr float rowHeight = kPlacementRowHeight;
    float const width = m_positionScroll->getContentSize().width;
    float const viewport = m_positionScroll->getContentSize().height;
    float const height = std::max(viewport, pages.size() * rowHeight);
    auto* placement = static_cast<PlacementScrollLayer*>(m_positionScroll);
    placement->selectedIndex = index;
    placement->rowCount = pages.size();
    content->setContentHeight(height);
    for (std::size_t i = 0; i < pages.size(); ++i) {
        bool const isDraft = i == index;
        std::string name;
        int officialId = 0;
        if (isDraft) name = slotDisplayName(m_draft);
        else if (SlotStore::officialKeyId(pages[i], officialId)) {
            auto replacement = store.slotForOfficial(officialId);
            name = replacement ? slotDisplayName(*replacement) : officialName(officialId);
        } else if (auto slot = store.find(pages[i].substr(2))) name = slotDisplayName(*slot);

        auto* row = CCNode::create();
        row->setContentSize({width, rowHeight});
        row->setPosition({0.f, height - (i + 1) * rowHeight});
        content->addChild(row);
        auto* background = paimon::ui::makeInset({width - 2.f, rowHeight - 3.f},
            isDraft ? 220 : 70, isDraft ? ccColor3B{65, 140, 45} : ccColor3B{0, 0, 0});
        background->setPosition({1.f, 1.f});
        row->addChild(background);
        if (auto* label = CCLabelBMFont::create(
                fmt::format("{}{}  {}", isDraft ? "> " : "", i + 1, name).c_str(), "bigFont.fnt")) {
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({6.f, rowHeight / 2.f});
            label->limitLabelWidth(width - 12.f, 0.35f, 0.12f);
            row->addChild(label, 2);
        }
        if (!isDraft) {
            auto* rowMenu = makeMenu(row, "position-row-menu"_spr, {width, rowHeight});
            auto* hitArea = CCSprite::create();
            hitArea->setTextureRect({0.f, 0.f, width, rowHeight - 2.f});
            hitArea->setOpacity(0);
            auto* item = CCMenuItemExt::createSpriteExtra(hitArea, [this, position = i + 1](CCMenuItemSpriteExtra*) {
                this->setSlotPosition(position);
            });
            item->setPosition({width / 2.f, rowHeight / 2.f});
            rowMenu->addChild(item);
        }
    }
    float const selectedY = height - (index + 0.5f) * rowHeight;
    float const offset = revealSelection ? viewport / 2.f - selectedY : previousY;
    content->setPositionY(std::clamp(offset, viewport - height, 0.f));
}

void SlotEditorPopup::setSlotPosition(std::size_t pos) {
    if (m_positionMax < 1) return;
    pos = std::clamp(pos, std::size_t{1}, m_positionMax);
    if (pos == m_position) return;
    m_position = pos;
    m_positionDirty = true;
    this->refreshPositionLabel();
    // row taps execute inside the list: rebuild only after the touch dispatch completes.
    WeakRef<SlotEditorPopup> weak(this);
    bool const reveal = !m_positionScroll || !static_cast<PlacementScrollLayer*>(m_positionScroll)->isDragging();
    Loader::get()->queueInMainThread([weak, reveal] {
        if (auto self = weak.lock(); self && self->getParent()) self->rebuildPositionList(reveal);
    });
}

void SlotEditorPopup::refreshPositionLabel() {
    if (!m_positionLabel) return;
    m_positionLabel->setString(fmt::format("{}/{}", m_position, m_positionMax).c_str());
}

void SlotEditorPopup::onImportById(CCObject*) {
    int const levelId = m_draft.levelId;
    if (levelId <= 0) {
        toast(tr("slot.editor.invalid_id"), NotificationIcon::Warning);
        return;
    }
    m_draft.source = Source::LevelId;
    this->restyleSourceChips();
    this->showSpinner(true);

    auto const request = ++m_importRequest;
    geode::WeakRef<CCNode> weakThis(this);
    SlotDownloads::get().fetch(levelId, [weakThis, levelId, request](GJGameLevel* level) {
        geode::Loader::get()->queueInMainThread([weakThis, levelId, request, retained = Ref<GJGameLevel>(level)] {
            auto ref = weakThis.lock();
            if (!ref) return;
            auto* self = typeinfo_cast<SlotEditorPopup*>(ref.data());
            if (!self || !self->m_alive.load() || self->m_importRequest != request) return;
            self->showSpinner(false);
            if (self->m_draft.source != Source::LevelId || self->m_draft.levelId != levelId) return;
            if (!retained) {
                toast(tr("slot.editor.import_failed"), NotificationIcon::Error);
                return;
            }
            self->prefillFromLevel(retained);
            toast(tr("slot.editor.imported"), NotificationIcon::Success);
        });
    });
}

void SlotEditorPopup::onBrowseGmd(CCObject*) {
    auto const request = ++m_importRequest;
    this->showSpinner(false);
    geode::WeakRef<CCNode> weakThis(this);
    pt::pickGmd([weakThis, request](Result<std::optional<std::filesystem::path>> res) {
        geode::Loader::get()->queueInMainThread([weakThis, request, res] {
            auto ref = weakThis.lock();
            if (!ref) return;
            auto* self = typeinfo_cast<SlotEditorPopup*>(ref.data());
            if (!self || !self->m_alive.load() || self->m_importRequest != request) return;
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
    auto imported = readGmd(path);
    if (!imported.info || imported.levelString.empty()) {
        toast(tr("slot.editor.import_failed"), NotificationIcon::Error);
        return;
    }
    // the file is only copied into our folder on save/test; until then the
    // pending path is just a draft field, so cancelling costs nothing.
    m_pendingGmd = path;
    m_draft.source = Source::Gmd;
    auto const& info = imported.info;
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
    // easy..extremedemon: the surprise should always draw a real face.
    std::uniform_int_distribution<size_t> diffDist(2, diffs.size() - 1);
    m_draft.difficulty = diffs[diffDist(rng)];

    // weighted towards unrated tiers so mythic stays special.
    std::array<Tier, 8> const tierBag = {
        Tier::None, Tier::None, Tier::None,
        Tier::Featured, Tier::Featured,
        Tier::Epic, Tier::Legendary, Tier::Mythic,
    };
    std::uniform_int_distribution<size_t> tierDist(0, tierBag.size() - 1);
    m_draft.tier = tierBag[tierDist(rng)];

    // vanilla-ish star ranges per difficulty, with a little jitter.
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
    std::string importedFile;
    if (slot.source == Source::LevelId) {
        if (slot.levelId <= 0) {
            toast(tr("slot.editor.invalid_id"), NotificationIcon::Warning);
            return {};
        }
        slot.gmdFile.clear();
    } else {
        std::string file = slot.gmdFile;
        if (!m_pendingGmd.empty()) {
            auto imported = store.importGmd(m_pendingGmd);
            if (!imported) {
                toast(tr("slot.editor.import_failed"), NotificationIcon::Error);
                return {};
            }
            file = *imported;
            importedFile = file;
        }
        if (file.empty()) {
            toast(tr("slot.editor.pick_gmd_first"), NotificationIcon::Warning);
            return {};
        }
        slot.gmdFile = file;
    }

    std::string id = slot.id;
    if (m_isNew || id.empty() || !store.update(slot)) {
        // re-add if it was deleted elsewhere while we edited.
        slot.id.clear();
        if (slot.replacesOfficialId == 0) {
            id = store.add(slot, store.orderIndexForVisiblePos(m_position));
        } else {
            id = store.add(slot);
        }
    }
    if (id.empty()) {
        if (!importedFile.empty()) store.discardGmd(importedFile);
        toast(tr("slot.editor.import_failed"), NotificationIcon::Error);
        return {};
    }
    if (m_positionDirty) {
        store.movePageToVisible(slot.replacesOfficialId != 0
            ? SlotStore::officialKey(slot.replacesOfficialId) : SlotStore::slotKey(id), m_position);
    }

    m_draft = slot;
    m_draft.id = id;
    m_isNew = false;
    m_positionDirty = false;
    m_pendingGmd.clear();
    // saving starts the shared persistent cache even with dynamic songs off.
    cacheSlotLevel(m_draft);
    // levelForSlot detects source changes during sync, after the current page's
    // identity has been read. Keeping the old entry until then preserves selection.
    refreshOfficialList();
    if (m_onSaved) m_onSaved();
    return id;
}

void SlotEditorPopup::onTest(CCObject*) {
    // test plays the saved slot: an unsaved draft has no cache entry for the
    // download callback to find, so commit first. the toast says so.
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
    // keep the user's replacement so showing the page again restores the same level.
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
