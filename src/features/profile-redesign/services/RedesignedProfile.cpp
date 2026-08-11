#include "RedesignedProfile.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/general.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/cocos.hpp>

#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GameToolbox.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/LevelCell.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/binding/LevelManagerDelegate.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJCommentListLayer.hpp>
#include <Geode/binding/CommentCell.hpp>
#include <Geode/modify/CommentCell.hpp>
#include <Geode/binding/GJComment.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/GJAccountSettingsLayer.hpp>
#include <Geode/binding/FriendsProfilePage.hpp>
#include <Geode/binding/FRequestProfilePage.hpp>
#include <Geode/binding/MessagesProfilePage.hpp>
#include <Geode/binding/GJWriteMessagePopup.hpp>
#include <Geode/binding/ShareCommentLayer.hpp>
#include <Geode/binding/InfoLayer.hpp>
#include <Geode/binding/LevelBrowserLayer.hpp>

#include "../../../framework/compat/SceneLocators.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/ScissorClipNode.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/FluidReveal.hpp"
#include "../../forum/services/ForumApi.hpp"
#include "../../emotes/EmoteRenderer.hpp"
#include "../../emotes/services/EmoteService.hpp"
#include "../../profiles/services/OwnProfileStats.hpp"
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/binding/CCScrollLayerExt.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCScale9Sprite.h>

#include <algorithm>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <functional>

using namespace geode::prelude;

namespace paimon::profile_redesign {
class ProfileRecentLevelLoader : public cocos2d::CCObject, public LevelManagerDelegate {
public:
    Ref<CCNode> m_container;
    CCSize      m_area = {0.f, 0.f};
    int         m_userID = 0;
    std::string m_key;
    bool        m_done = false;
    LoadingSpinner* m_spinner = nullptr;

    static ProfileRecentLevelLoader* create(CCNode* container, CCSize area, int userID) {
        auto r = new ProfileRecentLevelLoader();
        r->m_container = container;
        r->m_area = area;
        r->m_userID = userID;
        r->autorelease();
        return r;
    }

    ~ProfileRecentLevelLoader() override {
        if (auto* glm = GameLevelManager::get()) {
            if (glm->m_levelManagerDelegate == this) glm->m_levelManagerDelegate = nullptr;
        }
    }

    void clearArea() {
        if (m_spinner) { m_spinner->removeFromParent(); m_spinner = nullptr; }
        if (m_container) m_container->removeAllChildrenWithCleanup(true);
    }

    void start() { load(); }

    void load() {
        if (!m_container) return;
        auto glm = GameLevelManager::get();
        if (!glm) return;

        auto obj = GJSearchObject::create(SearchType::UsersLevels, std::to_string(m_userID));
        auto keyC = obj->getKey();
        std::string key = keyC ? std::string(keyC) : "";

        CCArray* levels = (!key.empty()) ? glm->getStoredOnlineLevels(key.c_str()) : nullptr;
        if (!levels || levels->count() == 0) {
            if (!key.empty() && m_key == key && m_done) { showNone(); return; }
            m_key = key;
            glm->m_levelManagerDelegate = this;
            if (!key.empty()) glm->getOnlineLevels(obj);
            clearArea();
            m_spinner = LoadingSpinner::create(24.f);
            if (m_spinner) {
                m_spinner->setPosition({m_area.width / 2.f, m_area.height / 2.f});
                m_container->addChild(m_spinner);
            }
            return;
        }
        GJGameLevel* firstLevel = nullptr;
        GJGameLevel* ratedLevel = nullptr;
        for (int i = 0; i < levels->count(); ++i) {
            auto* l = typeinfo_cast<GJGameLevel*>(levels->objectAtIndex(i));
            if (!l) continue;
            if (!firstLevel) firstLevel = l;
            if (l->m_stars > 0) { ratedLevel = l; break; }
        }
        GJGameLevel* lvl = ratedLevel ? ratedLevel : firstLevel;
        if (!lvl) { showNone(); return; }
        showCell(lvl);
    }

