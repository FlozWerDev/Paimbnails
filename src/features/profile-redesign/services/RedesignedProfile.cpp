#include "RedesignedProfile.hpp"
#include "ProfileLayout.hpp"
#include "ProfileLevelsPanel.hpp"
#include "ProfileScrollLayer.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/general.hpp>
#include <Geode/utils/cocos.hpp>

#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/GameToolbox.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJCommentListLayer.hpp>
#include <Geode/binding/CommentCell.hpp>
#include <Geode/binding/GJComment.hpp>
#include <Geode/binding/ShareCommentLayer.hpp>
#include <Geode/binding/LevelBrowserLayer.hpp>

#include "../../../framework/compat/SceneLocators.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/UnicodeLabel.hpp"
#include "../../profiles/services/ProfilePicCustomizer.hpp"
#include "../../profiles/services/ProfileNameDecorator.hpp"
#include "../../profiles/services/ProfileIdentityStore.hpp"
#include "../../profiles/ui/ProfileIdentityPopup.hpp"
#include "../../forum/services/ForumApi.hpp"
#include "../../emotes/EmoteRenderer.hpp"
#include "../../emotes/services/EmoteService.hpp"
#include "../../profiles/services/OwnProfileStats.hpp"
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/binding/CCScrollLayerExt.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCScale9Sprite.h>

#include <algorithm>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <functional>

using namespace geode::prelude;

namespace paimon::profile_redesign {
class ProfileActivityPanel : public cocos2d::CCNode {
public:
    using cocos2d::CCNode::init;
    enum class Mode { Posts, Reviews, Views, Levels };

    int m_accountID = 0;
    int m_userID = 0;
    Mode m_mode = Mode::Posts;
    cocos2d::CCSize m_area = {0.f, 0.f};
    ProfileScrollLayer* m_scroll = nullptr;
    ProfileLevelsPanel* m_levels = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_tabs;
    float m_offsets[4] = {};
    uint64_t m_gen = 0;

    static ProfileActivityPanel* create(int accountID, int userID, cocos2d::CCSize area) {
        auto panel = std::make_unique<ProfileActivityPanel>();
        if (!panel->init(accountID, userID, area)) return nullptr;
        panel->autorelease();
        return panel.release();
    }

    bool init(int accountID, int userID, cocos2d::CCSize area) {
        if (!CCNode::init()) return false;
        m_accountID = accountID;
        m_userID = userID;
        m_area = area;
        this->setContentSize(area);
        this->setAnchorPoint({0.5f, 0.5f});
        this->ignoreAnchorPointForPosition(false);

        auto menu = cocos2d::CCMenu::create();
        menu->setAnchorPoint({0.f, 0.f});
        menu->ignoreAnchorPointForPosition(false);
        menu->setContentSize(area);
        menu->setPosition({0.f, 0.f});
        this->addChild(menu, 2);

        bool const spanish = Localization::get().getLanguage() == Localization::Language::SPANISH;
        char const* const titles[] = {spanish ? "Publicaciones" : "Posts",
            spanish ? "Resenas" : "Reviews", spanish ? "Visitas" : "Views", spanish ? "Niveles" : "Levels"};
        auto const tabs = profileActivityTabs(area.width, area.height);
        char const* const tabIDs[] = {"rd-activity-posts"_spr, "rd-activity-reviews"_spr, "rd-activity-views"_spr, "rd-activity-levels"_spr};
        for (std::size_t index = 0; index < tabs.size(); ++index) {
            auto const& tab = tabs[index];
            auto* face = CCNode::create();
            face->setContentSize({tab.width, tab.height});
            auto* background = ButtonSprite::create("", static_cast<int>(tab.width), true,
                "goldFont.fnt", "GJ_button_02.png", 30.f, .8f);
            auto* label = paimon::ui::createUnicodeLabel(titles[index], "goldFont.fnt");
            if (!background || !label) return false;
            background->setID("tab-background");
            background->setScaleX(tab.width / background->getContentWidth());
            background->setScaleY(tab.height / background->getContentHeight());
            background->setPosition({tab.width * .5f, tab.height * .5f});
            face->addChild(background);
            label->setID("tab-label");
            label->setLimitLabelSize({tab.width - 10.f, tab.height - 7.f}, .42f, .1f);
            label->setPosition({tab.width * .5f, tab.height * .5f});
            face->addChild(label, 1);
            auto const mode = static_cast<Mode>(index);
            auto* button = CCMenuItemExt::createSpriteExtra(face,
                [this, mode](CCMenuItemSpriteExtra*) { selectMode(mode); });
            button->setID(tabIDs[index]);
            button->setPosition({tab.centerX(), tab.centerY()});
            button->m_scaleMultiplier = 1.04f;
            menu->addChild(button);
            m_tabs.push_back(button);
        }

        float const scrollH = std::max(10.f, area.height - 30.f);
        m_scroll = ProfileScrollLayer::create({area.width - 8.f, scrollH});
        m_scroll->setPosition({4.f, 4.f});
        m_scroll->setStealingTouches(true);
        this->addChild(m_scroll, 1);
        m_levels = ProfileLevelsPanel::create(userID, {area.width - 8.f, scrollH});
        if (!m_levels) return false;
        m_levels->setID("rd-creator-levels"_spr);
        m_levels->setPosition({4.f, 4.f});
        addChild(m_levels, 1);

        updateVisibility();
        return true;
    }

    void updateVisibility() {
        bool const posts = m_mode == Mode::Posts;
        bool const secondary = m_mode == Mode::Reviews || m_mode == Mode::Views;
        m_scroll->setVisible(secondary);
        m_scroll->setTouchEnabled(secondary);
        m_scroll->setMouseEnabled(secondary);
        if (m_levels) m_levels->setActive(m_mode == Mode::Levels);
        for (std::size_t index = 0; index < m_tabs.size(); ++index) {
            auto* background = typeinfo_cast<ButtonSprite*>(m_tabs[index]->getNormalImage()->getChildByID("tab-background"));
            if (background) background->updateBGImage(index == static_cast<std::size_t>(m_mode)
                ? "GJ_button_01.png" : "GJ_button_02.png");
        }
        auto* layer = getParent();
        if (!layer) return;
        if (auto* navigation = layer->getChildByID("rd-comment-navigation"_spr)) {
            navigation->setVisible(posts);
        }
        if (auto* scroll = typeinfo_cast<ScrollLayer*>(layer->getChildByID("rd-comment-content"_spr))) {
            scroll->setVisible(posts);
            scroll->setTouchEnabled(posts);
            scroll->setMouseEnabled(posts);
        }
        if (auto* actions = layer->getChildByID("rd-review-actions"_spr)) {
            actions->setVisible(m_mode == Mode::Reviews);
        }
    }

    void selectMode(Mode mode) {
        if (mode == m_mode) return;
        if (m_mode == Mode::Reviews || m_mode == Mode::Views) m_offsets[static_cast<int>(m_mode)] = m_scroll->getScrollFromTop();
        m_mode = mode;
        ++m_gen; // Discard a late Reviews/Views response when returning to Posts.
        updateVisibility();
        if (mode == Mode::Reviews || mode == Mode::Views) loadActivity();
    }

    void clearScroll() {
        if (m_scroll && m_scroll->m_contentLayer) m_scroll->m_contentLayer->removeAllChildren();
    }

    void showMessage(std::string const& msg) {
        clearScroll();
        if (!m_scroll) return;
        float h = m_scroll->getContentSize().height;
        auto lbl = cocos2d::CCLabelBMFont::create(msg.c_str(), "chatFont.fnt");
        lbl->setScale(0.42f);
        lbl->setColor({160, 160, 170});
        lbl->setAnchorPoint({0.5f, 0.5f});
        lbl->limitLabelWidth(m_area.width - 20.f, .45f, .25f);
        lbl->setPosition({m_scroll->getContentWidth() * 0.5f, h * 0.5f});
        m_scroll->m_contentLayer->setContentSize({m_scroll->getContentWidth(), h});
        m_scroll->m_contentLayer->addChild(lbl);
        m_scroll->scrollToTop();
    }

