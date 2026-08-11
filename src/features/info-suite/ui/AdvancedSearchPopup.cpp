#include "AdvancedSearchPopup.hpp"
#include "SearchPresetsPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace paimon::info {

namespace {

constexpr float kPopupW = 400.f;
constexpr float kPopupH = 290.f;
constexpr float kTabW = 366.f;
constexpr float kTabH = 168.f;

constexpr ccColor3B kOn{255, 255, 255};
constexpr ccColor3B kOff{115, 115, 115};

// 1..6 are the values the search API expects for Easy..Demon.
struct DiffDef { int value; char const* label; };
constexpr DiffDef kDifficulties[] = {
    {1, "Ez"}, {2, "Nm"}, {3, "Hd"}, {4, "Hr"}, {5, "In"}, {6, "Dm"},
};

constexpr char const* kLengths[] = {"Tiny", "Short", "Med", "Long", "XL"};

constexpr char const* kDemonNames[] = {
    "Demon: cualquiera", "Demon: facil", "Demon: medio",
    "Demon: duro", "Demon: insano", "Demon: extremo",
};

// GJDifficulty values for each demon sub type, indexed like kDemonNames.
constexpr int kDemonValues[] = {
    0,
    static_cast<int>(GJDifficulty::DemonEasy),
    static_cast<int>(GJDifficulty::DemonMedium),
    static_cast<int>(GJDifficulty::Demon),
    static_cast<int>(GJDifficulty::DemonInsane),
    static_cast<int>(GJDifficulty::DemonExtreme),
};

struct FlagDef {
    char const* label;
    bool AdvancedQuery::* field;
};

constexpr FlagDef kFlags[] = {
    {"Con estrellas",  &AdvancedQuery::star},
    {"Sin estrellas",  &AdvancedQuery::noStar},
    {"Featured",       &AdvancedQuery::featured},
    {"Epic",           &AdvancedQuery::epic},
    {"Legendary",      &AdvancedQuery::legendary},
    {"Mythic",         &AdvancedQuery::mythic},
    {"Originales",     &AdvancedQuery::original},
    {"Dos jugadores",  &AdvancedQuery::twoPlayer},
    {"Con monedas",    &AdvancedQuery::coins},
    {"Completados",    &AdvancedQuery::completed},
    {"Sin completar",  &AdvancedQuery::uncompleted},
    {"Plataformas",    &AdvancedQuery::platformer},
    {"Filtrar por song", &AdvancedQuery::songFilter},
};

int demonIndexOf(int value) {
    for (int i = 0; i < static_cast<int>(std::size(kDemonValues)); i++) {
        if (kDemonValues[i] == value) return i;
    }
    return 0;
}

int readInt(TextInput* input) {
    if (!input) return 0;
    std::string text(input->getString());
    if (text.empty()) return 0;
    auto parsed = geode::utils::numFromString<int>(text);
    return parsed.isOk() ? std::max(0, parsed.unwrap()) : 0;
}

void writeInt(TextInput* input, int value) {
    if (!input) return;
    input->setString(value > 0 ? std::to_string(value) : "");
}

TextInput* makeNumberInput(CCNode* parent, float width, char const* placeholder,
                           float x, float y) {
    auto input = TextInput::create(width, placeholder, "chatFont.fnt");
    input->setCommonFilter(CommonFilter::Uint);
    input->setMaxCharCount(9);
    input->setScale(0.8f);
    input->setPosition({x, y});
    parent->addChild(input);
    return input;
}

} // namespace

