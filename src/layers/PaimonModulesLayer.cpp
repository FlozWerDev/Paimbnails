#include "PaimonModulesLayer.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../utils/PaimonNotification.hpp"
#include "../features/info-suite/InfoCompat.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <algorithm>

using namespace geode::prelude;
namespace mods = paimon::modules;

namespace {

constexpr float kRowH = 60.f;
constexpr float kHeaderH = 26.f;
constexpr float kBannerH = 52.f;

namespace pal {
    constexpr ccColor4B kBgTop{112, 74, 44, 255};
    constexpr ccColor4B kBgBottom{38, 24, 15, 255};
    constexpr ccColor3B kInset{46, 30, 18};
    constexpr GLubyte kInsetOpacity = 240;
    constexpr ccColor3B kCardOn{96, 64, 36};
    constexpr ccColor3B kCardOff{52, 35, 22};
    constexpr GLubyte kCardOnOpacity = 245;
    constexpr GLubyte kCardOffOpacity = 215;
    constexpr ccColor3B kStateOn{150, 255, 150};
    constexpr ccColor3B kStateOff{180, 158, 130};
    constexpr ccColor3B kStateLocked{200, 140, 120};
    constexpr ccColor3B kAccentOff{120, 92, 60};
    constexpr ccColor3B kCount{255, 236, 200};
    constexpr ccColor3B kName{255, 244, 224};
    constexpr ccColor3B kDesc{214, 190, 162};
    constexpr ccColor3B kId{158, 132, 106};
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

} // namespace

PaimonModulesLayer* PaimonModulesLayer::create() {
    auto ret = new PaimonModulesLayer();
    if (ret && ret->init()) { ret->autorelease(); return ret; }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* PaimonModulesLayer::scene() {
    auto scene = CCScene::create();
    scene->addChild(PaimonModulesLayer::create());
    return scene;
}

bool PaimonModulesLayer::init() {
    if (!CCLayer::init()) return false;
    this->setKeypadEnabled(true);
    this->setTouchEnabled(true);

    auto win = CCDirector::get()->getWinSize();
    float cx = win.width / 2.f;
    float cy = win.height / 2.f;

    if (auto grad = CCLayerGradient::create(pal::kBgTop, pal::kBgBottom)) {
        grad->setContentSize(win);
        grad->setVector({0.2f, -1.f});
        this->addChild(grad, -10);
    }

    float panelW = std::min(480.f, win.width - 40.f);
    float panelH = win.height - 36.f;
    float panelLeft = cx - panelW / 2.f;
    float panelTop = cy + panelH / 2.f;
    float panelBot = cy - panelH / 2.f;

    if (auto frame = paimon::SpriteHelper::safeCreateScale9("GJ_square01.png")) {
        frame->setContentSize({panelW, panelH});
        frame->setPosition({cx, cy});
        this->addChild(frame, 0);
    } else {
        auto fallback = paimon::SpriteHelper::createColorPanel(panelW, panelH, {78, 52, 30}, 245, 8.f);
        fallback->setPosition({panelLeft, panelBot});
        this->addChild(fallback, 0);
    }

    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    this->addChild(m_menu, 20);

    auto title = CCLabelBMFont::create("Modulos", "goldFont.fnt");
    title->setPosition({cx, panelTop - 22.f});
    title->setScale(0.8f);
    this->addChild(title, 10);

    m_countLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_countLabel->setPosition({cx, panelTop - 42.f});
    m_countLabel->setScale(0.38f);
    m_countLabel->setColor(pal::kCount);
    this->addChild(m_countLabel, 10);

    auto backBtn = CCMenuItemSpriteExtra::create(
        CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png"),
        this, menu_selector(PaimonModulesLayer::onBack)
    );
    backBtn->setPosition({panelLeft - 4.f, panelTop - 2.f});
    m_menu->addChild(backBtn);

    // Section picker on the left, search box on the right.
    float filterY = panelTop - 66.f;
    float sectionW = std::min(panelW * 0.44f, 190.f);
    float sectionCx = panelLeft + 18.f + sectionW / 2.f;

    if (auto chip = paimon::SpriteHelper::createColorPanel(
            sectionW, 24.f, pal::kInset, pal::kInsetOpacity, 6.f)) {
        chip->setPosition({sectionCx - sectionW / 2.f, filterY - 12.f});
        this->addChild(chip, 1);
    }

    auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    prevSpr->setScale(0.5f);
    auto prevBtn = CCMenuItemSpriteExtra::create(
        prevSpr, this, menu_selector(PaimonModulesLayer::onPrevSection));
    prevBtn->setPosition({sectionCx - sectionW / 2.f + 12.f, filterY});
    m_menu->addChild(prevBtn);

    auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    nextSpr->setScale(0.5f);
    nextSpr->setFlipX(true);
    auto nextBtn = CCMenuItemSpriteExtra::create(
        nextSpr, this, menu_selector(PaimonModulesLayer::onNextSection));
    nextBtn->setPosition({sectionCx + sectionW / 2.f - 12.f, filterY});
    m_menu->addChild(nextBtn);

    m_sectionLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_sectionLabel->setPosition({sectionCx, filterY});
    m_sectionLabel->setScale(0.42f);
    this->addChild(m_sectionLabel, 10);

    float searchW = panelW - sectionW - 46.f;
    m_searchInput = TextInput::create(searchW, "Buscar modulo o id...", "chatFont.fnt");
    m_searchInput->setCommonFilter(CommonFilter::Any);
    m_searchInput->setMaxCharCount(32);
    m_searchInput->setPosition({panelLeft + panelW - 18.f - searchW / 2.f, filterY});
    m_searchInput->setScale(0.74f);
    // Plain `this`: the input is our own child, so it cannot outlive us. A
    // WeakRef here keeps the layer alive through the pool and then drops the
    // last reference from inside lock(), destroying us mid-callback.
    m_searchInput->setCallback([this](std::string const& text) {
        if (!this->getParent()) return;
        m_query = text;
        this->buildList();
        this->refreshCount();
    });
    this->addChild(m_searchInput, 10);

    float footerY = panelBot + 28.f;
    float actionScale = panelW < 370.f ? 0.50f : 0.60f;
    float actionOffset = std::min(panelW * 0.24f, 118.f);

    auto allOnSpr = ButtonSprite::create("Activar Todo", "bigFont.fnt", "GJ_button_01.png", .8f);
    allOnSpr->setScale(actionScale);
    auto allOnBtn = CCMenuItemSpriteExtra::create(allOnSpr, this, menu_selector(PaimonModulesLayer::onAllOn));
    allOnBtn->setPosition({cx - actionOffset, footerY});
    m_menu->addChild(allOnBtn);

    auto allOffSpr = ButtonSprite::create("Apagar Todo", "bigFont.fnt", "GJ_button_06.png", .8f);
    allOffSpr->setScale(actionScale);
    auto allOffBtn = CCMenuItemSpriteExtra::create(allOffSpr, this, menu_selector(PaimonModulesLayer::onAllOff));
    allOffBtn->setPosition({cx + actionOffset, footerY});
    m_menu->addChild(allOffBtn);

    float listTop = panelTop - 82.f;
    float listBot = footerY + 26.f;
    float scrollW = panelW - 30.f;
    float scrollH = listTop - listBot;
    float scrollX = cx - scrollW / 2.f;

    if (auto listBg = paimon::SpriteHelper::createColorPanel(
            scrollW + 14.f, scrollH + 14.f, pal::kInset, pal::kInsetOpacity, 8.f)) {
        listBg->setPosition({scrollX - 7.f, listBot - 7.f});
        this->addChild(listBg, 1);
    }

    m_scroll = ScrollLayer::create({scrollW, scrollH});
    m_scroll->setPosition({scrollX, listBot});
    this->addChild(m_scroll, 5);

    updateSectionLabel();
    buildList();
    refreshCount();
    return true;
}

void PaimonModulesLayer::collectVisible() {
    m_visible = mods::search(m_query);

    if (m_sectionIndex > 0) {
        auto wanted = mods::sections()[m_sectionIndex - 1];
        std::erase_if(m_visible, [wanted](mods::Module const* m) { return m->section != wanted; });
    }

    // Group by section, then by group, keeping catalog order inside each group.
    auto const& order = mods::sections();
    auto rank = [&order](mods::Section section) {
        auto it = std::find(order.begin(), order.end(), section);
        return static_cast<int>(std::distance(order.begin(), it));
    };
    std::stable_sort(m_visible.begin(), m_visible.end(),
        [&](mods::Module const* a, mods::Module const* b) {
            int ra = rank(a->section), rb = rank(b->section);
            if (ra != rb) return ra < rb;
            // Master rows lead their section.
            bool ma = *a->parent == '\0', mb = *b->parent == '\0';
            if (ma != mb) return ma;
            return std::string_view(a->group) < std::string_view(b->group);
        });
}

void PaimonModulesLayer::buildList() {
    collectVisible();

    float scrollW = m_scroll->getContentSize().width;
    float scrollH = m_scroll->getContentSize().height;

    m_rows.clear();
    m_rows.reserve(m_visible.size());

    auto content = m_scroll->m_contentLayer;
    content->removeAllChildren();

    if (m_visible.empty()) {
        content->setContentSize({scrollW, scrollH});
        auto empty = CCLabelBMFont::create("Sin resultados", "bigFont.fnt");
        empty->setScale(0.45f);
        empty->setColor(pal::kDesc);
        empty->setPosition({scrollW / 2.f, scrollH / 2.f});
        content->addChild(empty, 2);
        m_scroll->moveToTop();
        return;
    }

    int headerCount = 0;
    std::string lastHeader;
    for (auto const* mod : m_visible) {
        std::string header = fmt::format("{}  -  {}", mods::localizedSection(mod->section), mods::localizedGroup(mod->group));
        if (header != lastHeader) { headerCount++; lastHeader = header; }
    }

    // Banner shown when another mod is holding some of the visible modules back.
    int cededVisible = 0;
    for (auto const* mod : m_visible) {
        if (paimon::info::compat::isCeded(mod->key)) cededVisible++;
    }
    bool showBanner = cededVisible > 0;

    float totalH = headerCount * kHeaderH + m_visible.size() * kRowH + 10.f;
    if (showBanner) totalH += kBannerH;
    if (totalH < scrollH) totalH = scrollH;
    content->setContentSize({scrollW, totalH});

    auto togMenu = CCMenu::create();
    togMenu->setPosition({0.f, 0.f});
    togMenu->setContentSize({scrollW, totalH});
    content->addChild(togMenu, 3);

    float y = totalH;
    lastHeader.clear();
    int tag = 0;

    if (showBanner) {
        y -= kBannerH;
        float bannerW = scrollW - 6.f;
        float bannerH = kBannerH - 7.f;
        float bannerX = (scrollW - bannerW) / 2.f;
        float bannerY = y + (kBannerH - bannerH) / 2.f;

        if (auto panel = paimon::SpriteHelper::createColorPanel(
                bannerW, bannerH, {96, 52, 34}, 235, 7.f)) {
            panel->setPosition({bannerX, bannerY});
            content->addChild(panel, 0);
        }

        auto title = CCLabelBMFont::create("BetterInfo detectado", "bigFont.fnt");
        title->setAnchorPoint({0.f, 0.5f});
        title->setScale(0.42f);
        title->setColor(pal::kStateLocked);
        title->setPosition({bannerX + 14.f, bannerY + bannerH - 15.f});
        content->addChild(title, 2);

        auto note = CCLabelBMFont::create(
            fmt::format("{} modulo(s) en pausa para no duplicar su UI.", cededVisible).c_str(),
            "chatFont.fnt");
        note->setAnchorPoint({0.f, 0.5f});
        note->setScale(0.46f);
        note->limitLabelWidth(bannerW - 110.f, 0.46f, 0.2f);
        note->setColor(pal::kDesc);
        note->setPosition({bannerX + 14.f, bannerY + 14.f});
        content->addChild(note, 2);

        auto forceSprite = ButtonSprite::create("Forzar", "bigFont.fnt", "GJ_button_04.png", 0.55f);
        auto forceBtn = CCMenuItemSpriteExtra::create(
            forceSprite, this, menu_selector(PaimonModulesLayer::onToggleCompatForce));
        forceBtn->setPosition({bannerX + bannerW - 42.f, bannerY + bannerH / 2.f});
        togMenu->addChild(forceBtn);
    }

    for (auto const* mod : m_visible) {
        auto accentColor = sectionAccent(mod->section);
        std::string header = fmt::format("{}  -  {}", mods::localizedSection(mod->section), mods::localizedGroup(mod->group));

        if (header != lastHeader) {
            lastHeader = header;
            y -= kHeaderH;
            float hcy = y + kHeaderH / 2.f;

            auto tick = CCLayerColor::create(ccc4(accentColor.r, accentColor.g, accentColor.b, 255));
            tick->setContentSize({4.f, kHeaderH - 12.f});
            tick->setPosition({11.f, hcy - (kHeaderH - 12.f) / 2.f});
            content->addChild(tick, 2);

            auto label = CCLabelBMFont::create(header.c_str(), "goldFont.fnt");
            label->setAnchorPoint({0.f, 0.5f});
            label->setScale(0.44f);
            label->setPosition({22.f, hcy});
            content->addChild(label, 2);

            auto line = CCLayerColor::create(ccc4(255, 255, 255, 28));
            line->setContentSize({scrollW - 24.f, 1.f});
            line->setPosition({12.f, y + 1.f});
            content->addChild(line, 1);
        }

        y -= kRowH;
        float rowCenterY = y + kRowH / 2.f;
        bool ceded = paimon::info::compat::isCeded(mod->key);
        bool available = mods::isAvailable(*mod);
        bool selfOn = mods::isSelfEnabled(*mod);
        bool on = selfOn && available && !ceded;

        float cardW = scrollW - 6.f;
        float cardH = kRowH - 7.f;
        float cardX = (scrollW - cardW) / 2.f;
        float cardY = y + (kRowH - cardH) / 2.f;

        auto card = paimon::SpriteHelper::createColorPanel(
            cardW, cardH, on ? pal::kCardOn : pal::kCardOff,
            on ? pal::kCardOnOpacity : pal::kCardOffOpacity, 7.f);
        if (card) {
            card->setPosition({cardX, cardY});
            content->addChild(card, 0);
        }

        auto accent = CCLayerColor::create(ccc4(
            on ? accentColor.r : pal::kAccentOff.r,
            on ? accentColor.g : pal::kAccentOff.g,
            on ? accentColor.b : pal::kAccentOff.b, 255));
        accent->setContentSize({4.f, cardH - 14.f});
        accent->setPosition({cardX + 9.f, cardY + 7.f});
        content->addChild(accent, 1);

        bool showState = cardW >= 365.f;
        float rightReserve = showState ? 116.f : 76.f;
        float textX = cardX + 24.f;
        float textW = cardW - rightReserve;

        auto name = CCLabelBMFont::create(mods::localizedName(*mod), "bigFont.fnt");
        name->setAnchorPoint({0.f, 0.5f});
        name->setScale(0.44f);
        name->limitLabelWidth(textW, 0.44f, 0.2f);
        name->setColor(pal::kName);
        name->setPosition({textX, rowCenterY + 15.f});
        content->addChild(name, 2);

        auto desc = CCLabelBMFont::create(mods::localizedDescription(*mod), "chatFont.fnt");
        desc->setAnchorPoint({0.f, 0.5f});
        desc->setScale(0.46f);
        desc->limitLabelWidth(textW + 8.f, 0.46f, 0.2f);
        desc->setColor(pal::kDesc);
        desc->setPosition({textX, rowCenterY + 1.f});
        content->addChild(desc, 2);

        auto id = CCLabelBMFont::create(mod->id, "chatFont.fnt");
        id->setAnchorPoint({0.f, 0.5f});
        id->setScale(0.38f);
        id->limitLabelWidth(textW + 8.f, 0.38f, 0.18f);
        id->setColor(pal::kId);
        id->setPosition({textX, rowCenterY - 14.f});
        content->addChild(id, 2);

        CCLabelBMFont* state = nullptr;
        if (showState) {
            char const* text = ceded ? "EN PAUSA" : (!available ? "OFF*" : (on ? "ON" : "OFF"));
            state = CCLabelBMFont::create(text, "chatFont.fnt");
            state->setAnchorPoint({1.f, 0.5f});
            state->setScale(0.46f);
            state->setColor(ceded || !available
                ? pal::kStateLocked
                : (on ? pal::kStateOn : pal::kStateOff));
            state->setPosition({cardX + cardW - 47.f, rowCenterY - 1.f});
            content->addChild(state, 2);
        }

        auto toggler = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(PaimonModulesLayer::onToggle), 0.74f);
        toggler->setPosition({cardX + cardW - 23.f, rowCenterY});
        toggler->setTag(tag);
        toggler->toggle(selfOn);
        toggler->setEnabled(available);
        togMenu->addChild(toggler);

        m_rows.push_back({mod, toggler, accent, card, state, accentColor, ceded});
        tag++;
    }

    m_scroll->moveToTop();
}

void PaimonModulesLayer::updateSectionLabel() {
    if (!m_sectionLabel) return;
    char const* name = m_sectionIndex == 0
        ? "Todos"
        : mods::localizedSection(mods::sections()[m_sectionIndex - 1]);
    m_sectionLabel->setString(name);
    m_sectionLabel->limitLabelWidth(120.f, 0.42f, 0.2f);
    m_sectionLabel->setColor(m_sectionIndex == 0
        ? ccColor3B{255, 244, 224}
        : sectionAccent(mods::sections()[m_sectionIndex - 1]));
}

void PaimonModulesLayer::refreshCount() {
    int on = 0;
    for (auto const* mod : m_visible) {
        if (mods::isEnabled(*mod) && !paimon::info::compat::isCeded(mod->key)) on++;
    }
    m_countLabel->setString(
        fmt::format("{} de {} activos  ({} en total)", on, m_visible.size(), mods::all().size()).c_str());
}

void PaimonModulesLayer::refreshRow(int index, bool updateToggler) {
    if (index < 0 || index >= static_cast<int>(m_rows.size())) return;
    auto& row = m_rows[index];
    if (!row.mod) return;

    bool available = mods::isAvailable(*row.mod);
    bool selfOn = mods::isSelfEnabled(*row.mod);
    bool on = selfOn && available && !row.ceded;

    if (row.toggler) {
        // The row that was just clicked already has its sprite flipped by
        // CCMenuItemToggler's own native click handling; toggling it again
        // here races that update and can leave the checkbox stuck showing
        // the old state even though the label/data are correct.
        if (updateToggler) row.toggler->toggle(selfOn);
        row.toggler->setEnabled(available);
    }
    if (row.accent) row.accent->setColor(on ? row.accentColor : pal::kAccentOff);
    if (row.card) {
        row.card->setColor(on ? pal::kCardOn : pal::kCardOff);
        row.card->setOpacity(on ? pal::kCardOnOpacity : pal::kCardOffOpacity);
    }
    if (row.state) {
        row.state->setString(row.ceded ? "EN PAUSA" : (!available ? "OFF*" : (on ? "ON" : "OFF")));
        row.state->setColor(row.ceded || !available
            ? pal::kStateLocked
            : (on ? pal::kStateOn : pal::kStateOff));
    }
}

void PaimonModulesLayer::refreshAllRows(int skipTogglerIndex) {
    for (int i = 0; i < static_cast<int>(m_rows.size()); i++) {
        refreshRow(i, i != skipTogglerIndex);
    }
    refreshCount();
}

void PaimonModulesLayer::onToggle(CCObject* sender) {
    int tag = static_cast<CCMenuItemToggler*>(sender)->getTag();
    if (tag < 0 || tag >= static_cast<int>(m_rows.size())) return;

    auto const* mod = m_rows[tag].mod;
    if (!mod) return;

    mods::setEnabled(*mod, !mods::isSelfEnabled(*mod));
    // A master flips the whole subtree, so repaint everything except the
    // toggler that was just clicked (see refreshRow's updateToggler note).
    refreshAllRows(tag);
}

void PaimonModulesLayer::onToggleCompatForce(CCObject*) {
    auto* mod = Mod::get();
    if (!mod || !mod->hasSetting("info-compat-force")) return;

    bool forced = !mod->getSettingValue<bool>("info-compat-force");
    mod->setSettingValue<bool>("info-compat-force", forced);

    buildList();
    refreshCount();
    PaimonNotify::create(
        forced ? "Modulos de Info forzados junto a BetterInfo."
               : "Modulos de Info en pausa mientras BetterInfo este instalado.",
        forced ? NotificationIcon::Warning : NotificationIcon::Info)->show();
}

void PaimonModulesLayer::onAllOn(CCObject*) {
    for (auto const* mod : m_visible) mods::setEnabled(*mod, true);
    refreshAllRows();
    PaimonNotify::create("Modulos de la vista activados.", NotificationIcon::Success)->show();
}

void PaimonModulesLayer::onAllOff(CCObject*) {
    for (auto const* mod : m_visible) mods::setEnabled(*mod, false);
    refreshAllRows();
    PaimonNotify::create("Modulos de la vista desactivados.", NotificationIcon::Info)->show();
}

void PaimonModulesLayer::onPrevSection(CCObject*) {
    int count = static_cast<int>(mods::sections().size()) + 1;
    m_sectionIndex = (m_sectionIndex - 1 + count) % count;
    updateSectionLabel();
    buildList();
    refreshCount();
}

void PaimonModulesLayer::onNextSection(CCObject*) {
    int count = static_cast<int>(mods::sections().size()) + 1;
    m_sectionIndex = (m_sectionIndex + 1) % count;
    updateSectionLabel();
    buildList();
    refreshCount();
}

void PaimonModulesLayer::onBack(CCObject*) { CCDirector::get()->popScene(); }
void PaimonModulesLayer::keyBackClicked() { CCDirector::get()->popScene(); }