    void stackCells(std::vector<cocos2d::CCNode*>& cells) {
        if (!m_scroll) return;
        float w = m_scroll->getContentWidth();
        float scrollH = m_scroll->getContentSize().height;
        float pad = 3.f;
        float totalH = 0.f;
        for (auto* c : cells) totalH += c->getContentHeight() + pad;
        totalH = std::max(totalH, scrollH);
        auto container = cocos2d::CCLayer::create();
        container->setContentSize({w, totalH});
        float y = totalH;
        for (auto* c : cells) {
            y -= c->getContentHeight() + pad;
            c->setPosition({0.f, y});
            container->addChild(c);
        }
        m_scroll->m_contentLayer->setContentSize({w, totalH});
        m_scroll->m_contentLayer->addChild(container);
        m_scroll->scrollToFromTop(m_offsets[static_cast<int>(m_mode)]);
    }

    void loadActivity() {
        ++m_gen;
        showMessage("Loading...");
        if (m_mode == Mode::Reviews) loadReviews();
        else loadViews();
    }

    void loadReviews() {
        std::string username;
        if (auto gm = GameManager::get()) username = gm->m_playerName;
        std::string endpoint = fmt::format("/api/profile-ratings/{}?username={}",
            m_accountID, HttpClient::encodeQueryParam(username));
        uint64_t gen = m_gen;
        WeakRef<ProfileActivityPanel> self = this;
        HttpClient::get().get(endpoint, [self, gen](bool ok, std::string const& resp) {
            auto p = self.lock();
            if (!p || gen != p->m_gen) return;
            if (!ok) { p->showMessage("No reviews yet"); return; }
            auto parsed = matjson::parse(resp);
            if (!parsed.isOk()) { p->showMessage("No reviews yet"); return; }
            p->renderReviews(parsed.unwrap()["reviews"]);
        });
    }

    void renderReviews(matjson::Value const& reviews) {
        clearScroll();
        std::vector<cocos2d::CCNode*> cells;
        if (reviews.isArray()) {
            for (auto& item : reviews.asArray().unwrapOr(std::vector<matjson::Value>{})) {
                if (!item.isObject()) continue;
                std::string user = item["username"].asString().unwrapOr("???");
                float stars = static_cast<float>(item["stars"].asDouble().unwrapOr(0.0));
                std::string msg = item["message"].asString().unwrapOr("");
                if (auto cell = makeReviewCell(user, stars, msg)) cells.push_back(cell);
            }
        }
        if (cells.empty()) { showMessage("No reviews yet"); return; }
        stackCells(cells);
    }

    void loadViews() {
        uint64_t gen = m_gen;
        WeakRef<ProfileActivityPanel> self = this;
        paimon::forum::ForumApi::get().getProfileViews(m_accountID,
            [self, gen](paimon::forum::Result<std::vector<paimon::forum::ProfileView>> result) {
                auto p = self.lock();
                if (!p || gen != p->m_gen) return;
                if (!result.ok) { p->showMessage("Views unavailable"); return; }
                p->renderViews(result.data);
            });
    }

    void renderViews(std::vector<paimon::forum::ProfileView> const& views) {
        clearScroll();
        auto sorted = views;
        std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) {
            return a.viewedAt > b.viewedAt;
        });
        std::vector<cocos2d::CCNode*> cells;
        for (auto const& v : sorted) {
            if (auto cell = makeViewCell(v.viewerUsername, v.viewedAt)) cells.push_back(cell);
        }
        if (cells.empty()) { showMessage("No views yet"); return; }
        stackCells(cells);
    }

    cocos2d::CCNode* makeReviewCell(std::string const& user, float stars, std::string const& msg) {
        float w = m_scroll->getContentWidth();
        bool hasMsg = !msg.empty();
        float maxMsgW = w - 12.f;
        cocos2d::CCNode* msgNode = nullptr;
        if (hasMsg) {
            if (paimon::emotes::EmoteService::get().isLoaded() &&
                (paimon::emotes::EmoteRenderer::hasEmoteSyntax(msg) ||
                 paimon::emotes::EmoteRenderer::hasMentionSyntax(msg))) {
                msgNode = paimon::emotes::EmoteRenderer::renderComment(
                    msg, 13.f, maxMsgW, "chatFont.fnt", 0.4f);
            }
            if (!msgNode) {
                auto* lbl = cocos2d::CCLabelBMFont::create(
                    msg.c_str(), "chatFont.fnt", maxMsgW / 0.4f, kCCTextAlignmentLeft);
                if (lbl) {
                    lbl->setScale(0.4f);
                    lbl->setColor({210, 210, 220});
                    lbl->setAnchorPoint({0.f, 1.f});
                    msgNode = lbl;
                }
            }
        }
        float msgH = msgNode ? msgNode->getContentSize().height * msgNode->getScaleY() : 0.f;

        const float headerH = 14.f;
        float h = hasMsg ? std::max(24.f, headerH + msgH + 6.f) : 16.f;

        auto cell = cocos2d::CCNode::create();
        cell->setContentSize({w, h});
        if (auto bg = paimon::SpriteHelper::createDarkPanel(w, h, 50, 4.f)) {
            bg->setPosition({0.f, 0.f});
            cell->addChild(bg, -1);
        }
        auto name = cocos2d::CCLabelBMFont::create(user.c_str(), "goldFont.fnt");
        name->setScale(0.36f);
        name->setAnchorPoint({0.f, 0.5f});
        name->setPosition({5.f, h - 8.f});
        float maxNameW = w * 0.56f;
        if (name->getContentWidth() > 0.f && name->getScaledContentWidth() > maxNameW)
            name->setScale(maxNameW / name->getContentWidth());
        cell->addChild(name);
        auto starVal = cocos2d::CCLabelBMFont::create(fmt::format("{:.1f}", stars).c_str(), "bigFont.fnt");
        starVal->setScale(0.34f);
        starVal->setColor({255, 226, 92});
        starVal->setAnchorPoint({1.f, 0.5f});
        starVal->setPosition({w - 15.f, h - 8.f});
        cell->addChild(starVal);
        if (auto star = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png")) {
            star->setScale(0.26f);
            star->setColor({255, 226, 92});
            star->setAnchorPoint({1.f, 0.5f});
            star->setPosition({w - 4.f, h - 8.f});
            cell->addChild(star);
        }
        if (msgNode) {
            msgNode->setAnchorPoint({0.f, 1.f});
            msgNode->setPosition({6.f, h - headerH});
            cell->addChild(msgNode);
        }
        return cell;
    }

    cocos2d::CCNode* makeViewCell(std::string const& user, int64_t viewedAt) {
        float w = m_scroll->getContentWidth();
        float h = 16.f;
        auto cell = cocos2d::CCNode::create();
        cell->setContentSize({w, h});
        auto name = cocos2d::CCLabelBMFont::create(user.c_str(), "chatFont.fnt");
        name->setScale(0.45f);
        name->setColor({220, 220, 240});
        name->setAnchorPoint({0.f, 0.5f});
        name->setPosition({5.f, h * 0.5f});
        float maxNameW = w * 0.55f;
        if (name->getContentWidth() > 0.f && name->getScaledContentWidth() > maxNameW)
            name->setScale(maxNameW / name->getContentWidth());
        cell->addChild(name);
        auto t = cocos2d::CCLabelBMFont::create(
            paimon::forum::formatRelativeTime(viewedAt).c_str(), "chatFont.fnt");
        t->setScale(0.36f);
        t->setColor({160, 160, 180});
        t->setAnchorPoint({1.f, 0.5f});
        t->setPosition({w - 4.f, h * 0.5f});
        cell->addChild(t);
        if (auto line = paimon::SpriteHelper::createDarkPanel(w - 6.f, 0.8f, 40, 0.f)) {
            line->setPosition({3.f, 0.f});
            cell->addChild(line, -1);
        }
        return cell;
    }
};
static void removeByID(CCNode* parent, std::string const& id) {
    if (!parent) return;
    while (auto* n = parent->getChildByID(id)) n->removeFromParent();
}

