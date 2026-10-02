#include "PaimonModulesLayer.hpp"
#include "../ui/PaimonUI.hpp"
#include "../ui/PaimonPopup.hpp"
#include "../ui/PaiConfigKit.hpp"
#include "../utils/Localization.hpp"
#include "../utils/PaimonNotification.hpp"
#include "../features/info-suite/InfoCompat.hpp"
#include "../framework/compat/ModCompat.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <algorithm>

using namespace geode::prelude;
namespace mods = paimon::modules;
namespace kit = paimon::configkit;

namespace {

constexpr float kRowH = 36.f;
constexpr float kRowIdH = 44.f;
constexpr float kHeaderH = 20.f;
constexpr float kBannerH = 40.f;
constexpr float kSidebarX = 15.f;
constexpr float kSidebarW = 118.f;
constexpr float kHeaderButtonsW = 136.f;
constexpr size_t kUndoLimit = 20;
constexpr char const* kForceKey = "info-compat-force";

namespace pal {
    constexpr GLubyte kCardOnOpacity = 70;
    constexpr GLubyte kCardOffOpacity = 120;
    constexpr ccColor3B kStateOn{130, 255, 130};
    constexpr ccColor3B kStateOff{190, 190, 200};
    constexpr ccColor3B kStateLocked{255, 120, 120};
    constexpr ccColor3B kAccentOff{90, 90, 100};
    constexpr ccColor3B kName{255, 255, 255};
    constexpr ccColor3B kDesc{210, 210, 220};
    constexpr ccColor3B kId{150, 150, 165};
    constexpr ccColor3B kBadge{255, 210, 90};
}

char const* text(char const* spanish, char const* english) {
    return Localization::get().getLanguage() == Localization::Language::SPANISH ? spanish : english;
}

ccColor3B sectionAccent(mods::Section section) {
    switch (section) {
        case mods::Section::Editor:   return {255, 190, 105};
        case mods::Section::Menu:     return {245, 195, 110};
        case mods::Section::Browser:  return {120, 210, 255};
        case mods::Section::Level:    return {170, 190, 255};
        case mods::Section::Info:     return {130, 240, 220};
        case mods::Section::Gameplay: return {255, 175, 130};
        case mods::Section::Profile:  return {215, 165, 255};
        case mods::Section::Social:   return {255, 140, 145};
        case mods::Section::Global:   return {145, 240, 210};
        case mods::Section::System:   return {190, 235, 120};
    }
    return {245, 195, 110};
}

bool isActive(mods::Module const& mod) {
    return mods::isEnabled(mod) && !paimon::info::compat::isCeded(mod.key);
}

bool isEdited(mods::Module const& mod) {
    return mods::isSelfEnabled(mod) != mod.defaultOn;
}

// first switched-off module up the parent chain, the one the user has to enable.
mods::Module const* blockingParent(mods::Module const& mod) {
    char const* parent = mod.parent;
    for (int depth = 0; parent && *parent && depth < 8; ++depth) {
        auto const* owner = mods::find(parent);
        if (!owner) return nullptr;
        if (!mods::isSelfEnabled(*owner)) return owner;
        parent = owner->parent;
    }
    return nullptr;
}

char const* stateText(bool ceded, bool available, bool on) {
    if (ceded) return text("EN PAUSA", "PAUSED");
    if (!available) return text("BLOQUEADO", "LOCKED");
    return on ? text("ACTIVO", "ON") : text("APAGADO", "OFF");
}

ccColor3B stateColor(bool ceded, bool available, bool on) {
    if (ceded || !available) return pal::kStateLocked;
    return on ? pal::kStateOn : pal::kStateOff;
}

struct ActionEntry {
    std::string title;
    std::string desc;
    std::string button;
    paimon::ui::Btn skin;
    std::function<void()> run;
};

struct ToggleEntry {
    std::string title;
    std::string desc;
    bool value;
    std::function<void(bool)> onChange;
};

class ModulesActionsPopup : public PaimonPopup {
public:
    static ModulesActionsPopup* create(std::string const& subtitle,
        std::vector<ActionEntry> actions, std::vector<ToggleEntry> toggles) {
        auto* popup = new ModulesActionsPopup();
        if (popup->init(subtitle, std::move(actions), std::move(toggles))) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }

protected:
    bool init(std::string const& subtitle, std::vector<ActionEntry> actions, std::vector<ToggleEntry> toggles) {
        constexpr float kW = 340.f, kH = 290.f, kListW = 310.f;
        if (!PaimonPopup::init(kW, kH)) return false;
        setTitle(text("Acciones", "Actions"));
        m_actions = std::move(actions);

        auto* note = CCLabelBMFont::create(subtitle.c_str(), "chatFont.fnt");
        note->setColor(paimon::ui::palette::muted);
        note->limitLabelWidth(kListW, 0.5f, 0.2f);
        note->setPosition({kW / 2.f, kH - 40.f});
        m_mainLayer->addChild(note);

        std::vector<CCNode*> rows;
        for (size_t i = 0; i < m_actions.size(); ++i) {
            auto const& entry = m_actions[i];
            rows.push_back(kit::makeButtonRow(kListW, entry.title.c_str(), entry.desc.c_str(),
                entry.button.c_str(), [this, i] { run(i); }, entry.skin));
        }
        for (auto& toggle : toggles) {
            rows.push_back(kit::makeToggleRow(kListW, toggle.title.c_str(), toggle.desc.c_str(),
                toggle.value, std::move(toggle.onChange)));
        }

        float const listH = kH - 66.f;
        auto* listBg = paimon::ui::makeInset({kListW + 8.f, listH + 8.f}, 60);
        listBg->setPosition({(kW - kListW) / 2.f - 4.f, 10.f});
        m_mainLayer->addChild(listBg);
        auto* scroll = kit::makeScrollStack({kListW, listH}, rows, 3.f);
        scroll->setPosition({(kW - kListW) / 2.f, 14.f});
        m_mainLayer->addChild(scroll, 1);
        return true;
    }

private:
    void run(size_t index) {
        if (index >= m_actions.size()) return;
        Ref<ModulesActionsPopup> guard = this;
        auto callback = m_actions[index].run;
        this->onClose(nullptr);
        if (callback) callback();
    }

