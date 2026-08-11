#include <Geode/Geode.hpp>
#include <Geode/modify/MessagesProfilePage.hpp>
#include <Geode/modify/GJMessageCell.hpp>
#include <Geode/binding/GJUserMessage.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/GJCommentListLayer.hpp>
#include <Geode/binding/BoomListView.hpp>
#include <Geode/binding/TableView.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/GJWriteMessagePopup.hpp>
#include <Geode/binding/CCTextInputNode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/ProfilePage.hpp>
#include <Geode/ui/TextInput.hpp>

#include "../../core/RuntimeLifecycle.hpp"
#include "../../utils/GeodeTextInputSafe.hpp"
#include "../../utils/Localization.hpp"
#include "../../utils/PaimonNotification.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

namespace {

constexpr float kListFadeOutDuration = 0.10f;
constexpr float kListFadeInDuration = 0.18f;
constexpr float kListSlideDistance = 6.f;
constexpr int kListFadeActionTag = 0x504D01;
constexpr int kListMoveActionTag = 0x504D02;
constexpr int kListSwapActionTag = 0x504D03;

// Cached: this runs per cell load; getSettingValue takes a mutex.
bool redesignOn() {
    static bool s_enabled = Mod::get()->getSettingValue<bool>("messages-redesign-enabled");
    static auto s_listener = [] {
        listenForSettingChanges<bool>("messages-redesign-enabled", [](bool v) {
            s_enabled = v;
        });
        return 0;
    }();
    (void)s_listener;
    return s_enabled;
}

std::string loc(char const* key) {
    return Localization::get().getString(key);
}

struct UserIcon {
    int iconID = 1;
    int color1 = 0;
    int color2 = 3;
    IconType type = IconType::Cube;
    bool glow = false;
};

int iconIDFor(GJUserScore* score) {
    if (!score) return 1;
    switch (score->m_iconType) {
        case IconType::Cube: return score->m_playerCube;
        case IconType::Ship: return score->m_playerShip;
        case IconType::Ball: return score->m_playerBall;
        case IconType::Ufo: return score->m_playerUfo;
        case IconType::Wave: return score->m_playerWave;
        case IconType::Robot: return score->m_playerRobot;
        case IconType::Spider: return score->m_playerSpider;
        case IconType::Swing: return score->m_playerSwing;
        case IconType::Jetpack: return score->m_playerJetpack;
        default: return score->m_playerCube;
    }
}

UserIcon iconFromScore(GJUserScore* score) {
    return {
        std::max(iconIDFor(score), 1),
        score ? score->m_color1 : 0,
        score ? score->m_color2 : 3,
        score ? score->m_iconType : IconType::Cube,
        score && score->m_glowEnabled,
    };
}

std::unordered_map<int, UserIcon>& userIconCache() {
    static auto* cache = new std::unordered_map<int, UserIcon>();
    return *cache;
}

void cacheUserIcons(CCArray* scores) {
    if (!scores) return;
    for (auto* score : CCArrayExt<GJUserScore*>(scores)) {
        if (!score || score->m_accountID <= 0) continue;
        userIconCache()[score->m_accountID] = iconFromScore(score);
    }
}

std::optional<UserIcon> getUserIcon(int accountID) {
    if (auto it = userIconCache().find(accountID); it != userIconCache().end()) {
        return it->second;
    }
    auto* glm = GameLevelManager::get();
    if (!glm) return std::nullopt;

    cacheUserIcons(glm->getStoredUserList(UserListType::Friends));
    if (auto it = userIconCache().find(accountID); it != userIconCache().end()) {
        return it->second;
    }

    auto* score = glm->userInfoForAccountID(accountID);
    if (!score || score->m_accountID <= 0) return std::nullopt;
    return userIconCache().emplace(accountID, iconFromScore(score)).first->second;
}

void fadeOutTree(CCNode* node) {
    if (!node) return;

    if (typeinfo_cast<CCRGBAProtocol*>(node)) {
        node->stopActionByTag(kListFadeActionTag);
        auto* fade = CCFadeTo::create(kListFadeOutDuration, 0);
        fade->setTag(kListFadeActionTag);
        node->runAction(fade);
    }

    if (auto* children = node->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) fadeOutTree(child);
    }
}