static void removeGeneratedChildren(CCNode* parent) {
    if (!parent || !parent->getChildren()) return;

    const std::string prefix = "rd-generated-"_spr;
    std::vector<CCNode*> generated;
    for (auto* child : CCArrayExt<CCNode*>(parent->getChildren())) {
        auto const id = std::string(child->getID());
        if (id.rfind(prefix, 0) == 0) generated.push_back(child);
    }
    for (auto* child : generated) child->removeFromParent();
}

static std::string accountRailSignature(CCNode* menu) {
    if (!menu || !menu->getChildren()) return {};
    std::string result;
    for (auto* node : CCArrayExt<CCNode*>(menu->getChildren())) {
        result += fmt::format("{}:{};", node->getID(), node->isVisible());
    }
    return result;
}

static bool arrangeAccountRail(CCMenu* menu, ProfileRect const& area) {
    if (!menu) return false;
    constexpr std::string_view order[] = {
        "message-button", "requests-button", "comment-button", "profile-info-button"_spr,
        "profile-settings-button"_spr,
        "thumbs-gear-button"_spr, "add-moderator-button"_spr,
        "block-button", "unblock-button", "report-button", "mod-button", "ban-user-button"_spr,
        "friend-button", "notifications-button", "settings-button", "follow-button", "socials-button",
    };
    std::vector<CCNode*> buttons;
    for (auto* node : CCArrayExt<CCNode*>(menu->getChildren())) {
        if (node->isVisible()) buttons.push_back(node);
    }
    std::stable_sort(buttons.begin(), buttons.end(), [&](auto* left, auto* right) {
        return std::find(std::begin(order), std::end(order), left->getID()) <
            std::find(std::begin(order), std::end(order), right->getID());
    });
    menu->setLayout(nullptr);
    float const height = std::min(area.height, static_cast<float>(buttons.size()) * 36.f + 8.f);
    float const rowHeight = (height - 8.f) / std::max(1.f, static_cast<float>(buttons.size()));
    float const iconSize = std::max(1.f, std::min(26.f, rowHeight - 6.f));
    menu->setContentSize({area.width, height});
    menu->setPosition({area.centerX(), area.top() - height * .5f});
    for (std::size_t index = 0; index < buttons.size(); ++index) {
        auto* node = buttons[index];
        auto const bounds = node->getContentSize();
        float const scale = std::min(1.f, iconSize / std::max(1.f, std::max(bounds.width, bounds.height)));
        node->setScale(scale);
        if (auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(node)) item->m_baseScale = scale;
        node->setPosition({area.width * .5f, height - 4.f - rowHeight * (static_cast<float>(index) + .5f)});
    }
    menu->setUserObject("rd-layout-signature"_spr, CCString::create(accountRailSignature(menu)));
    return !buttons.empty();
}

static void collectNodesByID(CCNode* root, std::string const& id, std::vector<CCNode*>& out) {
    if (!root) return;
    // content cells have their own badges and controls; they belong to the post/level.
    if (root->getID() == "rd-comment-content"_spr || root->getID() == "rd-level-container"_spr ||
        root->getID() == "rd-activity-panel"_spr) return;
    if (root->getID() == id) out.push_back(root);
    if (auto* kids = root->getChildren()) {
        for (auto* k : CCArrayExt<CCNode*>(kids)) collectNodesByID(k, id, out);
    }
}

static void hideInlineSocialButtons(CCNode* layer) {
    // the native My Socials popup owns the networks and their URL handling.
    // also hide copies left in a previous layout when rebuilding the page.
    for (auto const* id : {"youtube-button", "twitter-button", "twitch-button", "instagram-button", "tiktok-button"}) {
        std::vector<CCNode*> duplicates;
        collectNodesByID(layer, id, duplicates);
        for (auto* node : duplicates) node->setVisible(false);
    }
}

// chips forward the tap to the hidden vanilla item, keeping its breakdown.
static CCMenuItem* findVanillaStatButton(CCNode* layer, char const* statID) {
    if (!layer || !statID) return nullptr;
    auto* icon = layer->getChildByIDRecursive(fmt::format("{}-icon", statID));
    for (CCNode* node = icon; node; node = node->getParent()) {
        if (auto* item = typeinfo_cast<CCMenuItem*>(node)) return item;
    }
    return nullptr;
}

// ids adopted by buildinplace(); needssettlepass() uses the same set.
static std::unordered_set<std::string> const& relocatableIDs(bool /*ownProfile*/) {
    static std::unordered_set<std::string> const ids = {
        "close-button", "refresh-button",
        "message-button", "friend-button", "settings-button", "requests-button", "comment-button",
        "notifications-button", "block-button", "unblock-button", "report-button", "mod-button",
        "prev-page-button", "next-page-button", "info-button",
        "socials-button",
        "comment-history-button", "my-levels-button", "my-lists-button", "follow-button",
        "profile-settings-button"_spr, "thumbs-gear-button"_spr, "profile-info-button"_spr,
        "add-moderator-button"_spr, "ban-user-button"_spr,
        "paimon-moderator-badge"_spr, "paimon-admin-badge"_spr,
        "paimon-custom-badge"_spr, "paimon-user-status-dot"_spr,
        "paimon-level-badge"_spr,
        "profile-reviews-btn"_spr, "rate-profile-btn"_spr,
        "paimon-thumb-count-btn"_spr, "profile-music-pause-button"_spr,
        "copy-icons-button"_spr, "2p-toggler"_spr,
    };
    return ids;
}

// the icon row has its own gjcommentlistlayer; hide only the account-comments list.
static GJCommentListLayer* asVanillaCommentList(cocos2d::CCNode* node) {
    auto* list = typeinfo_cast<GJCommentListLayer*>(node);
    if (!list || list->getID() == "icon-background") return nullptr;
    return list;
}

static bool hasVisibleVanillaCommentList(cocos2d::CCLayer* layer) {
    if (!layer || !layer->getChildren()) return false;
    for (auto* child : CCArrayExt<CCNode*>(layer->getChildren())) {
        auto* list = asVanillaCommentList(child);
        if (list && list->isVisible()) return true;
    }
    return false;
}

bool needsSettlePass(cocos2d::CCLayer* layer, cocos2d::CCNode* buttonMenu, bool ownProfile) {
    if (!layer) return false;
    for (auto const* id : {"rd-options-rail"_spr, "rd-socials-rail"_spr}) {
        if (auto* rail = layer->getChildByID(id)) {
            auto* signature = typeinfo_cast<CCString*>(rail->getUserObject("rd-layout-signature"_spr));
            if (signature && signature->getCString() != accountRailSignature(rail)) return true;
        }
    }
// vanilla refresh rebuilds comments and invalidates the previous layout.
    if (hasVisibleVanillaCommentList(layer)) return true;
    auto const& ids = relocatableIDs(ownProfile);
    std::string const rdPrefix = "rd-"_spr;
    bool needs = false;
    std::unordered_set<std::string> seen;
    auto walk = [&](auto const& self, CCNode* node, bool insideRd) -> void {
        if (!node || needs) return;
        auto const id = std::string(node->getID());
        if (id == "rd-comment-content"_spr || id == "rd-level-container"_spr || id == "rd-activity-panel"_spr) return;
        bool const rd = insideRd || id.rfind(rdPrefix, 0) == 0;
        if (!id.empty() && ids.contains(id)) {
            if (!rd || !seen.insert(id).second) { needs = true; return; }
        }
        if (auto* kids = node->getChildren()) {
            for (auto* k : CCArrayExt<CCNode*>(kids)) {
                self(self, k, rd);
                if (needs) return;
            }
        }
    };
    walk(walk, layer, false);
    if (!needs && buttonMenu && !buttonMenu->hasAncestor(layer)) {
        walk(walk, buttonMenu, false);
    }
    return needs;
}

