#include "MyLevelFilterPopup.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/LevelBrowserLayer.hpp>
#include <Geode/binding/LocalLevelManager.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>

using namespace geode::prelude;
namespace ui = paimon::ui;

namespace paimon::editorfilters {

namespace {
    constexpr float kPopupW = 420.f;
    constexpr float kPopupH = 290.f;
    constexpr float kRowGap = 8.f;

    // Length chip tags map to the FilterState length bools.
    bool* lengthForTag(int tag) {
        auto& f = state();
        switch (tag) {
            case 1: return &f.tiny;
            case 2: return &f.shortLen;
            case 3: return &f.medium;
            case 4: return &f.longLen;
            case 5: return &f.xl;
        }
        return nullptr;
    }

    bool* statusForTag(int tag) {
        auto& f = state();
        if (tag == 6) return &f.verified;
        if (tag == 7) return &f.unverified;
        return nullptr;
    }

    TriState* flagForTag(int tag) {
        auto& f = state();
        switch (tag) {
            case 10: return &f.customSong;
            case 11: return &f.uploaded;
            case 12: return &f.twoPlayer;
            case 13: return &f.coins;
            case 14: return &f.lowDetail;
        }
        return nullptr;
    }

    char const* flagBase(int tag) {
        switch (tag) {
            case 10: return "Custom song";
            case 11: return "Uploaded";
            case 12: return "2 player";
            case 13: return "Coins";
            case 14: return "LDM";
        }
        return "";
    }
}

CCMenuItemSpriteExtra* MyLevelFilterPopup::makeChip(char const* text, int tag, SEL_MenuHandler sel) {
    auto* spr = ui::makeButtonSprite(text, ui::Btn::Gray, 0.f, 0.56f);
    auto* chip = CCMenuItemSpriteExtra::create(spr, this, sel);
    chip->setTag(tag);
    return chip;
}

bool MyLevelFilterPopup::init() {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);
    this->setTitle("Filter My Levels");
    addInfoButton("Filtrar mis niveles",
        "Pulsa las fichas de <cy>Length</c> y <cy>Status</c> para activar o quitar "
        "cada duracion o estado. Las fichas de <cy>Flags</c> ciclan <cg>Si</c>/<cr>No</c>/gris. "
        "Escribe texto en <cg>Name</c> para buscar por nombre, un <cg>Song ID</c> o un rango de "
        "<cg>Objects</c>. Elige el <cy>Sort</c> con las flechas. <cg>Apply</c> aplica y "
        "<cr>Reset</c> limpia todo.");
    addCorners();

    auto size = m_mainLayer->getContentSize();
    float const cx = size.width / 2.f;

    load();

    float const bottomBar = 42.f;
    float const topPad = 40.f;
    CCSize scrollSize{size.width - 28.f, size.height - topPad - bottomBar};

    auto* frame = ui::makeInset(scrollSize, 110);
    frame->setPosition({14.f, bottomBar});
    m_mainLayer->addChild(frame);

    m_scroll = ScrollLayer::create(scrollSize);
    m_scroll->setPosition({14.f, bottomBar});
    m_scroll->setID("mylevels-filter-scroll"_spr);
    m_scroll->m_contentLayer->setID("mylevels-filter-content"_spr);
    m_mainLayer->addChild(m_scroll);

    auto* content = m_scroll->m_contentLayer;
    float const colW = scrollSize.width;
    float y = 0.f;

    auto addSection = [&](char const* heading, float h) -> CCNode* {
        auto* panel = ui::makePanel({colW - 8.f, h}, heading);
        content->addChild(panel);
        return panel;
    };

    // Length chips row.
    {
        auto* panel = addSection("Length", 52.f);
        auto* menu = CCMenu::create();
        menu->setContentSize({colW - 24.f, 24.f});
        menu->ignoreAnchorPointForPosition(false);
        menu->setLayout(RowLayout::create()->setGap(4.f)->setAxisAlignment(AxisAlignment::Start));
        char const* names[] = {"Tiny", "Short", "Medium", "Long", "XL"};
        for (int i = 0; i < 5; i++) {
            auto* chip = makeChip(names[i], i + 1, menu_selector(MyLevelFilterPopup::onLengthChip));
            m_lengthChips[i + 1] = chip;
            menu->addChild(chip);
        }
        menu->setPosition({(colW - 8.f) / 2.f, 15.f});
        menu->updateLayout();
        panel->addChild(menu);
    }

