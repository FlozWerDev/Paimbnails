#include "PostDetailPopup.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/ui/MDTextArea.hpp>
#include "../../../utils/SpriteHelper.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using paimon::forum::ForumApi;
using paimon::forum::Post;
using paimon::forum::Reply;
using paimon::forum::Author;

namespace {
    constexpr float POPUP_W = 440.f;
    constexpr float POPUP_H = 320.f;
    constexpr float SCROLL_W = 410.f;

    // construye el sprite de icono (SimplePlayer) del autor
    static SimplePlayer* makeAuthorIcon(Author const& a, float targetSize) {
        auto* gm = GameManager::get();
        int iconID = std::max(1, a.iconID);
        auto* player = SimplePlayer::create(iconID);
        if (!player) return nullptr;
        if (a.iconType > 0) {
            player->updatePlayerFrame(iconID, static_cast<IconType>(a.iconType));
        }
        if (gm) {
            auto col1 = gm->colorForIdx(a.color1);
            auto col2 = gm->colorForIdx(a.color2);
            player->setColor(col1);
            player->setSecondColor(col2);
            if (a.glowEnabled) player->setGlowOutline(col2);
            else               player->disableGlowOutline();
        }
        float maxDim = std::max(player->getContentSize().width, player->getContentSize().height);
        // SimplePlayer contentSize es poco confiable: incluye glow/hitbox/areas vacias.
        // Usamos un tamano de referencia empirico (~30px base) para evitar iconos
        // microscopicos cuando contentSize es anomalo.
        float gdRefSize = 30.f;
        float scale = (maxDim > 10.f && maxDim < 80.f) ? (targetSize / maxDim) : (targetSize / gdRefSize);
        player->setScale(std::max(scale, 0.55f));
        return player;
    }

    static CCNode* makeDarkPanel(float w, float h, GLubyte alpha = 70) {
        auto bg = CCLayerColor::create(ccc4(0, 0, 0, alpha));
        bg->setContentSize({w, h});
        bg->ignoreAnchorPointForPosition(false);
        bg->setAnchorPoint({0.f, 0.f});
        return bg;
    }
}

bool PostDetailPopup::init(Post const& post, CopyableFunction<void()> onChanged) {
    if (!Popup::init(POPUP_W, POPUP_H)) return false;
    m_post = post;
    m_onChanged = std::move(onChanged);

    this->setTitle(m_post.title.c_str());

    rebuild();
    paimon::markDynamicPopup(this);
    this->scheduleUpdate();
    return true;
}