void fadeInTree(CCNode* node) {
    if (!node) return;

    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node)) {
        auto const target = rgba->getOpacity();
        node->stopActionByTag(kListFadeActionTag);
        rgba->setOpacity(0);
        if (target > 0) {
            auto* fade = CCFadeTo::create(kListFadeInDuration, target);
            fade->setTag(kListFadeActionTag);
            node->runAction(fade);
        }
    }

    if (auto* children = node->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) fadeInTree(child);
    }
}

} // namespace

class $modify(PaimonMessagesPage, MessagesProfilePage) {
    struct Fields {
        Ref<CCArray> m_all;
        std::string m_query;
        Ref<geode::TextInput> m_search;
        int m_lastShownCount = 0;
        int m_filterGeneration = 0;
        bool m_searchDetached = false;
        bool m_selectLatch = false;

        // keyBackClicked/onClose are the normal path; this is the guaranteed
        // one (onExit hooks never install on Windows).
        ~Fields() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (m_search && !m_searchDetached) {
                paimon::ui::detachGeodeTextInput(m_search);
            }
        }
    };

    $override
    bool init(bool sent) {
        if (!MessagesProfilePage::init(sent)) return false;
        if (redesignOn()) buildPaimonInbox();
        return true;
    }

    CCNode* findPopupBg() {
        if (!m_mainLayer) return nullptr;
        if (auto* bg = m_mainLayer->getChildByID("background")) return bg;
        auto* children = m_mainLayer->getChildren();
        if (!children) return nullptr;
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (typeinfo_cast<CCScale9Sprite*>(child)) return child;
        }
        return nullptr;
    }

    CCLabelBMFont* findTitleLabel(CCNode* bg) {
        if (!m_mainLayer || !bg) return nullptr;
        auto box = bg->boundingBox();
        auto* children = m_mainLayer->getChildren();
        if (!children) return nullptr;
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            auto* label = typeinfo_cast<CCLabelBMFont*>(child);
            if (!label) continue;
            if (label == m_levelsLabel || label == m_errorLabel) continue;
            auto pos = label->getPosition();
            if (std::fabs(pos.x - box.getMidX()) > 90.f) continue;
            if (pos.y < box.getMaxY() - 18.f || pos.y > box.getMaxY() + 35.f) continue;
            return label;
        }
        return nullptr;
    }

    void buildPaimonInbox() {
        auto* bg = findPopupBg();
        if (!bg || !m_mainLayer) return;

        auto box = bg->boundingBox();

        if (auto* input = geode::TextInput::create(175.f, loc("msgs.search.placeholder"))) {
            input->setScale(0.7f);
            input->setCommonFilter(CommonFilter::Any);
            input->setCallback(paimon::ui::safeTextInputCallback<PaimonMessagesPage>(
                this, &PaimonMessagesPage::onSearchChanged));
            input->setPosition({box.getMidX() + 55.f, box.getMinY() + 22.f});
            input->setID("paimon-msgs-search"_spr);
            if (auto* inputBg = input->getBGSprite()) {
                inputBg->setColor({105, 57, 31});
                inputBg->setOpacity(190);
            }
            m_mainLayer->addChild(input, 11);
            m_fields->m_search = input;
        }

        if (!m_sentMessages && m_buttonMenu) {
            if (auto* spr = ButtonSprite::create(
                    loc("msgs.selectread").c_str(), "goldFont.fnt", "GJ_button_01.png", 0.8f)) {
                spr->setScale(0.55f);
                auto* item = CCMenuItemSpriteExtra::create(
                    spr, this, menu_selector(PaimonMessagesPage::onSelectRead));
                item->setID("paimon-msgs-selectread"_spr);
                CCPoint wp = m_mainLayer->convertToWorldSpace(
                    {box.getMinX() + 168.f, box.getMinY() + 22.f});
                item->setPosition(m_buttonMenu->convertToNodeSpace(wp));
                m_buttonMenu->addChild(item);
            }
        }
    }

    static bool matchesQuery(GJUserMessage* msg, std::string const& q) {
        if (q.empty()) return true;
        if (!msg) return false;
        auto contains = [&](std::string const& s) {
            return geode::utils::string::toLower(s).find(q) != std::string::npos;
        };
        return contains(msg->m_title) || contains(msg->m_username);
    }

    $override
    void setupCommentsBrowser(CCArray* messages) {
        if (!redesignOn()) {
            MessagesProfilePage::setupCommentsBrowser(messages);
            return;
        }
        auto* copy = CCArray::create();
        if (messages) copy->addObjectsFromArray(messages);
        m_fields->m_all = copy;
        if (m_listLayer && m_listLayer->getParent()) transitionFilteredList();
        else rebuildFilteredList(false);
    }

    void rebuildFilteredList(bool animate) {
        auto* all = m_fields->m_all.data();
        if (!all) return;

        auto* shown = CCArray::create();
        for (auto* msg : CCArrayExt<GJUserMessage*>(all)) {
            if (matchesQuery(msg, m_fields->m_query)) shown->addObject(msg);
        }

        if (m_listLayer) {
            m_listLayer->removeFromParent();
            m_listLayer = nullptr;
        }
        MessagesProfilePage::setupCommentsBrowser(shown);
        if (animate && m_listLayer) {
            auto const target = m_listLayer->getPosition();
            m_listLayer->setPositionY(target.y - kListSlideDistance);
            fadeInTree(m_listLayer);

            auto* move = CCEaseSineOut::create(
                CCMoveTo::create(kListFadeInDuration, target));
            move->setTag(kListMoveActionTag);
            m_listLayer->runAction(move);
        }
        m_fields->m_selectLatch = false;
        m_fields->m_lastShownCount = static_cast<int>(shown->count());
        updateUnreadBadge();
    }

    void transitionFilteredList() {
        auto const generation = ++m_fields->m_filterGeneration;
        if (!m_listLayer || !m_listLayer->getParent()) {
            rebuildFilteredList(true);
            return;
        }

        auto* outgoing = m_listLayer;
        outgoing->stopActionByTag(kListSwapActionTag);
        fadeOutTree(outgoing);

        WeakRef<MessagesProfilePage> self = this;
        auto* swap = CCSequence::create(
            CCDelayTime::create(kListFadeOutDuration),
            CallFuncExt::create([self, generation] {
                auto page = self.lock();
                if (!page) return;
                auto* messages = static_cast<PaimonMessagesPage*>(page.data());
                if (messages->m_fields->m_filterGeneration != generation) return;
                messages->rebuildFilteredList(true);
            }),
            nullptr);
        swap->setTag(kListSwapActionTag);
        outgoing->runAction(swap);
    }

    void updateUnreadBadge() {
        if (!m_mainLayer) return;
        if (auto* old = m_mainLayer->getChildByID("paimon-msgs-badge"_spr)) {
            old->removeFromParent();
        }

        std::string text;
        if (!m_fields->m_query.empty()) {
            text = fmt::format(fmt::runtime(loc("msgs.results")), m_fields->m_lastShownCount);
        } else if (!m_sentMessages && m_fields->m_all) {
            int unread = 0;
            for (auto* msg : CCArrayExt<GJUserMessage*>(m_fields->m_all.data())) {
                if (msg && !msg->m_read) ++unread;
            }
            if (unread > 0) {
                text = fmt::format(fmt::runtime(loc("msgs.unread.count")), unread);
            }
        }
        if (text.empty()) return;

        auto* bg = findPopupBg();
        if (!bg) return;
        auto* label = CCLabelBMFont::create(text.c_str(), "goldFont.fnt");
        if (!label) return;
        label->setScale(0.5f);
        label->setAnchorPoint({0.f, 0.5f});
        label->setID("paimon-msgs-badge"_spr);
        if (auto* title = findTitleLabel(bg)) {
            auto tbox = title->boundingBox();
            label->setPosition({tbox.getMaxX() + 10.f, tbox.getMidY() - 2.f});
        } else {
            auto box = bg->boundingBox();
            label->setPosition({box.getMidX() + 105.f, box.getMaxY() - 2.f});
        }
        m_mainLayer->addChild(label, 12);
    }

    void onSearchChanged(std::string const& text) {
        auto q = geode::utils::string::trim(text);
        m_fields->m_query = geode::utils::string::toLower(q);
        if (m_fields->m_all) transitionFilteredList();
    }

    void resetSearchAfterDelete(int messageID) {
        auto* all = m_fields->m_all.data();
        if (all) {
            for (unsigned int i = all->count(); i > 0; --i) {
                auto* msg = typeinfo_cast<GJUserMessage*>(all->objectAtIndex(i - 1));
                if (msg && msg->m_messageID == messageID) {
                    all->removeObjectAtIndex(i - 1);
                    break;
                }
            }
        }

        if (!m_fields->m_query.empty()) {
            m_fields->m_query.clear();
            if (m_fields->m_search) m_fields->m_search->setString("", false);
        }
        transitionFilteredList();
    }

    void onSelectRead(CCObject*) {
        if (!m_listLayer || !m_listLayer->m_list || !m_listLayer->m_list->m_entries) return;
        auto* entries = m_listLayer->m_list->m_entries;

        if (!m_fields->m_selectLatch) {
            int selected = 0;
            for (auto* msg : CCArrayExt<GJUserMessage*>(entries)) {
                if (!msg || !msg->m_read) continue;
                msg->m_toggled = true;
                ++selected;
            }
            if (selected == 0) {
                PaimonNotify::show(loc("msgs.selectread.none"), NotificationIcon::Info);
                return;
            }
            syncVisibleTogglers();
            m_fields->m_selectLatch = true;
            PaimonNotify::show(
                fmt::format(fmt::runtime(loc("msgs.selectread.some")), selected),
                NotificationIcon::Success);
        } else {
            for (auto* msg : CCArrayExt<GJUserMessage*>(entries)) {
                if (msg) msg->m_toggled = false;
            }
            syncVisibleTogglers();
            m_fields->m_selectLatch = false;
            PaimonNotify::show(loc("msgs.selectread.cleared"), NotificationIcon::Info);
        }
    }

    void syncVisibleTogglers() {
        if (!m_listLayer || !m_listLayer->m_list) return;
        auto* tv = m_listLayer->m_list->m_tableView;
        if (!tv || !tv->m_cellArray) return;
        for (auto* node : CCArrayExt<CCNode*>(tv->m_cellArray)) {
            auto* cell = typeinfo_cast<GJMessageCell*>(node);
            if (!cell || !cell->m_message || !cell->m_toggler) continue;
            cell->m_toggler->toggle(cell->m_message->m_toggled);
        }
    }

    void detachSearch() {
        ++m_fields->m_filterGeneration;
        if (m_listLayer) m_listLayer->stopActionByTag(kListSwapActionTag);
        if (m_fields->m_search && !m_fields->m_searchDetached) {
            paimon::ui::detachGeodeTextInput(m_fields->m_search);
            m_fields->m_searchDetached = true;
        }
    }

    $override
    void keyBackClicked() {
        detachSearch();
        MessagesProfilePage::keyBackClicked();
    }

    $override
    void onClose(CCObject* sender) {
        detachSearch();
        MessagesProfilePage::onClose(sender);
    }
};