    // Status chips.
    {
        auto* panel = addSection("Status", 52.f);
        auto* menu = CCMenu::create();
        menu->setContentSize({colW - 24.f, 24.f});
        menu->ignoreAnchorPointForPosition(false);
        menu->setLayout(RowLayout::create()->setGap(8.f)->setAxisAlignment(AxisAlignment::Start));
        auto* v = makeChip("Verified", 6, menu_selector(MyLevelFilterPopup::onStatusChip));
        auto* u = makeChip("Unverified", 7, menu_selector(MyLevelFilterPopup::onStatusChip));
        m_statusChips[6] = v;
        m_statusChips[7] = u;
        menu->addChild(v);
        menu->addChild(u);
        menu->setPosition({(colW - 8.f) / 2.f, 15.f});
        menu->updateLayout();
        panel->addChild(menu);
    }

    // Tri-state flag chips.
    {
        auto* panel = addSection("Flags", 84.f);
        auto* menu = CCMenu::create();
        menu->setContentSize({colW - 24.f, 56.f});
        menu->ignoreAnchorPointForPosition(false);
        menu->setLayout(RowLayout::create()->setGap(4.f)->setGrowCrossAxis(true)
            ->setAxisAlignment(AxisAlignment::Start)->setCrossAxisOverflow(false));
        for (int tag = 10; tag <= 14; tag++) {
            auto* chip = makeChip(flagBase(tag), tag, menu_selector(MyLevelFilterPopup::onFlagChip));
            m_flagChips[tag] = chip;
            menu->addChild(chip);
        }
        menu->setPosition({(colW - 8.f) / 2.f, 28.f});
        menu->updateLayout();
        panel->addChild(menu);
    }

    // Text inputs: name + song id + object range.
    {
        auto* panel = addSection("Search", 110.f);

        auto* nameLabel = ui::makeLabel("Name contains", colW - 24.f, 0.4f);
        nameLabel->setAnchorPoint({0.f, 0.5f});
        nameLabel->setPosition({10.f, 80.f});
        panel->addChild(nameLabel);

        m_nameInput = TextInput::create(colW - 28.f, "Name...");
        m_nameInput->setString(state().nameQuery);
        m_nameInput->setPosition({(colW - 8.f) / 2.f, 66.f});
        panel->addChild(m_nameInput);

        auto* songLabel = ui::makeLabel("Song ID", (colW - 32.f) / 2.f, 0.4f);
        songLabel->setAnchorPoint({0.f, 0.5f});
        songLabel->setPosition({10.f, 44.f});
        panel->addChild(songLabel);

        m_songInput = TextInput::create((colW - 36.f) / 2.f, "any");
        m_songInput->setFilter("0123456789");
        m_songInput->setString(state().songID);
        m_songInput->setPosition({(colW - 8.f) / 4.f + 2.f, 30.f});
        panel->addChild(m_songInput);

        auto* objLabel = ui::makeLabel("Objects min / max", (colW - 32.f) / 2.f, 0.4f);
        objLabel->setAnchorPoint({0.f, 0.5f});
        objLabel->setPosition({(colW - 8.f) / 2.f + 4.f, 44.f});
        panel->addChild(objLabel);

        m_minObjInput = TextInput::create((colW - 44.f) / 4.f, "min");
        m_minObjInput->setFilter("0123456789");
        if (state().minObjects > 0) m_minObjInput->setString(std::to_string(state().minObjects));
        m_minObjInput->setPosition({(colW - 8.f) * 0.62f, 30.f});
        panel->addChild(m_minObjInput);

        m_maxObjInput = TextInput::create((colW - 44.f) / 4.f, "max");
        m_maxObjInput->setFilter("0123456789");
        if (state().maxObjects > 0) m_maxObjInput->setString(std::to_string(state().maxObjects));
        m_maxObjInput->setPosition({(colW - 8.f) * 0.86f, 30.f});
        panel->addChild(m_maxObjInput);
    }