    std::vector<ActionEntry> m_actions;
};

} // namespace

PaimonModulesLayer* PaimonModulesLayer::create() {
    auto ret = new PaimonModulesLayer();
    if (ret && ret->init()) { ret->autorelease(); return ret; }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* PaimonModulesLayer::scene() {
    auto scene = CCScene::create();
    if (auto* layer = PaimonModulesLayer::create()) scene->addChild(layer);
    return scene;
}

bool PaimonModulesLayer::init() {
    if (!CCLayer::init()) return false;
    this->setKeypadEnabled(true);
    this->setTouchEnabled(true);

    auto const win = CCDirector::get()->getWinSize();
    float const top = win.height;
    int const sectionCount = static_cast<int>(mods::sections().size());
    m_sectionIndex = std::clamp(Mod::get()->getSavedValue<int>("modules-last-section", 0), 0, sectionCount);
    m_showIds = Mod::get()->getSavedValue<bool>("modules-show-ids", false);
    for (auto const& mod : mods::all()) {
        if (mod.parent && *mod.parent) m_childCount[mod.parent]++;
    }

    paimon::ui::decorateScene(this);

    auto* window = NineSlice::create("GJ_square01.png");
    window->setContentSize({win.width - 16.f, win.height - 44.f});
    window->setAnchorPoint({0.f, 0.f});
    window->setPosition({8.f, 8.f});
    this->addChild(window, 1);
    paimon::ui::addCorners(window, window->getContentSize(), SideArtStyle::PopupGold, 0.4f);

    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    this->addChild(m_menu, 20);

    auto* title = paimon::ui::makeTitle(text("Modulos", "Modules"), 160.f, 0.7f);
    title->setAnchorPoint({0.f, 0.5f});
    title->setPosition({40.f, top - 18.f});
    this->addChild(title, 10);

    auto* backSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    backSpr->setScale(0.62f);
    auto* backBtn = CCMenuItemSpriteExtra::create(backSpr, this, menu_selector(PaimonModulesLayer::onBack));
    backBtn->setPosition({20.f, top - 18.f});
    m_menu->addChild(backBtn);

    auto* helpSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
    helpSpr->setScale(0.7f);
    auto* helpBtn = CCMenuItemExt::createSpriteExtra(helpSpr, [this](CCMenuItemSpriteExtra*) { openHelp(); });
    helpBtn->setPosition({win.width - 20.f, top - 18.f});
    m_menu->addChild(helpBtn);

    float const panelBot = 15.f;
    float const panelTop = win.height - 43.f;
    buildSidebar(panelTop, panelBot);

    float const detailsX = kSidebarX + kSidebarW + 8.f;
    float const detailsW = win.width - detailsX - 15.f;
    auto* details = paimon::ui::makeInset({detailsW, panelTop - panelBot}, 45);
    details->setPosition({detailsX, panelBot});
    this->addChild(details, 2);
    m_contentX = detailsX + 10.f;
    m_innerW = detailsW - 20.f;

    float const headerY = panelTop - 15.f;
    buildHeader(headerY);

    constexpr float kSearchScale = 0.75f;
    float const searchY = headerY - 33.f;
    float const searchW = m_innerW - 22.f;
    m_searchInput = TextInput::create(searchW / kSearchScale,
        text("Buscar modulo, grupo o id...", "Search module, group or id..."), "chatFont.fnt");
    m_searchInput->setCommonFilter(CommonFilter::Any);
    m_searchInput->setMaxCharCount(32);
    m_searchInput->setScale(kSearchScale);
    m_searchInput->setPosition({m_contentX + searchW / 2.f, searchY});
    // plain this: the input is our child and cannot outlive us;
    // a weakref would keep us alive into lock(), mid-callback.
    m_searchInput->setCallback([this](std::string const& value) {
        if (!this->getParent()) return;
        m_query = value;
        rebuild();
    });
    this->addChild(m_searchInput, 10);

    auto* clearBtn = paimon::ui::makeFrameButton("GJ_deleteIcon_001.png", 0.45f, [this] {
        if (!m_searchInput || m_query.empty()) return;
        m_searchInput->setString("");
        m_query.clear();
        rebuild();
    });
    clearBtn->setPosition({m_contentX + m_innerW - 8.f, searchY});
    m_menu->addChild(clearBtn);

    float const filterY = searchY - 25.f;
    buildFilterBar(filterY);

    float const listTop = filterY - kit::kTabBarHeight / 2.f - 5.f;
    float const listBot = panelBot + 6.f;
    m_scroll = ScrollLayer::create({m_innerW, listTop - listBot});
    m_scroll->setPosition({m_contentX, listBot});
    this->addChild(m_scroll, 5);

    refreshSidebar();
    refreshUndo();
    rebuild();
    return true;
}

void PaimonModulesLayer::buildSidebar(float panelTop, float panelBot) {
    float const cx = kSidebarX + kSidebarW / 2.f;
    auto* bg = paimon::ui::makeInset({kSidebarW, panelTop - panelBot}, 80);
    bg->setPosition({kSidebarX, panelBot});
    this->addChild(bg, 2);

    auto const& order = mods::sections();
    int const count = static_cast<int>(order.size()) + 1;
    // the bottom 34 units hold the global counter.
    float const firstY = panelTop - 14.f;
    float const lastY = panelBot + 34.f;
    float const step = std::min(23.f, (firstY - lastY) / std::max(1.f, static_cast<float>(count - 1)));
    float const scale = std::clamp(step / 30.f * 0.82f, 0.42f, 0.58f);

    m_sectionBtns.clear();
    for (int i = 0; i < count; ++i) {
        char const* name = i == 0 ? text("Todos", "All") : mods::localizedSection(order[i - 1]);
        auto* button = paimon::ui::makeButton(name, [this, i] {
            if (m_sectionIndex == i) return;
            m_sectionIndex = i;
            Mod::get()->setSavedValue("modules-last-section", i);
            refreshSidebar();
            rebuild();
        }, paimon::ui::Btn::Gray, kSidebarW - 16.f, scale, "bigFont.fnt");
        button->setPosition({cx, firstY - step * static_cast<float>(i)});
        m_menu->addChild(button);
        m_sectionBtns.push_back(button);
    }

    m_totalLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_totalLabel->setColor(paimon::ui::palette::muted);
    m_totalLabel->setPosition({cx, panelBot + 14.f});
    this->addChild(m_totalLabel, 10);
}

void PaimonModulesLayer::buildHeader(float headerY) {
    m_titleLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_titleLabel->setAnchorPoint({0.f, 0.5f});
    m_titleLabel->setPosition({m_contentX, headerY});
    this->addChild(m_titleLabel, 10);

    m_countLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_countLabel->setAnchorPoint({0.f, 0.5f});
    m_countLabel->setColor(paimon::ui::palette::muted);
    m_countLabel->setPosition({m_contentX, headerY - 15.f});
    this->addChild(m_countLabel, 10);

    constexpr float kActionsW = 66.f;
    constexpr float kUndoW = 62.f;
    float const right = m_contentX + m_innerW;
    float const y = headerY - 7.f;

    auto* actionsBtn = paimon::ui::makeButton(text("Acciones", "Actions"), [this] { openActions(); },
        paimon::ui::Btn::Cyan, kActionsW, 0.5f);
    actionsBtn->setPosition({right - kActionsW / 2.f, y});
    m_menu->addChild(actionsBtn);

    m_undoBtn = CCMenuItemSpriteExtra::create(
        paimon::ui::makeButtonSprite(text("Deshacer", "Undo"), paimon::ui::Btn::Gray, kUndoW, 0.5f),
        this, menu_selector(PaimonModulesLayer::onUndo));
    m_undoBtn->setPosition({right - kActionsW - 6.f - kUndoW / 2.f, y});
    m_menu->addChild(m_undoBtn);

    paimon::ui::matchButtonLabels({actionsBtn, m_undoBtn});
}

void PaimonModulesLayer::buildFilterBar(float y) {
    auto* bar = kit::makeTabBar(m_innerW,
        {text("Todos", "All"), text("Activos", "On"), text("Apagados", "Off"), text("Editados", "Edited")},
        m_filter, [this](int value) {
            m_filter = value;
            rebuild();
        });
    bar->setPosition({m_contentX, y - kit::kTabBarHeight / 2.f});
    this->addChild(bar, 10);
}

void PaimonModulesLayer::rebuild() {
    buildList();
    refreshCount();
}

void PaimonModulesLayer::collectVisible() {
    m_visible = mods::search(m_query);

    if (m_sectionIndex > 0) {
        auto wanted = mods::sections()[m_sectionIndex - 1];
        std::erase_if(m_visible, [wanted](mods::Module const* m) { return m->section != wanted; });
    }
    if (m_filter != kFilterAll) {
        int const filter = m_filter;
        std::erase_if(m_visible, [filter](mods::Module const* m) {
            if (filter == kFilterOn) return !isActive(*m);
            if (filter == kFilterOff) return isActive(*m);
            return !isEdited(*m);
        });
    }

    auto const& order = mods::sections();
    auto rank = [&order](mods::Section section) {
        auto it = std::find(order.begin(), order.end(), section);
        return static_cast<int>(std::distance(order.begin(), it));
    };
    std::stable_sort(m_visible.begin(), m_visible.end(),
        [&](mods::Module const* a, mods::Module const* b) {
            int ra = rank(a->section), rb = rank(b->section);
            if (ra != rb) return ra < rb;
            // master rows lead their section.
            bool ma = *a->parent == '\0', mb = *b->parent == '\0';
            if (ma != mb) return ma;
            return std::string_view(a->group) < std::string_view(b->group);
        });
}

void PaimonModulesLayer::buildList() {
    collectVisible();

    float const scrollW = m_scroll->getContentSize().width;
    float const scrollH = m_scroll->getContentSize().height;

    m_rows.clear();
    m_rows.reserve(m_visible.size());

    auto* content = m_scroll->m_contentLayer;
    content->removeAllChildren();

    if (m_visible.empty()) {
        content->setContentSize({scrollW, scrollH});
        char const* message = !m_query.empty()
            ? text("Sin resultados. Prueba otro nombre o id.", "No results. Try another name or id.")
            : m_filter == kFilterOn ? text("No hay modulos activos en esta vista.", "No active modules in this view.")
            : m_filter == kFilterOff ? text("Todo esta activo en esta vista.", "Everything is on in this view.")
            : m_filter == kFilterEdited ? text("Nada editado: todo sigue en su valor por defecto.",
                "Nothing edited: everything is at its default.")
            : text("Sin modulos.", "No modules.");
        auto* hint = kit::makeHint(scrollW, message);
        hint->setPosition({0.f, (scrollH - hint->getContentSize().height) / 2.f});
        content->addChild(hint);
        m_scroll->moveToTop();
        return;
    }

    bool const allSections = m_sectionIndex == 0;
    auto headerOf = [allSections](mods::Module const* mod) {
        return allSections
            ? fmt::format("{}  -  {}", mods::localizedSection(mod->section), mods::localizedGroup(mod->group))
            : std::string(mods::localizedGroup(mod->group));
    };

    int headerCount = 0;
    int cededVisible = 0;
    std::string lastHeader;
    for (auto const* mod : m_visible) {
        auto header = headerOf(mod);
        if (header != lastHeader) { headerCount++; lastHeader = std::move(header); }
        if (paimon::info::compat::isCeded(mod->key)) cededVisible++;
    }
    bool const showBanner = cededVisible > 0;
    float const rowH = m_showIds ? kRowIdH : kRowH;

    float totalH = headerCount * kHeaderH + static_cast<float>(m_visible.size()) * rowH + 6.f;
    if (showBanner) totalH += kBannerH;
    totalH = std::max(totalH, scrollH);
    content->setContentSize({scrollW, totalH});

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({scrollW, totalH});
    content->addChild(menu, 3);

    float y = totalH;

    if (showBanner) {
        y -= kBannerH;
        float const bannerW = scrollW - 4.f;
        float const bannerH = kBannerH - 4.f;
        float const bx = 2.f;
        float const by = y + 2.f;

        auto* panel = paimon::ui::makeInset({bannerW, bannerH}, 140, {120, 30, 30});
        panel->setPosition({bx, by});
        content->addChild(panel, 0);

        auto* bannerTitle = paimon::ui::makeLabel(text("BetterInfo detectado", "BetterInfo detected"),
            bannerW - 90.f, 0.32f, pal::kStateLocked);
        bannerTitle->setAnchorPoint({0.f, 0.5f});
        bannerTitle->setPosition({bx + 10.f, by + bannerH - 11.f});
        content->addChild(bannerTitle, 2);

        auto* note = CCLabelBMFont::create(fmt::format(fmt::runtime(text(
            "{} modulo(s) en pausa para no duplicar su UI.",
            "{} module(s) paused to avoid duplicating its UI.")), cededVisible).c_str(), "chatFont.fnt");
        note->setAnchorPoint({0.f, 0.5f});
        note->limitLabelWidth(bannerW - 90.f, 0.42f, 0.2f);
        note->setColor(pal::kDesc);
        note->setPosition({bx + 10.f, by + 10.f});
        content->addChild(note, 2);

        auto* forceBtn = CCMenuItemSpriteExtra::create(
            paimon::ui::makeButtonSprite(text("Forzar", "Force"), paimon::ui::Btn::Gray, 60.f, 0.5f),
            this, menu_selector(PaimonModulesLayer::onToggleCompatForce));
        forceBtn->setPosition({bx + bannerW - 38.f, by + bannerH / 2.f});
        menu->addChild(forceBtn);
    }

    lastHeader.clear();
    int tag = 0;
    for (auto const* mod : m_visible) {
        auto const accentColor = sectionAccent(mod->section);
        auto header = headerOf(mod);

        if (header != lastHeader) {
            y -= kHeaderH;
            float const hcy = y + kHeaderH / 2.f;

            auto* tick = CCLayerColor::create(ccc4(accentColor.r, accentColor.g, accentColor.b, 255));
            tick->setContentSize({3.f, 10.f});
            tick->setPosition({6.f, hcy - 5.f});
            content->addChild(tick, 2);

            auto* label = paimon::ui::makeTitle(header.c_str(), scrollW - 24.f, 0.4f);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({14.f, hcy});
            content->addChild(label, 2);

            auto* line = paimon::ui::makeDivider(scrollW - 12.f, paimon::ui::palette::gold, 70);
            line->setPosition({scrollW / 2.f, y + 1.f});
            content->addChild(line, 1);
            lastHeader = std::move(header);
        }

        y -= rowH;
        float const cy = y + rowH / 2.f;
        bool const ceded = paimon::info::compat::isCeded(mod->key);
        bool const available = mods::isAvailable(*mod);
        bool const selfOn = mods::isSelfEnabled(*mod);

        float const cardX = 2.f;
        float const cardW = scrollW - 4.f;
        float const cardH = rowH - 4.f;
        float const cardY = y + 2.f;

        auto* card = paimon::ui::makeInset({cardW, cardH}, pal::kCardOffOpacity);
        card->setPosition({cardX, cardY});
        content->addChild(card, 0);

        auto* accent = CCLayerColor::create(ccc4(255, 255, 255, 255));
        accent->setContentSize({3.f, cardH - 12.f});
        accent->setPosition({cardX + 7.f, cardY + 6.f});
        content->addChild(accent, 1);

        bool const showState = cardW >= 300.f;
        float const togglerX = cardX + cardW - 17.f;
        float const stateRight = togglerX - 17.f;
        float const infoX = showState ? stateRight - 66.f : togglerX - 24.f;
        float const textX = cardX + 16.f;
        float const textW = std::max(40.f, infoX - 12.f - textX);

        auto it = m_childCount.find(mod->id);
        int const children = it == m_childCount.end() ? 0 : it->second;
        float const nameY = cy + (m_showIds ? 11.f : 7.f);

        auto* name = CCLabelBMFont::create(mods::localizedName(*mod), "bigFont.fnt");
        name->setAnchorPoint({0.f, 0.5f});
        name->limitLabelWidth(children > 0 ? textW - 32.f : textW, 0.34f, 0.16f);
        name->setColor(pal::kName);
        name->setPosition({textX, nameY});
        content->addChild(name, 2);

        if (children > 0) {
            auto* badge = CCLabelBMFont::create(fmt::format("{} sub", children).c_str(), "chatFont.fnt");
            badge->setAnchorPoint({0.f, 0.5f});
            badge->setScale(0.38f);
            badge->setColor(pal::kBadge);
            badge->setPosition({textX + name->getScaledContentSize().width + 5.f, nameY});
            content->addChild(badge, 2);
        }

        auto* desc = CCLabelBMFont::create("", "chatFont.fnt");
        desc->setAnchorPoint({0.f, 0.5f});
        desc->setPosition({textX, cy + (m_showIds ? -2.f : -6.f)});
        content->addChild(desc, 2);

        if (m_showIds) {
            auto* id = CCLabelBMFont::create(mod->id, "chatFont.fnt");
            id->setAnchorPoint({0.f, 0.5f});
            id->limitLabelWidth(textW, 0.34f, 0.16f);
            id->setColor(pal::kId);
            id->setPosition({textX, cy - 13.f});
            content->addChild(id, 2);
        }

        CCLabelBMFont* state = nullptr;
        if (showState) {
            state = CCLabelBMFont::create("", "bigFont.fnt");
            state->setAnchorPoint({1.f, 0.5f});
            state->setPosition({stateRight, cy});
            content->addChild(state, 2);
        }

        auto* infoSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        infoSpr->setScale(0.42f);
        auto* infoBtn = CCMenuItemExt::createSpriteExtra(infoSpr,
            [this, mod](CCMenuItemSpriteExtra*) { showDetails(mod); });
        infoBtn->setPosition({infoX, cy});
        infoBtn->setSizeMult(1.4f);
        menu->addChild(infoBtn);

        auto* toggler = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(PaimonModulesLayer::onToggle), 0.6f);
        toggler->setPosition({togglerX, cy});
        toggler->setTag(tag);
        toggler->toggle(selfOn);
        toggler->setEnabled(available);
        menu->addChild(toggler);

        m_rows.push_back({mod, toggler, accent, card, state, desc, accentColor, textW, ceded});
        refreshRow(tag, false);
        tag++;
    }

    m_scroll->moveToTop();
}

