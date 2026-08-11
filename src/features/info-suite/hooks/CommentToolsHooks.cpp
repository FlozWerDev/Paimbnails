// Comment Tools + Unregistered Profiles:
//  - estimated real dates on comment cells
//  - jump to any comment page instead of clicking the arrow N times
//  - a usable screen for green, account-less players

#include "../InfoModule.hpp"
#include "../services/CommentDates.hpp"
#include "../services/IdBadge.hpp"
#include "../services/InfoStore.hpp"
#include "../ui/JumpToPagePopup.hpp"
#include "../ui/UnregisteredProfilePopup.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../../utils/PaimonButtonHighlighter.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/GJComment.hpp>
#include <Geode/modify/CommentCell.hpp>
#include <Geode/modify/InfoLayer.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace {

char const* const kDateBadgeID = "info-suite-comment-date";

bool commentToolsEnabled() {
    return paimon::info::moduleEnabled("info-mod-comment-tools");
}

bool unregProfilesEnabled() {
    return paimon::info::moduleEnabled("info-mod-unreg-profiles");
}

} // namespace

class $modify(PaimonInfoSuiteCommentTools, CommentCell) {
    static void onModify(auto& self) {
        paimon::hooks::afterAllPaimonUiOrVeryLate(self, "CommentCell::loadFromComment");
    }

    void loadFromComment(GJComment* comment) {
        CommentCell::loadFromComment(comment);
        if (!comment) return;

        // Remembering the name is what later lets a blank green name be filled
        // in, so it runs for that module rather than for comment tools.
        if (paimon::info::moduleEnabled("info-mod-green-users")) {
            std::string name(comment->m_userName);
            if (!name.empty() && name != "-") {
                paimon::info::InfoStore::get().rememberUsername(comment->m_userID, name);
            }
        }

        if (!commentToolsEnabled()) return;

        paimon::info::noteComment(comment->m_commentID, std::string(comment->m_uploadDate));

        if (this->getChildByID(kDateBadgeID)) return;
        auto date = paimon::info::estimateCommentDate(comment->m_commentID);
        if (date.empty()) return;

        auto* badge = paimon::info::makeIdBadge(fmt::format("~ {}", date), 0.34f);
        if (!badge) return;

        badge->setID(kDateBadgeID);
        badge->setAnchorPoint({1.f, 1.f});
        badge->setPosition({this->getContentSize().width - 5.f,
                            this->getContentSize().height - 4.f});
        this->addChild(badge, 100);
    }

    // Tapping a green player's name does nothing in vanilla, since there is no
    // account to open. Show what we can instead of a dead button.
    void onViewProfile(CCObject* sender) {
        if (unregProfilesEnabled() && m_comment && m_comment->m_accountID <= 0
            && m_comment->m_userID > 0) {
            auto popup = paimon::info::UnregisteredProfilePopup::create(
                m_comment->m_userID, std::string(m_comment->m_userName));
            if (popup) {
                popup->show();
                return;
            }
        }
        CommentCell::onViewProfile(sender);
    }
};

// Jump to page for the comment list on the level / profile info screen.
class $modify(PaimonInfoSuiteCommentPages, InfoLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterAllPaimonUiOrVeryLate(self, "InfoLayer::init");
    }

    struct Fields {
        int m_perPage = 0;
    };

    bool init(GJGameLevel* level, GJUserScore* score, GJLevelList* list) {
        if (!InfoLayer::init(level, score, list)) return false;
        addJumpButton();
        return true;
    }

    void loadPage(int page, bool noSetup) {
        InfoLayer::loadPage(page, noSetup);
        // The page size is only knowable once a page has been laid out.
        int perPage = m_pageEndIdx - m_pageStartIdx + 1;
        if (perPage > 0) m_fields->m_perPage = perPage;
    }

    void addJumpButton() {
        if (!commentToolsEnabled() || !m_pageLabel) return;
        if (this->getChildByID("info-suite-comment-jump"_spr)) return;

        CCSprite* icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_longBtn02_001.png");
        if (!icon) icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plusBtn_001.png");
        if (!icon) return;
        icon->setScale(0.5f);

        auto btn = CCMenuItemSpriteExtra::create(
            icon, this, menu_selector(PaimonInfoSuiteCommentPages::onJumpToCommentPage));
        if (!btn) return;
        PaimonButtonHighlighter::registerButton(btn);

        auto menu = CCMenu::create();
        menu->setID("info-suite-comment-jump"_spr);
        // Right beside the page counter, wherever the layer put it.
        menu->setPosition({m_pageLabel->getPositionX() + 46.f, m_pageLabel->getPositionY()});
        menu->addChild(btn);

        if (auto* parent = m_pageLabel->getParent()) parent->addChild(menu, 20);
        else this->addChild(menu, 20);
    }

    void onJumpToCommentPage(CCObject*) {
        int perPage = m_fields->m_perPage > 0
            ? m_fields->m_perPage
            : std::max(1, m_pageEndIdx - m_pageStartIdx + 1);

        int pageCount = m_itemCount > 0 ? (m_itemCount + perPage - 1) / perPage : 0;

        auto self = WeakRef<InfoLayer>(this);
        auto popup = paimon::info::JumpToPagePopup::create(m_page + 1, pageCount,
            [self](int page) {
                auto ref = self.lock();
                if (!ref) return;
                static_cast<PaimonInfoSuiteCommentPages*>(ref.data())
                    ->loadPage(std::max(0, page - 1), false);
            });

        if (popup) popup->show();
    }
};