    // Sort selector.
    {
        auto* panel = addSection("Sort", 50.f);
        auto* menu = CCMenu::create();
        menu->setContentSize({colW - 24.f, 26.f});
        menu->ignoreAnchorPointForPosition(false);

        auto* prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
        prevSpr->setScale(0.6f);
        auto* prev = CCMenuItemSpriteExtra::create(prevSpr, this,
            menu_selector(MyLevelFilterPopup::onSortPrev));
        prev->setPosition({18.f, 13.f});
        menu->addChild(prev);

        auto* nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
        nextSpr->setScale(0.6f);
        nextSpr->setFlipX(true);
        auto* next = CCMenuItemSpriteExtra::create(nextSpr, this,
            menu_selector(MyLevelFilterPopup::onSortNext));
        next->setPosition({colW - 32.f, 13.f});
        menu->addChild(next);

        menu->setPosition({4.f, 15.f});
        panel->addChild(menu);

        m_sortLabel = CCLabelBMFont::create(sortLabel(state().sort), "bigFont.fnt");
        m_sortLabel->setScale(0.45f);
        m_sortLabel->limitLabelWidth(colW - 80.f, 0.45f, 0.2f);
        m_sortLabel->setPosition({(colW - 8.f) / 2.f, 15.f});
        panel->addChild(m_sortLabel);
    }

    // Stack the panels top-down and size the content layer.
    auto* children = content->getChildren();
    float totalH = 0.f;
    for (int i = children->count() - 1; i >= 0; i--) {
        auto* node = static_cast<CCNode*>(children->objectAtIndex(i));
        totalH += node->getContentSize().height + kRowGap;
    }
    totalH += kRowGap;
    content->setContentSize({colW, std::max(totalH, scrollSize.height)});

    y = content->getContentSize().height - kRowGap;
    for (unsigned i = 0; i < children->count(); i++) {
        auto* node = static_cast<CCNode*>(children->objectAtIndex(i));
        float h = node->getContentSize().height;
        y -= h;
        node->setPosition({4.f, y});
        y -= kRowGap;
    }
    m_scroll->scrollToTop();

    // Bottom bar: count + Reset + Apply.
    m_countLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_countLabel->setScale(0.42f);
    m_countLabel->setAnchorPoint({0.f, 0.5f});
    m_countLabel->setPosition({16.f, 20.f});
    m_mainLayer->addChild(m_countLabel);

    auto* barMenu = CCMenu::create();
    auto* reset = ui::makeButton("Reset",
        [this]() { this->onReset(nullptr); }, ui::Btn::Red, 70.f, 0.6f);
    auto* apply = ui::makeButton("Apply",
        [this]() { this->onApply(nullptr); }, ui::Btn::Green, 80.f, 0.6f);
    reset->setPosition({-50.f, 0.f});
    apply->setPosition({50.f, 0.f});
    barMenu->addChild(reset);
    barMenu->addChild(apply);
    barMenu->setPosition({size.width - 90.f, 20.f});
    m_mainLayer->addChild(barMenu);

    refreshChips();
    refreshCount();
    return true;
}

void MyLevelFilterPopup::onLengthChip(CCObject* sender) {
    int tag = static_cast<CCNode*>(sender)->getTag();
    if (auto* b = lengthForTag(tag)) *b = !*b;
    refreshChips();
    refreshCount();
}

void MyLevelFilterPopup::onStatusChip(CCObject* sender) {
    int tag = static_cast<CCNode*>(sender)->getTag();
    if (auto* b = statusForTag(tag)) *b = !*b;
    refreshChips();
    refreshCount();
}

void MyLevelFilterPopup::onFlagChip(CCObject* sender) {
    int tag = static_cast<CCNode*>(sender)->getTag();
    if (auto* t = flagForTag(tag)) {
        *t = static_cast<TriState>((static_cast<int>(*t) + 1) % 3);
    }
    refreshChips();
    refreshCount();
}