    void showCell(GJGameLevel* level) {
        clearArea();
        auto cell = LevelCell::create(m_area.width, m_area.height);
        if (!cell) { showNone(); return; }
        cell->setContentSize(m_area);
        cell->loadFromLevel(level);
        cell->setPosition({0.f, 0.f});

        if (cell->m_backgroundLayer) cell->m_backgroundLayer->setVisible(false);
        for (auto* ch : CCArrayExt<CCNode*>(cell->getChildren())) {
            if (typeinfo_cast<cocos2d::CCLayerColor*>(ch)) ch->setVisible(false);
        }
        if (cell->m_mainLayer) {
            for (auto* ch : CCArrayExt<CCNode*>(cell->m_mainLayer->getChildren())) {
                if (typeinfo_cast<cocos2d::CCLayerColor*>(ch)) ch->setVisible(false);
            }
        }
        if (cell->m_mainMenu) {
            for (auto* ch : CCArrayExt<CCNode*>(cell->m_mainMenu->getChildren())) {
                auto* item = typeinfo_cast<cocos2d::CCMenuItem*>(ch);
                if (item && item != cell->m_button) {
                    item->setVisible(false);
                    item->setEnabled(false);
                }
            }
        }
        std::function<void(CCNode*)> hideGoldLabels = [&](CCNode* node) {
            if (!node) return;
            for (auto* ch : CCArrayExt<CCNode*>(node->getChildren())) {
                if (auto* lbl = typeinfo_cast<CCLabelBMFont*>(ch)) {
                    const char* fnt = lbl->getFntFile();
                    if (fnt && std::string_view(fnt).find("goldFont") != std::string_view::npos) {
                        lbl->setVisible(false);
                        continue;
                    }
                }
                hideGoldLabels(ch);
            }
        };
        hideGoldLabels(cell);
        const float radius = std::clamp(std::min(m_area.width, m_area.height) * 0.22f, 6.f, 12.f);
        if (auto* stencil = paimon::SpriteHelper::createRoundedRectStencil(m_area.width, m_area.height, radius)) {
            if (auto* clip = cocos2d::CCClippingNode::create(stencil)) {
                clip->setContentSize(m_area);
                clip->setAnchorPoint({0.f, 0.f});
                clip->setPosition({0.f, 0.f});
                clip->setID("rd-level-cell-round-clip"_spr);
                clip->addChild(cell);
                m_container->addChild(clip);
                return;
            }
        }
        m_container->addChild(cell);
    }

    void showNone() {
        clearArea();
        auto l = CCLabelBMFont::create("No levels", "goldFont.fnt");
        l->setScale(0.45f);
        l->setPosition({m_area.width / 2.f, m_area.height / 2.f});
        m_container->addChild(l);
    }

    void loadLevelsFinished(cocos2d::CCArray*, char const* key) override {
        if (!key || m_key.empty() || m_key != key) return;
        m_done = true;
        load();
    }
    void loadLevelsFailed(char const* key) override {
        if (!key || m_key.empty() || m_key != key) return;
        m_done = true;
        showNone();
    }
};
class DualScrollLayer : public geode::ScrollLayer {
public:
    CCScrollLayerExt* m_commentScroller = nullptr;
    cocos2d::CCNode* m_commentClip = nullptr;

    DualScrollLayer(cocos2d::CCRect const& rect, bool sw, bool v)
        : geode::ScrollLayer(rect, sw, v) {}

    static DualScrollLayer* create(cocos2d::CCSize const& size) {
        auto r = new DualScrollLayer({0.f, 0.f, size.width, size.height}, true, true);
        r->autorelease();
        return r;
    }

    static cocos2d::CCRect worldRect(cocos2d::CCNode* n) {
        auto bl = n->convertToWorldSpace({0.f, 0.f});
        auto tr = n->convertToWorldSpace(n->getContentSize());
        return cocos2d::CCRect(std::min(bl.x, tr.x), std::min(bl.y, tr.y),
                               std::fabs(tr.x - bl.x), std::fabs(tr.y - bl.y));
    }