AdvancedSearchPopup* AdvancedSearchPopup::create() {
    auto ret = new AdvancedSearchPopup();
    if (ret && ret->init()) { ret->autorelease(); return ret; }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool AdvancedSearchPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;

    paimon::markDynamicPopup(this);
    this->setTitle("Busqueda avanzada");

    auto const content = m_mainLayer->getContentSize();
    float const cx = content.width / 2.f;

    buildTabs(cx, content.height - 54.f);

    // Both tabs are positioned by their bottom left corner so their children can
    // be laid out in plain 0..kTabW / 0..kTabH coordinates.
    float tabOriginX = cx - kTabW / 2.f;
    float tabOriginY = content.height - 70.f - kTabH;

    m_serverTab = CCNode::create();
    m_serverTab->setContentSize({kTabW, kTabH});
    m_serverTab->setPosition({tabOriginX, tabOriginY});
    m_mainLayer->addChild(m_serverTab);
    buildServerTab(m_serverTab, kTabW);

    m_refineTab = CCNode::create();
    m_refineTab->setContentSize({kTabW, kTabH});
    m_refineTab->setPosition({tabOriginX, tabOriginY});
    m_mainLayer->addChild(m_refineTab);
    buildRefineTab(m_refineTab, kTabW);

    auto menu = CCMenu::create();
    menu->setPosition({cx, 24.f});
    menu->setContentSize({kPopupW - 40.f, 32.f});
    menu->setLayout(RowLayout::create()->setGap(10.f)->setAxisAlignment(AxisAlignment::Center));
    m_mainLayer->addChild(menu);

    auto presetsSpr = ButtonSprite::create("Presets", "bigFont.fnt", "GJ_button_04.png", 0.6f);
    menu->addChild(CCMenuItemSpriteExtra::create(
        presetsSpr, this, menu_selector(AdvancedSearchPopup::onPresets)));

    auto saveSpr = ButtonSprite::create("Guardar", "bigFont.fnt", "GJ_button_03.png", 0.6f);
    menu->addChild(CCMenuItemSpriteExtra::create(
        saveSpr, this, menu_selector(AdvancedSearchPopup::onSavePreset)));

    auto searchSpr = ButtonSprite::create("Buscar", "goldFont.fnt", "GJ_button_01.png", 0.7f);
    menu->addChild(CCMenuItemSpriteExtra::create(
        searchSpr, this, menu_selector(AdvancedSearchPopup::onSearch)));

    menu->updateLayout();

    refreshTabs();
    return true;
}

void AdvancedSearchPopup::buildTabs(float centerX, float y) {
    auto menu = CCMenu::create();
    menu->setPosition({centerX, y});
    menu->setContentSize({kTabW, 26.f});
    menu->setLayout(RowLayout::create()->setGap(6.f)->setAxisAlignment(AxisAlignment::Center));
    m_mainLayer->addChild(menu);

    for (auto const* name : {"Servidor", "Refinar"}) {
        auto spr = ButtonSprite::create(name, "bigFont.fnt", "GJ_button_04.png", 0.7f);
        if (spr) spr->setScale(0.58f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(AdvancedSearchPopup::onTab));
        btn->setTag(static_cast<int>(m_tabButtons.size()));
        menu->addChild(btn);
        m_tabButtons.push_back(btn);
    }

    menu->updateLayout();
}