void PaimonModulesLayer::refreshCount() {
    int on = 0, edited = 0, paused = 0;
    for (auto const* mod : m_visible) {
        if (paimon::info::compat::isCeded(mod->key)) paused++;
        else if (mods::isEnabled(*mod)) on++;
        if (isEdited(*mod)) edited++;
    }

    std::string line = fmt::format(fmt::runtime(text("{} de {} activos", "{} of {} on")), on, m_visible.size());
    if (edited > 0) line += fmt::format(fmt::runtime(text("  -  {} editados", "  -  {} edited")), edited);
    if (paused > 0) line += fmt::format(fmt::runtime(text("  -  {} en pausa", "  -  {} paused")), paused);

    float const textW = std::max(60.f, m_innerW - kHeaderButtonsW);
    m_countLabel->setString(line.c_str());
    m_countLabel->limitLabelWidth(textW, 0.42f, 0.2f);

    m_titleLabel->setString(m_sectionIndex == 0
        ? text("Todos los modulos", "All modules")
        : mods::localizedSection(mods::sections()[m_sectionIndex - 1]));
    m_titleLabel->limitLabelWidth(textW, 0.6f, 0.25f);

    int totalOn = 0;
    for (auto const& mod : mods::all()) {
        if (isActive(mod)) totalOn++;
    }
    m_totalLabel->setString(fmt::format(fmt::runtime(text("{} / {} activos", "{} / {} on")),
        totalOn, mods::all().size()).c_str());
    m_totalLabel->limitLabelWidth(kSidebarW - 12.f, 0.42f, 0.2f);
}