void PostDetailPopup::rebuild() {
    auto contentSize = m_mainLayer->getContentSize();
    float cx = contentSize.width / 2.f;

    // Limpia children previos (excepto titulo y close — los maneja Popup).
    // Recolectamos primero para evitar invalidar el iterador.
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

    // Layout vertical (top → bottom):
    //   header (autor + fecha) ........ 22 px
    //   tags row (opcional) ........... 14 px
    //   description box ............... 44 px
    //   action buttons row ............ 22 px
    //   replies label ................. 14 px
    //   replies scroll ................ resto
    //   reply input row ............... 32 px
    constexpr float kRowGap = 6.f;
    constexpr float kHeaderH = 28.f;
    constexpr float kTagsH   = 14.f;
    constexpr float kDescH   = 46.f;
    constexpr float kActionH = 24.f;
    constexpr float kReplyLblH = 14.f;
    constexpr float kInputH  = 30.f;

    // ── Header: autor + fecha ──────────────────────────────────────────
    // Dejamos margen para que el boton de cerrar (X) del Popup no tape la fila
    float headerBot = contentSize.height - kHeaderH - 32.f;
    auto headerRow = makeAuthorRow(m_post.author, m_post.createdAt, contentSize.width - 36.f);
    headerRow->setPosition({18.f, headerBot + kHeaderH});
    headerRow->setID("rebuild-block"_spr);
    m_mainLayer->addChild(headerRow);

    // ── Tags row ───────────────────────────────────────────────────────
    float tagsBot = headerBot - kRowGap - kTagsH;
    if (!m_post.tags.empty()) {
        auto tagRow = CCNode::create();
        tagRow->setContentSize({contentSize.width - 24.f, kTagsH});
        tagRow->setAnchorPoint({0.f, 0.f});
        tagRow->setPosition({12.f, tagsBot});
        tagRow->setID("rebuild-block"_spr);
        float x = 0.f;
        for (auto const& tag : m_post.tags) {
            auto chip = ButtonSprite::create(tag.c_str(), "bigFont.fnt", "GJ_button_05.png", 0.7f);
            chip->setScale(0.24f);
            chip->setAnchorPoint({0.f, 0.5f});
            chip->setPosition({x, kTagsH / 2.f});
            tagRow->addChild(chip);
            x += chip->getScaledContentSize().width + 4.f;
            if (x > contentSize.width - 24.f) break;
        }
        m_mainLayer->addChild(tagRow);
    } else {
        tagsBot = headerBot;
    }

    // ── Description box ────────────────────────────────────────────────
    float descBot = tagsBot - kRowGap - kDescH;
    {
        auto descBg = makeDarkPanel(contentSize.width - 24.f, kDescH, 60);
        descBg->setPosition({12.f, descBot});
        descBg->setID("rebuild-block"_spr);
        m_mainLayer->addChild(descBg);

        auto desc = MDTextArea::create(
            m_post.description.empty() ? "*(no description)*" : m_post.description,
            {contentSize.width - 36.f, kDescH}
        );
        if (desc) {
            desc->setAnchorPoint({0.f, 0.f});
            desc->setPosition({18.f, descBot});
            desc->setID("rebuild-block"_spr);
            m_mainLayer->addChild(desc);
        }
    }

    // ── Action buttons (like / report / delete) ────────────────────────
    float actionY = descBot - kRowGap - kActionH / 2.f;
    float btnX = 18.f;

    {
        std::string likeText = fmt::format("{} {}", m_post.likedByMe ? "Liked" : "Like", m_post.likes);
        auto likeSpr = ButtonSprite::create(likeText.c_str(), "bigFont.fnt",
            m_post.likedByMe ? "GJ_button_01.png" : "GJ_button_04.png", 0.8f);
        likeSpr->setScale(0.36f);
        auto likeBtn = CCMenuItemSpriteExtra::create(likeSpr, this, menu_selector(PostDetailPopup::onLikePost));
        likeBtn->setPosition({btnX + likeSpr->getScaledContentSize().width / 2.f, actionY});
        likeBtn->setID("rebuild-btn"_spr);
        m_buttonMenu->addChild(likeBtn);
        btnX += likeSpr->getScaledContentSize().width + 10.f;
    }
    {
        auto reportSpr = ButtonSprite::create("Report", "bigFont.fnt", "GJ_button_06.png", 0.8f);
        reportSpr->setScale(0.32f);
        auto reportBtn = CCMenuItemSpriteExtra::create(reportSpr, this, menu_selector(PostDetailPopup::onReportPost));
        reportBtn->setPosition({btnX + reportSpr->getScaledContentSize().width / 2.f, actionY});
        reportBtn->setID("rebuild-btn"_spr);
        m_buttonMenu->addChild(reportBtn);
        btnX += reportSpr->getScaledContentSize().width + 10.f;
    }

    // delete (solo dueno)
    auto* acc = GJAccountManager::get();
    int myId = acc ? acc->m_accountID : 0;
    if (myId > 0 && myId == m_post.author.accountID) {
        auto delSpr = ButtonSprite::create("Delete", "bigFont.fnt", "GJ_button_06.png", 0.8f);
        delSpr->setScale(0.32f);
        delSpr->setColor({255, 120, 120});
        auto delBtn = CCMenuItemSpriteExtra::create(delSpr, this, menu_selector(PostDetailPopup::onDeletePost));
        delBtn->setPosition({btnX + delSpr->getScaledContentSize().width / 2.f, actionY});
        delBtn->setID("rebuild-btn"_spr);
        m_buttonMenu->addChild(delBtn);
        btnX += delSpr->getScaledContentSize().width + 10.f;
    }

    // ── Replies area (scroll) ──────────────────────────────────────────
    float actionBot = actionY - kActionH / 2.f;
    float replyLblBot = actionBot - kRowGap - kReplyLblH;
    float inputBot = 6.f;
    float scrollBot = inputBot + kInputH + kRowGap;
    float scrollH = replyLblBot - scrollBot;
    if (scrollH < 30.f) scrollH = 30.f;

    {
        auto repliesLbl = CCLabelBMFont::create(
            fmt::format("Replies ({})", static_cast<int>(m_post.replies.size())).c_str(),
            "bigFont.fnt"
        );
        repliesLbl->setScale(0.3f);
        repliesLbl->setAnchorPoint({0.f, 0.5f});
        repliesLbl->setPosition({14.f, replyLblBot + kReplyLblH / 2.f});
        repliesLbl->setColor({200, 200, 220});
        repliesLbl->setID("rebuild-block"_spr);
        m_mainLayer->addChild(repliesLbl);

        auto scrollBg = makeDarkPanel(SCROLL_W, scrollH, 50);
        scrollBg->setPosition({(contentSize.width - SCROLL_W) / 2.f, scrollBot});
        scrollBg->setID("rebuild-block"_spr);
        m_mainLayer->addChild(scrollBg);

        m_scroll = ScrollLayer::create({SCROLL_W, scrollH});
        m_scroll->setPosition({(contentSize.width - SCROLL_W) / 2.f, scrollBot});
        m_scroll->setID("rebuild-block"_spr);
        m_mainLayer->addChild(m_scroll, 5);

        // Construye replies
        float cardGap = 4.f;
        float cardX = 4.f;
        float cardW = SCROLL_W - 8.f;
        float totalH = 4.f;
        std::vector<CCNode*> cards;
        for (auto const& r : m_post.replies) {
            auto card = makeReplyCard(r, cardW);
            cards.push_back(card);
            totalH += card->getContentSize().height + cardGap;
        }
        if (totalH < scrollH) totalH = scrollH;

        m_scroll->m_contentLayer->setContentSize({SCROLL_W, totalH});

        float y = totalH;
        for (auto* card : cards) {
            y -= card->getContentSize().height;
            card->setPosition({cardX, y});
            m_scroll->m_contentLayer->addChild(card);
            y -= cardGap;
        }
        m_scroll->scrollToTop();

        if (m_post.replies.empty()) {
            auto empty = CCLabelBMFont::create("No replies yet - be the first!", "bigFont.fnt");
            empty->setScale(0.32f);
            empty->setColor({150, 150, 170});
            empty->setPosition({SCROLL_W / 2.f, scrollH / 2.f});
            m_scroll->m_contentLayer->addChild(empty);
        }
    }

    // ── Reply input + send ─────────────────────────────────────────────
    if (!m_post.locked) {
        float inputW = SCROLL_W - 70.f;
        m_replyInput = TextInput::create(inputW, "Write a reply...", "chatFont.fnt");
        m_replyInput->setCommonFilter(CommonFilter::Any);
        m_replyInput->setMaxCharCount(400);
        m_replyInput->setPosition({
            (contentSize.width - SCROLL_W) / 2.f + inputW / 2.f,
            inputBot + kInputH / 2.f
        });
        m_replyInput->setScale(0.75f);
        m_replyInput->setID("rebuild-block"_spr);
        m_mainLayer->addChild(m_replyInput);

        auto sendSpr = ButtonSprite::create("Reply", "goldFont.fnt", "GJ_button_01.png", 0.8f);
        sendSpr->setScale(0.45f);
        auto sendBtn = CCMenuItemSpriteExtra::create(sendSpr, this, menu_selector(PostDetailPopup::onSubmitReply));
        sendBtn->setPosition({contentSize.width - 32.f, inputBot + kInputH / 2.f});
        sendBtn->setID("rebuild-btn"_spr);
        m_buttonMenu->addChild(sendBtn);

        // Cooldown label
        m_cooldownLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_cooldownLabel->setScale(0.4f);
        m_cooldownLabel->setPosition({contentSize.width / 2.f, inputBot + kInputH + 6.f});
        m_cooldownLabel->setColor({255, 180, 80});
        m_cooldownLabel->setVisible(false);
        m_mainLayer->addChild(m_cooldownLabel, 10);
        updateCooldownLabel();
    }
}