class $modify(PaimonMessageCell, GJMessageCell) {
    $override
    void loadFromMessage(GJUserMessage* msg) {
        GJMessageCell::loadFromMessage(msg);
        if (redesignOn()) restylePaimonCell();
    }

    $override
    void markAsRead() {
        GJMessageCell::markAsRead();
        if (!redesignOn()) return;
        restylePaimonCell();
        for (CCNode* n = this->getParent(); n; n = n->getParent()) {
            if (auto* page = typeinfo_cast<MessagesProfilePage*>(n)) {
                static_cast<PaimonMessagesPage*>(page)->updateUnreadBadge();
                break;
            }
        }
    }

    $override
    void uploadActionFinished(int id, int response) {
        int const messageID = m_message ? m_message->m_messageID : 0;
        WeakRef<MessagesProfilePage> page;
        for (CCNode* node = getParent(); node; node = node->getParent()) {
            if (auto* messages = typeinfo_cast<MessagesProfilePage*>(node)) {
                page = messages;
                break;
            }
        }

        GJMessageCell::uploadActionFinished(id, response);

        auto pageRef = page.lock();
        if (messageID > 0 && pageRef) {
            static_cast<PaimonMessagesPage*>(pageRef.data())->resetSearchAfterDelete(messageID);
        }
    }