static bool isCommentDecoration(CCNode* node) {
    if (!node) return false;
    std::string const id = std::string(node->getID());
    if (!id.empty()) {
        if (id.find("paimon-") != std::string::npos) return false;
        if (id == "background" || id == "comment-background" ||
            id == "left-border" || id == "right-border" ||
            id == "top-border" || id == "bottom-border") {
            return true;
        }
        if (id.find("happy_textures") != std::string::npos) return true;
    }
    return typeinfo_cast<cocos2d::CCLayerColor*>(node) ||
           typeinfo_cast<cocos2d::extension::CCScale9Sprite*>(node);
}
static void stripCommentDecorations(CCNode* listNode) {
    if (!listNode) return;
    auto walk = [&](auto const& self, CCNode* node) -> void {
        auto* kids = node->getChildren();
        if (!kids) return;
        for (auto* k : CCArrayExt<CCNode*>(kids)) {
            if (!k) continue;
            std::string const kid = std::string(k->getID());
            if (!kid.empty() && kid.find("paimon-") != std::string::npos) continue;
            if (isCommentDecoration(k)) {
                k->setVisible(false);
                continue;
            }
            if (typeinfo_cast<CCMenu*>(k)) continue;
            self(self, k);
        }
    };
    walk(walk, listNode);
}

static CCScrollLayerExt* findScroller(CCNode* root) {
    if (!root) return nullptr;
    if (auto* s = typeinfo_cast<CCScrollLayerExt*>(root)) return s;
    if (auto* kids = root->getChildren()) {
        for (auto* k : CCArrayExt<CCNode*>(kids)) {
            if (auto* s = findScroller(k)) return s;
        }
    }
    return nullptr;
}

static CommentCell* makeAccountCommentCell(GJComment* comment, float width, float height) {
    if (!comment) return nullptr;
    auto* cell = new CommentCell("", width, height);
    if (!cell->init()) {
        delete cell;
        return nullptr;
    }
    cell->autorelease();
    cell->m_accountComment = true;
    cell->loadFromComment(comment);
    return cell;
}

static void setMenuTouchPriority(CCNode* root, int priority) {
    if (!root) return;
    if (auto* menu = typeinfo_cast<CCMenu*>(root)) {
        menu->setTouchPriority(priority);
    }
    if (auto* children = root->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            setMenuTouchPriority(child, priority);
        }
    }
}

// remembered on every redesigned post. A later loadFromComment (the translator
// repaints text that way) rebuilds GD's brown bubble and fresh menus, so the
// same look and touch priority have to be put back.
class AccountCellStyle : public cocos2d::CCObject {
public:
    float width = 0.f;
    float height = 0.f;
    int touchPriority = 0;

    static AccountCellStyle* create(float width, float height, int touchPriority) {
        auto* style = new AccountCellStyle();
        style->width = width;
        style->height = height;
        style->touchPriority = touchPriority;
        style->autorelease();
        return style;
    }
};

static void styleAccountCommentCell(CommentCell* cell, float w, float h) {
    if (!cell) return;
    stripCommentDecorations(cell);
    if (cell->m_backgroundLayer) cell->m_backgroundLayer->setVisible(false);
    if (auto* p = cell->getChildByID("paimon-comment-bg-panel"_spr)) p->setVisible(false);

    if (!cell->getChildByID("paimon-rd-comment-panel"_spr)) {
        auto* panel = paimon::SpriteHelper::createColorPanel(
            w - 4.f, h - 2.f, {30, 33, 48}, 60, 4.f);
        if (panel) {
            panel->ignoreAnchorPointForPosition(false);
            panel->setPosition({2.f, 1.f});
            panel->setZOrder(-20);
            panel->setID("paimon-rd-comment-panel"_spr);
            cell->addChild(panel);
        }
    }
}

void restyleAccountCommentCell(CommentCell* cell) {
    if (!cell) return;
    auto* style = typeinfo_cast<AccountCellStyle*>(cell->getUserObject("paimon-rd-cell-style"_spr));
    if (!style) return;
    styleAccountCommentCell(cell, style->width, style->height);
    setMenuTouchPriority(cell, style->touchPriority);
}

static void hideVanillaChrome(CCNode* root) {
    if (!root) return;

    static constexpr std::string_view exactIDs[] = {
        "username-menu", "stats-menu", "socials-menu",
        "left-menu", "right-menu", "bottom-menu", "icon-background", "icon-background-border", "follow-hint",
        "floor-line", "username-label", "global-rank-label",
        "global-rank-icon", "my-stuff-label", "my-stuff-arrow",
        "my-levels-hint", "my-lists-hint",
    };
    static constexpr std::string_view idFragments[] = {
        "global-rank", "my-stuff",
    };

    auto hide = [&](auto&& self, CCNode* node) -> void {
        // comment cells reuse the same node ids as the profile header
        // ("username-label", "username-menu"), so walking into them erased the
        // author of every comment on the page. The icon row ("icon-background")
        // is still treated as chrome and hidden below.
        if (typeinfo_cast<CommentCell*>(node) || asVanillaCommentList(node)) return;

        auto const id = std::string(node->getID());
        if (!id.empty()) {
            bool shouldHide = std::find(exactIDs, std::end(exactIDs), id) != std::end(exactIDs);
            if (!shouldHide) {
                shouldHide = std::any_of(std::begin(idFragments), std::end(idFragments),
                    [&](std::string_view fragment) { return id.find(fragment) != std::string::npos; });
            }
            if (shouldHide) node->setVisible(false);
        }

        if (!node->getChildren()) return;
        for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) self(self, child);
    };
    hide(hide, root);
}

void prepareInPlace(CCLayer* layer) {
    if (!layer) return;

    hideVanillaChrome(layer);
    CCNode* background = layer->getChildByID("background");
    if (!background && layer->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(layer->getChildren())) {
            if (typeinfo_cast<cocos2d::extension::CCScale9Sprite*>(child)) {
                background = child;
                break;
            }
        }
    }

    constexpr ccColor3B neutralBackground{255, 255, 255};
    if (auto* scale9 = typeinfo_cast<cocos2d::extension::CCScale9Sprite*>(background)) {
        scale9->setCascadeColorEnabled(true);
        scale9->setColor(neutralBackground);
        scale9->setOpacity(255);
    } else if (auto* sprite = typeinfo_cast<CCSprite*>(background)) {
        sprite->setColor(neutralBackground);
        sprite->setOpacity(255);
    }
}

static bool hasActiveBackdrop(CCLayer* layer) {
    if (!layer || !layer->getChildren()) return false;
    for (auto* ch : CCArrayExt<CCNode*>(layer->getChildren())) {
        if (typeinfo_cast<cocos2d::CCClippingNode*>(ch)) return true;
    }
    return false;
}