void AdvancedSearchPopup::buildServerTab(CCNode* parent, float width) {
    float top = kTabH;

    m_queryInput = TextInput::create(width - 40.f, "Texto o id del nivel", "chatFont.fnt");
    m_queryInput->setScale(0.85f);
    m_queryInput->setPosition({width / 2.f, top - 14.f});
    parent->addChild(m_queryInput);

    auto diffMenu = CCMenu::create();
    diffMenu->setPosition({width / 2.f, top - 42.f});
    diffMenu->setContentSize({width - 20.f, 26.f});
    diffMenu->setLayout(RowLayout::create()->setGap(4.f)->setAxisAlignment(AxisAlignment::Center));
    parent->addChild(diffMenu);

    for (int i = 0; i < static_cast<int>(std::size(kDifficulties)); i++) {
        auto spr = ButtonSprite::create(kDifficulties[i].label, "bigFont.fnt",
                                        "GJ_button_04.png", 0.7f);
        if (spr) spr->setScale(0.5f);
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(AdvancedSearchPopup::onDifficulty));
        btn->setTag(i);
        diffMenu->addChild(btn);
        m_difficultyButtons.push_back(btn);
    }
    diffMenu->updateLayout();

    auto lenMenu = CCMenu::create();
    lenMenu->setPosition({width / 2.f, top - 68.f});
    lenMenu->setContentSize({width - 20.f, 26.f});
    lenMenu->setLayout(RowLayout::create()->setGap(4.f)->setAxisAlignment(AxisAlignment::Center));
    parent->addChild(lenMenu);

    for (int i = 0; i < static_cast<int>(std::size(kLengths)); i++) {
        auto spr = ButtonSprite::create(kLengths[i], "bigFont.fnt", "GJ_button_04.png", 0.7f);
        if (spr) spr->setScale(0.5f);
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(AdvancedSearchPopup::onLength));
        btn->setTag(i);
        lenMenu->addChild(btn);
        m_lengthButtons.push_back(btn);
    }
    lenMenu->updateLayout();

    auto demonMenu = CCMenu::create();
    demonMenu->setPosition({0.f, 0.f});
    parent->addChild(demonMenu);

    auto demonSpr = ButtonSprite::create("<>", "bigFont.fnt", "GJ_button_04.png", 0.7f);
    if (demonSpr) demonSpr->setScale(0.45f);
    auto demonBtn = CCMenuItemSpriteExtra::create(
        demonSpr, this, menu_selector(AdvancedSearchPopup::onDemonCycle));
    demonBtn->setPosition({26.f, top - 92.f});
    demonMenu->addChild(demonBtn);

    m_demonLabel = CCLabelBMFont::create(kDemonNames[0], "chatFont.fnt");
    m_demonLabel->setAnchorPoint({0.f, 0.5f});
    m_demonLabel->setScale(0.52f);
    m_demonLabel->setPosition({46.f, top - 92.f});
    parent->addChild(m_demonLabel);

    m_songInput = makeNumberInput(parent, 90.f, "Song ID", width - 62.f, top - 92.f);

    // Flags, two per row inside a scroll so the popup never has to grow.
    float scrollH = 62.f;
    auto scroll = ScrollLayer::create({width - 20.f, scrollH});
    scroll->setPosition({10.f, top - 92.f - 14.f - scrollH});
    parent->addChild(scroll);

    int flagCount = static_cast<int>(std::size(kFlags));
    int rows = (flagCount + 1) / 2;
    float rowH = 24.f;
    float totalH = std::max(scrollH, rows * rowH);
    scroll->m_contentLayer->setContentSize({width - 20.f, totalH});

    auto flagMenu = CCMenu::create();
    flagMenu->setPosition({0.f, 0.f});
    flagMenu->setContentSize({width - 20.f, totalH});
    scroll->m_contentLayer->addChild(flagMenu);

    float halfW = (width - 20.f) / 2.f;
    for (int i = 0; i < flagCount; i++) {
        int row = i / 2;
        int col = i % 2;
        float y = totalH - (row + 0.5f) * rowH;
        float x = col * halfW;

        auto label = CCLabelBMFont::create(kFlags[i].label, "chatFont.fnt");
        label->setAnchorPoint({0.f, 0.5f});
        label->setScale(0.5f);
        label->limitLabelWidth(halfW - 34.f, 0.5f, 0.24f);
        label->setPosition({x + 6.f, y});
        scroll->m_contentLayer->addChild(label);

        auto toggler = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(AdvancedSearchPopup::onFlag), 0.5f);
        toggler->setPosition({x + halfW - 16.f, y});
        toggler->setTag(i);
        flagMenu->addChild(toggler);
    }

    scroll->moveToTop();
}