CCNode* PostDetailPopup::makeAuthorRow(Author const& author, int64_t when, float w) {
    // Header de una sola fila: [icono] [nombre]   [relativa]   [absoluta]
    float h = 28.f;
    auto row = CCNode::create();
    row->setContentSize({w, h});
    row->setAnchorPoint({0.f, 1.f});

    float iconSize = 24.f;
    if (auto* icon = makeAuthorIcon(author, iconSize)) {
        icon->setPosition({iconSize / 2.f + 4.f, h / 2.f});
        row->addChild(icon, 5);
    }

    float nameX = iconSize + 14.f;
    auto nameLbl = CCLabelBMFont::create(
        author.username.empty() ? "Anonymous" : author.username.c_str(),
        "goldFont.fnt"
    );
    nameLbl->setScale(0.38f);
    nameLbl->setAnchorPoint({0.f, 0.5f});
    nameLbl->setPosition({nameX, h / 2.f});
    row->addChild(nameLbl);
    float nameRight = nameX + nameLbl->getScaledContentSize().width;

    auto dateLbl = CCLabelBMFont::create(
        paimon::forum::formatRelativeTime(when).c_str(),
        "chatFont.fnt"
    );
    dateLbl->setScale(0.45f);
    dateLbl->setColor({150, 150, 170});
    dateLbl->setAnchorPoint({0.f, 0.5f});
    dateLbl->setPosition({nameRight + 10.f, h / 2.f});
    row->addChild(dateLbl);

    auto absLbl = CCLabelBMFont::create(
        paimon::forum::formatAbsoluteTime(when).c_str(),
        "chatFont.fnt"
    );
    absLbl->setScale(0.42f);
    absLbl->setColor({120, 120, 140});
    absLbl->setAnchorPoint({1.f, 0.5f});
    absLbl->setPosition({w - 4.f, h / 2.f});
    row->addChild(absLbl);

    return row;
}