void PaimonModulesLayer::refreshRow(int index, bool updateToggler) {
    if (index < 0 || index >= static_cast<int>(m_rows.size())) return;
    auto& row = m_rows[index];
    if (!row.mod) return;

    bool const available = mods::isAvailable(*row.mod);
    bool const selfOn = mods::isSelfEnabled(*row.mod);
    bool const on = selfOn && available && !row.ceded;

    if (row.toggler) {
        // the clicked row already flipped its sprite; flipping again sticks the checkbox.
        if (updateToggler) row.toggler->toggle(selfOn);
        row.toggler->setEnabled(available);
    }
    if (row.accent) row.accent->setColor(on ? row.accentColor : pal::kAccentOff);
    if (row.card) row.card->setOpacity(on ? pal::kCardOnOpacity : pal::kCardOffOpacity);
    if (row.state) {
        row.state->setString(stateText(row.ceded, available, on));
        row.state->limitLabelWidth(54.f, 0.26f, 0.1f);
        row.state->setColor(stateColor(row.ceded, available, on));
    }
    if (row.desc) {
        auto const* owner = available ? nullptr : blockingParent(*row.mod);
        std::string const line = owner
            ? fmt::format(fmt::runtime(text("Requiere: {}", "Requires: {}")), mods::localizedName(*owner))
            : std::string(mods::localizedDescription(*row.mod));
        row.desc->setString(line.c_str());
        row.desc->limitLabelWidth(row.descWidth, 0.4f, 0.16f);
        row.desc->setColor(owner ? pal::kStateLocked : pal::kDesc);
    }
}