void AdvancedSearchPopup::buildRefineTab(CCNode* parent, float width) {
    float top = kTabH;
    float labelX = 14.f;
    float minX = width - 190.f;
    float maxX = width - 70.f;

    struct RangeRow { char const* label; float y; };
    RangeRow const rows[] = {
        {"Rango de IDs", top - 24.f},
        {"Version del juego", top - 60.f},
        {"Conteo de objetos", top - 96.f},
    };

    for (auto const& row : rows) {
        auto label = CCLabelBMFont::create(row.label, "bigFont.fnt");
        label->setAnchorPoint({0.f, 0.5f});
        label->setScale(0.42f);
        label->setPosition({labelX, row.y});
        parent->addChild(label);
    }

    m_minIDInput  = makeNumberInput(parent, 100.f, "min", minX, rows[0].y);
    m_maxIDInput  = makeNumberInput(parent, 100.f, "max", maxX, rows[0].y);
    m_minVerInput = makeNumberInput(parent, 100.f, "min", minX, rows[1].y);
    m_maxVerInput = makeNumberInput(parent, 100.f, "max", maxX, rows[1].y);
    m_minObjInput = makeNumberInput(parent, 100.f, "min", minX, rows[2].y);
    m_maxObjInput = makeNumberInput(parent, 100.f, "max", maxX, rows[2].y);

    auto hint = CCLabelBMFont::create(
        "El servidor no soporta estos filtros: se aplican a cada\n"
        "pagina al llegar, asi que una pagina puede traer menos\n"
        "de 10 niveles. La version se escribe como 21 o 22.",
        "chatFont.fnt");
    hint->setAlignment(kCCTextAlignmentCenter);
    hint->setScale(0.42f);
    hint->setColor({170, 170, 170});
    hint->setPosition({width / 2.f, top - 140.f});
    parent->addChild(hint);
}

void AdvancedSearchPopup::refreshTabs() {
    if (m_serverTab) m_serverTab->setVisible(m_tab == 0);
    if (m_refineTab) m_refineTab->setVisible(m_tab == 1);

    for (int i = 0; i < static_cast<int>(m_tabButtons.size()); i++) {
        if (auto* btn = m_tabButtons[i]) btn->setColor(i == m_tab ? kOn : kOff);
    }
}

void AdvancedSearchPopup::refreshDifficultyButtons() {
    for (int i = 0; i < static_cast<int>(m_difficultyButtons.size()); i++) {
        bool on = std::find(m_query.difficulties.begin(), m_query.difficulties.end(),
                            kDifficulties[i].value) != m_query.difficulties.end();
        if (auto* btn = m_difficultyButtons[i]) btn->setColor(on ? kOn : kOff);
    }
}

void AdvancedSearchPopup::refreshLengthButtons() {
    for (int i = 0; i < static_cast<int>(m_lengthButtons.size()); i++) {
        bool on = std::find(m_query.lengths.begin(), m_query.lengths.end(), i)
                != m_query.lengths.end();
        if (auto* btn = m_lengthButtons[i]) btn->setColor(on ? kOn : kOff);
    }
}

void AdvancedSearchPopup::refreshDemonLabel() {
    if (!m_demonLabel) return;
    m_demonLabel->setString(kDemonNames[demonIndexOf(m_query.demonFilter)]);
}

void AdvancedSearchPopup::onTab(CCObject* sender) {
    m_tab = static_cast<CCNode*>(sender)->getTag();
    refreshTabs();
}

void AdvancedSearchPopup::onDifficulty(CCObject* sender) {
    int index = static_cast<CCNode*>(sender)->getTag();
    if (index < 0 || index >= static_cast<int>(std::size(kDifficulties))) return;

    int value = kDifficulties[index].value;
    auto& list = m_query.difficulties;
    auto it = std::find(list.begin(), list.end(), value);
    if (it != list.end()) list.erase(it);
    else list.push_back(value);

    std::sort(list.begin(), list.end());
    refreshDifficultyButtons();
}