void buildInPlace(CCLayer* layer, CCNode* buttonMenu, GJUserScore* score,
                  GJCommentListLayer* commentList, int accountId, bool ownProfile,
                  CCArray* comments, bool commentsLoaded) {
    if (!layer || !score) return;
    auto gm = GameManager::get();
    if (!gm) return;

    auto geo = paimon::compat::InfoLayerLocator::findPopupGeometry(layer);
    const CCPoint c = geo.center;
    const CCSize  sz = (geo.size.width > 80.f) ? geo.size : CCSize(440.f, 290.f);
    const float hw = sz.width * 0.5f;
    const float hh = sz.height * 0.5f;
    const bool backdrop = hasActiveBackdrop(layer);
    const GLubyte cardA = backdrop ? 175 : 195;
    auto const layout = profileLayout(c.x, c.y, sz.width, sz.height);

    constexpr int Z_CARD = 55;
    constexpr int Z_CONTENT = 58;

    const float railXLeft = c.x - hw + 24.f;
    const float railXRight = c.x + hw - 24.f;
    const float cornerY = c.y + hh - 24.f;
    const float contentW = layout.header.width;
    const float contentLeft = layout.header.x;
    const float contentRight = contentLeft + contentW;
    const float contentX = layout.header.centerX();
    const float headerY = layout.header.centerY();
    const float iconsY = layout.icons.centerY();
    const float bottomY = layout.footer.centerY();

    Ref<GJUserScore> scoreRef = score;
    prepareInPlace(layer);
// remove stale clips from older builds; they broke vanilla scroll state.
    removeByID(layer, "rd-comments-clip"_spr);
// refresh can leave old lists behind; hide every account-comments list.
    auto hideCommentList = [](GJCommentListLayer* list) {
        if (!list) return;
        if (auto* scroller = findScroller(list)) {
            scroller->setTouchEnabled(false);
            scroller->setMouseEnabled(false);
        }
        list->setVisible(false);
    };
    hideCommentList(asVanillaCommentList(commentList));
    for (auto* child : CCArrayExt<CCNode*>(layer->getChildren())) {
        auto* candidate = asVanillaCommentList(child);
        if (candidate && candidate != commentList) hideCommentList(candidate);
    }

    for (auto const& id : {
        "rd-header-card"_spr, "rd-icons-card"_spr, "rd-level-card"_spr, "rd-level-container"_spr,
        "rd-comments-card"_spr, "rd-side-card"_spr, "rd-comments-title"_spr,
        "rd-options-rail-bg"_spr, "rd-socials-rail-bg"_spr,
        "rd-close-bg"_spr, "rd-swap-bg"_spr, "rd-bottom-row-bg"_spr,
        "rd-rank-title"_spr, "rd-rank-value"_spr
    }) {
        removeByID(layer, id);
    }

    auto makeCard = [&](std::string const& id, CCPoint position, CCSize size, GLubyte opacity) {
        auto* card = paimon::SpriteHelper::createColorPanel(
            size.width, size.height, {8, 12, 22}, opacity, 7.f
        );
        if (!card) return;
        card->setID(id);
        card->setAnchorPoint({0.5f, 0.5f});
        card->setPosition(position);
        card->setZOrder(Z_CARD);
        layer->addChild(card);
    };

    auto getOrCreateMenu = [&](std::string const& id, CCPoint position, CCSize size) -> CCMenu* {
        auto* menu = typeinfo_cast<CCMenu*>(layer->getChildByID(id));
        if (!menu) {
            removeByID(layer, id);
            menu = CCMenu::create();
            menu->setID(id);
            layer->addChild(menu);
        }
        menu->setVisible(true);
        menu->setContentSize(size);
        menu->ignoreAnchorPointForPosition(false);
        menu->setAnchorPoint({0.5f, 0.5f});
        menu->setPosition(position);
        menu->setZOrder(Z_CONTENT);
        return menu;
    };

    auto makeRail = [&](CCPoint position, CCSize size, std::string const& id) -> CCMenu* {
        if (auto* bg = paimon::SpriteHelper::safeCreateScale9("square02_small.png")) {
            bg->setContentSize({size.width + 6.f, size.height + 6.f});
            bg->setOpacity(backdrop ? 170 : 190);
            bg->setColor({38, 41, 55});
            bg->setAnchorPoint({0.5f, 0.5f});
            bg->setPosition(position);
            bg->setZOrder(Z_CARD);
            bg->setID(id + "-bg");
            layer->addChild(bg);
        }
        auto* menu = getOrCreateMenu(id, position, size);
        auto* colLayout = ColumnLayout::create()->setAxisReverse(true)->setCrossAxisOverflow(false)
            ->setAxisAlignment(AxisAlignment::Center)->setAutoScale(true)->setGap(5.f);
        colLayout->ignoreInvisibleChildren(true);
        menu->setLayout(colLayout);
        return menu;
    };

    auto relocate = [&](CCMenu* destination, std::string const& id) -> bool {
        if (!destination) return false;
// reloads recreate buttons with the same ids; keep the fresh match and remove
// stale relocated copies.
        std::vector<CCNode*> matches;
        if (buttonMenu) collectNodesByID(buttonMenu, id, matches);
        {
            std::vector<CCNode*> layerMatches;
            collectNodesByID(layer, id, layerMatches);
            for (auto* n : layerMatches) {
                if (std::find(matches.begin(), matches.end(), n) == matches.end()) {
                    matches.push_back(n);
                }
            }
        }
        if (matches.empty()) return false;
        CCNode* node = matches.front();
        for (size_t i = 1; i < matches.size(); ++i) matches[i]->removeFromParent();
        if (node->getParent() != destination) {
            node->retain();
            node->removeFromParent();
            destination->addChild(node);
            node->release();
        }
// do not force visibility; owning features may hide buttons until applicable.
        node->setLayoutOptions(AxisLayoutOptions::create()->setScaleLimits(0.45f, 1.f));
        return true;
    };

    auto fitAccountRail = [&](CCMenu* menu, std::string const& id, ProfileRect const& area) {
        if (!menu) return;
        bool const visible = arrangeAccountRail(menu, area);
        menu->setVisible(visible);
        if (auto* bg = layer->getChildByID(id + "-bg")) {
            bg->setVisible(visible);
            auto const size = menu->getContentSize();
            bg->setContentSize({size.width + 6.f, size.height + 6.f});
            bg->setPosition(menu->getPosition());
        }
    };

    auto* closeMenu = makeRail({railXLeft, cornerY}, {34.f, 34.f}, "rd-close"_spr);
    auto* swapMenu = makeRail({railXRight, cornerY}, {34.f, 34.f}, "rd-swap"_spr);
    removeGeneratedChildren(closeMenu);
    removeGeneratedChildren(swapMenu);
    relocate(closeMenu, "close-button");
    relocate(swapMenu, "refresh-button");
    closeMenu->updateLayout();
    swapMenu->updateLayout();

// hide the whole vanilla menu so asynchronously added buttons cannot flash.
    if (buttonMenu) buttonMenu->setVisible(false);
    auto const& accountArea = layout.accountRail;
    const CCSize railSize = {accountArea.width, accountArea.height};
    const CCPoint accountCenter{accountArea.centerX(), accountArea.centerY()};
    auto* optionsRail = makeRail(accountCenter, railSize, "rd-options-rail"_spr);
    removeGeneratedChildren(optionsRail);
    for (auto const* id : {"message-button", "requests-button", "comment-button",
        "block-button", "unblock-button", "report-button", "mod-button"}) relocate(optionsRail, id);

    for (auto const& id : {
        "flozwer.paimbnails2/profile-info-button",
        "flozwer.paimbnails2/profile-settings-button",
        "flozwer.paimbnails2/thumbs-gear-button",
        "flozwer.paimbnails2/add-moderator-button",
        "flozwer.paimbnails2/ban-user-button",
    }) relocate(optionsRail, id);
    auto const& socialArea = layout.socialRail;
    const CCPoint socialCenter{socialArea.centerX(), socialArea.centerY()};
    auto* socialsRail = makeRail(socialCenter, {socialArea.width, socialArea.height}, "rd-socials-rail"_spr);
    removeGeneratedChildren(socialsRail);
    // separate the previous second column into its own panel on the right.
    for (auto const* id : {"friend-button", "notifications-button", "settings-button", "follow-button", "socials-button"}) {
        relocate(socialsRail, id);
    }
    hideInlineSocialButtons(layer);
    // only My Socials is governed by these parental controls; the other
    // account controls now share this panel and retain native visibility.
    if (auto* socials = socialsRail->getChildByID("socials-button")) {
        auto const restricted = gm->getGameVariable("0218") || gm->getGameVariable("0219");
        auto* nativeVisibility = typeinfo_cast<CCBool*>(socials->getUserObject("rd-native-social-visibility"_spr));
        if (restricted) {
            if (!nativeVisibility) socials->setUserObject("rd-native-social-visibility"_spr, CCBool::create(socials->isVisible()));
            socials->setVisible(false);
        } else if (nativeVisibility) {
            socials->setVisible(nativeVisibility->getValue());
            socials->setUserObject("rd-native-social-visibility"_spr, nullptr);
        }
    }
    fitAccountRail(optionsRail, "rd-options-rail"_spr, accountArea);
    fitAccountRail(socialsRail, "rd-socials-rail"_spr, socialArea);

    makeCard("rd-header-card"_spr, {contentX, headerY}, {contentW, layout.header.height}, cardA);
    makeCard("rd-icons-card"_spr, {contentX, iconsY}, {contentW, layout.icons.height}, cardA);
    makeCard("rd-comments-card"_spr, {contentX, layout.activity.centerY()}, {contentW, layout.activity.height}, cardA);
    {
        auto* sidePanel = typeinfo_cast<ProfileActivityPanel*>(layer->getChildByID("rd-activity-panel"_spr));
        auto previousMode = ProfileActivityPanel::Mode::Posts;
        float previousOffsets[4] = {};
        if (sidePanel && (sidePanel->m_accountID != accountId || sidePanel->m_userID != score->m_userID ||
            sidePanel->m_area.width != contentW || sidePanel->m_area.height != layout.activity.height)) {
            if (sidePanel->m_accountID == accountId) {
                previousMode = sidePanel->m_mode;
                std::copy(std::begin(sidePanel->m_offsets), std::end(sidePanel->m_offsets), previousOffsets);
            }
            ++sidePanel->m_gen;
            sidePanel->removeFromParent();
            sidePanel = nullptr;
        }
        if (!sidePanel) {
            sidePanel = ProfileActivityPanel::create(accountId, score->m_userID, {contentW, layout.activity.height});
            if (sidePanel) {
                sidePanel->setID("rd-activity-panel"_spr);
                sidePanel->setZOrder(Z_CONTENT);
                layer->addChild(sidePanel);
                std::copy(std::begin(previousOffsets), std::end(previousOffsets), sidePanel->m_offsets);
                sidePanel->selectMode(previousMode);
            }
        }
        if (sidePanel) sidePanel->setPosition({contentX, layout.activity.centerY()});
    }
    {
        const float innerW = layout.posts.width;
        const float innerH = layout.posts.height;

        auto* scroll = typeinfo_cast<geode::ScrollLayer*>(
            layer->getChildByID("rd-comment-content"_spr));
        if (!scroll) {
            scroll = ProfileScrollLayer::create({innerW, innerH});
            scroll->setID("rd-comment-content"_spr);
            scroll->ignoreAnchorPointForPosition(false);
            scroll->setAnchorPoint({0.5f, 0.5f});
            scroll->setStealingTouches(true);
            scroll->setUserFlag("alk.better-touch-prio/steals-touch", true);
            layer->addChild(scroll);
        }
        scroll->setPosition({layout.posts.centerX(), layout.posts.centerY()});
        scroll->setContentSize({innerW, innerH});
        scroll->setZOrder(Z_CONTENT + 1);
        int count = comments ? comments->count() : 0;
        std::string sig = !commentsLoaded ? "loading" : ("n" + std::to_string(count));
        sig += fmt::format("-{}x{}", innerW, innerH);
        if (commentsLoaded && count > 0) {
            if (auto* c0 = typeinfo_cast<GJComment*>(comments->objectAtIndex(0)))
                sig += "_" + std::to_string(c0->m_commentID);
        }
        auto* prevSig = typeinfo_cast<cocos2d::CCString*>(
            scroll->getUserObject("rd-csig"_spr));
        bool changed = !prevSig || std::string(prevSig->getCString()) != sig;

        if (changed) {
            scroll->setUserObject("rd-csig"_spr, cocos2d::CCString::create(sig));
            scroll->m_contentLayer->removeAllChildren();

            geode::log::info("[paim-redesign] build comments loaded={} count={}",
                commentsLoaded, count);

            auto centeredMsg = [&](char const* msg) {
                scroll->m_contentLayer->setContentSize({innerW, innerH});
                auto* l = CCLabelBMFont::create(msg, "chatFont.fnt");
                if (!l) return;
                l->setScale(0.5f);
                l->setColor({150, 150, 165});
                l->setAnchorPoint({0.5f, 0.5f});
                l->setPosition({innerW * 0.5f, innerH * 0.5f});
                scroll->m_contentLayer->addChild(l);
            };

            if (!commentsLoaded) {
                centeredMsg("Loading...");
            } else if (count == 0) {
                centeredMsg(ownProfile ? "You haven't posted yet" : "No posts yet");
            } else {
                const float naturalW = 350.f;
                const float naturalH = 80.f;
                const float scale = innerW / naturalW;
                const float scaledH = naturalH * scale;
                const float gap = 3.f;
                const float totalH = std::max(innerH, count * (scaledH + gap));
                scroll->m_contentLayer->setContentSize({innerW, totalH});
                float y = totalH;
// the like button only fires if the cell menus are dispatched before both the
// scroller and the popup's own menu.
                int cellPrio = scroll->getTouchPriority() - 1;
                if (auto* vanillaMenu = typeinfo_cast<CCMenu*>(buttonMenu)) {
                    cellPrio = std::min(cellPrio, vanillaMenu->getTouchPriority() - 1);
                }
                for (auto* obj : CCArrayExt<CCObject*>(comments)) {
                    auto* comment = typeinfo_cast<GJComment*>(obj);
                    if (!comment) continue;
                    auto* cell = makeAccountCommentCell(comment, naturalW, naturalH);
                    if (!cell) continue;
                    styleAccountCommentCell(cell, naturalW, naturalH);
                    setMenuTouchPriority(cell, cellPrio);
                    cell->setUserObject("paimon-rd-cell-style"_spr,
                        AccountCellStyle::create(naturalW, naturalH, cellPrio));
                    cell->ignoreAnchorPointForPosition(false);
                    cell->setAnchorPoint({0.f, 0.f});
                    cell->setPosition({0.f, 0.f});
                    cell->setScale(scale);

                    auto* holder = CCNode::create();
                    holder->setContentSize({innerW, scaledH});
                    holder->setAnchorPoint({0.f, 0.f});
                    holder->ignoreAnchorPointForPosition(false);
                    y -= (scaledH + gap);
                    holder->setPosition({0.f, y});
                    holder->addChild(cell);
                    scroll->m_contentLayer->addChild(holder);
                }
            }
            scroll->scrollToTop();
        }
    }
    {
        // name stays centered; the official badge hugs its left edge and the
        // connection dot its right edge, so late arrivals never shift the name.
        constexpr float kRowH = 22.f;
        constexpr float kSideReserve = 76.f;
        constexpr float kNameGap = 6.f;
        constexpr float kSlotW = 22.f;
        float const rowW = std::max(120.f, contentW - kSideReserve * 2.f);
        auto* menu = getOrCreateMenu(
            "rd-username"_spr, {contentX, headerY + 17.f}, {rowW, kRowH}
        );
        removeGeneratedChildren(menu);
        menu->setLayout(nullptr);

        auto const picCfg = ProfilePicCustomizer::get().getConfig();
        bool ownProfile = false;
        if (auto* am = GJAccountManager::sharedState()) {
            ownProfile = am->m_accountID != 0 && am->m_accountID == score->m_accountID;
        }
        char const* nameFont = (ownProfile && !picCfg.profileFont.empty())
            ? picCfg.profileFont.c_str() : "bigFont.fnt";

        auto nameLabel = CCLabelBMFont::create(std::string(score->m_userName).c_str(), nameFont);
        if (!nameLabel) {
            nameLabel = CCLabelBMFont::create(std::string(score->m_userName).c_str(), "bigFont.fnt");
        }
        if (!nameLabel) return;
        nameLabel->limitLabelWidth(rowW - (kSlotW + kNameGap) * 2.f, 0.62f, 0.3f);
        if (ownProfile && picCfg.nameConfig.enabled) {
            paimon::profile_name::decorate(nameLabel, picCfg.nameConfig);
        }
        CCNode* nameNode = nullptr;
        if (auto nameBtn = CCMenuItemExt::createSpriteExtra(nameLabel, [scoreRef, layer](CCMenuItemSpriteExtra*) {
                if (!scoreRef) return;
                std::string const name = scoreRef->m_userName;
                auto* store = paimon::profiles::IdentityStore::get(layer);
                if (store && !store->entries().empty()) {
                    if (auto* popup = ProfileIdentityPopup::create(name, store->entries())) {
                        popup->show();
                    }
                    return;
                }
                geode::utils::clipboard::write(name);
                Notification::create("Username copied", NotificationIcon::Info, 1.0f)->show();
            })) {
            nameBtn->setID("rd-generated-username"_spr);
            menu->addChild(nameBtn);
            nameNode = nameBtn;
        }

        if (score->m_modBadge > 0) {
            char const* badgeFrame = "modBadge_01_001.png";
            if (score->m_modBadge == 2) badgeFrame = "modBadge_02_001.png";
            else if (score->m_modBadge == 3) badgeFrame = "modBadge_03_001.png";
            if (auto badge = paimon::SpriteHelper::safeCreateWithFrameName(badgeFrame)) {
                badge->setID("rd-generated-gd-badge"_spr);
                badge->setScale(0.72f);
                menu->addChild(badge);
            }
        }

        for (auto const& id : {
            "flozwer.paimbnails2/paimon-moderator-badge",
            "flozwer.paimbnails2/paimon-admin-badge",
            "flozwer.paimbnails2/paimon-user-status-dot",
        }) relocate(menu, id);

        float const midX = rowW / 2.f;
        float const midY = kRowH / 2.f;
        float const nameHalf = nameNode ? nameNode->getScaledContentSize().width / 2.f : 0.f;
        if (nameNode) nameNode->setPosition({midX, midY});

        // nearest to the name first, then outward.
        float leftEdge = midX - nameHalf - kNameGap;
        for (auto const& id : std::vector<std::string>{
            "flozwer.paimbnails2/paimon-admin-badge",
            "flozwer.paimbnails2/paimon-moderator-badge",
            "rd-generated-gd-badge"_spr,
        }) {
            auto* node = menu->getChildByID(id);
            if (!node) continue;
            float const w = node->getScaledContentSize().width;
            node->setPosition({leftEdge - w / 2.f, midY});
            leftEdge -= w + kNameGap;
        }

        if (auto* dot = menu->getChildByID("flozwer.paimbnails2/paimon-user-status-dot")) {
            float const w = dot->getScaledContentSize().width;
            dot->setPosition({midX + nameHalf + kNameGap + w / 2.f, midY});
        }

        if (score->m_globalRank > 0) {
            auto* rankTitle = CCLabelBMFont::create("GLOBAL", "chatFont.fnt");
            rankTitle->setID("rd-rank-title"_spr);
            rankTitle->setScale(0.36f);
            rankTitle->setColor({170, 205, 235});
            rankTitle->setPosition({contentRight - 48.f, headerY + 24.f});
            rankTitle->setZOrder(Z_CONTENT);
            layer->addChild(rankTitle);

            auto rankText = fmt::format("#{}", GameToolbox::pointsToString(score->m_globalRank));
            auto* rankValue = CCLabelBMFont::create(rankText.c_str(), "bigFont.fnt");
            rankValue->setID("rd-rank-value"_spr);
            rankValue->limitLabelWidth(54.f, 0.35f, 0.22f);
            rankValue->setColor({255, 226, 92});
            rankValue->setPosition({contentRight - 48.f, headerY + 13.f});
            rankValue->setZOrder(Z_CONTENT);
            layer->addChild(rankValue);
        }
    }
    {
        auto* menu = getOrCreateMenu(
            "rd-stats"_spr, {contentX, headerY - 4.f}, {contentW - 12.f, 16.f}
        );
        auto* secondary = getOrCreateMenu(
            "rd-secondary-stats"_spr, {contentX, headerY - 20.f}, {contentW - 12.f, 16.f});
        menu->setAnchorPoint({0.5f, 0.5f});
        removeGeneratedChildren(menu);
        removeGeneratedChildren(secondary);
        menu->setLayout(RowLayout::create()->setAxisAlignment(AxisAlignment::Center)
            ->setGap(10.f)->setCrossAxisOverflow(false)->setAutoScale(true));
        secondary->setLayout(RowLayout::create()->setAxisAlignment(AxisAlignment::Center)
            ->setGap(10.f)->setCrossAxisOverflow(false)->setAutoScale(true));
        CCMenu* destination = menu;

        int statIndex = 0;
        auto addStat = [&](int value, char const* iconFrame, ccColor3B color,
                           char const* statID) {
            constexpr float kChipH = 16.f;
            auto const chipID = fmt::format("{}rd-generated-stat-{}", ""_spr, statIndex++);
            auto chip = CCNode::create();
            chip->setAnchorPoint({0.5f, 0.5f});

            float cursor = 0.f;
            if (auto icon = paimon::SpriteHelper::safeCreateWithFrameName(iconFrame)) {
                icon->setScale(0.42f);
                icon->setAnchorPoint({0.f, 0.5f});
                icon->setPosition({cursor, kChipH * 0.5f});
                chip->addChild(icon);
                cursor += icon->getContentWidth() * 0.42f + 2.f;
            }
            auto label = CCLabelBMFont::create(GameToolbox::pointsToString(value).c_str(), "bigFont.fnt");
            label->setScale(0.4f);
            label->setColor(color);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({cursor, kChipH * 0.5f});
            chip->addChild(label);
            cursor += label->getContentWidth() * 0.4f;

            chip->setContentSize({std::max(cursor, 1.f), kChipH});

            if (auto* vanilla = findVanillaStatButton(layer, statID)) {
                Ref<CCMenuItem> target = vanilla;
                if (auto* btn = CCMenuItemExt::createSpriteExtra(chip,
                        [target](CCMenuItemSpriteExtra*) {
                            if (target && target->getParent() && target->isEnabled()) {
                                target->activate();
                            }
                        })) {
                    btn->setID(chipID);
                    destination->addChild(btn);
                    return;
                }
            }
            chip->setID(chipID);
            destination->addChild(chip);
        };
// prefer live local stats so async rebuilds keep the own profile current.
        if (ownProfile) {
            paimon::profiles::applyLiveOwnProfileStats(score);
        }
        int const stars = score->m_stars;
        int const moons = score->m_moons;
        int const diamonds = score->m_diamonds;
        int const demons = score->m_demons;
        int const userCoins = score->m_userCoins;
        int const secretCoins = score->m_secretCoins;
        addStat(stars, "GJ_starsIcon_001.png", {233, 253, 113}, "stars");
        addStat(moons, "GJ_moonsIcon_001.png", {109, 215, 249}, "moons");
        addStat(demons, "GJ_demonIcon_001.png", {240, 140, 140}, "demons");
        addStat(score->m_creatorPoints, "GJ_hammerIcon_001.png", {182, 186, 186}, "creator-points");
        destination = secondary;
        addStat(diamonds, "GJ_diamondsIcon_001.png", {90, 245, 255}, "diamonds");
        addStat(userCoins, "GJ_coinsIcon2_001.png", {255, 255, 255}, "user-coins");
        addStat(secretCoins, "GJ_coinsIcon_001.png", {248, 138, 0}, "coins");
        addStat(score->m_followers, "GJ_followIcon_001.png", {153, 226, 170}, "followers");
        menu->updateLayout();
        secondary->updateLayout();
    }
    {
        removeByID(layer, "rd-icons"_spr);
        if (auto* playerMenu = layer->getChildByID("player-menu")) {
            playerMenu->setVisible(true);
            playerMenu->ignoreAnchorPointForPosition(false);
            playerMenu->setAnchorPoint({0.5f, 0.5f});
            auto src = playerMenu->getContentSize();
            float sc = 1.f;
            if (src.width > 1.f) sc = std::min(1.f, (contentW - 18.f) / src.width);
            playerMenu->setScale(sc);
            playerMenu->setPosition({contentX, iconsY});
            playerMenu->setZOrder(Z_CONTENT);
        }
    }

    auto* row = typeinfo_cast<CCMenu*>(layer->getChildByID("rd-bottom-row"_spr));
    if (!row) {
        row = CCMenu::create();
        row->setID("rd-bottom-row"_spr);
        row->ignoreAnchorPointForPosition(false);
        row->setAnchorPoint({0.5f, 0.5f});
        row->setZOrder(Z_CONTENT);
        layer->addChild(row);
    }
    row->setVisible(true);
    // native comment/review actions live beside the footer, freeing the tab row.
    float const footerWidth = contentW - 112.f;
    row->setContentSize({footerWidth, 28.f});
    row->setPosition({contentX, bottomY});
    {
        auto* rowLayout = RowLayout::create()->setGap(7.f)->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisOverflow(false)->setAutoScale(true);
        rowLayout->ignoreInvisibleChildren(true);
        row->setLayout(rowLayout);
    }
    {
        if (auto* bg = paimon::SpriteHelper::safeCreateScale9("square02_small.png")) {
            bg->setContentSize({footerWidth, 30.f});
            bg->setOpacity(backdrop ? 170 : 190);
            bg->setColor({38, 41, 55});
            bg->setAnchorPoint({0.5f, 0.5f});
            bg->setPosition({contentX, bottomY});
            bg->setZOrder(Z_CARD);
            bg->setID("rd-bottom-row-bg"_spr);
            layer->addChild(bg);
        }

        for (auto const* id : {
            "comment-history-button", "my-levels-button", "my-lists-button"
        }) relocate(row, id);
        for (auto const* id : {
            "paimon-thumb-count-btn", "copy-icons-button"
        }) relocate(row, std::string("flozwer.paimbnails2/") + id);
        if (ownProfile) {
            int const userID = score->m_userID;
            auto addBrowseBtn = [&](std::string const& id, char const* baseFrame,
                                    char const* iconFrame, char const* caption,
                                    std::function<void()> cb) {
                if (row->getChildByID(id)) return;
                if (id == "rd-generated-my-levels"_spr && row->getChildByID("my-levels-button")) return;
                if (id == "rd-generated-my-lists"_spr && row->getChildByID("my-lists-button")) return;
                CCNode* content = nullptr;
                if (baseFrame) {
                    if (auto base = paimon::SpriteHelper::safeCreateWithFrameName(baseFrame)) {
                        if (auto icon = paimon::SpriteHelper::safeCreateWithFrameName(iconFrame)) {
                            float bd = std::max(base->getContentWidth(), base->getContentHeight());
                            float idim = std::max(icon->getContentWidth(), icon->getContentHeight());
                            if (idim > 1.f) icon->setScale((bd * 0.6f) / idim);
                            icon->setPosition({base->getContentWidth() * 0.5f, base->getContentHeight() * 0.5f});
                            base->addChild(icon);
                        }
                        content = base;
                    }
                }
                if (!content) content = paimon::SpriteHelper::safeCreateWithFrameName(iconFrame);
                if (!content) content = ButtonSprite::create(caption, "bigFont.fnt", "GJ_button_05.png", 0.7f);
                if (!content) return;
                float maxDim = std::max(content->getContentWidth(), content->getContentHeight());
                if (maxDim > 1.f) content->setScale(31.f / maxDim);
                if (auto btn = CCMenuItemExt::createSpriteExtra(content,
                        [cb](CCMenuItemSpriteExtra*) { if (cb) cb(); })) {
                    btn->setID(id);
                    btn->setLayoutOptions(AxisLayoutOptions::create()->setScaleLimits(0.45f, 1.f));
                    row->addChild(btn);
                }
            };
            addBrowseBtn("rd-generated-my-levels"_spr, nullptr, "accountBtn_myLevels_001.png", "Levels", [userID] {
                auto obj = GJSearchObject::create(SearchType::UsersLevels, std::to_string(userID));
                CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, LevelBrowserLayer::scene(obj)));
            });
            addBrowseBtn("rd-generated-my-lists"_spr, "GJ_plainBtn_001.png", "GJ_listAddBtn_001.png", "Lists", [] {
                auto obj = GJSearchObject::create(SearchType::MyLists, "");
                CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, LevelBrowserLayer::scene(obj)));
            });
        }
        relocate(row, "flozwer.paimbnails2/profile-music-pause-button");

        row->updateLayout();