CCNode* PostDetailPopup::makeReplyCard(Reply const& r, float w) {
    // Layout reply card (3 filas):
    //   row1 (top): icono + name + relative time (right)
    //   row2 (mid): contenido truncado
    //   row3 (bot): action buttons (Like, Reply, Report, Delete)
    constexpr float kRow1 = 22.f;
    constexpr float kRow2 = 22.f;
    constexpr float kRow3 = 22.f;
    constexpr float kPad  = 5.f;
    constexpr float kRowGap = 3.f;
    float h = kRow1 + kRow2 + kRow3 + kPad * 2.f + kRowGap * 2.f;

    auto card = CCNode::create();
    card->setContentSize({w, h});
    card->setAnchorPoint({0.f, 0.f});

    auto bg = paimon::SpriteHelper::createRoundedRect(
        w, h, 4.f,
        {0.08f, 0.09f, 0.15f, 0.85f},
        {0.35f, 0.45f, 0.7f, 0.5f},
        1.f
    );
    if (bg) {
        bg->setPosition({0.f, 0.f});
        card->addChild(bg, 0);
    }

    // ── row1: icon + name + relative time ──
    float row1Y = h - kPad - kRow1 / 2.f;
    float iconSize = 20.f;
    if (auto* icon = makeAuthorIcon(r.author, iconSize)) {
        icon->setPosition({iconSize / 2.f + 6.f, row1Y});
        card->addChild(icon, 5);
    }
    auto name = CCLabelBMFont::create(
        r.author.username.empty() ? "Anonymous" : r.author.username.c_str(),
        "goldFont.fnt"
    );
    name->setScale(0.32f);
    name->setAnchorPoint({0.f, 0.5f});
    name->setPosition({iconSize + 18.f, row1Y});
    card->addChild(name);

    auto when = CCLabelBMFont::create(paimon::forum::formatRelativeTime(r.createdAt).c_str(), "chatFont.fnt");
    when->setScale(0.40f);
    when->setColor({150, 150, 170});
    when->setAnchorPoint({1.f, 0.5f});
    when->setPosition({w - 8.f, row1Y});
    card->addChild(when);

    // ── row2: contenido (single-line truncated) ──
    float row2Y = h - kPad - kRow1 - kRowGap - kRow2 / 2.f;
    std::string preview = r.content;
    if (preview.size() > 120) preview = preview.substr(0, 117) + "...";
    auto content = CCLabelBMFont::create(preview.empty() ? " " : preview.c_str(), "chatFont.fnt");
    content->setScale(0.52f);
    content->setColor({230, 230, 240});
    content->setAnchorPoint({0.f, 0.5f});
    content->setPosition({8.f, row2Y});
    if (content->getScaledContentSize().width > w - 16.f) {
        content->setScale(content->getScale() * (w - 16.f) / content->getScaledContentSize().width);
    }
    card->addChild(content);

    // ── row3: action buttons row ──
    auto menu = CCMenu::create();
    menu->setContentSize({w - 12.f, kRow3});
    menu->setAnchorPoint({0.f, 0.f});
    menu->setPosition({6.f, kPad});
    menu->ignoreAnchorPointForPosition(false);
    menu->setLayout(
        RowLayout::create()->setGap(6.f)->setAxisAlignment(AxisAlignment::Start)->setAutoScale(false)
    );
    card->addChild(menu, 10);

    std::string replyId = r.id;
    auto self = this;

    {
        std::string lt = fmt::format("{} {}", r.likedByMe ? "Liked" : "Like", r.likes);
        auto spr = ButtonSprite::create(lt.c_str(), "bigFont.fnt",
            r.likedByMe ? "GJ_button_01.png" : "GJ_button_04.png", 0.7f);
        spr->setScale(0.26f);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [self, replyId](CCMenuItemSpriteExtra*) {
            self->onLikeReplyById(replyId);
        });
        menu->addChild(btn);
    }
    {
        auto spr = ButtonSprite::create("Reply", "bigFont.fnt", "GJ_button_05.png", 0.7f);
        spr->setScale(0.26f);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [self, replyId](CCMenuItemSpriteExtra*) {
            self->onReplyToReply(replyId);
        });
        menu->addChild(btn);
    }
    {
        auto spr = ButtonSprite::create("Report", "bigFont.fnt", "GJ_button_06.png", 0.7f);
        spr->setScale(0.26f);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [self, replyId](CCMenuItemSpriteExtra*) {
            self->onReportReplyById(replyId);
        });
        menu->addChild(btn);
    }

    auto* acc = GJAccountManager::get();
    int myId = acc ? acc->m_accountID : 0;
    if (myId > 0 && myId == r.author.accountID) {
        auto spr = ButtonSprite::create("Delete", "bigFont.fnt", "GJ_button_06.png", 0.7f);
        spr->setScale(0.26f);
        spr->setColor({255, 120, 120});
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [self, replyId](CCMenuItemSpriteExtra*) {
            self->onDeleteReplyById(replyId);
        });
        menu->addChild(btn);
    }

    menu->updateLayout();

    if (!r.parentReplyId.empty()) {
        auto thread = CCLabelBMFont::create("-> thread", "chatFont.fnt");
        thread->setScale(0.4f);
        thread->setColor({120, 200, 255});
        thread->setAnchorPoint({1.f, 0.5f});
        thread->setPosition({w - 6.f, kPad + kRow3 / 2.f});
        card->addChild(thread);
    }

    return card;
}