void PaimonModulesLayer::refreshAllRows(int skipTogglerIndex) {
    for (int i = 0; i < static_cast<int>(m_rows.size()); i++) {
        refreshRow(i, i != skipTogglerIndex);
    }
    refreshCount();
}

void PaimonModulesLayer::refreshSidebar() {
    for (size_t i = 0; i < m_sectionBtns.size(); ++i) {
        paimon::ui::setButtonSkin(m_sectionBtns[i],
            static_cast<int>(i) == m_sectionIndex ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }
    paimon::ui::matchButtonLabels(m_sectionBtns);
}

void PaimonModulesLayer::refreshUndo() {
    if (!m_undoBtn) return;
    bool const ready = !m_undo.empty();
    m_undoBtn->setEnabled(ready);
    if (auto* sprite = typeinfo_cast<CCNodeRGBA*>(m_undoBtn->getNormalImage())) {
        sprite->setCascadeColorEnabled(true);
        sprite->setColor(ready ? ccColor3B{255, 255, 255} : ccColor3B{120, 120, 120});
    }
}

void PaimonModulesLayer::pushUndo(Snapshot snapshot) {
    if (snapshot.empty()) return;
    m_undo.push_back(std::move(snapshot));
    if (m_undo.size() > kUndoLimit) m_undo.erase(m_undo.begin());
    refreshUndo();
}

void PaimonModulesLayer::applyToVisible(bool useDefaults, bool value) {
    Snapshot snapshot;
    for (auto const* mod : m_visible) {
        bool const target = useDefaults ? mod->defaultOn : value;
        bool const current = mods::isSelfEnabled(*mod);
        if (current == target) continue;
        snapshot.emplace_back(mod, current);
        mods::setEnabled(*mod, target);
    }
    if (snapshot.empty()) {
        PaimonNotify::create(text("No habia nada que cambiar.", "Nothing to change."), NotificationIcon::Info)->show();
        return;
    }
    size_t const changed = snapshot.size();
    pushUndo(std::move(snapshot));
    refreshAllRows();
    PaimonNotify::create(fmt::format(fmt::runtime(text(
        "{} modulo(s) cambiados. Deshacer los revierte.",
        "{} module(s) changed. Undo reverts them.")), changed), NotificationIcon::Success)->show();
}

void PaimonModulesLayer::copyActiveIds() {
    std::string out;
    int count = 0;
    for (auto const* mod : m_visible) {
        if (!isActive(*mod)) continue;
        if (!out.empty()) out += '\n';
        out += mod->id;
        count++;
    }
    if (count == 0) {
        PaimonNotify::create(text("No hay modulos activos en la vista.", "No active modules in this view."),
            NotificationIcon::Info)->show();
        return;
    }
    geode::utils::clipboard::write(out);
    PaimonNotify::create(fmt::format(fmt::runtime(text("{} id(s) copiados.", "{} id(s) copied.")), count),
        NotificationIcon::Success)->show();
}

void PaimonModulesLayer::setCompatForced(bool forced) {
    auto* mod = Mod::get();
    if (!mod || !mod->hasSetting(kForceKey)) return;
    if (mod->getSettingValue<bool>(kForceKey) == forced) return;
    mod->setSettingValue<bool>(kForceKey, forced);

    // the force button lives inside the list being rebuilt.
    Ref<PaimonModulesLayer> self = this;
    Loader::get()->queueInMainThread([self] {
        if (self->getParent()) self->rebuild();
    });
    PaimonNotify::create(
        forced ? text("Modulos de Info forzados junto a BetterInfo.", "Info modules forced alongside BetterInfo.")
               : text("Modulos de Info en pausa mientras BetterInfo este instalado.",
                   "Info modules paused while BetterInfo is installed."),
        forced ? NotificationIcon::Warning : NotificationIcon::Info)->show();
}

void PaimonModulesLayer::openActions() {
    WeakRef<PaimonModulesLayer> self = this;
    auto guarded = [self](auto fn) {
        return [self, fn] {
            if (auto layer = self.lock(); layer && layer->getParent()) fn(layer.data());
        };
    };
    size_t const count = m_visible.size();

    std::vector<ActionEntry> actions = {
        {text("Activar todo", "Enable all"),
            fmt::format(fmt::runtime(text("Enciende los {} de la vista.", "Turns on the {} in view.")), count),
            text("Activar", "Enable"), paimon::ui::Btn::Green,
            guarded([](PaimonModulesLayer* layer) { layer->applyToVisible(false, true); })},
        {text("Apagar todo", "Disable all"),
            fmt::format(fmt::runtime(text("Apaga los {} de la vista.", "Turns off the {} in view.")), count),
            text("Apagar", "Disable"), paimon::ui::Btn::Red,
            guarded([](PaimonModulesLayer* layer) { layer->applyToVisible(false, false); })},
        {text("Valores por defecto", "Restore defaults"),
            text("Vuelve la vista a su estado original.", "Returns this view to its defaults."),
            text("Restaurar", "Restore"), paimon::ui::Btn::Blue,
            guarded([](PaimonModulesLayer* layer) { layer->applyToVisible(true, false); })},
        {text("Copiar activos", "Copy active"),
            text("Copia los ids activos al portapapeles.", "Copies the active ids to the clipboard."),
            text("Copiar", "Copy"), paimon::ui::Btn::Gray,
            guarded([](PaimonModulesLayer* layer) { layer->copyActiveIds(); })},
    };

    std::vector<ToggleEntry> toggles = {
        {text("Mostrar IDs", "Show IDs"),
            text("Muestra el id tecnico bajo cada modulo.", "Shows the technical id under each module."),
            m_showIds, [self](bool value) {
                auto layer = self.lock();
                if (!layer || !layer->getParent()) return;
                layer->m_showIds = value;
                Mod::get()->setSavedValue("modules-show-ids", value);
                layer->rebuild();
            }},
    };
    if (auto* mod = Mod::get(); mod && mod->hasSetting(kForceKey) && paimon::compat::ModCompat::isBetterInfoLoaded()) {
        toggles.push_back({text("Forzar con BetterInfo", "Force with BetterInfo"),
            text("Mantiene los modulos de Info aunque BetterInfo este instalado.",
                "Keeps Info modules on even with BetterInfo installed."),
            mod->getSettingValue<bool>(kForceKey), [self](bool value) {
                if (auto layer = self.lock(); layer && layer->getParent()) layer->setCompatForced(value);
            }});
    }

    char const* view = m_sectionIndex == 0
        ? text("Todos", "All")
        : mods::localizedSection(mods::sections()[m_sectionIndex - 1]);
    std::string subtitle = fmt::format(fmt::runtime(text("Vista: {}  -  {} modulo(s)", "View: {}  -  {} module(s)")),
        view, count);
    if (!m_query.empty() || m_filter != kFilterAll) subtitle += text("  (filtrada)", "  (filtered)");

    if (auto* popup = ModulesActionsPopup::create(subtitle, std::move(actions), std::move(toggles))) popup->show();
}

void PaimonModulesLayer::openHelp() {
    PopupManager::get().alert(text("Modulos", "Modules"), text(
        "Activa o apaga funciones completas. Los cambios se guardan al instante.\n\n"
        "<cy>Secciones</c>: donde actua cada modulo.\n"
        "<cy>Filtros</c>: solo activos, apagados o editados.\n"
        "<cy>Acciones</c>: activa, apaga o restaura toda la vista.\n"
        "<cy>Deshacer</c>: revierte los ultimos cambios.\n\n"
        "Un modulo <cr>bloqueado</c> necesita que su modulo principal este activo. Toca la <cy>i</c> para ver sus detalles.",
        "Turn whole features on or off. Changes save instantly.\n\n"
        "<cy>Sections</c>: where each module works.\n"
        "<cy>Filters</c>: only on, off or edited modules.\n"
        "<cy>Actions</c>: enable, disable or restore the whole view.\n"
        "<cy>Undo</c>: reverts your latest changes.\n\n"
        "A <cr>locked</c> module needs its main module turned on. Tap the <cy>i</c> for details.")).showInstant();
}

void PaimonModulesLayer::showDetails(mods::Module const* mod) {
    if (!mod) return;
    std::string body = fmt::format("{}\n\n<cy>ID</c>: {}\n<cy>{}</c>: {}  -  {}\n<cy>{}</c>: {}",
        mods::localizedDescription(*mod), mod->id,
        text("Seccion", "Section"), mods::localizedSection(mod->section), mods::localizedGroup(mod->group),
        text("Por defecto", "Default"), mod->defaultOn ? text("activado", "on") : text("apagado", "off"));

    if (mod->parent && *mod->parent) {
        if (auto const* owner = mods::find(mod->parent)) {
            body += fmt::format("\n<cy>{}</c>: {} ({})", text("Requiere", "Requires"), mods::localizedName(*owner),
                mods::isEnabled(*owner) ? text("activo", "on") : text("apagado", "off"));
        }
    }

    constexpr int kListed = 6;
    std::string children;
    int childCount = 0;
    for (auto const& other : mods::all()) {
        if (!other.parent || std::string_view(other.parent) != mod->id) continue;
        if (childCount < kListed) {
            if (!children.empty()) children += ", ";
            children += mods::localizedName(other);
        }
        childCount++;
    }
    if (childCount > 0) {
        if (childCount > kListed) {
            children += fmt::format(fmt::runtime(text(" y {} mas", " and {} more")), childCount - kListed);
        }
        body += fmt::format("\n<cy>{}</c>: {}", text("Controla", "Controls"), children);
    }
    if (paimon::info::compat::isCeded(mod->key)) {
        body += fmt::format("\n\n<cr>{}</c>", text("En pausa mientras BetterInfo este instalado.",
            "Paused while BetterInfo is installed."));
    }

    PopupManager::get().alert(mods::localizedName(*mod), body).showInstant();
}

void PaimonModulesLayer::onToggle(CCObject* sender) {
    int tag = static_cast<CCMenuItemToggler*>(sender)->getTag();
    if (tag < 0 || tag >= static_cast<int>(m_rows.size())) return;

    auto const* mod = m_rows[tag].mod;
    if (!mod) return;

    bool const current = mods::isSelfEnabled(*mod);
    pushUndo({{mod, current}});
    mods::setEnabled(*mod, !current);
    // a master flips its subtree; repaint all but the just-clicked toggler.
    refreshAllRows(tag);
}

void PaimonModulesLayer::onToggleCompatForce(CCObject*) {
    auto* mod = Mod::get();
    if (!mod || !mod->hasSetting(kForceKey)) return;
    setCompatForced(!mod->getSettingValue<bool>(kForceKey));
}

void PaimonModulesLayer::onUndo(CCObject*) {
    if (m_undo.empty()) return;
    auto snapshot = std::move(m_undo.back());
    m_undo.pop_back();
    for (auto const& [mod, value] : snapshot) mods::setEnabled(*mod, value);
    refreshAllRows();
    refreshUndo();
    PaimonNotify::create(fmt::format(fmt::runtime(text("Deshecho: {} modulo(s).", "Undone: {} module(s).")),
        snapshot.size()), NotificationIcon::Info)->show();
}

void PaimonModulesLayer::onBack(CCObject*) { CCDirector::get()->popScene(); }
void PaimonModulesLayer::keyBackClicked() { CCDirector::get()->popScene(); }