// size the bottom panel to the buttons it holds.
        if (auto* bg = layer->getChildByID("rd-bottom-row-bg"_spr)) {
            int n = 0;
            constexpr float gap = 7.f;
            float content = 0.f;
            for (auto* ch : CCArrayExt<CCNode*>(row->getChildren())) {
                if (!ch->isVisible()) continue;
                content += ch->getScaledContentSize().width;
                ++n;
            }
            if (n <= 0) {
                bg->setVisible(false);
                row->setVisible(false);
            } else {
                bg->setVisible(true);
                content += gap * static_cast<float>(n - 1);
                const float w = std::clamp(content + 18.f, 60.f, footerWidth);
                bg->setContentSize({w, 30.f});
                bg->setPosition({contentX, bottomY});
            }
        }
    }
    swapMenu->updateLayout();

    {
        auto* menu = getOrCreateMenu(
            "rd-corner"_spr, {contentRight - 14.f, headerY + 18.f}, {20.f, 20.f}
        );
        removeGeneratedChildren(menu);
        menu->setLayout(RowLayout::create());
        if (!relocate(menu, "info-button")) {
            if (auto infoSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png")) {
                infoSpr->setScale(0.58f);
                if (auto infoBtn = CCMenuItemExt::createSpriteExtra(infoSpr, [scoreRef](CCMenuItemSpriteExtra*) {
                        if (!scoreRef) return;
                        auto msg = fmt::format(
                            "AccountID: {}\nUserID: {}\nStars: {}\nMoons: {}\nDemons: {}\nUser Coins: {}",
                            scoreRef->m_accountID, scoreRef->m_userID,
                            GameToolbox::pointsToString(scoreRef->m_stars),
                            GameToolbox::pointsToString(scoreRef->m_moons),
                            GameToolbox::pointsToString(scoreRef->m_demons),
                            GameToolbox::pointsToString(scoreRef->m_userCoins));
                        PopupManager::get().alert(std::string(scoreRef->m_userName), msg).showInstant();
                    })) {
                    infoBtn->setID("rd-generated-info"_spr);
                    menu->addChild(infoBtn);
                }
            }
        }
        menu->updateLayout();
    }
    {
        auto const position = CCPoint{contentRight - 28.f, bottomY};
        auto* navigation = getOrCreateMenu("rd-comment-navigation"_spr, position, {48.f, 20.f});
        navigation->setLayout(RowLayout::create()->setGap(6.f)->setCrossAxisOverflow(false));
        relocate(navigation, "prev-page-button");
        relocate(navigation, "next-page-button");
        navigation->updateLayout();
        auto* reviews = getOrCreateMenu("rd-review-actions"_spr, position, {48.f, 20.f});
        reviews->setLayout(RowLayout::create()->setGap(6.f)->setCrossAxisOverflow(false));
        relocate(reviews, "profile-reviews-btn"_spr);
        relocate(reviews, "rate-profile-btn"_spr);
        reviews->updateLayout();
        if (auto* activity = typeinfo_cast<ProfileActivityPanel*>(layer->getChildByID("rd-activity-panel"_spr))) {
            activity->updateVisibility();
        }
    }
}

}