// ── Actions ─────────────────────────────────────────────────────────────

void PostDetailPopup::onLikePost(CCObject*) {
    auto postId = m_post.id;
    WeakRef<PostDetailPopup> self = this;
    ForumApi::get().togglePostLike(postId, [self, postId](paimon::forum::Result<bool>) {
        auto popup = self.lock();
        if (!popup) return;
        // toggle local (cache ya fue actualizada por ForumApi)
        popup->m_post.likedByMe = !popup->m_post.likedByMe;
        popup->m_post.likes += popup->m_post.likedByMe ? 1 : -1;
        if (popup->m_post.likes < 0) popup->m_post.likes = 0;
        if (popup->m_onChanged) popup->m_onChanged();
        popup->rebuild();
    });
}

void PostDetailPopup::onReportPost(CCObject*) {
    auto postId = m_post.id;
    ForumApi::get().reportPost(postId, "Reported from app", [](paimon::forum::Result<bool>) {});
    PaimonNotify::create("Report submitted", NotificationIcon::Success)->show();
}

void PostDetailPopup::onDeletePost(CCObject*) {
    auto postId = m_post.id;
    WeakRef<PostDetailPopup> self = this;
    ForumApi::get().deletePost(postId, [self](paimon::forum::Result<bool>) {
        auto popup = self.lock();
        if (!popup) return;
        if (popup->m_onChanged) popup->m_onChanged();
        PaimonNotify::create("Post deleted", NotificationIcon::Success)->show();
        popup->onClose(nullptr);
    });
}