    void scrollWheel(float y, float x) override {
        auto mp = geode::cocos::getMousePos();
        if (worldRect(this).containsPoint(mp)) {
            geode::ScrollLayer::scrollWheel(y, x);
        } else if (m_commentClip && m_commentScroller &&
                   worldRect(m_commentClip).containsPoint(mp)) {
            m_commentScroller->scrollLayer(y);
        }
    }
};class ProfileSidePanel : public cocos2d::CCNode {
public:
    enum class Mode { Reviews, Views };

    int m_accountID = 0;
    Mode m_mode = Mode::Reviews;
    cocos2d::CCSize m_area = {0.f, 0.f};
    DualScrollLayer* m_scroll = nullptr;
    ButtonSprite* m_toggleSpr = nullptr;
    uint64_t m_gen = 0;
    void setCommentRouting(cocos2d::CCNode* clip, CCScrollLayerExt* scroller) {
        if (!m_scroll) return;
        m_scroll->m_commentClip = clip;
        m_scroll->m_commentScroller = scroller;
    }

    static ProfileSidePanel* create(int accountID, cocos2d::CCSize area) {
        auto r = new ProfileSidePanel();
        if (r && r->init(accountID, area)) { r->autorelease(); return r; }
        CC_SAFE_DELETE(r);
        return nullptr;
    }

    bool init(int accountID, cocos2d::CCSize area) {
        if (!CCNode::init()) return false;
        m_accountID = accountID;
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

        m_toggleSpr = ButtonSprite::create("Reviews", "bigFont.fnt", "GJ_button_05.png", 0.7f);
        if (m_toggleSpr) {
            m_toggleSpr->setScale(0.46f);
            if (auto btn = CCMenuItemExt::createSpriteExtra(m_toggleSpr,
                    [this](CCMenuItemSpriteExtra*) { this->toggle(); })) {
                btn->setPosition({area.width * 0.5f, area.height - 9.f});
                menu->addChild(btn);
            }
        }

        float scrollH = std::max(10.f, area.height - 20.f);
        m_scroll = DualScrollLayer::create({area.width, scrollH});
        m_scroll->setPosition({0.f, 0.f});
        this->addChild(m_scroll, 1);

        load();
        return true;
    }

    void toggle() {
        m_mode = (m_mode == Mode::Reviews) ? Mode::Views : Mode::Reviews;
        if (m_toggleSpr) m_toggleSpr->setString(m_mode == Mode::Reviews ? "Reviews" : "Viewers");
        load();
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
        lbl->setPosition({m_area.width * 0.5f, h * 0.5f});
        m_scroll->m_contentLayer->setContentSize({m_area.width, h});
        m_scroll->m_contentLayer->addChild(lbl);
        m_scroll->scrollToTop();
    }

    void stackCells(std::vector<cocos2d::CCNode*>& cells) {
        if (!m_scroll) return;
        float w = m_area.width;
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
        m_scroll->scrollToTop();
    }

    void load() {
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
        WeakRef<ProfileSidePanel> self = this;
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
        WeakRef<ProfileSidePanel> self = this;
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
        float w = m_area.width;
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
        float w = m_area.width;
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

static void collectNodesByID(CCNode* root, std::string const& id, std::vector<CCNode*>& out) {
    if (!root) return;
    if (root->getID() == id) out.push_back(root);
    if (auto* kids = root->getChildren()) {
        for (auto* k : CCArrayExt<CCNode*>(kids)) collectNodesByID(k, id, out);
    }
}

// IDs adopted by buildInPlace(); needsSettlePass() uses the same set.
static std::vector<std::string> relocatableIDs(bool ownProfile) {
    std::vector<std::string> ids = {
        "close-button", "refresh-button",
        "message-button", "friend-button",
        "comment-history-button", "my-levels-button", "my-lists-button", "follow-button",
        "profile-settings-button"_spr, "thumbs-gear-button"_spr,
        "add-moderator-button"_spr, "ban-user-button"_spr,
        "paimon-moderator-badge"_spr, "paimon-admin-badge"_spr,
        "paimon-custom-badge"_spr, "paimon-user-status-dot"_spr,
        "profile-reviews-btn"_spr, "rate-profile-btn"_spr,
        "paimon-thumb-count-btn"_spr, "profile-music-pause-button"_spr,
        "copy-icons-button"_spr,
    };
    if (ownProfile) {
        ids.insert(ids.end(), {"settings-button", "requests-button", "comment-button"});
    } else {
        ids.push_back("block-button");
    }
    return ids;
}

// The icon row has its own GJCommentListLayer; hide only the account-comments list.
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
// Vanilla refresh rebuilds comments and invalidates the previous layout.
    if (hasVisibleVanillaCommentList(layer)) return true;
    auto const ids = relocatableIDs(ownProfile);
    std::string const rdPrefix = "rd-"_spr;
    bool needs = false;
    std::unordered_set<std::string> seen;
    auto walk = [&](auto const& self, CCNode* node, bool insideRd) -> void {
        if (!node || needs) return;
        auto const id = std::string(node->getID());
        bool const rd = insideRd || id.rfind(rdPrefix, 0) == 0;
        if (!id.empty() && std::find(ids.begin(), ids.end(), id) != ids.end()) {
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
#ifndef GEODE_IS_IOS
    if (!cell->init()) {
        delete cell;
        return nullptr;
    }
#endif
    cell->autorelease();
    cell->m_accountComment = true;
    cell->setUserFlag("paimbnails/profile-like-probe"_spr, true);
    cell->loadFromComment(comment);
    return cell;
}

class $modify(PaimonProfileLikeProbe, CommentCell) {
    void onLike(CCObject* sender) {
        if (this->getUserFlag("paimbnails/profile-like-probe"_spr)) {
            geode::log::info("[paim-redesign] profile comment like button reached CommentCell::onLike");
            return;
        }
        CommentCell::onLike(sender);
    }
};

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

static void logCommentMenus(CCNode* root) {
    if (!root) return;
    if (auto* menu = typeinfo_cast<CCMenu*>(root)) {
        geode::log::info(
            "[paim-redesign] comment menu id='{}' pos=({}, {}) size=({}, {}) visible={} touch={} children={}",
            menu->getID(), menu->getPositionX(), menu->getPositionY(),
            menu->getContentWidth(), menu->getContentHeight(), menu->isVisible(),
            menu->isTouchEnabled(), menu->getChildrenCount());
        if (auto* children = menu->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                auto* item = typeinfo_cast<CCMenuItem*>(child);
                if (!item) continue;
                auto center = item->convertToWorldSpace(item->getContentSize() / 2.f);
                geode::log::info(
                    "[paim-redesign] comment item id='{}' pos=({}, {}) size=({}, {}) world-center=({}, {}) visible={} enabled={}",
                    item->getID(), item->getPositionX(), item->getPositionY(),
                    item->getContentWidth(), item->getContentHeight(), center.x, center.y,
                    item->isVisible(), item->isEnabled());
            }
        }
    }
    if (auto* children = root->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) logCommentMenus(child);
    }
}

static void styleAccountCommentCell(CommentCell* cell, float w, float h) {
    if (!cell) return;
    stripCommentDecorations(cell);
    if (cell->m_iconSprite) {
        for (cocos2d::CCNode* n = cell->m_iconSprite; n && n != cell; n = n->getParent()) {
            if (auto* item = typeinfo_cast<cocos2d::CCMenuItem*>(n)) {
                item->setEnabled(false);
                break;
            }
        }
    }
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

static void accumulateLabelAABB(CCNode* node, cocos2d::CCRect& out, bool& has) {
    if (!node || !node->isVisible()) return;
    if (typeinfo_cast<cocos2d::CCLabelBMFont*>(node)) {
        auto const size = node->getContentSize();
        if (size.width > 0.5f && size.height > 0.5f) {
            auto t = node->nodeToWorldTransform();
            float xs[4] = {
                t.tx,
                t.a * size.width + t.tx,
                t.c * size.height + t.tx,
                t.a * size.width + t.c * size.height + t.tx,
            };
            float ys[4] = {
                t.ty,
                t.b * size.width + t.ty,
                t.d * size.height + t.ty,
                t.b * size.width + t.d * size.height + t.ty,
            };
            float minX = std::min({xs[0], xs[1], xs[2], xs[3]});
            float maxX = std::max({xs[0], xs[1], xs[2], xs[3]});
            float minY = std::min({ys[0], ys[1], ys[2], ys[3]});
            float maxY = std::max({ys[0], ys[1], ys[2], ys[3]});
            if (!has) {
                out = cocos2d::CCRect(minX, minY, maxX - minX, maxY - minY);
                has = true;
            } else {
                float nx = std::min(out.getMinX(), minX);
                float ny = std::min(out.getMinY(), minY);
                float xx = std::max(out.getMaxX(), maxX);
                float yy = std::max(out.getMaxY(), maxY);
                out = cocos2d::CCRect(nx, ny, xx - nx, yy - ny);
            }
        }
    }
    if (auto* kids = node->getChildren()) {
        for (auto* k : CCArrayExt<CCNode*>(kids)) accumulateLabelAABB(k, out, has);
    }
}

static void hideVanillaChrome(CCNode* root) {
    if (!root) return;

    static constexpr std::string_view exactIDs[] = {
        "username-menu", "stats-menu", "socials-menu",
        "left-menu", "right-menu", "bottom-menu", "icon-background",
        "floor-line", "username-label", "global-rank-label",
        "global-rank-icon", "my-stuff-label", "my-stuff-arrow",
        "my-levels-hint", "my-lists-hint",
    };
    static constexpr std::string_view idFragments[] = {
        "global-rank", "my-stuff",
    };

    auto hide = [&](auto&& self, CCNode* node) -> void {
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
    const GLubyte cardA = backdrop ? 105 : 135;

    constexpr int Z_CARD = 55;
    constexpr int Z_CONTENT = 58;

    const float railXLeft = c.x - hw + 24.f;
    const float railXRight = c.x + hw - 24.f;
    const float cornerY = c.y + hh - 24.f;
    const float contentW = std::max(260.f, sz.width - 104.f);
    const float contentLeft = c.x - contentW * 0.5f;
    const float contentRight = c.x + contentW * 0.5f;
    const float headerY = c.y + hh - 33.f;
    const float iconsY = c.y + hh - 80.f;
    const float levelY = c.y + hh - 129.f;
    const float commentsY = c.y - hh + 84.f;
    const float bottomY = c.y - hh + 24.f;

    Ref<GJUserScore> scoreRef = score;
    prepareInPlace(layer);
    constexpr float commentsH = 64.f;
    const float commentsGap = 10.f;
    const float commentsLeftW = (contentW - commentsGap) * 0.58f;
    const float commentsRightW = (contentW - commentsGap) * 0.42f;
    const float commentsLeftX = contentLeft + commentsLeftW * 0.5f;
    const float commentsRightX = contentRight - commentsRightW * 0.5f;
// Remove stale clips from older builds; they broke vanilla scroll state.
    removeByID(layer, "rd-comments-clip"_spr);
// Refresh can leave old lists behind; hide every account-comments list.
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
        "rd-header-card"_spr, "rd-icons-card"_spr, "rd-level-card"_spr,
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

    auto iconBtn = [&](CCMenu* rail, std::string const& id, char const* frame,
                       float scale, std::function<void()> cb) {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName(frame);
        if (!spr) return;
        spr->setScale(scale);
        if (auto btn = CCMenuItemExt::createSpriteExtra(spr, [cb](CCMenuItemSpriteExtra*) { if (cb) cb(); })) {
            btn->setID(id);
            rail->addChild(btn);
        }
    };

    auto makeRail = [&](CCPoint position, CCSize size, std::string const& id) -> CCMenu* {
        if (auto* bg = paimon::SpriteHelper::safeCreateScale9("square02_small.png")) {
            bg->setContentSize({size.width + 6.f, size.height + 6.f});
            bg->setOpacity(backdrop ? 115 : 145);
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
// Reloads recreate buttons with the same IDs; keep the fresh match and remove
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
// Do not force visibility; owning features may hide buttons until applicable.
        node->setLayoutOptions(AxisLayoutOptions::create()->setScaleLimits(0.45f, 1.f));
        return true;
    };

// Size each rail to its buttons instead of the full popup height.
    auto fitRail = [&](CCMenu* menu, std::string const& id, CCPoint center, float maxH) {
        if (!menu) return;
        auto* bg = layer->getChildByID(id + "-bg");
// Hidden buttons do not contribute to rail layout.
        int n = 0;
        constexpr float gap = 5.f;
    constexpr float innerPad = 8.f;
        float content = 0.f;
        for (auto* ch : CCArrayExt<CCNode*>(menu->getChildren())) {
            if (!ch->isVisible()) continue;
            content += ch->getScaledContentSize().height;
            ++n;
        }
        if (n <= 0) {
            menu->setVisible(false);
            if (bg) bg->setVisible(false);
            return;
        }
        menu->setVisible(true);
        if (bg) bg->setVisible(true);
        content += gap * static_cast<float>(n - 1);

        float h = std::clamp(content + innerPad, 40.f, maxH);
        const float w = menu->getContentSize().width;
// Keep the rail top-aligned so buttons grow downward.
        const float topY = center.y + maxH * 0.5f;
        const CCPoint anchored = {center.x, topY - h * 0.5f};
        menu->setContentSize({w, h});
        menu->setPosition(anchored);
        menu->updateLayout();
        if (bg) {
            bg->setContentSize({w + 6.f, h + 6.f});
            bg->setPosition(anchored);
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

// Hide the whole vanilla menu so asynchronously added buttons cannot flash.
    if (buttonMenu) buttonMenu->setVisible(false);
    const CCSize railSize = {34.f, sz.height - 88.f};
    const float railY = c.y - 12.f;
    auto* optionsRail = makeRail({railXLeft, railY}, railSize, "rd-options-rail"_spr);
    removeGeneratedChildren(optionsRail);
    if (ownProfile) {
        relocate(optionsRail, "settings-button");
        relocate(optionsRail, "friend-button");
        relocate(optionsRail, "requests-button");
        relocate(optionsRail, "message-button");
        relocate(optionsRail, "comment-button");
    } else {
        relocate(optionsRail, "message-button");
        relocate(optionsRail, "friend-button");
        relocate(optionsRail, "block-button");
    }

    for (auto const& id : {
        "flozwer.paimbnails2/profile-settings-button",
        "flozwer.paimbnails2/thumbs-gear-button",
        "flozwer.paimbnails2/add-moderator-button",
        "flozwer.paimbnails2/ban-user-button",
    }) relocate(optionsRail, id);
    optionsRail->updateLayout();
    fitRail(optionsRail, "rd-options-rail"_spr, {railXLeft, railY}, railSize.height);
    auto* socialsRail = makeRail({railXRight, railY}, railSize, "rd-socials-rail"_spr);
    removeGeneratedChildren(socialsRail);
    relocate(socialsRail, "flozwer.paimbnails2/copy-icons-button");
    auto addSocial = [&](gd::string const& url, char const* frame, std::string const& base) {
        std::string u = std::string(url);
        if (u.empty()) return;
        auto const id = fmt::format("{}rd-generated-social-{}", ""_spr, socialsRail->getChildrenCount());
        iconBtn(socialsRail, id, frame, 0.78f,
            [base, u] { web::openLinkInBrowser(base + u); });
    };
    addSocial(score->m_youtubeURL, "gj_ytIcon_001.png", "https://youtube.com/channel/");
    addSocial(score->m_twitterURL, "gj_twIcon_001.png", "https://twitter.com/");
    addSocial(score->m_twitchURL, "gj_twitchIcon_001.png", "https://twitch.tv/");
    addSocial(score->m_instagramURL, "gj_instaIcon_001.png", "https://instagram.com/");
    addSocial(score->m_tiktokURL, "gj_tiktokIcon_001.png", "https://tiktok.com/@");
    socialsRail->updateLayout();
    fitRail(socialsRail, "rd-socials-rail"_spr, {railXRight, railY}, railSize.height);

    makeCard("rd-header-card"_spr, {c.x, headerY}, {contentW, 48.f}, cardA);
    makeCard("rd-icons-card"_spr, {c.x, iconsY}, {contentW, 40.f}, cardA);
    makeCard("rd-comments-card"_spr, {commentsLeftX, commentsY}, {commentsLeftW, commentsH + 6.f}, cardA);
    makeCard("rd-side-card"_spr, {commentsRightX, commentsY}, {commentsRightW, commentsH + 6.f}, cardA);
    {
        auto* sidePanel = typeinfo_cast<ProfileSidePanel*>(layer->getChildByID("rd-side-panel"_spr));
        if (!sidePanel) {
            sidePanel = ProfileSidePanel::create(accountId, {commentsRightW - 8.f, commentsH});
            if (sidePanel) {
                sidePanel->setID("rd-side-panel"_spr);
                sidePanel->setZOrder(Z_CONTENT);
                layer->addChild(sidePanel);
            }
        }
        if (sidePanel) sidePanel->setPosition({commentsRightX, commentsY});
        if (sidePanel) {
            sidePanel->setCommentRouting(nullptr, nullptr);
        }
    }
    {
        const float innerW = std::max(20.f, commentsLeftW - 6.f);
        const float innerH = std::max(20.f, commentsH + 2.f);

        auto* scroll = typeinfo_cast<geode::ScrollLayer*>(
            layer->getChildByID("rd-comment-content"_spr));
        if (!scroll) {
            scroll = geode::ScrollLayer::create({innerW, innerH});
            scroll->setID("rd-comment-content"_spr);
            scroll->ignoreAnchorPointForPosition(false);
            scroll->setAnchorPoint({0.5f, 0.5f});
            scroll->setStealingTouches(true);
            scroll->setUserFlag("alk.better-touch-prio/steals-touch", true);
            scroll->setMouseEnabled(false);
            layer->addChild(scroll);
        }
        scroll->setPosition({commentsLeftX, commentsY});
        scroll->setZOrder(Z_CONTENT + 1);
        int count = comments ? comments->count() : 0;
        std::string sig = !commentsLoaded ? "loading" : ("n" + std::to_string(count));
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
                for (auto* obj : CCArrayExt<CCObject*>(comments)) {
                    auto* comment = typeinfo_cast<GJComment*>(obj);
                    if (!comment) continue;
                    auto* cell = makeAccountCommentCell(comment, naturalW, naturalH);
                    if (!cell) continue;
                    styleAccountCommentCell(cell, naturalW, naturalH);
                    setMenuTouchPriority(cell, scroll->getTouchPriority() - 1);
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
                    logCommentMenus(cell);
                }
            }
            scroll->scrollToTop();
        }
        if (auto* sp = typeinfo_cast<ProfileSidePanel*>(
                layer->getChildByID("rd-side-panel"_spr))) {
            sp->setCommentRouting(scroll, scroll);
        }
    }
    {
        auto* menu = getOrCreateMenu(
            "rd-username"_spr, {contentLeft + 8.f, headerY + 10.f}, {contentW - 98.f, 24.f}
        );
        menu->setAnchorPoint({0.f, 0.5f});
        removeGeneratedChildren(menu);
        menu->setLayout(RowLayout::create()->setAxisAlignment(AxisAlignment::Start)
            ->setGap(5.f)->setCrossAxisOverflow(false)->setAutoScale(true));

        auto nameLabel = CCLabelBMFont::create(std::string(score->m_userName).c_str(), "bigFont.fnt");
        nameLabel->limitLabelWidth(contentW - 112.f, 0.72f, 0.35f);
        if (auto nameBtn = CCMenuItemExt::createSpriteExtra(nameLabel, [scoreRef](CCMenuItemSpriteExtra*) {
                if (scoreRef) {
                    geode::utils::clipboard::write(std::string(scoreRef->m_userName));
                    Notification::create("Username copied", NotificationIcon::Info, 1.0f)->show();
                }
            })) {
            nameBtn->setID("rd-generated-username"_spr);
            menu->addChild(nameBtn);
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
            "flozwer.paimbnails2/paimon-custom-badge",
            "flozwer.paimbnails2/paimon-user-status-dot",
        }) relocate(menu, id);
        menu->updateLayout();

        if (score->m_globalRank > 0) {
            auto* rankTitle = CCLabelBMFont::create("GLOBAL", "chatFont.fnt");
            rankTitle->setID("rd-rank-title"_spr);
            rankTitle->setScale(0.36f);
            rankTitle->setColor({170, 205, 235});
            rankTitle->setPosition({contentRight - 48.f, headerY + 14.f});
            rankTitle->setZOrder(Z_CONTENT);
            layer->addChild(rankTitle);

            auto rankText = fmt::format("#{}", GameToolbox::pointsToString(score->m_globalRank));
            auto* rankValue = CCLabelBMFont::create(rankText.c_str(), "bigFont.fnt");
            rankValue->setID("rd-rank-value"_spr);
            rankValue->limitLabelWidth(54.f, 0.35f, 0.22f);
            rankValue->setColor({255, 226, 92});
            rankValue->setPosition({contentRight - 48.f, headerY + 2.f});
            rankValue->setZOrder(Z_CONTENT);
            layer->addChild(rankValue);
        }
    }
    {
        auto* menu = getOrCreateMenu(
            "rd-stats"_spr, {c.x, headerY - 13.f}, {contentW - 8.f, 18.f}
        );
        menu->setAnchorPoint({0.5f, 0.5f});
        removeGeneratedChildren(menu);
        menu->setLayout(RowLayout::create()->setAxisAlignment(AxisAlignment::Center)
            ->setGap(9.f)->setCrossAxisOverflow(false)->setAutoScale(true));

        int statIndex = 0;
        auto addStat = [&](int value, char const* iconFrame, ccColor3B color) {
            constexpr float kChipH = 16.f;
            auto chip = CCNode::create();
            chip->setID(fmt::format("{}rd-generated-stat-{}", ""_spr, statIndex++));
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
            menu->addChild(chip);
        };
// Prefer live local stats so async rebuilds keep the own profile current.
        if (ownProfile) {
            paimon::profiles::applyLiveOwnProfileStats(score);
        }
        int const stars = score->m_stars;
        int const moons = score->m_moons;
        int const diamonds = score->m_diamonds;
        int const demons = score->m_demons;
        int const userCoins = score->m_userCoins;
        int const secretCoins = score->m_secretCoins;
        addStat(stars, "GJ_starsIcon_001.png", {233, 253, 113});
        addStat(moons, "GJ_moonsIcon_001.png", {109, 215, 249});
        if (diamonds > 0)
            addStat(diamonds, "GJ_diamondsIcon_001.png", {90, 245, 255});
        addStat(demons, "GJ_demonIcon_001.png", {240, 140, 140});
        if (score->m_creatorPoints > 0)
            addStat(score->m_creatorPoints, "GJ_hammerIcon_001.png", {182, 186, 186});
        addStat(userCoins, "GJ_coinsIcon2_001.png", {255, 255, 255});
        addStat(secretCoins, "GJ_coinsIcon_001.png", {248, 138, 0});
        menu->updateLayout();
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
            playerMenu->setPosition({c.x, iconsY});
            playerMenu->setZOrder(Z_CONTENT);
        }
    }

    {
        const CCSize levelArea = {contentW - 6.f, 44.f};
        makeCard("rd-level-card"_spr, {c.x, levelY}, {contentW, 48.f}, cardA);
        auto* lc = layer->getChildByID("rd-level-container"_spr);
        if (!lc) {
            auto container = CCNode::create();
            container->setID("rd-level-container"_spr);
            container->setContentSize(levelArea);
            container->setAnchorPoint({0.5f, 0.5f});
            container->setZOrder(Z_CONTENT);
            layer->addChild(container);
            lc = container;
            auto loader = ProfileRecentLevelLoader::create(container, levelArea, score->m_userID);
            layer->setUserObject("rd-level-loader"_spr, loader);
            loader->start();
        }
        lc->setContentSize(levelArea);
        lc->setPosition({c.x, levelY});
    }

    auto* commentsTitle = CCLabelBMFont::create("LATEST POST", "chatFont.fnt");
    commentsTitle->setID("rd-comments-title"_spr);
    commentsTitle->setScale(0.34f);
    commentsTitle->setColor({190, 214, 235});
    commentsTitle->setAnchorPoint({0.f, 0.5f});
    commentsTitle->setPosition({contentLeft + 8.f, commentsY + commentsH * 0.5f + 7.f});
    commentsTitle->setZOrder(Z_CONTENT + 1);
    layer->addChild(commentsTitle);
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
    row->setContentSize({contentW - 8.f, 32.f});
    row->setPosition({c.x, bottomY});
    {
        auto* rowLayout = RowLayout::create()->setGap(7.f)->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisOverflow(false)->setAutoScale(true);
        rowLayout->ignoreInvisibleChildren(true);
        row->setLayout(rowLayout);
    }
    {
        if (auto* bg = paimon::SpriteHelper::safeCreateScale9("square02_small.png")) {
            bg->setContentSize({contentW, 38.f});
            bg->setOpacity(backdrop ? 115 : 145);
            bg->setColor({38, 41, 55});
            bg->setAnchorPoint({0.5f, 0.5f});
            bg->setPosition({c.x, bottomY});
            bg->setZOrder(Z_CARD);
            bg->setID("rd-bottom-row-bg"_spr);
            layer->addChild(bg);
        }

        for (auto const* id : {
            "comment-history-button", "my-levels-button", "my-lists-button", "follow-button"
        }) relocate(row, id);
        for (auto const* id : {
            "profile-reviews-btn", "rate-profile-btn", "paimon-thumb-count-btn"
        }) relocate(row, std::string("flozwer.paimbnails2/") + id);
        if (ownProfile) {
            int const userID = score->m_userID;
            auto addBrowseBtn = [&](std::string const& id, char const* baseFrame,
                                    char const* iconFrame, char const* caption,
                                    std::function<void()> cb) {
                if (row->getChildByID(id)) return;
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
// Size the bottom panel to the buttons it holds.
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
                const float w = std::clamp(content + 18.f, 60.f, contentW);
                bg->setContentSize({w, 38.f});
                bg->setPosition({c.x, bottomY});
            }
        }
    }
    swapMenu->updateLayout();

    {
        auto* menu = getOrCreateMenu(
            "rd-corner"_spr, {contentRight - 14.f, headerY + 9.f}, {24.f, 24.f}
        );
        removeGeneratedChildren(menu);
        menu->setLayout(RowLayout::create());
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
        menu->updateLayout();
    }
}

}