    void restylePaimonCell() {
        auto* msg = m_message;
        auto* main = m_mainLayer;
        if (!msg || !main) return;

        float const W = m_width;
        float const H = m_height;

        // Cells are reused while scrolling; drop our previous nodes first.
        if (auto* old = main->getChildByID("paimon-msg-avatar"_spr)) old->removeFromParent();
        if (auto* children = main->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                auto* menu = typeinfo_cast<CCMenu*>(child);
                if (!menu) continue;
                if (auto* old = menu->getChildByID("paimon-msg-avatar"_spr)) old->removeFromParent();
                if (auto* old = menu->getChildByID("paimon-msg-reply"_spr)) old->removeFromParent();
            }
        }

        float chipCx = 26.f;
        if (m_toggler && m_toggler->getParent()) {
            CCPoint wp = m_toggler->getParent()->convertToWorldSpace(m_toggler->getPosition());
            chipCx = main->convertToNodeSpace(wp).x + 30.f;
        }
        float const chipHalf = 13.f;
        float const contentLeft = chipCx + chipHalf + 6.f;

        float rightClusterLeft = W - 96.f;
        if (auto* children = main->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                auto* menu = typeinfo_cast<CCMenu*>(child);
                if (!menu || !menu->getChildren()) continue;
                for (auto* itemNode : CCArrayExt<CCNode*>(menu->getChildren())) {
                    auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(itemNode);
                    if (!item || item->getID() == "paimon-msg-reply"_spr) continue;
                    CCPoint p = main->convertToNodeSpace(
                        menu->convertToWorldSpace(item->getPosition()));
                    if (p.x <= W * 0.55f) continue;
                    rightClusterLeft = std::min(
                        rightClusterLeft, p.x - item->getScaledContentSize().width * 0.5f);
                }
            }
        }
        float const replyCx = std::max(W * 0.55f, rightClusterLeft - 16.f);

        // Shift the vanilla left content (subject / from / date) to make room
        // for the avatar. No-op when already shifted (cell reuse) since the
        // labels then start past contentLeft.
        std::vector<CCLabelBMFont*> leftLabels;
        float leftEdge = W;
        if (auto* children = main->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                auto* label = typeinfo_cast<CCLabelBMFont*>(child);
                if (!label) continue;
                auto lbox = label->boundingBox();
                if (lbox.getMinX() > W * 0.5f) continue;
                leftLabels.push_back(label);
                leftEdge = std::min(leftEdge, lbox.getMinX());
            }
        }
        float const delta = std::max(0.f, contentLeft - leftEdge);
        if (delta > 0.5f) {
            for (auto* label : leftLabels) {
                label->setPositionX(label->getPositionX() + delta);
            }
            if (auto* children = main->getChildren()) {
                for (auto* child : CCArrayExt<CCNode*>(children)) {
                    auto* menu = typeinfo_cast<CCMenu*>(child);
                    if (!menu || !menu->getChildren()) continue;
                    for (auto* itemNode : CCArrayExt<CCNode*>(menu->getChildren())) {
                        auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(itemNode);
                        if (!item) continue;
                        CCPoint wp = menu->convertToWorldSpace(item->getPosition());
                        if (main->convertToNodeSpace(wp).x >= W * 0.5f) continue;
                        item->setPositionX(item->getPositionX() + delta);
                    }
                }
            }
        }

        CCLabelBMFont* title = nullptr;
        for (auto* label : leftLabels) {
            if (!title || label->boundingBox().getMidY() > title->boundingBox().getMidY()) {
                title = label;
            }
        }
        if (title) {
            float const maxW = (replyCx - 14.f) - title->boundingBox().getMinX();
            if (maxW > 24.f) title->limitLabelWidth(maxW, title->getScale(), 0.25f);
        }

        CCMenu* host = m_toggler ? typeinfo_cast<CCMenu*>(m_toggler->getParent()) : nullptr;
        if (!host) {
            if (auto* children = main->getChildren()) {
                for (auto* child : CCArrayExt<CCNode*>(children)) {
                    if ((host = typeinfo_cast<CCMenu*>(child))) break;
                }
            }
        }

        auto userIcon = getUserIcon(msg->m_accountID);
        auto icon = userIcon.value_or(UserIcon{});
        if (auto* player = SimplePlayer::create(icon.iconID)) {
            auto* wrapper = CCNode::create();
            wrapper->setContentSize({chipHalf * 2.f, chipHalf * 2.f});
            wrapper->setAnchorPoint({0.5f, 0.5f});

            if (icon.type != IconType::Cube) {
                player->updatePlayerFrame(icon.iconID, icon.type);
            }
            auto* gm = GameManager::sharedState();
            auto color1 = gm ? gm->colorForIdx(icon.color1) : ccc3(175, 175, 175);
            auto color2 = gm ? gm->colorForIdx(icon.color2) : ccc3(255, 255, 255);
            player->setColors(color1, color2);
            if (icon.glow) player->setGlowOutline(color2);
            else player->disableGlowOutline();
            player->setPosition({chipHalf, chipHalf});
            auto maxDim = std::max(player->getContentWidth(), player->getContentHeight());
            if (maxDim > 0.f) player->setScale(23.f / maxDim);
            if (!userIcon) player->setOpacity(110);
            wrapper->addChild(player);

            if (host) {
                auto* item = CCMenuItemSpriteExtra::create(
                    wrapper, this, menu_selector(PaimonMessageCell::onPaimonProfile));
                item->setID("paimon-msg-avatar"_spr);
                item->m_scaleMultiplier = 1.08f;
                auto wp = main->convertToWorldSpace({chipCx, H * 0.5f});
                item->setPosition(host->convertToNodeSpace(wp));
                host->addChild(item);
            } else {
                wrapper->setID("paimon-msg-avatar"_spr);
                wrapper->setPosition({chipCx, H * 0.5f});
                main->addChild(wrapper, 4);
            }
        }

        if (host) {
            if (auto* spr = CCSprite::createWithSpriteFrameName("accountBtn_messages_001.png")) {
                spr->setScale(19.f / std::max(1.f, spr->getContentWidth()));
                auto* item = CCMenuItemSpriteExtra::create(
                    spr, this, menu_selector(PaimonMessageCell::onPaimonQuickReply));
                item->setID("paimon-msg-reply"_spr);
                CCPoint wp = main->convertToWorldSpace({replyCx, H * 0.5f});
                item->setPosition(host->convertToNodeSpace(wp));
                host->addChild(item);
            }
        }
    }

    void onPaimonProfile(CCObject*) {
        if (!m_message || m_message->m_accountID <= 0) return;
        if (auto* page = ProfilePage::create(m_message->m_accountID, false)) page->show();
    }

    void onPaimonQuickReply(CCObject*) {
        auto* msg = m_message;
        if (!msg) return;
        auto* popup = GJWriteMessagePopup::create(msg->m_accountID, msg->m_messageID);
        if (!popup) return;
        if (!msg->m_outgoing && popup->m_subjectInput) {
            std::string current = popup->m_subjectInput->getString();
            if (current.empty()) {
                std::string subject = msg->m_title;
                if (subject.rfind("Re:", 0) != 0) subject = "Re: " + subject;
                if (subject.size() > 35) subject = subject.substr(0, 35);
                popup->m_subjectInput->setString(subject);
            }
        }
        popup->show();
    }
};