void AdvancedSearchPopup::onLength(CCObject* sender) {
    int index = static_cast<CCNode*>(sender)->getTag();
    if (index < 0 || index >= static_cast<int>(std::size(kLengths))) return;

    auto& list = m_query.lengths;
    auto it = std::find(list.begin(), list.end(), index);
    if (it != list.end()) list.erase(it);
    else list.push_back(index);

    std::sort(list.begin(), list.end());
    refreshLengthButtons();
}

void AdvancedSearchPopup::onDemonCycle(CCObject*) {
    int next = (demonIndexOf(m_query.demonFilter) + 1) % static_cast<int>(std::size(kDemonValues));
    m_query.demonFilter = kDemonValues[next];
    refreshDemonLabel();
}

void AdvancedSearchPopup::onFlag(CCObject* sender) {
    auto* toggler = static_cast<CCMenuItemToggler*>(sender);
    int index = toggler->getTag();
    if (index < 0 || index >= static_cast<int>(std::size(kFlags))) return;

    // CCMenuItemToggler reports the state before the toggle, so invert it.
    m_query.*(kFlags[index].field) = !toggler->isToggled();
}

void AdvancedSearchPopup::collectInputs() {
    if (m_queryInput) m_query.query = std::string(m_queryInput->getString());
    m_query.songID = readInt(m_songInput);
    m_query.minID = readInt(m_minIDInput);
    m_query.maxID = readInt(m_maxIDInput);
    m_query.minGameVersion = readInt(m_minVerInput);
    m_query.maxGameVersion = readInt(m_maxVerInput);
    m_query.minObjects = readInt(m_minObjInput);
    m_query.maxObjects = readInt(m_maxObjInput);
}

void AdvancedSearchPopup::applyQuery(AdvancedQuery const& query) {
    m_query = query;

    if (m_queryInput) m_queryInput->setString(m_query.query);
    writeInt(m_songInput, m_query.songID);
    writeInt(m_minIDInput, m_query.minID);
    writeInt(m_maxIDInput, m_query.maxID);
    writeInt(m_minVerInput, m_query.minGameVersion);
    writeInt(m_maxVerInput, m_query.maxGameVersion);
    writeInt(m_minObjInput, m_query.minObjects);
    writeInt(m_maxObjInput, m_query.maxObjects);

    refreshDifficultyButtons();
    refreshLengthButtons();
    refreshDemonLabel();
}

void AdvancedSearchPopup::onSearch(CCObject*) {
    collectInputs();
    auto query = m_query;
    this->onClose(nullptr);
    runSearch(query);
}

void AdvancedSearchPopup::onSavePreset(CCObject*) {
    collectInputs();

    auto query = m_query;
    auto self = WeakRef<AdvancedSearchPopup>(this);
    auto popup = SearchPresetsPopup::createSaveDialog(query, [self]() {
        auto ref = self.lock();
        if (ref) {
            PaimonNotify::create("Preset guardado", NotificationIcon::Success)->show();
        }
    });
    if (popup) popup->show();
}

void AdvancedSearchPopup::onPresets(CCObject*) {
    auto self = WeakRef<AdvancedSearchPopup>(this);
    auto popup = SearchPresetsPopup::createPicker([self](AdvancedQuery const& query) {
        auto ref = self.lock();
        if (!ref) return;
        static_cast<AdvancedSearchPopup*>(ref.data())->applyQuery(query);
    });
    if (popup) popup->show();
}

void AdvancedSearchPopup::onClose(CCObject* sender) {
    for (auto* input : {m_queryInput, m_songInput, m_minIDInput, m_maxIDInput,
                        m_minVerInput, m_maxVerInput, m_minObjInput, m_maxObjInput}) {
        paimon::ui::detachGeodeTextInput(input);
    }
    m_queryInput = m_songInput = nullptr;
    m_minIDInput = m_maxIDInput = nullptr;
    m_minVerInput = m_maxVerInput = nullptr;
    m_minObjInput = m_maxObjInput = nullptr;

    Popup::onClose(sender);
}

} // namespace paimon::info