void MyLevelFilterPopup::onSortPrev(CCObject*) {
    state().sort = nextSort(state().sort, -1);
    if (m_sortLabel) {
        m_sortLabel->setString(sortLabel(state().sort));
        m_sortLabel->limitLabelWidth(m_scroll->getContentSize().width - 80.f, 0.45f, 0.2f);
    }
}

void MyLevelFilterPopup::onSortNext(CCObject*) {
    state().sort = nextSort(state().sort, 1);
    if (m_sortLabel) {
        m_sortLabel->setString(sortLabel(state().sort));
        m_sortLabel->limitLabelWidth(m_scroll->getContentSize().width - 80.f, 0.45f, 0.2f);
    }
}

void MyLevelFilterPopup::onReset(CCObject*) {
    reset();
    if (m_nameInput) m_nameInput->setString("");
    if (m_songInput) m_songInput->setString("");
    if (m_minObjInput) m_minObjInput->setString("");
    if (m_maxObjInput) m_maxObjInput->setString("");
    if (m_sortLabel) m_sortLabel->setString(sortLabel(state().sort));
    refreshChips();
    refreshCount();
}

void MyLevelFilterPopup::onApply(CCObject*) {
    syncFromInputs();
    save();
    reloadBrowser();
    refreshCount();
}

void MyLevelFilterPopup::syncFromInputs() {
    auto& f = state();
    if (m_nameInput) f.nameQuery = std::string(m_nameInput->getString());
    if (m_songInput) f.songID = std::string(m_songInput->getString());
    f.minObjects = 0;
    f.maxObjects = 0;
    if (m_minObjInput) {
        std::string s = m_minObjInput->getString();
        if (!s.empty()) f.minObjects = geode::utils::numFromString<int>(s).unwrapOr(0);
    }
    if (m_maxObjInput) {
        std::string s = m_maxObjInput->getString();
        if (!s.empty()) f.maxObjects = geode::utils::numFromString<int>(s).unwrapOr(0);
    }
}

void MyLevelFilterPopup::refreshChips() {
    for (auto& [tag, chip] : m_lengthChips) {
        auto* b = lengthForTag(tag);
        ui::setButtonSkin(chip, (b && *b) ? ui::Btn::Green : ui::Btn::Gray);
    }
    for (auto& [tag, chip] : m_statusChips) {
        auto* b = statusForTag(tag);
        ui::setButtonSkin(chip, (b && *b) ? ui::Btn::Green : ui::Btn::Gray);
    }
    for (auto& [tag, chip] : m_flagChips) {
        auto* t = flagForTag(tag);
        if (!t) continue;
        std::string label = flagBase(tag);
        ui::Btn skin = ui::Btn::Gray;
        if (*t == TriState::Yes) { label += ": Si"; skin = ui::Btn::Green; }
        else if (*t == TriState::No) { label += ": No"; skin = ui::Btn::Red; }
        if (auto* btn = typeinfo_cast<ButtonSprite*>(chip->getChildren()->objectAtIndex(0))) {
            btn->setString(label.c_str());
        }
        ui::setButtonSkin(chip, skin);
    }
}

void MyLevelFilterPopup::refreshCount() {
    if (!m_countLabel) return;
    syncFromInputs();

    auto* llm = LocalLevelManager::sharedState();
    auto* levels = llm ? llm->m_localLevels : nullptr;
    int total = levels ? levels->count() : 0;
    int shown = total;
    if (levels && anyActive()) {
        shown = 0;
        for (auto* level : CCArrayExt<GJGameLevel*>(levels)) {
            if (matches(level)) shown++;
        }
    }
    m_countLabel->setString(fmt::format("{} of {}", shown, total).c_str());
}

void MyLevelFilterPopup::reloadBrowser() {
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return;
    auto* browser = scene->getChildByType<LevelBrowserLayer>(0);
    if (!browser || !browser->m_searchObject) return;
    browser->m_searchObject->m_page = 0;
    browser->loadPage(browser->m_searchObject);
}

void MyLevelFilterPopup::onClose(CCObject* sender) {
    syncFromInputs();
    save();
    Popup::onClose(sender);
    reloadBrowser();
}

MyLevelFilterPopup* MyLevelFilterPopup::create() {
    auto ret = new MyLevelFilterPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

} // namespace paimon::editorfilters
