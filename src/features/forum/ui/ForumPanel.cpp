#include "ForumPanel.hpp"
#include "CreatePostPopup.hpp"
#include "PostDetailPopup.hpp"
#include "../services/ForumHelpers.hpp"
#include "../../liquid-glass/services/GlassDecorator.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../layers/PaimonModulesLayer.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../ui/PaimonPopup.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/FluidReveal.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/Utf8Cut.hpp"
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::forum {

namespace {

using paimon::loc::tr;

bool isSpanish() {
    return Localization::get().getLanguage() == Localization::Language::SPANISH;
}

std::string tf(char const* key, char const* en, char const* es) {
    return tr(key, isSpanish() ? es : en);
}

CCSprite* makeSizedFrame(char const* frame, float size, ccColor3B color = {255, 255, 255}) {
    auto* spr = paimon::SpriteHelper::safeCreateWithFrameName(frame);
    if (!spr) return nullptr;
    float maxDim = std::max(spr->getContentSize().width, spr->getContentSize().height);
    if (maxDim > 0.f) spr->setScale(size / maxDim);
    spr->setColor(color);
    return spr;
}

CCNode* makeBadgePill(std::string const& text, ccColor3B bgCol, ccColor3B txtCol, float scale = 0.24f) {
    auto* lbl = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    lbl->setScale(scale);
    lbl->setColor(txtCol);
    float w = lbl->getScaledContentSize().width + 8.f;
    float h = 13.f;

    auto* pill = CCNode::create();
    pill->setContentSize({w, h});
    pill->setAnchorPoint({0.f, 0.5f});
    pill->addChild(paimon::ui::makeInset({w, h}, 145, bgCol), -1);

    lbl->setPosition({w / 2.f, h / 2.f + 0.5f});
    pill->addChild(lbl, 1);
    return pill;
}

void setButtonCaption(CCMenuItemSpriteExtra* button, std::string const& text) {
    if (!button) return;
    if (auto* spr = typeinfo_cast<ButtonSprite*>(button->getNormalImage())) {
        spr->setString(text.c_str());
        if (spr->m_label && spr->m_absolute) {
            float const inner = std::max(8.f, spr->getContentSize().width - 14.f);
            spr->m_label->limitLabelWidth(inner, spr->m_scale, 0.1f);
        }
    }
}

CCNode* makeTagPillFace(std::string const& text, bool selected, float height = 16.f) {
    auto* lbl = CCLabelBMFont::create(displayTag(text).c_str(), "bigFont.fnt");
    lbl->setScale(0.24f);
    lbl->setColor(selected ? ccColor3B{165, 255, 175} : ccColor3B{165, 225, 255});
    float maxLabelW = 74.f;
    paimon::ui::shrinkLabelToFit(lbl, maxLabelW);

    float w = std::max(26.f, lbl->getScaledContentSize().width + 10.f);
    auto* pill = CCNode::create();
    pill->setContentSize({w, height});
    pill->setAnchorPoint({0.5f, 0.5f});

    ccColor3B bgCol = selected ? ccColor3B{35, 115, 50} : ccColor3B{20, 40, 72};
    pill->addChild(paimon::ui::makeInset({w, height}, selected ? 175 : 135, bgCol), -1);
    lbl->setPosition({w / 2.f, height / 2.f + 0.5f});
    pill->addChild(lbl, 1);
    return pill;
}

CCNode* makeActionPillFace(
    char const* iconFrame,
    std::string const& text,
    ccColor3B bgCol,
    ccColor3B iconCol,
    ccColor3B textCol,
    GLubyte opacity = 140,
    float height = 16.f
) {
    auto* icon = makeSizedFrame(iconFrame, 10.f, iconCol);
    auto* lbl = text.empty() ? nullptr : CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    float textW = 0.f;
    if (lbl) {
        lbl->setScale(0.24f);
        lbl->setColor(textCol);
        paimon::ui::shrinkLabelToFit(lbl, 56.f);
        textW = lbl->getScaledContentSize().width;
    }

    float iconW = icon ? 11.f : 0.f;
    float gap = (icon && lbl) ? 3.f : 0.f;
    float padX = 6.f;
    float w = std::max(height, padX * 2.f + iconW + gap + textW);

    auto* pill = CCNode::create();
    pill->setContentSize({w, height});
    pill->setAnchorPoint({0.5f, 0.5f});
    pill->addChild(paimon::ui::makeInset({w, height}, opacity, bgCol), -1);

    float x = padX;
    if (icon) {
        icon->setPosition({x + iconW / 2.f, height / 2.f});
        pill->addChild(icon, 1);
        x += iconW + gap;
    }
    if (lbl) {
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({x, height / 2.f + 0.5f});
        pill->addChild(lbl, 1);
    }
    return pill;
}

class CustomTagPopup : public PaimonPopup {
public:
    static CustomTagPopup* create(std::function<void(std::string const&)> onAdd) {
        auto* ret = new CustomTagPopup();
        if (ret && ret->init(std::move(onAdd))) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

protected:
    geode::TextInput* m_input = nullptr;
    std::function<void(std::string const&)> m_onAdd;

    bool init(std::function<void(std::string const&)> onAdd) {
        if (!PaimonPopup::init(260.f, 136.f)) return false;
        m_onAdd = std::move(onAdd);
        this->setTitle(tf("pai.hub.forum.create_tag", "Create Custom Tag", "Crear Etiqueta").c_str());

        auto size = m_mainLayer->getContentSize();
        auto* box = paimon::ui::makeInset({size.width - 28.f, 52.f}, 85);
        box->setPosition({14.f, 44.f});
        m_mainLayer->addChild(box, -1);

        m_input = TextInput::create(210.f,
            tf("pai.hub.forum.new_tag", "Tag name...", "Nombre de etiqueta...").c_str(),
            "chatFont.fnt");
        m_input->setCommonFilter(CommonFilter::Text);
        m_input->setMaxCharCount(24);
        m_input->setPosition({size.width / 2.f, 70.f});
        m_input->setScale(0.82f);
        m_mainLayer->addChild(m_input, 2);

        WeakRef<CustomTagPopup> self = this;
        auto* addBtn = paimon::ui::makeButton(
            tf("pai.hub.forum.create", "Add", "Anadir").c_str(),
            [self] {
                if (auto p = self.lock()) p->submit();
            },
            paimon::ui::Btn::Green, 92.f, 0.65f
        );
        addBtn->setPosition({size.width / 2.f, 22.f});
        m_buttonMenu->addChild(addBtn);

        return true;
    }

    void submit() {
        std::string val = m_input ? trimAscii(m_input->getString()) : "";
        if (val.empty()) {
            PaimonNotify::show(
                isSpanish() ? "Escribe un nombre para la etiqueta" : "Enter a tag name first",
                NotificationIcon::Warning
            );
            return;
        }
        auto cb = m_onAdd;
        this->onClose(nullptr);
        if (cb) cb(val);
    }

    void onExit() override {
        paimon::ui::detachGeodeTextInput(m_input);
        Popup::onExit();
    }
};

} // namespace

ForumPanel* ForumPanel::create(CCSize size, bool gdMode) {
    auto* ret = new ForumPanel();
    if (ret && ret->init(size, gdMode)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

ForumPanel::~ForumPanel() {
    paimon::ui::detachGeodeTextInput(m_searchInput);
}

void ForumPanel::onExit() {
    this->unscheduleUpdate();
    paimon::ui::detachGeodeTextInput(m_searchInput);
    CCNodeRGBA::onExit();
}

bool ForumPanel::init(CCSize size, bool gdMode) {
    if (!CCNodeRGBA::init()) return false;
    this->setContentSize(size);
    this->setAnchorPoint({0.f, 0.f});
    this->setCascadeOpacityEnabled(false);
    this->setID("forum-panel"_spr);

    m_gdMode = gdMode;
    m_knownTags = {
        "Guide", "Tip", "Question", "Bug", "Suggestion",
        "Showcase", "Discussion", "Help", "Level", "Art",
        "Music", "Video", "Feedback", "Challenge", "Other"
    };

    if (m_gdMode) {
        auto* gdFrame = NineSlice::create("GJ_square01.png");
        gdFrame->setContentSize({size.width + 6.f, size.height + 6.f});
        gdFrame->setAnchorPoint({0.f, 0.f});
        gdFrame->setPosition({-3.f, -3.f});
        this->addChild(gdFrame, -5);
        paimon::ui::addCorners(gdFrame, gdFrame->getContentSize(), SideArtStyle::PopupGold, 1.f);
    }

    bool const narrow = size.width < 470.f;
    m_sidebarW = narrow ? 112.f : 128.f;
    float const gap = 8.f;
    m_mainX = m_sidebarW + gap;
    m_mainW = std::max(180.f, size.width - m_mainX);

    buildSidebar();
    buildMainPanel();
    loadKnownTags();
    this->scheduleUpdate();
    return true;
}

void ForumPanel::setupGlassChrome() {
    if (m_gdMode) return;
    paimon::glass::attachGlass(m_sidebarBg, paimon::glass::GlassKind::Panel, paimon::glass::GlassScope::Popups);
    paimon::glass::attachGlass(m_mainBg, paimon::glass::GlassKind::Panel, paimon::glass::GlassScope::Popups);
}

void ForumPanel::buildSidebar() {
    auto const size = this->getContentSize();
    m_sidebarBg = paimon::ui::makeInset({m_sidebarW, size.height}, 84);
    m_sidebarBg->setPosition({0.f, 0.f});
    this->addChild(m_sidebarBg, 0);

    m_sidebarMenu = paimon::ui::makeZeroMenu("forum-sidebar-menu"_spr);
    this->addChild(m_sidebarMenu, 5);

    float const innerW = m_sidebarW - 14.f;
    float const cx = m_sidebarW / 2.f;
    float y = size.height - 8.f;

    WeakRef<ForumPanel> self = this;

    // Primary CTA: New Post
    m_newPostBtn = paimon::ui::makeButton(
        tf("pai.hub.forum.create.cta", "+ New Post", "+ Nuevo Post").c_str(),
        [self] {
            if (auto p = self.lock()) p->openCreatePost();
        },
        paimon::ui::Btn::Green, innerW, 0.54f
    );
    m_newPostBtn->setID("forum-new-post"_spr);
    m_newPostBtn->setPosition({cx, y - 11.f});
    m_sidebarMenu->addChild(m_newPostBtn);
    y -= 26.f;

    // 4 Feed Scope tabs
    struct ScopeDef {
        Scope scope;
        char const* key;
        char const* en;
        char const* es;
    };
    ScopeDef const scopes[] = {
        {Scope::All,        "pai.hub.forum.scope.all",        "All Posts",  "Todos"},
        {Scope::Saved,      "pai.hub.forum.scope.saved",      "Saved",      "Guardados"},
        {Scope::MyPosts,    "pai.hub.forum.scope.mine",       "My Posts",   "Mis Posts"},
        {Scope::Unanswered, "pai.hub.forum.scope.unanswered", "Unanswered", "Sin resp."},
    };
    m_scopeBtns.clear();
    for (auto const& def : scopes) {
        Scope sc = def.scope;
        auto* btn = paimon::ui::makeButton(
            tf(def.key, def.en, def.es).c_str(),
            [self, sc] {
                auto p = self.lock();
                if (!p) return;
                if (p->m_scope == sc) return;
                p->m_scope = sc;
                p->updateScopeButtons();
                p->applyFiltersAndRender();
            },
            paimon::ui::Btn::Gray, innerW, 0.45f, "bigFont.fnt"
        );
        btn->setPosition({cx, y - 9.f});
        m_sidebarMenu->addChild(btn);
        m_scopeBtns.push_back(btn);
        y -= 21.f;
    }
    updateScopeButtons();

    y -= 2.f;
    auto* div = paimon::ui::makeDivider(innerW, paimon::ui::palette::gold, 110);
    div->setPosition({cx, y});
    this->addChild(div, 2);
    y -= 10.f;

    // Tags header row
    auto* tagsTitle = CCLabelBMFont::create(
        tf("pai.hub.forum.tags.heading", "Tags", "Etiquetas").c_str(),
        "goldFont.fnt"
    );
    tagsTitle->setScale(0.36f);
    tagsTitle->setAnchorPoint({0.f, 0.5f});
    tagsTitle->setPosition({7.f, y - 4.f});
    this->addChild(tagsTitle, 2);

    auto* addTagBtn = paimon::ui::makeButton(
        "+",
        [self] {
            if (auto p = self.lock()) p->openCreateTagPopup();
        },
        paimon::ui::Btn::Green, 20.f, 0.36f, "bigFont.fnt"
    );
    addTagBtn->setPosition({m_sidebarW - 17.f, y - 4.f});
    m_sidebarMenu->addChild(addTagBtn);

    m_clearTagsBtn = paimon::ui::makeFrameButton("GJ_deleteIcon_001.png", 0.38f, [self] {
        auto p = self.lock();
        if (!p) return;
        p->m_selectedTags.clear();
        p->rebuildTagList();
        p->refreshPosts(true);
    });
    m_clearTagsBtn->setPosition({m_sidebarW - 35.f, y - 4.f});
    m_clearTagsBtn->setVisible(false);
    m_sidebarMenu->addChild(m_clearTagsBtn);

    y -= 14.f;
    m_tagsTopY = y;
    m_tagsBotY = 30.f;
    float const tagsH = std::max(32.f, m_tagsTopY - m_tagsBotY);

    m_tagsScroll = ScrollLayer::create({innerW, tagsH});
    if (m_tagsScroll) m_tagsScroll->setAnchorPoint({0.f, 0.f});
    m_tagsScroll->setPosition({7.f, m_tagsBotY});
    m_tagsScroll->setStealingTouches(true);
    m_tagsScroll->enableScrollWheel(false);
    m_tagsScroll->setID("forum-tags-scroll"_spr);
    this->addChild(m_tagsScroll, 4);

    // Bottom status strip in sidebar
    auto* divBot = paimon::ui::makeDivider(innerW, paimon::ui::palette::muted, 65);
    divBot->setPosition({cx, 26.f});
    this->addChild(divBot, 2);

    m_connStatusLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_connStatusLabel->setScale(0.21f);
    m_connStatusLabel->setAnchorPoint({0.f, 0.5f});
    m_connStatusLabel->setPosition({8.f, 17.f});
    this->addChild(m_connStatusLabel, 3);

    m_sidebarStatsLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_sidebarStatsLabel->setScale(0.36f);
    m_sidebarStatsLabel->setColor(paimon::ui::palette::muted);
    m_sidebarStatsLabel->setAnchorPoint({0.f, 0.5f});
    m_sidebarStatsLabel->setPosition({8.f, 8.f});
    this->addChild(m_sidebarStatsLabel, 3);

    rebuildTagList();
    updateSidebarStats();
}

void ForumPanel::buildMainPanel() {
    auto const size = this->getContentSize();
    m_mainBg = paimon::ui::makeInset({m_mainW, size.height}, 50);
    m_mainBg->setPosition({m_mainX, 0.f});
    this->addChild(m_mainBg, 0);

    m_mainMenu = paimon::ui::makeZeroMenu("forum-main-menu"_spr);
    this->addChild(m_mainMenu, 6);

    constexpr float kPad = 10.f;
    float const innerW = m_mainW - kPad * 2.f;
    float const left = m_mainX + kPad;
    float const right = m_mainX + m_mainW - kPad;

    WeakRef<ForumPanel> self = this;

    // Row 1: Search bar + Clear + Refresh + Info
    float const row1Y = size.height - 19.f;
    float const toolsW = 46.f;
    float const searchW = std::max(100.f, innerW - toolsW - 22.f);
    constexpr float kSearchScale = 0.72f;

    m_searchInput = TextInput::create(
        searchW / kSearchScale,
        tf("pai.hub.forum.search", "Search posts, authors, tags, IDs...", "Buscar posts, autores, tags, IDs...").c_str(),
        "chatFont.fnt"
    );
    m_searchInput->setCommonFilter(CommonFilter::Text);
    m_searchInput->setMaxCharCount(64);
    m_searchInput->setScale(kSearchScale);
    m_searchInput->setPosition({left + searchW / 2.f, row1Y});
    m_searchInput->setCallback([self](std::string const& text) {
        auto p = self.lock();
        if (!p || !p->getParent()) return;
        p->m_searchQuery = trimAscii(text);
        if (p->m_clearSearchBtn) p->m_clearSearchBtn->setVisible(!p->m_searchQuery.empty());
        p->applyFiltersAndRender();
        p->m_searchPending = true;
        p->m_searchDebounce = 0.38f;
    });
    this->addChild(m_searchInput, 8);

    m_clearSearchBtn = paimon::ui::makeFrameButton("GJ_deleteIcon_001.png", 0.42f, [self] {
        auto p = self.lock();
        if (!p) return;
        p->m_searchQuery.clear();
        p->m_searchPending = false;
        if (p->m_searchInput) p->m_searchInput->setString("");
        if (p->m_clearSearchBtn) p->m_clearSearchBtn->setVisible(false);
        p->refreshPosts(true);
    });
    m_clearSearchBtn->setPosition({left + searchW + 11.f, row1Y});
    m_clearSearchBtn->setVisible(false);
    m_mainMenu->addChild(m_clearSearchBtn);

    auto* refreshSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_updateBtn_001.png");
    m_refreshBtn = CCMenuItemExt::createSpriteExtra(
        refreshSpr ? static_cast<CCNode*>(refreshSpr) : CCLabelBMFont::create("R", "bigFont.fnt"),
        [self](CCMenuItemSpriteExtra*) {
            if (auto p = self.lock()) p->refreshPosts(false);
        }
    );
    float const refreshScale = refreshSpr ? 0.46f : 0.48f;
    m_refreshBtn->setScale(refreshScale);
    m_refreshBtn->m_baseScale = refreshScale;
    m_refreshBtn->setID("forum-refresh"_spr);
    m_refreshBtn->setPosition({right - 27.f, row1Y});
    m_mainMenu->addChild(m_refreshBtn);

    auto* infoBtn = paimon::ui::makeFrameButton("GJ_infoIcon_001.png", 0.52f, [self] {
        if (auto p = self.lock()) p->openHelpPopup();
    });
    infoBtn->setID("forum-info"_spr);
    infoBtn->setPosition({right - 7.f, row1Y});
    m_mainMenu->addChild(infoBtn);

    // Row 2: Sort pills + Active author/filter chip
    m_sortRowY = size.height - 43.f;
    struct SortDef {
        FeedSort mode;
        char const* key;
        char const* en;
        char const* es;
    };
    SortDef const sorts[] = {
        {FeedSort::Recent,    "pai.hub.forum.sort.recent",    "Recent",    "Recientes"},
        {FeedSort::Top,       "pai.hub.forum.sort.top",       "Top",       "Top"},
        {FeedSort::Liked,     "pai.hub.forum.sort.liked",     "Liked",     "Gustados"},
        {FeedSort::Discussed, "pai.hub.forum.sort.discussed", "Discussed", "Activos"},
    };
    bool const compactSort = m_mainW < 365.f;
    float const pillW = compactSort ? 47.f : 56.f;
    float const pillGap = 4.f;
    float sx = left + pillW / 2.f;

    m_sortBtns.clear();
    for (auto const& def : sorts) {
        FeedSort mode = def.mode;
        auto* btn = paimon::ui::makeButton(
            tf(def.key, def.en, def.es).c_str(),
            [self, mode] {
                auto p = self.lock();
                if (!p) return;
                if (p->m_sort == mode) return;
                p->m_sort = mode;
                p->updateSortButtons();
                p->refreshPosts(true);
            },
            paimon::ui::Btn::Gray, pillW, 0.44f, "bigFont.fnt"
        );
        btn->setPosition({sx, m_sortRowY});
        m_mainMenu->addChild(btn);
        m_sortBtns.push_back(btn);
        sx += pillW + pillGap;
    }
    m_sortRightEdge = sx;
    updateSortButtons();

    m_activeFilterMenu = paimon::ui::makeZeroMenu("forum-active-filters"_spr);
    this->addChild(m_activeFilterMenu, 7);

    // Row 3: Feed scroll area
    float const feedTop = size.height - 58.f;
    float const feedBot = 26.f;
    float const feedH = std::max(60.f, feedTop - feedBot);
    float const scrollbarGutter = 11.f;
    float const feedW = std::max(120.f, innerW - scrollbarGutter);
    m_feedRect = CCRect{left, feedBot, feedW, feedH};

    auto* feedBg = paimon::ui::makeInset({feedW, feedH}, 55);
    feedBg->setPosition({left, feedBot});
    this->addChild(feedBg, 1);

    m_feedScroll = ScrollLayer::create({feedW, feedH});
    if (m_feedScroll) m_feedScroll->setAnchorPoint({0.f, 0.f});
    m_feedScroll->setPosition({left, feedBot});
    m_feedScroll->setStealingTouches(true);
    m_feedScroll->enableScrollWheel(false);
    m_feedScroll->setID("forum-scroll"_spr);
    this->addChild(m_feedScroll, 4);

    if (auto* bar = Scrollbar::create(m_feedScroll)) {
        bar->setContentSize({6.f, feedH - 6.f});
        bar->setPosition({left + feedW + 6.f, feedBot + feedH / 2.f});
        bar->setID("forum-scrollbar"_spr);
        this->addChild(bar, 6);
        m_feedScrollbar = bar;
    }

    // Inline spinner inside feed area
    m_loadingNode = CCNode::create();
    m_loadingNode->setPosition({left + feedW / 2.f, feedBot + feedH / 2.f});
    m_loadingNode->setVisible(false);
    if (auto* spinner = LoadingSpinner::create(26.f)) {
        spinner->setPosition({0.f, 8.f});
        m_loadingNode->addChild(spinner);
    }
    auto* loadLbl = CCLabelBMFont::create(
        tf("pai.hub.forum.loading", "Loading posts...", "Cargando publicaciones...").c_str(),
        "chatFont.fnt"
    );
    loadLbl->setScale(0.46f);
    loadLbl->setColor(paimon::ui::palette::muted);
    loadLbl->setPosition({0.f, -16.f});
    m_loadingNode->addChild(loadLbl);
    this->addChild(m_loadingNode, 9);

    // Row 4: Footer & Pagination
    float const footerY = 13.f;
    m_footerStatusLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_footerStatusLabel->setScale(0.42f);
    m_footerStatusLabel->setColor(paimon::ui::palette::muted);
    m_footerStatusLabel->setAnchorPoint({0.f, 0.5f});
    m_footerStatusLabel->setPosition({left, footerY});
    this->addChild(m_footerStatusLabel, 3);

    auto makePageBtn = [&](bool flipX, auto&& onTap) {
        auto* spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_03_001.png");
        if (spr && flipX) spr->setFlipX(true);
        auto* btn = CCMenuItemExt::createSpriteExtra(
            spr ? static_cast<CCNode*>(spr)
                : static_cast<CCNode*>(CCLabelBMFont::create(flipX ? ">" : "<", "bigFont.fnt")),
            std::forward<decltype(onTap)>(onTap)
        );
        float s = 0.45f;
        if (spr) {
            auto const sz = spr->getContentSize();
            float const maxDim = std::max(sz.width, sz.height);
            if (maxDim > 0.f) s = 14.f / maxDim;
        }
        btn->setScale(s);
        btn->m_baseScale = s;
        return btn;
    };

    m_nextPageBtn = makePageBtn(true, [self](CCMenuItemSpriteExtra*) {
        auto p = self.lock();
        if (!p) return;
        ++p->m_page;
        p->refreshPosts(false);
    });
    m_nextPageBtn->setPosition({right - 8.f, footerY});
    m_mainMenu->addChild(m_nextPageBtn);

    m_pageLabel = CCLabelBMFont::create("1", "bigFont.fnt");
    m_pageLabel->setScale(0.30f);
    m_pageLabel->setColor(paimon::ui::palette::gold);
    m_pageLabel->setPosition({right - 25.f, footerY});
    this->addChild(m_pageLabel, 3);

    m_prevPageBtn = makePageBtn(false, [self](CCMenuItemSpriteExtra*) {
        auto p = self.lock();
        if (!p || p->m_page <= 1) return;
        --p->m_page;
        p->refreshPosts(false);
    });
    m_prevPageBtn->setPosition({right - 42.f, footerY});
    m_mainMenu->addChild(m_prevPageBtn);

    updateActiveFiltersBar();
    updateFooter();
}

void ForumPanel::onTabActivated() {
    if (!m_hasActivated) {
        m_hasActivated = true;
        refreshPosts(true);
    } else {
        updateScopeButtons();
        updateSidebarStats();
        updateFooter();
    }
}

void ForumPanel::loadKnownTags() {
    WeakRef<ForumPanel> self = this;
    ForumApi::get().listTags([self](Result<std::vector<std::string>> res) {
        auto p = self.lock();
        if (!p || !p->getParent() || !res.ok) return;
        std::unordered_set<std::string> seen;
        std::vector<std::string> merged;
        for (auto const& t : res.data) {
            std::string key = normalizeTagKey(t);
            if (key.empty() || !seen.insert(key).second) continue;
            merged.push_back(displayTag(t));
        }
        for (auto const& existing : p->m_knownTags) {
            std::string key = normalizeTagKey(existing);
            if (!key.empty() && seen.insert(key).second) {
                merged.push_back(existing);
            }
        }
        p->m_knownTags = std::move(merged);
        p->rebuildTagList();
    });
}

void ForumPanel::mergeTagsFromPosts(std::vector<Post> const& posts) {
    std::unordered_set<std::string> seen;
    for (auto const& t : m_knownTags) seen.insert(normalizeTagKey(t));
    for (auto const& t : m_customTags) seen.insert(normalizeTagKey(t));

    bool changed = false;
    for (auto const& post : posts) {
        for (auto const& t : post.tags) {
            std::string key = normalizeTagKey(t);
            if (key.empty() || !seen.insert(key).second) continue;
            m_knownTags.push_back(displayTag(t));
            changed = true;
        }
    }
    if (changed || !posts.empty()) {
        rebuildTagList();
    }
}

void ForumPanel::rebuildTagList() {
    if (!m_tagsScroll) return;
    if (m_clearTagsBtn) m_clearTagsBtn->setVisible(!m_selectedTags.empty());

    std::unordered_map<std::string, int> counts;
    for (auto const& post : m_loadedPosts) {
        for (auto const& t : post.tags) {
            counts[normalizeTagKey(t)]++;
        }
    }

    std::vector<std::string> allTags;
    std::unordered_set<std::string> seen;
    for (auto const& t : m_customTags) {
        std::string key = normalizeTagKey(t);
        if (!key.empty() && seen.insert(key).second) allTags.push_back(displayTag(t));
    }
    for (auto const& t : m_knownTags) {
        std::string key = normalizeTagKey(t);
        if (!key.empty() && seen.insert(key).second) allTags.push_back(displayTag(t));
    }

    std::unordered_set<std::string> selectedKeys;
    for (auto const& s : m_selectedTags) selectedKeys.insert(normalizeTagKey(s));

    // Float selected tags to the top so active filters are immediately visible.
    std::stable_sort(allTags.begin(), allTags.end(), [&](std::string const& a, std::string const& b) {
        bool sa = selectedKeys.count(normalizeTagKey(a)) > 0;
        bool sb = selectedKeys.count(normalizeTagKey(b)) > 0;
        if (sa != sb) return sa;
        int ca = counts[normalizeTagKey(a)];
        int cb = counts[normalizeTagKey(b)];
        if ((ca > 0) != (cb > 0)) return ca > 0;
        return false;
    });

    auto* content = m_tagsScroll->m_contentLayer;
    content->removeAllChildren();

    float const viewW = m_tagsScroll->getContentSize().width;
    float const viewH = m_tagsScroll->getContentSize().height;
    constexpr float kRowH = 18.f;
    constexpr float kGap = 3.f;

    float totalH = static_cast<float>(allTags.size()) * (kRowH + kGap);
    if (!allTags.empty()) totalH -= kGap;
    float contentH = std::max(viewH, totalH + 2.f);
    content->setContentSize({viewW, contentH});

    auto* menu = paimon::ui::makeZeroMenu();
    menu->setContentSize({viewW, contentH});
    content->addChild(menu, 2);

    WeakRef<ForumPanel> self = this;
    float y = contentH - 1.f;
    for (auto const& tag : allTags) {
        y -= kRowH;
        std::string const key = normalizeTagKey(tag);
        bool const active = selectedKeys.count(key) > 0;
        int const count = counts[key];

        auto* rowFace = CCNode::create();
        rowFace->setContentSize({viewW, kRowH});
        rowFace->setAnchorPoint({0.5f, 0.5f});

        ccColor3B bgCol = active ? ccColor3B{34, 120, 52} : ccColor3B{0, 0, 0};
        GLubyte alpha = active ? 190 : 75;
        rowFace->addChild(paimon::ui::makeInset({viewW, kRowH}, alpha, bgCol), -1);

        auto* nameLbl = CCLabelBMFont::create(tag.c_str(), "bigFont.fnt");
        nameLbl->setScale(0.24f);
        nameLbl->setAnchorPoint({0.f, 0.5f});
        nameLbl->setColor(active ? ccColor3B{255, 255, 255} : paimon::ui::palette::muted);
        nameLbl->setPosition({6.f, kRowH / 2.f + 0.5f});

        float countReserve = count > 0 ? 22.f : (active ? 14.f : 6.f);
        paimon::ui::shrinkLabelToFit(nameLbl, viewW - countReserve - 8.f);
        rowFace->addChild(nameLbl, 1);

        if (active || count > 0) {
            std::string rightTxt = active ? "x" : std::to_string(count);
            auto* countLbl = CCLabelBMFont::create(rightTxt.c_str(), "chatFont.fnt");
            countLbl->setScale(0.38f);
            countLbl->setColor(active ? paimon::ui::palette::gold : paimon::ui::palette::dim);
            countLbl->setAnchorPoint({1.f, 0.5f});
            countLbl->setPosition({viewW - 6.f, kRowH / 2.f});
            rowFace->addChild(countLbl, 1);
        }

        auto* btn = CCMenuItemExt::createSpriteExtra(rowFace, [self, tag](CCMenuItemSpriteExtra*) {
            if (auto p = self.lock()) p->toggleTagFilter(tag);
        });
        btn->m_scaleMultiplier = 1.03f;
        btn->setPosition({viewW / 2.f, y + kRowH / 2.f});
        menu->addChild(btn);

        y -= kGap;
    }

    m_tagsScroll->moveToTop();
    m_tagsScrollTargetSet = false;
}

void ForumPanel::updateScopeButtons() {
    auto bookmarks = getBookmarks();
    bool const es = isSpanish();
    std::string savedTxt = bookmarks.empty()
        ? tf("pai.hub.forum.scope.saved", "Saved", "Guardados")
        : fmt::format("{} ({})", tf("pai.hub.forum.scope.saved", "Saved", "Guardados"), bookmarks.size());

    if (m_scopeBtns.size() >= 2) {
        setButtonCaption(m_scopeBtns[1], savedTxt);
    }
    (void)es;

    for (size_t i = 0; i < m_scopeBtns.size(); ++i) {
        bool selected = static_cast<int>(i) == static_cast<int>(m_scope);
        paimon::ui::setButtonSkin(m_scopeBtns[i], selected ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }
    paimon::ui::matchButtonLabels(m_scopeBtns);
}

void ForumPanel::updateSortButtons() {
    for (size_t i = 0; i < m_sortBtns.size(); ++i) {
        bool selected = static_cast<int>(i) == static_cast<int>(m_sort);
        paimon::ui::setButtonSkin(m_sortBtns[i], selected ? paimon::ui::Btn::Cyan : paimon::ui::Btn::Gray);
    }
    paimon::ui::matchButtonLabels(m_sortBtns);
}

void ForumPanel::updateActiveFiltersBar() {
    if (!m_activeFilterMenu) return;
    m_activeFilterMenu->removeAllChildren();

    float const right = m_mainX + m_mainW - 10.f;
    float x = right;
    WeakRef<ForumPanel> self = this;

    bool const hasAnyFilter = m_authorFilterId > 0 || !m_selectedTags.empty() || !m_searchQuery.empty();
    if (hasAnyFilter) {
        std::string resetTxt = tf("pai.hub.forum.reset", "Reset", "Limpiar") + " x";
        auto* resetBtn = paimon::ui::makeButton(
            resetTxt.c_str(),
            [self] {
                if (auto p = self.lock()) p->clearAllFilters();
            },
            paimon::ui::Btn::Red, 0.f, 0.42f, "bigFont.fnt"
        );
        float w = resetBtn->getScaledContentSize().width;
        if (x - w >= m_sortRightEdge + 4.f) {
            resetBtn->setPosition({x - w / 2.f, m_sortRowY});
            m_activeFilterMenu->addChild(resetBtn);
            x -= w + 4.f;
        }
    }

    if (m_authorFilterId > 0) {
        std::string authorLabel = "@" + (m_authorFilterName.empty()
            ? std::to_string(m_authorFilterId) : m_authorFilterName) + " x";
        auto* authorBtn = paimon::ui::makeButton(
            authorLabel.c_str(),
            [self] {
                auto p = self.lock();
                if (!p) return;
                p->m_authorFilterId = 0;
                p->m_authorFilterName.clear();
                p->updateActiveFiltersBar();
                p->applyFiltersAndRender();
            },
            paimon::ui::Btn::Pink, 0.f, 0.40f, "bigFont.fnt"
        );
        float w = authorBtn->getScaledContentSize().width;
        if (x - w >= m_sortRightEdge + 4.f) {
            authorBtn->setPosition({x - w / 2.f, m_sortRowY});
            m_activeFilterMenu->addChild(authorBtn);
        }
    }
}

void ForumPanel::updateSidebarStats() {
    bool const offline = !ForumApi::get().hasServer() || ForumApi::get().lastListWasOffline();
    if (m_connStatusLabel) {
        m_connStatusLabel->setString(offline
            ? tf("pai.hub.forum.status.offline", "Offline cache", "Cache local").c_str()
            : tf("pai.hub.forum.status.online", "Live server", "Servidor activo").c_str());
        m_connStatusLabel->setColor(offline ? paimon::ui::palette::warning : paimon::ui::palette::success);
    }

    if (m_sidebarStatsLabel) {
        int replies = 0;
        for (auto const& post : m_loadedPosts) {
            replies += std::max(post.replyCount, static_cast<int>(post.replies.size()));
        }
        bool const es = isSpanish();
        m_sidebarStatsLabel->setString(fmt::format(
            "{} {} - {} {}",
            m_loadedPosts.size(), es ? "posts" : "posts",
            replies, es ? "resp." : "replies"
        ).c_str());
        paimon::ui::shrinkLabelToFit(m_sidebarStatsLabel, m_sidebarW - 16.f);
    }
}

void ForumPanel::updateFooter() {
    bool const es = isSpanish();
    if (m_footerStatusLabel) {
        auto cd = ForumApi::get().getPostCooldownRemaining();
        if (cd > 0) {
            m_footerStatusLabel->setString(fmt::format(
                fmt::runtime(es ? "Espera {}s para publicar" : "Post cooldown: {}s"), cd
            ).c_str());
            m_footerStatusLabel->setColor(paimon::ui::palette::warning);
        } else if (m_lastRequestError) {
            m_footerStatusLabel->setString(es ? "Error de conexion" : "Connection issue");
            m_footerStatusLabel->setColor(paimon::ui::palette::danger);
        } else if (ForumApi::get().lastListWasOffline() || !ForumApi::get().hasServer()) {
            m_footerStatusLabel->setString(fmt::format(
                fmt::runtime(es ? "Cache offline · {} visibles" : "Offline cache · {} shown"),
                m_displayedPosts.size()
            ).c_str());
            m_footerStatusLabel->setColor(paimon::ui::palette::warning);
        } else {
            m_footerStatusLabel->setString(fmt::format(
                fmt::runtime(es ? "{} publicaciones" : "{} posts shown"),
                m_displayedPosts.size()
            ).c_str());
            m_footerStatusLabel->setColor(paimon::ui::palette::muted);
        }
        paimon::ui::shrinkLabelToFit(m_footerStatusLabel, m_mainW - 86.f);
    }

    if (m_pageLabel) {
        m_pageLabel->setString(std::to_string(m_page).c_str());
    }
    bool const paginationActive = (m_scope == Scope::All);
    bool const canPrev = paginationActive && m_page > 1 && !m_isLoading;
    bool const canNext = paginationActive && !m_isLoading && static_cast<int>(m_loadedPosts.size()) >= kPageSize;
    if (m_prevPageBtn) {
        m_prevPageBtn->setEnabled(canPrev);
        m_prevPageBtn->setOpacity(canPrev ? 255 : 90);
    }
    if (m_nextPageBtn) {
        m_nextPageBtn->setEnabled(canNext);
        m_nextPageBtn->setOpacity(canNext ? 255 : 90);
    }
}

void ForumPanel::setLoading(bool loading) {
    m_isLoading = loading;
    if (m_loadingNode) m_loadingNode->setVisible(loading);
    if (loading) clearEmptyState();
    if (m_refreshBtn) {
        if (auto* spr = m_refreshBtn->getNormalImage()) {
            spr->stopAllActions();
            if (loading) {
                spr->runAction(CCRepeatForever::create(CCRotateBy::create(0.85f, 360.f)));
            } else {
                spr->setRotation(0.f);
            }
        }
    }
    updateFooter();
}

void ForumPanel::update(float dt) {
    paimon::configkit::stepWheelScroll(m_feedScroll, m_feedScrollTarget, m_feedScrollTargetSet, dt);
    paimon::configkit::stepWheelScroll(m_tagsScroll, m_tagsScrollTarget, m_tagsScrollTargetSet, dt);

    if (m_searchPending) {
        m_searchDebounce -= dt;
        if (m_searchDebounce <= 0.f) {
            m_searchPending = false;
            refreshPosts(true);
        }
    }

    m_cooldownAccum += dt;
    if (m_cooldownAccum >= 0.5f) {
        m_cooldownAccum = 0.f;
        bool const onCd = ForumApi::get().getPostCooldownRemaining() > 0;
        if (onCd || m_wasOnCooldown) {
            m_wasOnCooldown = onCd;
            updateFooter();
        }
    }
}

bool ForumPanel::handleScrollWheel(float x, float y) {
    if (paimon::configkit::queueWheelScroll(m_feedScroll, x, y, m_feedScrollTarget, m_feedScrollTargetSet)) {
        return true;
    }
    if (paimon::configkit::queueWheelScroll(m_tagsScroll, x, y, m_tagsScrollTarget, m_tagsScrollTargetSet)) {
        return true;
    }
    return false;
}

void ForumPanel::refreshPosts(bool resetPage) {
    if (resetPage) m_page = 1;

    if (!paimon::modules::isEnabled("paimbnails.forum.menu")) {
        m_loadedPosts.clear();
        m_displayedPosts.clear();
        setLoading(false);
        renderFeed();
        return;
    }

    if (m_feedScroll) m_feedScroll->m_contentLayer->removeAllChildren();
    clearEmptyState();
    m_lastRequestError = false;
    setLoading(true);

    ListFilter filter;
    switch (m_sort) {
        case FeedSort::Top:       filter.sort = SortMode::TopRated; break;
        case FeedSort::Liked:     filter.sort = SortMode::MostLiked; break;
        case FeedSort::Discussed:
        case FeedSort::Recent:
        default:                  filter.sort = SortMode::Recent; break;
    }
    for (auto const& t : m_selectedTags) {
        filter.tags.push_back(normalizeTagKey(t));
    }
    filter.query = m_searchQuery;
    filter.page = m_page;
    filter.limit = (m_sort == FeedSort::Discussed) ? 50 : kPageSize;

    WeakRef<ForumPanel> self = this;
    std::uint64_t const seq = ++m_requestSeq;
    ForumApi::get().listPosts(filter, [self, seq](Result<std::vector<Post>> res) {
        auto p = self.lock();
        if (!p || !p->getParent()) return;
        if (seq != p->m_requestSeq) return;
        p->setLoading(false);
        p->m_lastRequestError = !res.ok;
        p->m_loadedPosts = res.ok ? std::move(res.data) : std::vector<Post>{};
        p->mergeTagsFromPosts(p->m_loadedPosts);
        p->applyFiltersAndRender();
    });
}

void ForumPanel::applyFiltersAndRender() {
    std::vector<Post> pool = m_loadedPosts;

    // For Saved view, supplement with cached posts so bookmarks from earlier pages/sessions appear too.
    if (m_scope == Scope::Saved) {
        auto bookmarks = getBookmarks();
        std::unordered_set<std::string> bookmarkSet(bookmarks.begin(), bookmarks.end());
        std::unordered_set<std::string> seenIds;
        std::vector<Post> savedPool;
        for (auto const& post : pool) {
            if (bookmarkSet.count(post.id) && seenIds.insert(post.id).second) {
                savedPool.push_back(post);
            }
        }
        for (auto const& cached : ForumApi::get().cachedPosts()) {
            if (bookmarkSet.count(cached.id) && seenIds.insert(cached.id).second) {
                savedPool.push_back(cached);
            }
        }
        pool = std::move(savedPool);
    }

    auto* acc = GJAccountManager::get();
    int const myAccountId = acc ? acc->m_accountID : 0;
    std::string const qLower = toLowerAscii(m_searchQuery);

    std::unordered_set<std::string> wantedTags;
    for (auto const& t : m_selectedTags) wantedTags.insert(normalizeTagKey(t));

    std::vector<Post> filtered;
    filtered.reserve(pool.size());

    for (auto const& post : pool) {
        if (m_scope == Scope::MyPosts) {
            if (myAccountId <= 0 || post.author.accountID != myAccountId) continue;
        } else if (m_scope == Scope::Unanswered) {
            int rCount = std::max(post.replyCount, static_cast<int>(post.replies.size()));
            if (rCount > 0) continue;
        }

        if (m_authorFilterId > 0 && post.author.accountID != m_authorFilterId) continue;

        if (!wantedTags.empty()) {
            bool tagMatch = false;
            for (auto const& pt : post.tags) {
                if (wantedTags.count(normalizeTagKey(pt))) {
                    tagMatch = true;
                    break;
                }
            }
            if (!tagMatch) continue;
        }

        if (!qLower.empty()) {
            bool match = toLowerAscii(post.title).find(qLower) != std::string::npos
                || toLowerAscii(post.description).find(qLower) != std::string::npos
                || toLowerAscii(post.author.username).find(qLower) != std::string::npos;
            if (!match) {
                for (auto const& pt : post.tags) {
                    if (toLowerAscii(pt).find(qLower) != std::string::npos) {
                        match = true;
                        break;
                    }
                }
            }
            if (!match) continue;
        }

        filtered.push_back(post);
    }

    std::stable_sort(filtered.begin(), filtered.end(), [this](Post const& a, Post const& b) {
        if (a.pinned != b.pinned) return a.pinned;
        switch (m_sort) {
            case FeedSort::Top: {
                int sa = a.likes - a.reportCount;
                int sb = b.likes - b.reportCount;
                if (sa != sb) return sa > sb;
                break;
            }
            case FeedSort::Liked:
                if (a.likes != b.likes) return a.likes > b.likes;
                break;
            case FeedSort::Discussed: {
                int ra = std::max(a.replyCount, static_cast<int>(a.replies.size()));
                int rb = std::max(b.replyCount, static_cast<int>(b.replies.size()));
                if (ra != rb) return ra > rb;
                if (a.likes != b.likes) return a.likes > b.likes;
                break;
            }
            case FeedSort::Recent:
            default:
                break;
        }
        return a.createdAt > b.createdAt;
    });

    m_displayedPosts = std::move(filtered);
    updateScopeButtons();
    updateActiveFiltersBar();
    updateSidebarStats();
    updateFooter();
    renderFeed();
}

void ForumPanel::renderFeed() {
    if (!m_feedScroll) return;
    auto* content = m_feedScroll->m_contentLayer;
    content->removeAllChildren();
    clearEmptyState();

    if (m_displayedPosts.empty()) {
        content->setContentSize(m_feedScroll->getContentSize());
        showEmptyState();
        return;
    }

    float const viewW = m_feedScroll->getContentSize().width;
    float const viewH = m_feedScroll->getContentSize().height;
    float const cardPadX = 4.f;
    float const cardW = viewW - cardPadX * 2.f;
    constexpr float kCardGap = 5.f;

    std::vector<CCNode*> cards;
    cards.reserve(m_displayedPosts.size());
    float totalH = 4.f;
    for (size_t i = 0; i < m_displayedPosts.size(); ++i) {
        auto* card = makePostCard(m_displayedPosts[i], cardW, static_cast<int>(i));
        totalH += card->getContentSize().height + kCardGap;
        cards.push_back(card);
    }
    if (!cards.empty()) totalH -= kCardGap;
    totalH += 4.f;

    float const contentH = std::max(viewH, totalH);
    content->setContentSize({viewW, contentH});

    float y = contentH - 4.f;
    for (size_t i = 0; i < cards.size(); ++i) {
        float h = cards[i]->getContentSize().height;
        y -= h;
        cards[i]->setPosition({cardPadX, y});
        content->addChild(cards[i], 1);

        paimon::fluid::RevealOpts reveal;
        reveal.startDelay = 0.015f * static_cast<float>(std::min<size_t>(i, 12));
        paimon::fluid::revealNode(cards[i], reveal);

        y -= kCardGap;
    }

    m_feedScroll->moveToTop();
    m_feedScrollTargetSet = false;
}

void ForumPanel::clearEmptyState() {
    if (m_emptyNode) {
        m_emptyNode->removeFromParent();
        m_emptyNode = nullptr;
    }
}

void ForumPanel::showEmptyState() {
    clearEmptyState();
    bool const es = isSpanish();
    bool const moduleOn = paimon::modules::isEnabled("paimbnails.forum.menu");

    m_emptyNode = CCNode::create();
    m_emptyNode->setContentSize(m_feedRect.size);
    m_emptyNode->setPosition(m_feedRect.origin);
    this->addChild(m_emptyNode, 8);

    float const cx = m_feedRect.size.width / 2.f;
    float const cy = m_feedRect.size.height / 2.f;

    std::string heading;
    std::string sub;
    std::string btnLabel;
    paimon::ui::Btn btnSkin = paimon::ui::Btn::Green;
    std::function<void()> onAction;
    WeakRef<ForumPanel> self = this;

    bool const hasFilters = !m_selectedTags.empty() || !m_searchQuery.empty() || m_authorFilterId > 0;

    if (!moduleOn) {
        heading = tf("pai.hub.forum.empty.disabled_title", "Forum module is disabled", "Modulo de foro desactivado");
        sub = tf("pai.hub.forum.empty.disabled_desc",
            "Enable Forum in Modules to view and publish posts.",
            "Activa Foro en Modulos para ver y publicar posts.");
        btnLabel = tf("pai.hub.forum.empty.open_modules", "Open Modules", "Abrir Modulos");
        btnSkin = paimon::ui::Btn::Cyan;
        onAction = [] {
            if (auto* scene = PaimonModulesLayer::scene()) {
                CCDirector::get()->pushScene(scene);
            }
        };
    } else if (hasFilters) {
        heading = tf("pai.hub.forum.empty.filtered_title", "No matching posts", "Sin coincidencias");
        sub = tf("pai.hub.forum.empty.filtered_desc",
            "Try clearing active tags, search text, or author filter.",
            "Prueba quitando las etiquetas, busqueda o filtro de autor.");
        btnLabel = tf("pai.hub.forum.empty.clear_filters", "Clear Filters", "Limpiar Filtros");
        btnSkin = paimon::ui::Btn::Cyan;
        onAction = [self] {
            if (auto p = self.lock()) p->clearAllFilters();
        };
    } else if (m_scope == Scope::Saved) {
        heading = tf("pai.hub.forum.empty.saved_title", "No saved posts yet", "No tienes posts guardados");
        sub = tf("pai.hub.forum.empty.saved_desc",
            "Tap the star icon on any post to bookmark it here.",
            "Toca la estrella en cualquier post para guardarlo aqui.");
        btnLabel = tf("pai.hub.forum.scope.all", "Browse All", "Ver Todos");
        btnSkin = paimon::ui::Btn::Green;
        onAction = [self] {
            if (auto p = self.lock()) {
                p->m_scope = Scope::All;
                p->updateScopeButtons();
                p->applyFiltersAndRender();
            }
        };
    } else if (m_scope == Scope::MyPosts) {
        auto* acc = GJAccountManager::get();
        bool loggedIn = acc && acc->m_accountID > 0;
        heading = tf("pai.hub.forum.empty.mine_title", "You haven't posted yet", "Aun no has publicado");
        sub = loggedIn
            ? tf("pai.hub.forum.empty.mine_desc",
                "Share a tip, showcase a level, or ask a question.",
                "Comparte un consejo, muestra un nivel o haz una pregunta.")
            : tf("pai.hub.forum.empty.login_desc",
                "Sign in to your GD account to see and publish your posts.",
                "Inicia sesion con tu cuenta de GD para ver y crear posts.");
        btnLabel = tf("pai.hub.forum.create.cta", "+ New Post", "+ Nuevo Post");
        btnSkin = paimon::ui::Btn::Green;
        onAction = [self] {
            if (auto p = self.lock()) p->openCreatePost();
        };
    } else if (m_scope == Scope::Unanswered) {
        heading = tf("pai.hub.forum.empty.unanswered_title", "Everything has replies!", "Todo tiene respuestas!");
        sub = tf("pai.hub.forum.empty.unanswered_desc",
            "No unanswered posts on this page.",
            "No hay publicaciones sin responder en esta pagina.");
        btnLabel = tf("pai.hub.forum.scope.all", "Show All", "Ver Todos");
        btnSkin = paimon::ui::Btn::Cyan;
        onAction = [self] {
            if (auto p = self.lock()) {
                p->m_scope = Scope::All;
                p->updateScopeButtons();
                p->applyFiltersAndRender();
            }
        };
    } else {
        heading = m_lastRequestError
            ? tf("pai.hub.forum.empty.error_title", "Could not load posts", "No se pudieron cargar")
            : tf("pai.hub.forum.empty.all_title", "No posts yet", "Aun no hay publicaciones");
        sub = m_lastRequestError
            ? tf("pai.hub.forum.empty.error_desc", "Check your connection and retry.", "Revisa tu conexion y reintenta.")
            : tf("pai.hub.forum.no_posts", "Be the first to start the conversation!", "Se el primero en iniciar la conversacion!");
        btnLabel = m_lastRequestError
            ? tf("pai.hub.forum.retry", "Retry", "Reintentar")
            : tf("pai.hub.forum.create.cta", "+ New Post", "+ Nuevo Post");
        btnSkin = m_lastRequestError ? paimon::ui::Btn::Cyan : paimon::ui::Btn::Green;
        onAction = [self, err = m_lastRequestError] {
            if (auto p = self.lock()) {
                if (err) p->refreshPosts(false);
                else p->openCreatePost();
            }
        };
    }

    auto* titleLbl = CCLabelBMFont::create(heading.c_str(), "goldFont.fnt");
    titleLbl->limitLabelWidth(m_feedRect.size.width - 28.f, 0.56f, 0.25f);
    titleLbl->setPosition({cx, cy + 26.f});
    m_emptyNode->addChild(titleLbl);

    auto* subLbl = CCLabelBMFont::create(sub.c_str(), "chatFont.fnt",
        (m_feedRect.size.width - 36.f) / 0.46f, kCCTextAlignmentCenter);
    subLbl->setScale(0.46f);
    subLbl->setColor(paimon::ui::palette::muted);
    subLbl->setPosition({cx, cy + 2.f});
    m_emptyNode->addChild(subLbl);

    if (!btnLabel.empty() && onAction) {
        auto* menu = paimon::ui::makeZeroMenu();
        menu->setContentSize(m_feedRect.size);
        m_emptyNode->addChild(menu, 2);

        auto* btn = paimon::ui::makeButton(btnLabel.c_str(), std::move(onAction), btnSkin, 0.f, 0.58f);
        btn->setPosition({cx, cy - 28.f});
        menu->addChild(btn);
    }
    (void)es;
}

CCNode* ForumPanel::makePostCard(Post const& post, float cardW, int) {
    bool const es = isSpanish();
    bool const saved = isBookmarked(post.id);
    auto* acc = GJAccountManager::get();
    int const myId = acc ? acc->m_accountID : 0;
    bool const isMine = myId > 0 && post.author.accountID == myId;
    int const replyCount = std::max(post.replyCount, static_cast<int>(post.replies.size()));

    std::string snippet = trimAscii(post.description);
    for (char& ch : snippet) {
        if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
    }
    paimon::utf8::clampToLimit(snippet, 120);
    if (snippet.size() >= 117) snippet += "...";

    constexpr float kHeaderH = 20.f;
    constexpr float kFooterH = 21.f;
    constexpr float kPadX = 11.f;
    float const bodyTextW = cardW - kPadX * 2.f;

    auto* titleLbl = CCLabelBMFont::create(
        post.title.empty() ? (es ? "(sin titulo)" : "(untitled)") : post.title.c_str(),
        "goldFont.fnt"
    );
    titleLbl->setAnchorPoint({0.f, 1.f});
    titleLbl->limitLabelWidth(bodyTextW, 0.46f, 0.22f);
    float const titleH = titleLbl->getScaledContentSize().height;

    CCLabelBMFont* descLbl = nullptr;
    float descH = 0.f;
    if (!snippet.empty()) {
        descLbl = CCLabelBMFont::create(snippet.c_str(), "chatFont.fnt", bodyTextW / 0.41f, kCCTextAlignmentLeft);
        descLbl->setScale(0.41f);
        descLbl->setColor({220, 224, 236});
        descLbl->setAnchorPoint({0.f, 1.f});
        while (descLbl->getScaledContentSize().height > 23.f && snippet.size() > 16) {
            paimon::utf8::clampToLimit(snippet, snippet.size() - 8);
            descLbl->setString((snippet + "...").c_str());
        }
        descH = descLbl->getScaledContentSize().height;
    }

    float const bodyH = titleH + (descLbl ? (descH + 2.f) : 0.f) + 6.f;
    float const cardH = std::max(58.f, kHeaderH + bodyH + kFooterH);

    auto* card = CCNodeRGBA::create();
    card->setContentSize({cardW, cardH});
    card->setAnchorPoint({0.f, 0.f});
    card->setCascadeOpacityEnabled(true);

    // Card surface
    ccColor3B surfaceCol = post.pinned ? ccColor3B{32, 26, 8} : ccColor3B{0, 0, 0};
    GLubyte surfaceAlpha = post.pinned ? 115 : 88;
    card->addChild(paimon::ui::makeInset({cardW, cardH}, surfaceAlpha, surfaceCol), -2);

    // 3px left accent stripe
    ccColor4B accentCol = {95, 115, 155, 190};
    if (post.pinned) accentCol = {255, 210, 75, 240};
    else if (post.locked) accentCol = {235, 95, 95, 225};
    else if (isMine) accentCol = {115, 235, 130, 225};
    else if (saved) accentCol = {105, 215, 255, 225};

    auto* stripe = CCLayerColor::create(accentCol, 3.f, cardH - 6.f);
    stripe->setPosition({2.f, 3.f});
    card->addChild(stripe, 2);

    auto* menu = paimon::ui::makeZeroMenu();
    menu->setContentSize({cardW, cardH});
    menu->ignoreAnchorPointForPosition(true);
    card->addChild(menu, 4);

    WeakRef<ForumPanel> self = this;
    std::string postId = post.id;

    // -------------------------------------------------------------------------
    // Top Row (non-overlapping with body): Author pill + status badges + date
    // -------------------------------------------------------------------------
    float const headerY = cardH - kHeaderH / 2.f - 1.f;
    std::string authorName = post.author.username.empty()
        ? (es ? "Anonimo" : "Anonymous")
        : post.author.username;

    auto* authorLbl = CCLabelBMFont::create(authorName.c_str(), "bigFont.fnt");
    authorLbl->setScale(0.28f);
    authorLbl->setColor(isMine ? paimon::ui::palette::success : paimon::ui::palette::text);
    paimon::ui::shrinkLabelToFit(authorLbl, cardW * 0.42f);

    float const iconSize = 15.f;
    float const authorBtnW = iconSize + 6.f + authorLbl->getScaledContentSize().width + 6.f;
    auto* authorFace = CCNode::create();
    authorFace->setContentSize({authorBtnW, 18.f});
    authorFace->setAnchorPoint({0.5f, 0.5f});

    if (auto* icon = makeAuthorIcon(post.author, iconSize)) {
        icon->setPosition({iconSize / 2.f + 1.f, 9.f});
        authorFace->addChild(icon, 2);
    }
    authorLbl->setAnchorPoint({0.f, 0.5f});
    authorLbl->setPosition({iconSize + 6.f, 9.f});
    authorFace->addChild(authorLbl, 2);

    int const authorId = post.author.accountID;
    auto* authorBtn = CCMenuItemExt::createSpriteExtra(authorFace, [self, authorId, authorName](CCMenuItemSpriteExtra*) {
        if (authorId > 0) {
            openAuthorProfile(authorId);
        } else if (auto p = self.lock()) {
            p->filterByAuthor(authorId, authorName);
        }
    });
    authorBtn->m_scaleMultiplier = 1.04f;
    authorBtn->setPosition({kPadX + authorBtnW / 2.f, headerY});
    menu->addChild(authorBtn);

    float badgeX = kPadX + authorBtnW + 4.f;
    if (post.pinned) {
        auto* pinBadge = makeBadgePill("PIN", {125, 92, 16}, paimon::ui::palette::gold);
        pinBadge->setPosition({badgeX, headerY});
        card->addChild(pinBadge, 2);
        badgeX += pinBadge->getContentSize().width + 3.f;
    }
    if (isMine) {
        auto* youBadge = makeBadgePill(es ? "TU" : "YOU", {28, 96, 42}, paimon::ui::palette::success);
        youBadge->setPosition({badgeX, headerY});
        card->addChild(youBadge, 2);
        badgeX += youBadge->getContentSize().width + 3.f;
    }
    if (post.locked) {
        if (auto* lockIcon = makeSizedFrame("GJ_lock_001.png", 11.f, paimon::ui::palette::danger)) {
            lockIcon->setPosition({badgeX + 6.f, headerY});
            card->addChild(lockIcon, 2);
        }
    }

    auto* timeLbl = CCLabelBMFont::create(formatTimeShort(post.createdAt, es).c_str(), "chatFont.fnt");
    timeLbl->setScale(0.38f);
    timeLbl->setColor(paimon::ui::palette::dim);
    timeLbl->setAnchorPoint({1.f, 0.5f});
    timeLbl->setPosition({cardW - 8.f, headerY});
    card->addChild(timeLbl, 2);

    // -------------------------------------------------------------------------
    // Middle Band: Full-width clickable Title + Snippet (opens PostDetailPopup)
    // -------------------------------------------------------------------------
    float const bodyBot = kFooterH + 1.f;
    float const bodyTop = cardH - kHeaderH - 1.f;
    float const hitBodyH = std::max(18.f, bodyTop - bodyBot);
    float const hitBodyW = cardW - 16.f;

    auto* bodyFace = CCNode::create();
    bodyFace->setContentSize({hitBodyW, hitBodyH});
    bodyFace->setAnchorPoint({0.5f, 0.5f});

    titleLbl->setPosition({2.f, hitBodyH - 1.f});
    bodyFace->addChild(titleLbl, 1);

    if (descLbl) {
        descLbl->setPosition({2.f, hitBodyH - titleH - 3.f});
        bodyFace->addChild(descLbl, 1);
    }

    auto* bodyBtn = CCMenuItemExt::createSpriteExtra(bodyFace, [self, postId](CCMenuItemSpriteExtra*) {
        if (auto p = self.lock()) p->openPostDetail(postId);
    });
    bodyBtn->m_scaleMultiplier = 1.015f;
    bodyBtn->setPosition({cardW / 2.f, bodyBot + hitBodyH / 2.f});
    menu->addChild(bodyBtn);

    // -------------------------------------------------------------------------
    // Bottom Band: Action Buttons (Right) & Clickable Tag Chips (Left)
    // -------------------------------------------------------------------------
    float const footerY = 11.f;
    float rx = cardW - 8.f;

    // Bookmark Star
    auto* starFace = makeActionPillFace(
        "GJ_starsIcon_001.png", "",
        saved ? ccColor3B{120, 92, 18} : ccColor3B{30, 32, 42},
        saved ? paimon::ui::palette::gold : paimon::ui::palette::dim,
        paimon::ui::palette::text,
        saved ? 170 : 120
    );
    auto* starBtn = CCMenuItemExt::createSpriteExtra(starFace, [self, postId](CCMenuItemSpriteExtra*) {
        if (auto p = self.lock()) p->toggleBookmarkOnFeed(postId);
    });
    starBtn->m_scaleMultiplier = 1.08f;
    starBtn->setPosition({rx - starFace->getContentSize().width / 2.f, footerY});
    menu->addChild(starBtn);
    rx -= starFace->getContentSize().width + 4.f;

    // Replies pill
    auto* replyFace = makeActionPillFace(
        "GJ_chatBtn_001.png", std::to_string(replyCount),
        {28, 42, 68},
        replyCount > 0 ? paimon::ui::palette::info : paimon::ui::palette::muted,
        replyCount > 0 ? paimon::ui::palette::text : paimon::ui::palette::muted,
        135
    );
    auto* replyBtn = CCMenuItemExt::createSpriteExtra(replyFace, [self, postId](CCMenuItemSpriteExtra*) {
        if (auto p = self.lock()) p->openPostDetail(postId);
    });
    replyBtn->m_scaleMultiplier = 1.06f;
    replyBtn->setPosition({rx - replyFace->getContentSize().width / 2.f, footerY});
    menu->addChild(replyBtn);
    rx -= replyFace->getContentSize().width + 4.f;

    // Like pill
    auto* likeFace = makeActionPillFace(
        "GJ_likesIcon_001.png", std::to_string(post.likes),
        post.likedByMe ? ccColor3B{125, 36, 68} : ccColor3B{32, 34, 46},
        post.likedByMe ? ccColor3B{255, 130, 175} : paimon::ui::palette::muted,
        post.likedByMe ? ccColor3B{255, 200, 220} : paimon::ui::palette::text,
        post.likedByMe ? 175 : 135
    );
    auto* likeBtn = CCMenuItemExt::createSpriteExtra(likeFace, [self, postId](CCMenuItemSpriteExtra*) {
        if (auto p = self.lock()) p->toggleLikeOnFeed(postId);
    });
    likeBtn->m_scaleMultiplier = 1.06f;
    likeBtn->setPosition({rx - likeFace->getContentSize().width / 2.f, footerY});
    menu->addChild(likeBtn);
    rx -= likeFace->getContentSize().width + 4.f;

    // Detected GD Level ID pill
    auto levelIds = extractLevelIds(post.title + "\n" + post.description);
    if (!levelIds.empty()) {
        int lvlId = levelIds.front();
        auto* lvlFace = makeActionPillFace(
            "GJ_playBtn2_001.png", fmt::format("#{}", lvlId),
            {18, 86, 108},
            paimon::ui::palette::info,
            paimon::ui::palette::text,
            165
        );
        auto* lvlBtn = CCMenuItemExt::createSpriteExtra(lvlFace, [lvlId](CCMenuItemSpriteExtra*) {
            openLevelById(lvlId);
        });
        lvlBtn->m_scaleMultiplier = 1.06f;
        lvlBtn->setPosition({rx - lvlFace->getContentSize().width / 2.f, footerY});
        menu->addChild(lvlBtn);
        rx -= lvlFace->getContentSize().width + 4.f;
    }

    // Left: clickable tag chips up to `rx - 4.f`
    std::unordered_set<std::string> selectedKeys;
    for (auto const& s : m_selectedTags) selectedKeys.insert(normalizeTagKey(s));

    float tx = kPadX;
    int shownTags = 0;
    for (auto const& rawTag : post.tags) {
        if (shownTags >= 3) break;
        bool active = selectedKeys.count(normalizeTagKey(rawTag)) > 0;
        auto* tagFace = makeTagPillFace(rawTag, active);
        float tw = tagFace->getContentSize().width;
        if (tx + tw > rx - 6.f) break;

        std::string tagCopy = displayTag(rawTag);
        auto* tagBtn = CCMenuItemExt::createSpriteExtra(tagFace, [self, tagCopy](CCMenuItemSpriteExtra*) {
            if (auto p = self.lock()) p->toggleTagFilter(tagCopy);
        });
        tagBtn->m_scaleMultiplier = 1.05f;
        tagBtn->setPosition({tx + tw / 2.f, footerY});
        menu->addChild(tagBtn);
        tx += tw + 3.f;
        ++shownTags;
    }

    if (static_cast<int>(post.tags.size()) > shownTags && tx + 18.f <= rx - 2.f) {
        auto* more = CCLabelBMFont::create(
            fmt::format("+{}", static_cast<int>(post.tags.size()) - shownTags).c_str(),
            "chatFont.fnt"
        );
        more->setScale(0.36f);
        more->setColor(paimon::ui::palette::dim);
        more->setAnchorPoint({0.f, 0.5f});
        more->setPosition({tx + 1.f, footerY});
        card->addChild(more, 2);
    }

    return card;
}

void ForumPanel::filterByAuthor(int accountID, std::string const& username) {
    m_authorFilterId = accountID;
    m_authorFilterName = username;
    m_scope = Scope::All;
    updateScopeButtons();
    updateActiveFiltersBar();
    applyFiltersAndRender();
}

void ForumPanel::filterByTag(std::string const& tag) {
    std::string key = normalizeTagKey(tag);
    if (key.empty()) return;
    m_selectedTags.clear();
    m_selectedTags.push_back(displayTag(tag));
    m_scope = Scope::All;
    updateScopeButtons();
    rebuildTagList();
    refreshPosts(true);
}

void ForumPanel::toggleTagFilter(std::string const& tag) {
    std::string const key = normalizeTagKey(tag);
    auto it = std::find_if(m_selectedTags.begin(), m_selectedTags.end(), [&](std::string const& s) {
        return normalizeTagKey(s) == key;
    });
    if (it != m_selectedTags.end()) {
        m_selectedTags.erase(it);
    } else {
        m_selectedTags.push_back(displayTag(tag));
    }
    rebuildTagList();
    refreshPosts(true);
}

void ForumPanel::clearAllFilters() {
    m_selectedTags.clear();
    m_searchQuery.clear();
    m_searchPending = false;
    m_authorFilterId = 0;
    m_authorFilterName.clear();
    if (m_searchInput) m_searchInput->setString("");
    if (m_clearSearchBtn) m_clearSearchBtn->setVisible(false);
    rebuildTagList();
    updateActiveFiltersBar();
    refreshPosts(true);
}

void ForumPanel::toggleLikeOnFeed(std::string const& postId) {
    auto* acc = GJAccountManager::get();
    if (!acc || acc->m_accountID <= 0) {
        PaimonNotify::show(
            isSpanish() ? "Inicia sesion en tu cuenta de GD para dar like." : "Sign in to your GD account to like posts.",
            NotificationIcon::Warning
        );
        return;
    }

    auto flipPost = [](Post& p) {
        p.likedByMe = !p.likedByMe;
        p.likes = std::max(0, p.likes + (p.likedByMe ? 1 : -1));
    };

    for (auto& p : m_loadedPosts) {
        if (p.id == postId) flipPost(p);
    }
    applyFiltersAndRender();

    WeakRef<ForumPanel> self = this;
    ForumApi::get().togglePostLike(postId, [self, postId, flipPost](Result<bool> res) {
        auto panel = self.lock();
        if (!panel || !panel->getParent()) return;
        if (!res.ok) {
            for (auto& p : panel->m_loadedPosts) {
                if (p.id == postId) flipPost(p);
            }
            panel->applyFiltersAndRender();
            PaimonNotify::show(
                res.error.empty() ? (isSpanish() ? "Error al dar like" : "Failed to update like") : res.error.c_str(),
                NotificationIcon::Warning
            );
        }
    });
}

void ForumPanel::toggleBookmarkOnFeed(std::string const& postId) {
    bool saved = toggleBookmark(postId);
    PaimonNotify::show(
        saved
            ? (isSpanish() ? "Guardado en favoritos" : "Saved to bookmarks")
            : (isSpanish() ? "Quitado de guardados" : "Removed from bookmarks"),
        NotificationIcon::Info
    );
    applyFiltersAndRender();
}

void ForumPanel::openCreatePost() {
    if (!paimon::modules::isEnabled("paimbnails.forum.menu")) {
        PaimonNotify::show(
            isSpanish() ? "El modulo de Foro esta desactivado." : "Forum module is disabled.",
            NotificationIcon::Warning
        );
        return;
    }

    std::vector<std::string> available = m_knownTags;
    std::unordered_set<std::string> seen;
    for (auto const& t : available) seen.insert(normalizeTagKey(t));
    for (auto const& t : m_customTags) {
        if (seen.insert(normalizeTagKey(t)).second) available.push_back(displayTag(t));
    }

    WeakRef<ForumPanel> self = this;
    auto* popup = CreatePostPopup::create(std::move(available), [self](Post const& created) {
        auto panel = self.lock();
        if (!panel || !panel->getParent()) return;
        for (auto const& t : created.tags) {
            std::string key = normalizeTagKey(t);
            bool known = std::any_of(panel->m_knownTags.begin(), panel->m_knownTags.end(),
                [&](std::string const& k) { return normalizeTagKey(k) == key; });
            bool custom = std::any_of(panel->m_customTags.begin(), panel->m_customTags.end(),
                [&](std::string const& k) { return normalizeTagKey(k) == key; });
            if (!known && !custom) panel->m_customTags.push_back(displayTag(t));
        }
        panel->m_scope = Scope::All;
        panel->m_sort = FeedSort::Recent;
        panel->updateScopeButtons();
        panel->updateSortButtons();
        panel->rebuildTagList();
        panel->refreshPosts(true);
    });
    if (popup) popup->show();
}

void ForumPanel::openCreateTagPopup() {
    WeakRef<ForumPanel> self = this;
    auto* popup = CustomTagPopup::create([self](std::string const& tag) {
        auto panel = self.lock();
        if (!panel || !panel->getParent()) return;
        std::string key = normalizeTagKey(tag);
        if (key.empty()) return;

        bool exists = std::any_of(panel->m_knownTags.begin(), panel->m_knownTags.end(),
            [&](std::string const& t) { return normalizeTagKey(t) == key; })
            || std::any_of(panel->m_customTags.begin(), panel->m_customTags.end(),
                [&](std::string const& t) { return normalizeTagKey(t) == key; });
        if (!exists) {
            panel->m_customTags.push_back(displayTag(tag));
        }

        bool selected = std::any_of(panel->m_selectedTags.begin(), panel->m_selectedTags.end(),
            [&](std::string const& t) { return normalizeTagKey(t) == key; });
        if (!selected) {
            panel->m_selectedTags.push_back(displayTag(tag));
        }
        panel->rebuildTagList();
        panel->refreshPosts(true);
    });
    if (popup) popup->show();
}

void ForumPanel::openPostDetail(std::string const& postId) {
    if (postId.empty()) return;
    setLoading(true);
    WeakRef<ForumPanel> self = this;
    ForumApi::get().getPost(postId, [self](Result<Post> res) {
        auto panel = self.lock();
        if (!panel || !panel->getParent()) return;
        panel->setLoading(false);
        if (!res.ok) {
            PaimonNotify::show(
                isSpanish() ? "No se pudo cargar la publicacion" : "Failed to load post",
                NotificationIcon::Error
            );
            return;
        }

        PostDetailCallbacks callbacks;
        callbacks.onChanged = [self] {
            if (auto p = self.lock(); p && p->getParent()) {
                p->refreshPosts(false);
            }
        };
        callbacks.onFilterTag = [self](std::string const& tag) {
            if (auto p = self.lock(); p && p->getParent()) {
                p->filterByTag(tag);
            }
        };
        callbacks.onFilterAuthor = [self](int accountID, std::string const& username) {
            if (auto p = self.lock(); p && p->getParent()) {
                p->filterByAuthor(accountID, username);
            }
        };

        if (auto* popup = PostDetailPopup::create(res.data, std::move(callbacks))) {
            popup->show();
        }
    });
}

void ForumPanel::openHelpPopup() {
    bool const es = isSpanish();
    std::string body = es
        ? "Explora publicaciones de la comunidad, guias, ideas y niveles.\n\n"
          "<cy>Atajos utiles:</c>\n"
          "- Toca el <cg>autor</c> de un post para abrir su perfil.\n"
          "- Toca la <cy>estrella</c> para guardar posts en <cy>Guardados</c>.\n"
          "- Los <cb>IDs de nivel</c> detectados muestran un boton directo para abrir el nivel en GD.\n"
          "- Filtra por <cg>etiquetas</c>, busqueda por texto o publicaciones sin responder."
        : "Browse community posts, guides, ideas, and level showcases.\n\n"
          "<cy>Useful tips:</c>\n"
          "- Tap a post <cg>author</c> to open their GD profile.\n"
          "- Tap the <cy>star</c> icon to bookmark posts into <cy>Saved</c>.\n"
          "- Detected <cb>GD Level IDs</c> show a one-tap button to open the level directly.\n"
          "- Filter by <cg>tags</c>, search keywords/authors, or unanswered posts.";
    PopupManager::get().alert(es ? "Ayuda del Foro" : "Forum Help", body).showInstant();
}

} // namespace paimon::forum
