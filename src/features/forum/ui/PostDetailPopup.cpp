#include "PostDetailPopup.hpp"
#include "../services/ForumHelpers.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/cocos/touch_dispatcher/CCTouchDispatcher.h>
#include <Geode/ui/General.hpp>
#include <Geode/ui/MDTextArea.hpp>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;
using paimon::forum::Author;
using paimon::forum::ForumApi;
using paimon::forum::Post;
using paimon::forum::Reply;

namespace {

constexpr float POPUP_W = 470.f;
constexpr float POPUP_H = 326.f;
constexpr float CONTENT_PAD = 14.f;
constexpr float SCROLL_GUTTER = 10.f;

int popupMenuPrio() {
    if (auto* disp = CCDirector::get()->getTouchDispatcher()) {
        return disp->getTargetPrio() - 2;
    }
    return -130;
}

bool isSpanish() {
    return Localization::get().getLanguage() == Localization::Language::SPANISH;
}

std::string tf(char const* key, char const* en, char const* es) {
    return paimon::loc::tr(key, isSpanish() ? es : en);
}

CCSprite* makeIcon(char const* frame, float size, ccColor3B color = {255, 255, 255}) {
    auto* spr = paimon::SpriteHelper::safeCreateWithFrameName(frame);
    if (!spr) return nullptr;
    float maxDim = std::max(spr->getContentSize().width, spr->getContentSize().height);
    if (maxDim > 0.f) spr->setScale(size / maxDim);
    spr->setColor(color);
    return spr;
}

CCNode* makeSmallPill(
    char const* frame,
    std::string const& text,
    ccColor3B bgCol,
    ccColor3B iconCol,
    ccColor3B txtCol,
    GLubyte alpha = 150,
    float height = 18.f
) {
    auto* icon = frame ? makeIcon(frame, 10.f, iconCol) : nullptr;
    auto* lbl = text.empty() ? nullptr : CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    float textW = 0.f;
    if (lbl) {
        lbl->setScale(0.25f);
        lbl->setColor(txtCol);
        paimon::ui::shrinkLabelToFit(lbl, 76.f);
        textW = lbl->getScaledContentSize().width;
    }

    float iconW = icon ? 11.f : 0.f;
    float gap = (icon && lbl) ? 4.f : 0.f;
    float padX = 7.f;
    float w = std::max(height, padX * 2.f + iconW + gap + textW);

    auto* pill = CCNode::create();
    pill->setContentSize({w, height});
    pill->setAnchorPoint({0.5f, 0.5f});
    pill->addChild(paimon::ui::makeInset({w, height}, alpha, bgCol), -1);

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

class ReportReasonPopup : public PaimonPopup {
public:
    static ReportReasonPopup* create(bool isReply, std::function<void(std::string const&)> onSubmit) {
        auto* ret = new ReportReasonPopup();
        if (ret && ret->init(isReply, std::move(onSubmit))) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

protected:
    std::function<void(std::string const&)> m_onSubmit;

    bool init(bool isReply, std::function<void(std::string const&)> onSubmit) {
        if (!PaimonPopup::init(270.f, 166.f)) return false;
        m_onSubmit = std::move(onSubmit);
        this->setTitle(isReply
            ? tf("pai.hub.forum.report.reply_title", "Report Reply", "Reportar Respuesta").c_str()
            : tf("pai.hub.forum.report.post_title", "Report Post", "Reportar Publicacion").c_str());

        auto size = m_mainLayer->getContentSize();
        auto* hint = CCLabelBMFont::create(
            tf("pai.hub.forum.report.hint", "Select a reason:", "Selecciona un motivo:").c_str(),
            "chatFont.fnt"
        );
        hint->setScale(0.48f);
        hint->setColor(paimon::ui::palette::muted);
        hint->setPosition({size.width / 2.f, size.height - 40.f});
        m_mainLayer->addChild(hint);

        struct ReasonDef { char const* key; char const* en; char const* es; };
        ReasonDef const reasons[] = {
            {"pai.hub.forum.report.spam", "Spam / Self-promo", "Spam / Autopromocion"},
            {"pai.hub.forum.report.abusive", "Harassment / Offensive", "Acoso / Ofensivo"},
            {"pai.hub.forum.report.offtopic", "Off-topic / Misleading", "Fuera de tema / Enganoso"},
        };

        WeakRef<ReportReasonPopup> self = this;
        float y = size.height - 68.f;
        for (auto const& r : reasons) {
            std::string reasonEn = r.en;
            auto* btn = paimon::ui::makeButton(
                tf(r.key, r.en, r.es).c_str(),
                [self, reasonEn] {
                    if (auto p = self.lock()) {
                        auto cb = p->m_onSubmit;
                        p->onClose(nullptr);
                        if (cb) cb(reasonEn);
                    }
                },
                paimon::ui::Btn::Red, 220.f, 0.54f, "bigFont.fnt"
            );
            btn->setPosition({size.width / 2.f, y});
            m_buttonMenu->addChild(btn);
            y -= 28.f;
        }

        paimon::markDynamicPopup(this);
        return true;
    }
};

} // namespace

bool PostDetailPopup::init(Post const& post, PostDetailCallbacks callbacks) {
    if (!PaimonPopup::init(POPUP_W, POPUP_H)) return false;
    this->setMouseEnabled(true);
    m_post = post;
    m_callbacks = std::move(callbacks);

    this->setTitle(m_post.title.empty() ? "(untitled)" : m_post.title.c_str());
    this->addInfoButton(
        tf("pai.hub.forum.detail.info_title", "Post Details", "Detalles del Post"),
        isSpanish()
            ? "Lee el contenido y sus respuestas.\n\n"
              "- Toca al <cg>autor</c> para abrir su perfil de GD.\n"
              "- Toca una <cb>etiqueta</c> para filtrar el foro por ella.\n"
              "- Si el post incluye un <cy>ID de nivel</c>, puedes abrirlo en GD al instante."
            : "Read the post and join the conversation.\n\n"
              "- Tap any <cg>author</c> to open their GD profile.\n"
              "- Tap a <cb>tag</c> to filter the forum by it.\n"
              "- Detected <cy>GD Level IDs</c> can be opened directly in-game."
    );

    rebuild();
    fetchAuthorStatus();
    paimon::markDynamicPopup(this);
    this->scheduleUpdate();
    return true;
}

void PostDetailPopup::fetchAuthorStatus() {
    if (m_post.author.accountID <= 0) return;
    WeakRef<PostDetailPopup> self = this;
    ForumApi::get().getUserStatus(m_post.author.accountID, [self](paimon::forum::Result<paimon::forum::UserStatus> res) {
        auto popup = self.lock();
        if (!popup || !popup->getParent() || !res.ok) return;
        bool const es = isSpanish();
        if (res.data.online) {
            popup->m_authorStatusText = es ? "En linea" : "Online";
            popup->m_authorStatusColor = paimon::ui::palette::success;
        } else if (res.data.lastSeen > 0) {
            popup->m_authorStatusText = paimon::forum::formatTimeShort(res.data.lastSeen, es);
            popup->m_authorStatusColor = paimon::ui::palette::dim;
        } else {
            popup->m_authorStatusText = es ? "Desconectado" : "Offline";
            popup->m_authorStatusColor = paimon::ui::palette::dim;
        }
        if (popup->m_authorStatusLabel) {
            popup->m_authorStatusLabel->setString(popup->m_authorStatusText.c_str());
            popup->m_authorStatusLabel->setColor(popup->m_authorStatusColor);
        }
    });
}

void PostDetailPopup::refreshFromServer() {
    WeakRef<PostDetailPopup> self = this;
    ForumApi::get().getPost(m_post.id, [self](paimon::forum::Result<Post> res) {
        auto popup = self.lock();
        if (!popup || !popup->getParent()) return;
        if (!res.ok) {
            PaimonNotify::show(
                isSpanish() ? "No se pudo actualizar" : "Failed to refresh",
                NotificationIcon::Warning
            );
            return;
        }
        popup->m_post = res.data;
        popup->rebuild();
        PaimonNotify::show(
            isSpanish() ? "Publicacion actualizada" : "Post refreshed",
            NotificationIcon::Info
        );
    });
}

void PostDetailPopup::rebuild() {
    auto contentSize = m_mainLayer->getContentSize();
    float const innerW = contentSize.width - CONTENT_PAD * 2.f;
    bool const es = isSpanish();

    paimon::ui::detachGeodeTextInput(m_replyInput);

    {
        std::vector<CCNode*> toRemove;
        for (auto child : CCArrayExt<CCNode*>(m_mainLayer->getChildren())) {
            if (child->getID() == "rebuild-block"_spr) toRemove.push_back(child);
        }
        for (auto* c : toRemove) c->removeFromParent();
    }
    {
        std::vector<CCNode*> toRemove;
        for (auto child : CCArrayExt<CCNode*>(m_buttonMenu->getChildren())) {
            if (child->getID() == "rebuild-btn"_spr) toRemove.push_back(child);
        }
        for (auto* c : toRemove) c->removeFromParent();
    }

    m_replyInput = nullptr;
    m_cooldownLabel = nullptr;
    m_replyCharLabel = nullptr;
    m_authorStatusLabel = nullptr;
    m_scroll = nullptr;
    m_scrollTargetSet = false;

    WeakRef<PostDetailPopup> self = this;

    // -------------------------------------------------------------------------
    // 1. Author Header Card
    // -------------------------------------------------------------------------
    constexpr float kHeaderH = 36.f;
    float const headerBot = contentSize.height - 32.f - kHeaderH;

    auto* headerBg = paimon::ui::makeInset({innerW, kHeaderH}, 80);
    headerBg->setPosition({CONTENT_PAD, headerBot});
    headerBg->setID("rebuild-block"_spr);
    m_mainLayer->addChild(headerBg);

    auto* headerRow = makeAuthorRow(m_post.author, m_post.createdAt, innerW);
    headerRow->setPosition({CONTENT_PAD, headerBot + kHeaderH});
    headerRow->setID("rebuild-block"_spr);
    m_mainLayer->addChild(headerRow, 2);

    // -------------------------------------------------------------------------
    // 2. Tags + Detected Level IDs Row (optional)
    // -------------------------------------------------------------------------
    auto levelIds = paimon::forum::extractLevelIds(m_post.title + "\n" + m_post.description);
    bool const hasMetaStrip = !m_post.tags.empty() || !levelIds.empty();
    float cursorTop = headerBot - 6.f;

    if (hasMetaStrip) {
        constexpr float kMetaH = 22.f;
        float const metaBot = cursorTop - kMetaH;

        auto* metaBg = paimon::ui::makeInset({innerW, kMetaH}, 60);
        metaBg->setPosition({CONTENT_PAD, metaBot});
        metaBg->setID("rebuild-block"_spr);
        m_mainLayer->addChild(metaBg);

        auto* metaMenu = paimon::ui::makeZeroMenu();
        metaMenu->setPosition({0.f, 0.f});
        metaMenu->setTouchPriority(popupMenuPrio());
        metaMenu->setID("rebuild-block"_spr);
        m_mainLayer->addChild(metaMenu, 6);

        float rightBound = CONTENT_PAD + innerW - 6.f;
        if (!levelIds.empty()) {
            int lvlId = levelIds.front();
            auto* copyFace = makeSmallPill("GJ_copyBtn_001.png", "", {38, 42, 54},
                paimon::ui::palette::muted, paimon::ui::palette::text, 150, 17.f);
            auto* copyBtn = CCMenuItemExt::createSpriteExtra(copyFace, [lvlId, es](CCMenuItemSpriteExtra*) {
                if (geode::utils::clipboard::write(std::to_string(lvlId))) {
                    PaimonNotify::show(
                        es ? fmt::format("ID {} copiado", lvlId).c_str()
                           : fmt::format("Copied ID {}", lvlId).c_str(),
                        NotificationIcon::Success
                    );
                }
            });
            copyBtn->setPosition({rightBound - copyFace->getContentSize().width / 2.f, metaBot + kMetaH / 2.f});
            metaMenu->addChild(copyBtn);
            rightBound -= copyFace->getContentSize().width + 4.f;

            auto* openFace = makeSmallPill("GJ_playBtn2_001.png",
                fmt::format("{} #{}", es ? "Nivel" : "Level", lvlId),
                {18, 92, 114}, paimon::ui::palette::info, paimon::ui::palette::text, 175, 17.f);
            auto* openBtn = CCMenuItemExt::createSpriteExtra(openFace, [lvlId](CCMenuItemSpriteExtra*) {
                paimon::forum::openLevelById(lvlId);
            });
            openBtn->setPosition({rightBound - openFace->getContentSize().width / 2.f, metaBot + kMetaH / 2.f});
            metaMenu->addChild(openBtn);
            rightBound -= openFace->getContentSize().width + 6.f;
        }

        float tx = CONTENT_PAD + 6.f;
        for (auto const& rawTag : m_post.tags) {
            std::string tag = paimon::forum::displayTag(rawTag);
            auto* chipFace = makeSmallPill(nullptr, tag, {24, 52, 92},
                paimon::ui::palette::info, {170, 225, 255}, 150, 16.f);
            float cw = chipFace->getContentSize().width;
            if (tx + cw > rightBound) break;
            auto* chipBtn = CCMenuItemExt::createSpriteExtra(chipFace, [self, tag](CCMenuItemSpriteExtra*) {
                auto popup = self.lock();
                if (!popup) return;
                if (popup->m_callbacks.onFilterTag) {
                    auto fn = popup->m_callbacks.onFilterTag;
                    popup->onClose(nullptr);
                    fn(tag);
                }
            });
            chipBtn->setPosition({tx + cw / 2.f, metaBot + kMetaH / 2.f});
            metaMenu->addChild(chipBtn);
            tx += cw + 4.f;
        }

        cursorTop = metaBot - 6.f;
    }

    // -------------------------------------------------------------------------
    // 3. Description Area (MDTextArea inside dark inset)
    // -------------------------------------------------------------------------
    float const descH = hasMetaStrip ? 56.f : 72.f;
    float const descBot = cursorTop - descH;
    {
        auto* descBg = paimon::ui::makeInset({innerW, descH}, 75);
        descBg->setPosition({CONTENT_PAD, descBot});
        descBg->setID("rebuild-block"_spr);
        m_mainLayer->addChild(descBg);

        std::string bodyText = m_post.description.empty()
            ? (es ? "*(sin descripcion)*" : "*(no description)*")
            : m_post.description;
        if (auto* desc = MDTextArea::create(bodyText, {innerW - 14.f, descH - 6.f})) {
            desc->setAnchorPoint({0.f, 0.f});
            desc->setPosition({CONTENT_PAD + 7.f, descBot + 3.f});
            desc->setID("rebuild-block"_spr);
            m_mainLayer->addChild(desc, 2);
        }
    }

    // -------------------------------------------------------------------------
    // 4. Action Bar (Like, Bookmark, Copy, Refresh, Report, Delete)
    // -------------------------------------------------------------------------
    constexpr float kActionH = 24.f;
    float const actionY = descBot - 6.f - kActionH / 2.f;
    {
        auto* acc = GJAccountManager::get();
        int const myId = acc ? acc->m_accountID : 0;
        bool const canDelete = myId > 0 && myId == m_post.author.accountID;
        bool const saved = paimon::forum::isBookmarked(m_post.id);

        auto* bar = CCMenu::create();
        bar->setID("rebuild-block"_spr);
        bar->setContentSize({innerW, kActionH});
        bar->setAnchorPoint({0.f, 0.5f});
        bar->setPosition({CONTENT_PAD, actionY});
        bar->setTouchPriority(popupMenuPrio());
        bar->setLayout(
            RowLayout::create()
                ->setGap(5.f)
                ->setAutoScale(false)
                ->setAxisAlignment(AxisAlignment::Start)
        );
        m_mainLayer->addChild(bar, 6);

        auto* likeFace = makeSmallPill(
            "GJ_likesIcon_001.png",
            fmt::format("{} {}", m_post.likedByMe ? (es ? "Te gusta" : "Liked") : (es ? "Me gusta" : "Like"), m_post.likes),
            m_post.likedByMe ? ccColor3B{130, 38, 72} : ccColor3B{36, 40, 52},
            m_post.likedByMe ? ccColor3B{255, 130, 175} : paimon::ui::palette::muted,
            m_post.likedByMe ? ccColor3B{255, 210, 225} : paimon::ui::palette::text,
            175, 20.f
        );
        bar->addChild(CCMenuItemSpriteExtra::create(likeFace, this, menu_selector(PostDetailPopup::onLikePost)));

        auto* saveFace = makeSmallPill(
            "GJ_starsIcon_001.png",
            saved ? (es ? "Guardado" : "Saved") : (es ? "Guardar" : "Save"),
            saved ? ccColor3B{120, 92, 18} : ccColor3B{36, 40, 52},
            saved ? paimon::ui::palette::gold : paimon::ui::palette::muted,
            saved ? paimon::ui::palette::gold : paimon::ui::palette::text,
            165, 20.f
        );
        bar->addChild(CCMenuItemSpriteExtra::create(saveFace, this, menu_selector(PostDetailPopup::onBookmarkPost)));

        auto* copyFace = makeSmallPill(
            "GJ_copyBtn_001.png",
            es ? "Copiar" : "Copy",
            {36, 40, 52}, paimon::ui::palette::info, paimon::ui::palette::text, 150, 20.f
        );
        bar->addChild(CCMenuItemSpriteExtra::create(copyFace, this, menu_selector(PostDetailPopup::onCopyPost)));

        auto* refreshFace = makeSmallPill(
            "GJ_updateBtn_001.png",
            es ? "Actualizar" : "Refresh",
            {36, 40, 52}, paimon::ui::palette::success, paimon::ui::palette::text, 150, 20.f
        );
        bar->addChild(CCMenuItemExt::createSpriteExtra(refreshFace, [self](CCMenuItemSpriteExtra*) {
            if (auto p = self.lock()) p->refreshFromServer();
        }));

        auto* reportFace = makeSmallPill(
            "GJ_reportBtn_001.png",
            es ? "Reportar" : "Report",
            {62, 34, 34}, paimon::ui::palette::warning, paimon::ui::palette::warning, 150, 20.f
        );
        bar->addChild(CCMenuItemSpriteExtra::create(reportFace, this, menu_selector(PostDetailPopup::onReportPost)));

        if (canDelete) {
            auto* delFace = makeSmallPill(
                "GJ_trashBtn_001.png",
                es ? "Borrar" : "Delete",
                {115, 32, 32}, paimon::ui::palette::danger, paimon::ui::palette::danger, 175, 20.f
            );
            bar->addChild(CCMenuItemSpriteExtra::create(delFace, this, menu_selector(PostDetailPopup::onDeletePost)));
        }

        bar->updateLayout();
    }

    // -------------------------------------------------------------------------
    // 5. Replies Header (Count + Sort Toggle)
    // -------------------------------------------------------------------------
    constexpr float kReplyLblH = 18.f;
    float const replyLblBot = actionY - kActionH / 2.f - 5.f - kReplyLblH;
    {
        auto* repliesLbl = CCLabelBMFont::create(
            fmt::format("{} ({})", es ? "Respuestas" : "Replies", static_cast<int>(m_post.replies.size())).c_str(),
            "goldFont.fnt"
        );
        repliesLbl->setScale(0.42f);
        repliesLbl->setAnchorPoint({0.f, 0.5f});
        repliesLbl->setPosition({CONTENT_PAD, replyLblBot + kReplyLblH / 2.f});
        repliesLbl->setID("rebuild-block"_spr);
        m_mainLayer->addChild(repliesLbl);

        auto* sortMenu = CCMenu::create();
        sortMenu->setID("rebuild-block"_spr);
        sortMenu->setContentSize({200.f, kReplyLblH});
        sortMenu->setAnchorPoint({1.f, 0.5f});
        sortMenu->setPosition({contentSize.width - CONTENT_PAD, replyLblBot + kReplyLblH / 2.f});
        sortMenu->setTouchPriority(popupMenuPrio());
        sortMenu->setLayout(
            RowLayout::create()->setGap(4.f)->setAutoScale(false)->setAxisAlignment(AxisAlignment::End)
        );
        m_mainLayer->addChild(sortMenu, 6);

        char const* sortNamesEn[] = {"Oldest", "Newest", "Top"};
        char const* sortNamesEs[] = {"Antiguas", "Nuevas", "Top"};
        for (int s = 0; s < 3; ++s) {
            bool active = (m_replySort == s);
            auto* pill = makeSmallPill(
                nullptr,
                es ? sortNamesEs[s] : sortNamesEn[s],
                active ? ccColor3B{24, 108, 128} : ccColor3B{34, 38, 48},
                paimon::ui::palette::text,
                active ? paimon::ui::palette::text : paimon::ui::palette::muted,
                active ? 185 : 130, 15.f
            );
            auto* btn = CCMenuItemExt::createSpriteExtra(pill, [self, s](CCMenuItemSpriteExtra*) {
                auto p = self.lock();
                if (!p || p->m_replySort == s) return;
                p->m_replySort = s;
                p->rebuild();
            });
            sortMenu->addChild(btn);
        }
        sortMenu->updateLayout();
    }

    // -------------------------------------------------------------------------
    // 6. Composer Layout Metrics & Replies Scroll
    // -------------------------------------------------------------------------
    constexpr float kInputH = 28.f;
    float const inputBot = 10.f;
    bool const hasReplyBanner = !m_post.locked && !m_replyTo.empty();
    float const bannerH = hasReplyBanner ? 16.f : 0.f;
    float const scrollBot = inputBot + kInputH + bannerH + 6.f;
    float const scrollH = std::max(36.f, replyLblBot - scrollBot);
    float const scrollW = innerW - SCROLL_GUTTER;

    {
        auto* scrollBg = paimon::ui::makeInset({innerW, scrollH}, 85);
        scrollBg->setPosition({CONTENT_PAD, scrollBot});
        scrollBg->setID("rebuild-block"_spr);
        m_mainLayer->addChild(scrollBg);

        m_scroll = ScrollLayer::create({scrollW, scrollH});
        if (m_scroll) m_scroll->setAnchorPoint({0.f, 0.f});
        m_scroll->setPosition({CONTENT_PAD + 2.f, scrollBot});
        m_scroll->setStealingTouches(true);
        m_scroll->enableScrollWheel(false);
        m_scroll->setID("rebuild-block"_spr);
        m_mainLayer->addChild(m_scroll, 5);

        if (auto* bar = Scrollbar::create(m_scroll)) {
            bar->setContentSize({6.f, scrollH - 6.f});
            bar->setPosition({CONTENT_PAD + innerW - 5.f, scrollBot + scrollH / 2.f});
            bar->setID("rebuild-block"_spr);
            m_mainLayer->addChild(bar, 7);
        }

        auto threaded = paimon::forum::buildThreadedReplies(m_post.replies, m_replySort);
        std::vector<CCNode*> cards;
        float totalH = 4.f;
        constexpr float kGap = 4.f;
        int idx = 0;
        for (auto const& item : threaded) {
            float indent = item.depth > 0 ? 14.f : 0.f;
            auto* card = makeReplyCard(item.reply, item.depth, item.parentAuthor, scrollW - 4.f - indent, idx++);
            cards.push_back(card);
            totalH += card->getContentSize().height + kGap;
        }
        if (!cards.empty()) totalH -= kGap;
        totalH += 4.f;

        float const contentH = std::max(scrollH, totalH);
        m_scroll->m_contentLayer->setContentSize({scrollW, contentH});

        float y = contentH - 4.f;
        for (size_t i = 0; i < cards.size(); ++i) {
            float indent = threaded[i].depth > 0 ? 16.f : 2.f;
            float h = cards[i]->getContentSize().height;
            y -= h;
            cards[i]->setPosition({indent, y});
            m_scroll->m_contentLayer->addChild(cards[i]);
            y -= kGap;
        }
        m_scroll->moveToTop();

        if (threaded.empty()) {
            auto* empty = CCLabelBMFont::create(
                es ? "Aun no hay respuestas - se el primero!" : "No replies yet - be the first to chime in!",
                "chatFont.fnt"
            );
            empty->setScale(0.48f);
            empty->setColor(paimon::ui::palette::muted);
            empty->setPosition({scrollW / 2.f, scrollH / 2.f});
            m_scroll->m_contentLayer->addChild(empty);
        }
    }

    // -------------------------------------------------------------------------
    // 7. Reply Composer (or Locked Banner)
    // -------------------------------------------------------------------------
    if (!m_post.locked) {
        if (hasReplyBanner) {
            float const bannerY = inputBot + kInputH + 8.f;
            auto* bannerMenu = paimon::ui::makeZeroMenu();
            bannerMenu->setPosition({0.f, 0.f});
            bannerMenu->setTouchPriority(popupMenuPrio());
            bannerMenu->setID("rebuild-block"_spr);
            m_mainLayer->addChild(bannerMenu, 6);

            auto* bannerFace = makeSmallPill(
                "GJ_chatBtn_001.png",
                fmt::format("{} @{}  x", es ? "Respondiendo a" : "Replying to", m_replyToAuthor),
                {24, 68, 96}, paimon::ui::palette::info, paimon::ui::palette::info, 160, 14.f
            );
            auto* cancelThreadBtn = CCMenuItemExt::createSpriteExtra(bannerFace, [self](CCMenuItemSpriteExtra*) {
                auto p = self.lock();
                if (!p) return;
                p->m_replyTo.clear();
                p->m_replyToAuthor.clear();
                p->rebuild();
            });
            cancelThreadBtn->setPosition({CONTENT_PAD + bannerFace->getContentSize().width / 2.f, bannerY});
            bannerMenu->addChild(cancelThreadBtn);
        }

        float const sendBtnW = 66.f;
        float const counterW = 36.f;
        float const inputW = innerW - sendBtnW - counterW - 10.f;
        float const inputCenterY = inputBot + kInputH / 2.f;

        auto* inputBg = paimon::ui::makeInset({innerW - sendBtnW - 6.f, kInputH}, 65);
        inputBg->setPosition({CONTENT_PAD, inputBot});
        inputBg->setID("rebuild-block"_spr);
        m_mainLayer->addChild(inputBg);

        constexpr float kInputScale = 0.78f;
        m_replyInput = TextInput::create(inputW / kInputScale,
            es ? "Escribe una respuesta..." : "Write a reply...", "chatFont.fnt");
        m_replyInput->setCommonFilter(CommonFilter::TextEmojis);
        m_replyInput->setMaxCharCount(400);
        m_replyInput->setScale(kInputScale);
        m_replyInput->setPosition({CONTENT_PAD + 4.f + inputW / 2.f, inputCenterY});
        m_replyInput->setCallback([self](std::string const&) {
            if (auto p = self.lock()) p->updateReplyCounter();
        });
        m_replyInput->setID("rebuild-block"_spr);
        m_mainLayer->addChild(m_replyInput, 4);

        m_replyCharLabel = CCLabelBMFont::create("0/400", "chatFont.fnt");
        m_replyCharLabel->setScale(0.36f);
        m_replyCharLabel->setColor(paimon::ui::palette::dim);
        m_replyCharLabel->setAnchorPoint({1.f, 0.5f});
        m_replyCharLabel->setPosition({CONTENT_PAD + innerW - sendBtnW - 10.f, inputCenterY});
        m_replyCharLabel->setID("rebuild-block"_spr);
        m_mainLayer->addChild(m_replyCharLabel, 4);

        auto* sendBtn = paimon::ui::makeButton(
            es ? "Enviar" : "Reply",
            [self] {
                if (auto p = self.lock()) p->onSubmitReply(nullptr);
            },
            paimon::ui::Btn::Green, sendBtnW, 0.54f
        );
        sendBtn->setPosition({contentSize.width - CONTENT_PAD - sendBtnW / 2.f, inputCenterY});
        sendBtn->setID("rebuild-btn"_spr);
        m_buttonMenu->addChild(sendBtn);

        m_cooldownLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_cooldownLabel->setScale(0.38f);
        m_cooldownLabel->setAnchorPoint({1.f, 0.5f});
        m_cooldownLabel->setPosition({contentSize.width - CONTENT_PAD, inputBot + kInputH + 8.f});
        m_cooldownLabel->setColor(paimon::ui::palette::warning);
        m_cooldownLabel->setVisible(false);
        m_cooldownLabel->setID("rebuild-block"_spr);
        m_mainLayer->addChild(m_cooldownLabel, 10);
        updateCooldownLabel();
    } else {
        auto* lockedBg = paimon::ui::makeInset({innerW, kInputH}, 70, {48, 22, 22});
        lockedBg->setPosition({CONTENT_PAD, inputBot});
        lockedBg->setID("rebuild-block"_spr);
        m_mainLayer->addChild(lockedBg);

        auto* locked = CCLabelBMFont::create(
            es ? "Este post esta cerrado - respuestas desactivadas." : "This post is locked - replies are disabled.",
            "bigFont.fnt"
        );
        locked->setScale(0.30f);
        locked->setColor(paimon::ui::palette::danger);
        locked->setPosition({contentSize.width / 2.f, inputBot + kInputH / 2.f});
        locked->setID("rebuild-block"_spr);
        m_mainLayer->addChild(locked);
    }
}

CCNode* PostDetailPopup::makeAuthorRow(Author const& author, int64_t when, float w) {
    constexpr float h = 36.f;
    bool const es = isSpanish();

    auto* row = CCNode::create();
    row->setContentSize({w, h});
    row->setAnchorPoint({0.f, 1.f});

    auto* menu = paimon::ui::makeZeroMenu();
    menu->setContentSize({w, h});
    menu->ignoreAnchorPointForPosition(true);
    menu->setTouchPriority(popupMenuPrio());
    row->addChild(menu, 4);

    float const iconSize = 26.f;
    std::string const name = author.username.empty() ? (es ? "Anonimo" : "Anonymous") : author.username;
    auto* nameLbl = CCLabelBMFont::create(name.c_str(), "goldFont.fnt");
    nameLbl->setScale(0.46f);
    paimon::ui::shrinkLabelToFit(nameLbl, w * 0.40f);

    float const authorBtnW = iconSize + 12.f + nameLbl->getScaledContentSize().width;
    auto* authorFace = CCNode::create();
    authorFace->setContentSize({authorBtnW, h});
    authorFace->setAnchorPoint({0.5f, 0.5f});

    if (auto* icon = paimon::forum::makeAuthorIcon(author, iconSize)) {
        icon->setPosition({iconSize / 2.f + 4.f, h / 2.f});
        authorFace->addChild(icon, 2);
    }
    nameLbl->setAnchorPoint({0.f, 0.5f});
    nameLbl->setPosition({iconSize + 10.f, h / 2.f + 5.f});
    authorFace->addChild(nameLbl, 2);

    int const accountId = author.accountID;
    auto* authorBtn = CCMenuItemExt::createSpriteExtra(authorFace, [accountId](CCMenuItemSpriteExtra*) {
        paimon::forum::openAuthorProfile(accountId);
    });
    authorBtn->m_scaleMultiplier = 1.03f;
    authorBtn->setPosition({authorBtnW / 2.f + 4.f, h / 2.f});
    menu->addChild(authorBtn);

    std::string subLine = paimon::forum::formatTimeShort(when, es);
    std::string absTime = ForumApi::get().hasServer() ? paimon::forum::formatAbsoluteTime(when) : "";
    if (!absTime.empty()) subLine += "  (" + absTime + ")";
    auto* dateLbl = CCLabelBMFont::create(subLine.c_str(), "chatFont.fnt");
    dateLbl->setScale(0.42f);
    dateLbl->setColor(paimon::ui::palette::muted);
    dateLbl->setAnchorPoint({0.f, 0.5f});
    dateLbl->setPosition({iconSize + 14.f, h / 2.f - 8.f});
    row->addChild(dateLbl, 2);

    // Live author presence status + "More by author" filter button on right
    float rx = w - 6.f;
    if (m_callbacks.onFilterAuthor && accountId > 0) {
        WeakRef<PostDetailPopup> self = this;
        auto* moreFace = makeSmallPill(
            nullptr,
            es ? "Sus posts" : "Author posts",
            {34, 42, 64}, paimon::ui::palette::info, paimon::ui::palette::info, 150, 17.f
        );
        auto* moreBtn = CCMenuItemExt::createSpriteExtra(moreFace, [self, accountId, name](CCMenuItemSpriteExtra*) {
            auto popup = self.lock();
            if (!popup) return;
            if (popup->m_callbacks.onFilterAuthor) {
                auto fn = popup->m_callbacks.onFilterAuthor;
                popup->onClose(nullptr);
                fn(accountId, name);
            }
        });
        moreBtn->setPosition({rx - moreFace->getContentSize().width / 2.f, h / 2.f});
        menu->addChild(moreBtn);
        rx -= moreFace->getContentSize().width + 8.f;
    }

    m_authorStatusLabel = CCLabelBMFont::create(m_authorStatusText.c_str(), "bigFont.fnt");
    m_authorStatusLabel->setScale(0.23f);
    m_authorStatusLabel->setColor(m_authorStatusColor);
    m_authorStatusLabel->setAnchorPoint({1.f, 0.5f});
    m_authorStatusLabel->setPosition({rx, h / 2.f});
    row->addChild(m_authorStatusLabel, 2);

    return row;
}

CCNode* PostDetailPopup::makeReplyCard(
    Reply const& r,
    int depth,
    std::string const& parentAuthor,
    float w,
    int index
) {
    bool const es = isSpanish();
    auto* acc = GJAccountManager::get();
    int const myId = acc ? acc->m_accountID : 0;
    bool const isMine = myId > 0 && myId == r.author.accountID;

    constexpr float kHeaderH = 20.f;
    constexpr float kFooterH = 20.f;
    constexpr float kPad = 6.f;

    std::string bodyText = r.content.empty() ? " " : r.content;
    float const wrapW = std::max(80.f, w - 20.f);
    auto* content = CCLabelBMFont::create(bodyText.c_str(), "chatFont.fnt", wrapW / 0.48f, kCCTextAlignmentLeft);
    content->setScale(0.48f);
    content->setColor({245, 245, 250});
    content->setAnchorPoint({0.f, 1.f});

    float const bodyH = std::max(16.f, content->getScaledContentSize().height + 2.f);
    float const h = kPad + kHeaderH + 3.f + bodyH + 4.f + kFooterH + kPad;

    auto* card = CCNode::create();
    card->setContentSize({w, h});
    card->setAnchorPoint({0.f, 0.f});

    ccColor3B bgCol = depth > 0 ? ccColor3B{14, 24, 38} : (index % 2 == 0 ? ccColor3B{0, 0, 0} : ccColor3B{12, 14, 20});
    GLubyte bgAlpha = depth > 0 ? 110 : (index % 2 == 0 ? 90 : 70);
    card->addChild(paimon::ui::makeInset({w, h}, bgAlpha, bgCol), -1);

    if (depth > 0) {
        auto* threadBar = CCLayerColor::create({100, 210, 255, 200}, 2.f, h - 6.f);
        threadBar->setPosition({2.f, 3.f});
        card->addChild(threadBar, 2);
    } else if (isMine) {
        auto* selfBar = CCLayerColor::create({130, 255, 130, 210}, 2.f, h - 6.f);
        selfBar->setPosition({2.f, 3.f});
        card->addChild(selfBar, 2);
    }

    auto* menu = paimon::ui::makeZeroMenu();
    menu->setContentSize({w, h});
    menu->ignoreAnchorPointForPosition(true);
    menu->setTouchPriority(popupMenuPrio());
    card->addChild(menu, 6);

    float const row1Y = h - kPad - kHeaderH / 2.f;
    float const iconSize = 16.f;
    std::string const authorName = r.author.username.empty() ? (es ? "Anonimo" : "Anonymous") : r.author.username;

    auto* nameLbl = CCLabelBMFont::create(authorName.c_str(), "goldFont.fnt");
    nameLbl->setScale(0.32f);
    paimon::ui::shrinkLabelToFit(nameLbl, w * 0.40f);

    float const authorBtnW = iconSize + 8.f + nameLbl->getScaledContentSize().width;
    auto* authorFace = CCNode::create();
    authorFace->setContentSize({authorBtnW, kHeaderH});
    authorFace->setAnchorPoint({0.5f, 0.5f});

    if (auto* icon = paimon::forum::makeAuthorIcon(r.author, iconSize)) {
        icon->setPosition({iconSize / 2.f + 2.f, kHeaderH / 2.f});
        authorFace->addChild(icon, 2);
    }
    nameLbl->setAnchorPoint({0.f, 0.5f});
    nameLbl->setPosition({iconSize + 6.f, kHeaderH / 2.f});
    authorFace->addChild(nameLbl, 2);

    int const replyAccId = r.author.accountID;
    auto* authorBtn = CCMenuItemExt::createSpriteExtra(authorFace, [replyAccId](CCMenuItemSpriteExtra*) {
        paimon::forum::openAuthorProfile(replyAccId);
    });
    authorBtn->m_scaleMultiplier = 1.04f;
    authorBtn->setPosition({8.f + authorBtnW / 2.f, row1Y});
    menu->addChild(authorBtn);

    float badgeX = 12.f + authorBtnW;
    if (isMine) {
        auto* you = CCLabelBMFont::create(es ? "(tu)" : "(you)", "chatFont.fnt");
        you->setScale(0.38f);
        you->setColor(paimon::ui::palette::success);
        you->setAnchorPoint({0.f, 0.5f});
        you->setPosition({badgeX, row1Y});
        card->addChild(you, 2);
        badgeX += you->getScaledContentSize().width + 5.f;
    }

    if (depth > 0 && !parentAuthor.empty()) {
        auto* replyToLbl = CCLabelBMFont::create(fmt::format("-> @{}", parentAuthor).c_str(), "chatFont.fnt");
        replyToLbl->setScale(0.36f);
        replyToLbl->setColor(paimon::ui::palette::info);
        replyToLbl->setAnchorPoint({0.f, 0.5f});
        replyToLbl->setPosition({badgeX, row1Y});
        paimon::ui::shrinkLabelToFit(replyToLbl, std::max(30.f, w - badgeX - 58.f));
        card->addChild(replyToLbl, 2);
    }

    auto* when = CCLabelBMFont::create(paimon::forum::formatTimeShort(r.createdAt, es).c_str(), "chatFont.fnt");
    when->setScale(0.38f);
    when->setColor(paimon::ui::palette::dim);
    when->setAnchorPoint({1.f, 0.5f});
    when->setPosition({w - 8.f, row1Y});
    card->addChild(when, 2);

    content->setPosition({10.f, h - kPad - kHeaderH - 3.f});
    card->addChild(content, 2);

    // Bottom action row inside card
    auto* actionsMenu = CCMenu::create();
    actionsMenu->setContentSize({w - 16.f, kFooterH});
    actionsMenu->setAnchorPoint({0.f, 0.f});
    actionsMenu->setPosition({8.f, kPad});
    actionsMenu->ignoreAnchorPointForPosition(false);
    actionsMenu->setTouchPriority(popupMenuPrio());
    actionsMenu->setLayout(
        RowLayout::create()->setGap(5.f)->setAxisAlignment(AxisAlignment::Start)->setAutoScale(false)
    );
    card->addChild(actionsMenu, 8);

    WeakRef<PostDetailPopup> self = this;
    std::string const replyId = r.id;

    {
        auto* face = makeSmallPill(
            "GJ_likesIcon_001.png",
            std::to_string(r.likes),
            r.likedByMe ? ccColor3B{125, 36, 68} : ccColor3B{32, 36, 46},
            r.likedByMe ? ccColor3B{255, 130, 175} : paimon::ui::palette::muted,
            r.likedByMe ? ccColor3B{255, 200, 220} : paimon::ui::palette::text,
            155, 16.f
        );
        actionsMenu->addChild(CCMenuItemExt::createSpriteExtra(face, [self, replyId](CCMenuItemSpriteExtra*) {
            if (auto p = self.lock()) p->onLikeReplyById(replyId);
        }));
    }
    if (!m_post.locked) {
        auto* face = makeSmallPill(
            "GJ_chatBtn_001.png",
            es ? "Responder" : "Reply",
            {28, 52, 82}, paimon::ui::palette::info, paimon::ui::palette::text, 145, 16.f
        );
        actionsMenu->addChild(CCMenuItemExt::createSpriteExtra(face, [self, replyId, authorName](CCMenuItemSpriteExtra*) {
            if (auto p = self.lock()) p->onReplyToReply(replyId, authorName);
        }));
    }
    {
        auto* face = makeSmallPill(
            "GJ_reportBtn_001.png",
            es ? "Reportar" : "Report",
            {58, 34, 34}, paimon::ui::palette::warning, paimon::ui::palette::warning, 140, 16.f
        );
        actionsMenu->addChild(CCMenuItemExt::createSpriteExtra(face, [self, replyId](CCMenuItemSpriteExtra*) {
            if (auto p = self.lock()) p->onReportReplyById(replyId);
        }));
    }
    if (isMine) {
        auto* face = makeSmallPill(
            "GJ_trashBtn_001.png",
            es ? "Borrar" : "Delete",
            {115, 32, 32}, paimon::ui::palette::danger, paimon::ui::palette::danger, 165, 16.f
        );
        actionsMenu->addChild(CCMenuItemExt::createSpriteExtra(face, [self, replyId](CCMenuItemSpriteExtra*) {
            if (auto p = self.lock()) p->onDeleteReplyById(replyId);
        }));
    }

    actionsMenu->updateLayout();
    return card;
}

void PostDetailPopup::onLikePost(CCObject*) {
    auto* acc = GJAccountManager::get();
    if (!acc || acc->m_accountID <= 0) {
        PaimonNotify::show(
            isSpanish() ? "Inicia sesion en GD para dar like." : "Sign in to GD to like posts.",
            NotificationIcon::Warning
        );
        return;
    }
    auto flip = [](Post& p) {
        p.likedByMe = !p.likedByMe;
        p.likes = std::max(0, p.likes + (p.likedByMe ? 1 : -1));
    };
    flip(m_post);
    if (m_callbacks.onChanged) m_callbacks.onChanged();
    rebuild();

    auto postId = m_post.id;
    WeakRef<PostDetailPopup> self = this;
    ForumApi::get().togglePostLike(postId, [self, flip](paimon::forum::Result<bool> res) {
        auto popup = self.lock();
        if (!popup) return;
        if (!res.ok) {
            flip(popup->m_post);
            if (popup->m_callbacks.onChanged) popup->m_callbacks.onChanged();
            popup->rebuild();
            PaimonNotify::show(
                res.error.empty() ? (isSpanish() ? "Error al dar like" : "Failed to like") : res.error.c_str(),
                NotificationIcon::Warning
            );
        }
    });
}

void PostDetailPopup::onBookmarkPost(CCObject*) {
    bool saved = paimon::forum::toggleBookmark(m_post.id);
    PaimonNotify::show(
        saved
            ? (isSpanish() ? "Guardado en favoritos" : "Saved to bookmarks")
            : (isSpanish() ? "Quitado de guardados" : "Removed from bookmarks"),
        NotificationIcon::Info
    );
    if (m_callbacks.onChanged) m_callbacks.onChanged();
    rebuild();
}

void PostDetailPopup::onCopyPost(CCObject*) {
    std::string payload = m_post.title;
    if (!m_post.description.empty()) payload += "\n\n" + m_post.description;
    if (geode::utils::clipboard::write(payload)) {
        PaimonNotify::show(
            isSpanish() ? "Publicacion copiada al portapapeles" : "Post copied to clipboard",
            NotificationIcon::Success
        );
    }
}

void PostDetailPopup::submitReport(std::string const& targetId, bool isReply) {
    bool const es = isSpanish();
    WeakRef<PostDetailPopup> self = this;
    auto* popup = ReportReasonPopup::create(isReply, [self, targetId, isReply, es](std::string const& reason) {
        auto done = [es](paimon::forum::Result<bool> res) {
            PaimonNotify::show(
                res.ok
                    ? (es ? "Reporte enviado" : "Report submitted")
                    : (es ? "No se pudo reportar (inicia sesion)" : "Report failed (login required)"),
                res.ok ? NotificationIcon::Success : NotificationIcon::Warning
            );
        };
        if (isReply) ForumApi::get().reportReply(targetId, reason, done);
        else ForumApi::get().reportPost(targetId, reason, done);
    });
    if (popup) paimon::configkit::showAbove(popup, this);
}

void PostDetailPopup::onReportPost(CCObject*) {
    submitReport(m_post.id, false);
}

void PostDetailPopup::onDeletePost(CCObject*) {
    bool const es = isSpanish();
    WeakRef<PostDetailPopup> self = this;
    std::string postId = m_post.id;
    auto* alert = geode::createQuickPopup(
        es ? "Borrar publicacion" : "Delete Post",
        es ? "Seguro que quieres borrar esta publicacion?\nNo se puede deshacer."
           : "Are you sure you want to delete this post?\nThis cannot be undone.",
        es ? "Cancelar" : "Cancel",
        es ? "Borrar" : "Delete",
        [self, postId, es](auto*, bool confirmed) {
            if (!confirmed) return;
            ForumApi::get().deletePost(postId, [self, es](paimon::forum::Result<bool> res) {
                auto popup = self.lock();
                if (!popup) return;
                if (!res.ok || !res.data) {
                    PaimonNotify::show(es ? "No se pudo borrar" : "Failed to delete post", NotificationIcon::Error);
                    return;
                }
                if (popup->m_callbacks.onChanged) popup->m_callbacks.onChanged();
                PaimonNotify::show(es ? "Publicacion borrada" : "Post deleted", NotificationIcon::Success);
                popup->onClose(nullptr);
            });
        },
        false
    );
    paimon::configkit::showAbove(alert, this);
}

void PostDetailPopup::onSubmitReply(CCObject*) {
    if (!m_replyInput || m_submittingReply) return;
    bool const es = isSpanish();

    auto* acc = GJAccountManager::get();
    if (!acc || acc->m_accountID <= 0) {
        PaimonNotify::show(es ? "Inicia sesion en GD para responder" : "Sign in to GD to reply", NotificationIcon::Warning);
        return;
    }

    std::string content = paimon::forum::trimAscii(m_replyInput->getString());
    if (content.empty()) {
        PaimonNotify::show(es ? "Escribe algo primero" : "Type something first", NotificationIcon::Warning);
        return;
    }

    auto cd = ForumApi::get().getReplyCooldownRemaining();
    if (cd > 0) {
        PaimonNotify::show(
            fmt::format(fmt::runtime(es ? "Espera {}s antes de responder." : "Wait {}s before replying again."), cd).c_str(),
            NotificationIcon::Warning
        );
        return;
    }

    m_submittingReply = true;
    paimon::forum::CreateReplyRequest req;
    req.postId = m_post.id;
    req.parentReplyId = m_replyTo;
    req.content = content;

    WeakRef<PostDetailPopup> self = this;
    auto postId = m_post.id;
    ForumApi::get().createReply(req, [self, postId, es](paimon::forum::Result<Reply> res) {
        auto popup = self.lock();
        if (!popup) return;
        if (!res.ok) {
            popup->m_submittingReply = false;
            if (res.error.find("Rate limited") != std::string::npos) {
                PaimonNotify::show(
                    es ? "Vas muy rapido. Espera un momento." : "You're replying too fast. Please wait.",
                    NotificationIcon::Warning
                );
            } else {
                PaimonNotify::show(es ? "Error al responder" : "Failed to reply", NotificationIcon::Error);
            }
            return;
        }
        ForumApi::get().getPost(postId, [self, es](paimon::forum::Result<Post> r2) {
            auto popup2 = self.lock();
            if (!popup2) return;
            popup2->m_submittingReply = false;
            if (r2.ok) popup2->m_post = r2.data;
            popup2->m_replyTo.clear();
            popup2->m_replyToAuthor.clear();
            if (popup2->m_callbacks.onChanged) popup2->m_callbacks.onChanged();
            PaimonNotify::show(es ? "Respuesta publicada!" : "Reply posted!", NotificationIcon::Success);
            popup2->rebuild();
        });
    });
}

void PostDetailPopup::onLikeReplyById(std::string id) {
    auto* acc = GJAccountManager::get();
    if (!acc || acc->m_accountID <= 0) {
        PaimonNotify::show(isSpanish() ? "Inicia sesion en GD para dar like." : "Sign in to GD to like replies.", NotificationIcon::Warning);
        return;
    }

    WeakRef<PostDetailPopup> self = this;
    auto postId = m_post.id;
    ForumApi::get().toggleReplyLike(id, [self, postId](paimon::forum::Result<bool> res) {
        auto popup = self.lock();
        if (!popup) return;
        if (!res.ok) {
            PaimonNotify::show(
                res.error.empty() ? (isSpanish() ? "Error al dar like" : "Failed to like") : res.error.c_str(),
                NotificationIcon::Warning
            );
            return;
        }
        ForumApi::get().getPost(postId, [self](paimon::forum::Result<Post> r) {
            auto popup2 = self.lock();
            if (!popup2) return;
            if (r.ok) popup2->m_post = r.data;
            if (popup2->m_callbacks.onChanged) popup2->m_callbacks.onChanged();
            popup2->rebuild();
        });
    });
}

void PostDetailPopup::onReportReplyById(std::string id) {
    submitReport(id, true);
}

void PostDetailPopup::onDeleteReplyById(std::string id) {
    bool const es = isSpanish();
    WeakRef<PostDetailPopup> self = this;
    auto postId = m_post.id;
    auto* alert = geode::createQuickPopup(
        es ? "Borrar respuesta" : "Delete Reply",
        es ? "Eliminar esta respuesta?" : "Delete this reply?",
        es ? "Cancelar" : "Cancel",
        es ? "Borrar" : "Delete",
        [self, id, postId, es](auto*, bool confirmed) {
            if (!confirmed) return;
            ForumApi::get().deleteReply(id, [self, postId, es](paimon::forum::Result<bool> res) {
                auto popup = self.lock();
                if (!popup) return;
                if (!res.ok || !res.data) {
                    PaimonNotify::show(es ? "No se pudo borrar" : "Failed to delete reply", NotificationIcon::Error);
                    return;
                }
                ForumApi::get().getPost(postId, [self](paimon::forum::Result<Post> r) {
                    auto popup2 = self.lock();
                    if (!popup2) return;
                    if (r.ok) popup2->m_post = r.data;
                    if (popup2->m_callbacks.onChanged) popup2->m_callbacks.onChanged();
                    popup2->rebuild();
                });
            });
        },
        false
    );
    paimon::configkit::showAbove(alert, this);
}

void PostDetailPopup::onReplyToReply(std::string id, std::string authorName) {
    m_replyTo = std::move(id);
    m_replyToAuthor = std::move(authorName);
    std::string preservedText = m_replyInput ? m_replyInput->getString() : "";
    rebuild();
    if (m_replyInput) {
        if (!preservedText.empty()) m_replyInput->setString(preservedText);
        updateReplyCounter();
        m_replyInput->focus();
    }
}

void PostDetailPopup::updateReplyCounter() {
    if (!m_replyCharLabel || !m_replyInput) return;
    size_t len = m_replyInput->getString().size();
    m_replyCharLabel->setString(fmt::format("{}/400", len).c_str());
    m_replyCharLabel->setColor(len >= 380 ? paimon::ui::palette::warning : paimon::ui::palette::dim);
}

void PostDetailPopup::updateCooldownLabel() {
    if (!m_cooldownLabel) return;
    auto cd = ForumApi::get().getReplyCooldownRemaining();
    if (cd > 0) {
        m_cooldownLabel->setString(
            fmt::format(fmt::runtime(isSpanish() ? "Espera {}s" : "Wait {}s"), cd).c_str()
        );
        m_cooldownLabel->setVisible(true);
    } else {
        m_cooldownLabel->setString("");
        m_cooldownLabel->setVisible(false);
    }
}

bool PostDetailPopup::scrollWheel(float x, float y) {
    if (paimon::configkit::queueWheelScroll(m_scroll, x, y, m_scrollTarget, m_scrollTargetSet)) {
        return true;
    }
    return PaimonPopup::scrollWheel(x, y);
}

void PostDetailPopup::onExit() {
    this->unscheduleUpdate();
    paimon::ui::detachGeodeTextInput(m_replyInput);
    Popup::onExit();
}

void PostDetailPopup::update(float dt) {
    paimon::configkit::stepWheelScroll(m_scroll, m_scrollTarget, m_scrollTargetSet, dt);
    updateCooldownLabel();
}

PostDetailPopup* PostDetailPopup::create(Post const& post, CopyableFunction<void()> onChanged) {
    PostDetailCallbacks cb;
    cb.onChanged = std::move(onChanged);
    return create(post, std::move(cb));
}

PostDetailPopup* PostDetailPopup::create(Post const& post, PostDetailCallbacks callbacks) {
    if (!paimon::modules::isEnabled("paimbnails.forum.menu")) return nullptr;
    auto* ret = new PostDetailPopup();
    if (ret && ret->init(post, std::move(callbacks))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}