void PostDetailPopup::onSubmitReply(CCObject*) {
    if (!m_replyInput) return;
    std::string content = m_replyInput->getString();
    if (content.empty()) {
        PaimonNotify::create("Type something first", NotificationIcon::Warning)->show();
        return;
    }

    // Check cooldown
    auto cd = ForumApi::get().getReplyCooldownRemaining();
    if (cd > 0) {
        PaimonNotify::create(fmt::format("Please wait {} seconds before replying again.", cd).c_str(), NotificationIcon::Warning)->show();
        return;
    }

    paimon::forum::CreateReplyRequest req;
    req.postId = m_post.id;
    req.parentReplyId = m_replyTo;
    req.content = content;

    WeakRef<PostDetailPopup> self = this;
    auto postId = m_post.id;
    ForumApi::get().createReply(req, [self, postId](paimon::forum::Result<Reply> res) {
        auto popup = self.lock();
        if (!popup) return;
        if (!res.ok) {
            if (res.error.find("Rate limited") != std::string::npos) {
                PaimonNotify::create("You're replying too fast. Please wait a moment.", NotificationIcon::Warning)->show();
            } else {
                PaimonNotify::create("Failed to reply", NotificationIcon::Error)->show();
            }
            return;
        }
        // recargar post desde cache
        ForumApi::get().getPost(postId, [self](paimon::forum::Result<Post> r2) {
            auto popup = self.lock();
            if (!popup) return;
            if (r2.ok) popup->m_post = r2.data;
            popup->m_replyTo.clear();
            if (popup->m_onChanged) popup->m_onChanged();
            popup->rebuild();
        });
    });
}

void PostDetailPopup::onLikeReplyById(std::string id) {
    WeakRef<PostDetailPopup> self = this;
    auto postId = m_post.id;
    ForumApi::get().toggleReplyLike(id, [self, postId](paimon::forum::Result<bool>) {
        auto popup = self.lock();
        if (!popup) return;
        ForumApi::get().getPost(postId, [self](paimon::forum::Result<Post> r) {
            auto popup = self.lock();
            if (!popup) return;
            if (r.ok) popup->m_post = r.data;
            if (popup->m_onChanged) popup->m_onChanged();
            popup->rebuild();
        });
    });
}

void PostDetailPopup::onReportReplyById(std::string id) {
    ForumApi::get().reportReply(id, "Reported from app", [](paimon::forum::Result<bool>) {});
    PaimonNotify::create("Reply reported", NotificationIcon::Success)->show();
}

void PostDetailPopup::onDeleteReplyById(std::string id) {
    WeakRef<PostDetailPopup> self = this;
    auto postId = m_post.id;
    ForumApi::get().deleteReply(id, [self, postId](paimon::forum::Result<bool>) {
        auto popup = self.lock();
        if (!popup) return;
        ForumApi::get().getPost(postId, [self](paimon::forum::Result<Post> r) {
            auto popup = self.lock();
            if (!popup) return;
            if (r.ok) popup->m_post = r.data;
            if (popup->m_onChanged) popup->m_onChanged();
            popup->rebuild();
        });
    });
}

void PostDetailPopup::onReplyToReply(std::string id) {
    m_replyTo = id;
    if (m_replyInput) {
        m_replyInput->setString("@reply ");
        PaimonNotify::create("Replying in thread", NotificationIcon::Info)->show();
    }
}

void PostDetailPopup::updateCooldownLabel() {
    if (!m_cooldownLabel) return;
    auto cd = ForumApi::get().getReplyCooldownRemaining();
    if (cd > 0) {
        m_cooldownLabel->setString(fmt::format("Wait {}s to reply", cd).c_str());
        m_cooldownLabel->setVisible(true);
    } else {
        m_cooldownLabel->setString("");
        m_cooldownLabel->setVisible(false);
    }
}

void PostDetailPopup::update(float) {
    updateCooldownLabel();
}

PostDetailPopup* PostDetailPopup::create(Post const& post, CopyableFunction<void()> onChanged) {
    auto ret = new PostDetailPopup();
    if (ret && ret->init(post, std::move(onChanged))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}
