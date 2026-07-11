#include "PostDetailPopup.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/ui/MDTextArea.hpp>
#include <Geode/ui/General.hpp>
#include "../../../utils/SpriteHelper.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using paimon::forum::ForumApi;
using paimon::forum::Post;
using paimon::forum::Reply;
using paimon::forum::Author;

namespace {
    constexpr float POPUP_W = 460.f;
    constexpr float POPUP_H = 320.f;
    constexpr float SCROLL_W = 430.f;

    // GD vanilla list palette (matches the hub's forum/news lists)
    constexpr ccColor4B kRowDark  = {161, 88, 44, 255};
    constexpr ccColor4B kRowLight = {194, 114, 62, 255};
    constexpr ccColor3B kTextSoft = {255, 235, 190};

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
        // SimplePlayer contentSize is unreliable (glow/hitbox/empty areas); use a ~30px reference to avoid tiny icons.
        float gdRefSize = 30.f;
        float scale = (maxDim > 10.f && maxDim < 80.f) ? (targetSize / maxDim) : (targetSize / gdRefSize);
        player->setScale(std::max(scale, 0.55f));
        return player;
    }

    // GD-style dark inset (square02b tinted black), like vanilla list wells.
    static CCNode* makeDarkPanel(float w, float h, GLubyte alpha = 70) {
        return paimon::SpriteHelper::createDarkPanel(w, h, alpha);
    }
}

bool PostDetailPopup::init(Post const& post, CopyableFunction<void()> onChanged) {
    if (!Popup::init(POPUP_W, POPUP_H)) return false;
    m_post = post;
    m_onChanged = std::move(onChanged);

    // GD-style: keep the vanilla GJ_square01 popup background and gold title.
    this->setTitle(m_post.title.c_str());
    if (m_title) {
        float maxTitleW = POPUP_W - 90.f;
        if (m_title->getScaledContentSize().width > maxTitleW) {
            m_title->setScale(
                m_title->getScale() * maxTitleW / m_title->getScaledContentSize().width
            );
        }
    }

    rebuild();
    paimon::markDynamicPopup(this);
    this->scheduleUpdate();
    return true;
}

void PostDetailPopup::rebuild() {
    auto contentSize = m_mainLayer->getContentSize();
    float cx = contentSize.width / 2.f;

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

    // m_replyInput and m_cooldownLabel are "rebuild-block" children freed just
    // above; they are only recreated in the !locked branch below. Null them now
    // so that when a thread is locked (branch skipped) they don't dangle — the
    // per-frame update()->updateCooldownLabel() and onSubmitReply/onReplyToReply
    // all null-check these pointers.
    m_replyInput = nullptr;
    m_cooldownLabel = nullptr;

    constexpr float kRowGap   = 7.f;
    constexpr float kHeaderH  = 34.f;
    constexpr float kTagsH    = 18.f;
    constexpr float kDescH    = 50.f;
    constexpr float kActionH  = 28.f;
    constexpr float kReplyLblH = 16.f;
    constexpr float kInputH   = 30.f;

    float headerBot = contentSize.height - kHeaderH - 32.f;
    auto headerRow = makeAuthorRow(m_post.author, m_post.createdAt, contentSize.width - 36.f);
    headerRow->setPosition({18.f, headerBot + kHeaderH});
    headerRow->setID("rebuild-block"_spr);
    m_mainLayer->addChild(headerRow);

    float tagsBot = headerBot - kRowGap - kTagsH;
    bool hasTags = !m_post.tags.empty();
    if (hasTags) {
        auto tagPanel = makeDarkPanel(contentSize.width - 24.f, kTagsH, 50);
        if (tagPanel) {
            tagPanel->setPosition({12.f, tagsBot});
            tagPanel->setID("rebuild-block"_spr);
            m_mainLayer->addChild(tagPanel);
        }

        auto tagRow = CCNode::create();
        tagRow->setContentSize({contentSize.width - 24.f, kTagsH});
        tagRow->setAnchorPoint({0.f, 0.f});
        tagRow->setPosition({12.f, tagsBot});
        tagRow->setID("rebuild-block"_spr);
        m_mainLayer->addChild(tagRow);

        float x = 8.f;
        for (auto const& tag : m_post.tags) {
            auto chip = ButtonSprite::create(tag.c_str(), "bigFont.fnt", "GJ_button_05.png", 0.7f);
            chip->setScale(0.24f);
            chip->setAnchorPoint({0.f, 0.5f});
            chip->setPosition({x, kTagsH / 2.f});
            tagRow->addChild(chip);
            x += chip->getScaledContentSize().width + 4.f;
            if (x > contentSize.width - 32.f) break;
        }
    } else {
        tagsBot = headerBot - kRowGap;
    }

    float descBot = tagsBot - kRowGap - kDescH;
    {
        if (auto descBg = makeDarkPanel(contentSize.width - 24.f, kDescH, 70)) {
            descBg->setPosition({12.f, descBot});
            descBg->setID("rebuild-block"_spr);
            m_mainLayer->addChild(descBg);
        }

        auto desc = MDTextArea::create(
            m_post.description.empty() ? "*(no description)*" : m_post.description,
            {contentSize.width - 36.f, kDescH - 4.f}
        );
        if (desc) {
            desc->setAnchorPoint({0.f, 0.f});
            desc->setPosition({18.f, descBot + 2.f});
            desc->setID("rebuild-block"_spr);
            m_mainLayer->addChild(desc);
        }
    }

    float actionY = descBot - kRowGap - kActionH / 2.f;
    {
        auto* acc = GJAccountManager::get();
        int myId = acc ? acc->m_accountID : 0;
        bool canDelete = myId > 0 && myId == m_post.author.accountID;

        auto bar = CCMenu::create();
        bar->setID("rebuild-btn"_spr);
        bar->setContentSize({contentSize.width - 36.f, kActionH});
        bar->setAnchorPoint({0.f, 0.5f});
        bar->setPosition({18.f, actionY});
        bar->setLayout(
            RowLayout::create()
                ->setGap(8.f)
                ->setAutoScale(false)
                ->setAxisAlignment(AxisAlignment::Start)
        );
        m_buttonMenu->addChild(bar);

        std::string likeText = fmt::format("{}  {}",
            m_post.likedByMe ? "Liked" : "Like", m_post.likes);
        auto likeSpr = ButtonSprite::create(likeText.c_str(), "bigFont.fnt",
            m_post.likedByMe ? "GJ_button_01.png" : "GJ_button_04.png", 0.8f);
        likeSpr->setScale(0.42f);
        auto likeBtn = CCMenuItemSpriteExtra::create(likeSpr, this,
            menu_selector(PostDetailPopup::onLikePost));
        bar->addChild(likeBtn);

        auto reportSpr = ButtonSprite::create("Report", "bigFont.fnt", "GJ_button_06.png", 0.8f);
        reportSpr->setScale(0.36f);
        auto reportBtn = CCMenuItemSpriteExtra::create(reportSpr, this,
            menu_selector(PostDetailPopup::onReportPost));
        bar->addChild(reportBtn);

        if (canDelete) {
            auto delSpr = ButtonSprite::create("Delete", "bigFont.fnt", "GJ_button_06.png", 0.8f);
            delSpr->setScale(0.36f);
            delSpr->setColor({255, 110, 110});
            auto delBtn = CCMenuItemSpriteExtra::create(delSpr, this,
                menu_selector(PostDetailPopup::onDeletePost));
            bar->addChild(delBtn);
        }

        bar->updateLayout();
    }

    float actionBot = actionY - kActionH / 2.f;
    float replyLblBot = actionBot - kRowGap - kReplyLblH;
    float inputBot = 8.f;
    float scrollBot = inputBot + kInputH + kRowGap;
    float scrollH = replyLblBot - scrollBot;
    if (scrollH < 30.f) scrollH = 30.f;

    {
        auto repliesLbl = CCLabelBMFont::create(
            fmt::format("Replies  ({})", static_cast<int>(m_post.replies.size())).c_str(),
            "goldFont.fnt"
        );
        repliesLbl->setScale(0.42f);
        repliesLbl->setAnchorPoint({0.f, 0.5f});
        repliesLbl->setPosition({18.f, replyLblBot + kReplyLblH / 2.f});
        repliesLbl->setID("rebuild-block"_spr);
        m_mainLayer->addChild(repliesLbl);

        if (auto scrollBg = makeDarkPanel(SCROLL_W, scrollH, 90)) {
            scrollBg->setPosition({(contentSize.width - SCROLL_W) / 2.f, scrollBot});
            scrollBg->setID("rebuild-block"_spr);
            m_mainLayer->addChild(scrollBg);
        }

        m_scroll = ScrollLayer::create({SCROLL_W, scrollH});
        m_scroll->setPosition({(contentSize.width - SCROLL_W) / 2.f, scrollBot});
        m_scroll->setID("rebuild-block"_spr);
        m_mainLayer->addChild(m_scroll, 5);

        // GD comment-list borders framing the reply list
        if (auto borders = geode::ListBorders::create()) {
            borders->setContentSize({SCROLL_W + 4.f, scrollH});
            borders->setPosition({contentSize.width / 2.f, scrollBot + scrollH / 2.f});
            borders->setID("rebuild-block"_spr);
            m_mainLayer->addChild(borders, 6);
        }

        // vanilla list: full-width rows, no gaps, alternating browns
        float cardW = SCROLL_W;
        float totalH = 0.f;
        std::vector<CCNode*> cards;
        int idx = 0;
        for (auto const& r : m_post.replies) {
            auto card = makeReplyCard(r, cardW, idx++);
            cards.push_back(card);
            totalH += card->getContentSize().height;
        }
        if (totalH < scrollH) totalH = scrollH;

        m_scroll->m_contentLayer->setContentSize({SCROLL_W, totalH});

        float y = totalH;
        for (auto* card : cards) {
            y -= card->getContentSize().height;
            card->setPosition({0.f, y});
            m_scroll->m_contentLayer->addChild(card);
        }
        m_scroll->scrollToTop();

        if (m_post.replies.empty()) {
            auto empty = CCLabelBMFont::create(
                "No replies yet — be the first to chime in!", "bigFont.fnt");
            empty->setScale(0.32f);
            empty->setColor(kTextSoft);
            empty->setPosition({SCROLL_W / 2.f, scrollH / 2.f});
            m_scroll->m_contentLayer->addChild(empty);
        }
    }

    if (!m_post.locked) {
        float inputW = SCROLL_W - 75.f;
        float inputCenterY = inputBot + kInputH / 2.f;

        if (auto inputBg = makeDarkPanel(SCROLL_W, kInputH + 6.f, 50)) {
            inputBg->setPosition({(contentSize.width - SCROLL_W) / 2.f, inputBot - 3.f});
            inputBg->setID("rebuild-block"_spr);
            m_mainLayer->addChild(inputBg);
        }

        m_replyInput = TextInput::create(inputW, "Write a reply...", "chatFont.fnt");
        m_replyInput->setCommonFilter(CommonFilter::Any);
        m_replyInput->setMaxCharCount(400);
        m_replyInput->setPosition({
            (contentSize.width - SCROLL_W) / 2.f + inputW / 2.f + 6.f,
            inputCenterY
        });
        m_replyInput->setScale(0.78f);
        m_replyInput->setID("rebuild-block"_spr);
        m_mainLayer->addChild(m_replyInput);

        auto sendSpr = ButtonSprite::create("Reply", "goldFont.fnt", "GJ_button_01.png", 0.8f);
        sendSpr->setScale(0.5f);
        auto sendBtn = CCMenuItemSpriteExtra::create(sendSpr, this,
            menu_selector(PostDetailPopup::onSubmitReply));
        sendBtn->setPosition({contentSize.width - 38.f, inputCenterY});
        sendBtn->setID("rebuild-btn"_spr);
        m_buttonMenu->addChild(sendBtn);

        m_cooldownLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_cooldownLabel->setScale(0.42f);
        m_cooldownLabel->setPosition({contentSize.width / 2.f, inputBot + kInputH + 8.f});
        m_cooldownLabel->setColor({255, 180, 80});
        m_cooldownLabel->setVisible(false);
        m_cooldownLabel->setID("rebuild-block"_spr);
        m_mainLayer->addChild(m_cooldownLabel, 10);
        updateCooldownLabel();
    } else {
        auto lockedBg = makeDarkPanel(SCROLL_W, kInputH, 60);
        if (lockedBg) {
            lockedBg->setPosition({(contentSize.width - SCROLL_W) / 2.f, inputBot});
            lockedBg->setID("rebuild-block"_spr);
            m_mainLayer->addChild(lockedBg);
        }
        auto locked = CCLabelBMFont::create(
            "This post is locked — replies are disabled.", "bigFont.fnt");
        locked->setScale(0.34f);
        locked->setColor({200, 200, 200});
        locked->setPosition({contentSize.width / 2.f, inputBot + kInputH / 2.f});
        locked->setID("rebuild-block"_spr);
        m_mainLayer->addChild(locked);
    }
}

CCNode* PostDetailPopup::makeAuthorRow(Author const& author, int64_t when, float w) {
    float h = 34.f;
    auto row = CCNode::create();
    row->setContentSize({w, h});
    row->setAnchorPoint({0.f, 1.f});

    float iconSize = 30.f;
    if (auto* icon = makeAuthorIcon(author, iconSize)) {
        icon->setPosition({iconSize / 2.f + 4.f, h / 2.f});
        row->addChild(icon, 5);
    }

    float nameX = iconSize + 16.f;
    auto nameLbl = CCLabelBMFont::create(
        author.username.empty() ? "Anonymous" : author.username.c_str(),
        "goldFont.fnt"
    );
    nameLbl->setScale(0.44f);
    nameLbl->setAnchorPoint({0.f, 0.5f});
    nameLbl->setPosition({nameX, h / 2.f + 5.f});
    row->addChild(nameLbl);

    auto dateLbl = CCLabelBMFont::create(
        paimon::forum::formatRelativeTime(when).c_str(),
        "chatFont.fnt"
    );
    dateLbl->setScale(0.46f);
    dateLbl->setColor(kTextSoft);
    dateLbl->setAnchorPoint({0.f, 0.5f});
    dateLbl->setPosition({nameX, h / 2.f - 8.f});
    row->addChild(dateLbl);

    auto absLbl = CCLabelBMFont::create(
        paimon::forum::formatAbsoluteTime(when).c_str(),
        "chatFont.fnt"
    );
    absLbl->setScale(0.42f);
    absLbl->setColor(kTextSoft);
    absLbl->setOpacity(180);
    absLbl->setAnchorPoint({1.f, 0.5f});
    absLbl->setPosition({w - 4.f, h / 2.f});
    row->addChild(absLbl);

    return row;
}

CCNode* PostDetailPopup::makeReplyCard(Reply const& r, float w, int index) {
    constexpr float kRow1   = 22.f;
    constexpr float kRow2   = 26.f;
    constexpr float kRow3   = 24.f;
    constexpr float kPad    = 6.f;
    constexpr float kRowGap = 4.f;
    float h = kRow1 + kRow2 + kRow3 + kPad * 2.f + kRowGap * 2.f;

    auto card = CCNode::create();
    card->setContentSize({w, h});
    card->setAnchorPoint({0.f, 0.f});

    auto* acc = GJAccountManager::get();
    int myId = acc ? acc->m_accountID : 0;
    bool isMine = myId > 0 && myId == r.author.accountID;

    // vanilla GD list row: alternating browns + hairline separator
    auto bg = CCLayerColor::create(index % 2 == 0 ? kRowLight : kRowDark);
    bg->setContentSize({w, h});
    bg->setPosition({0.f, 0.f});
    card->addChild(bg, 0);

    if (index > 0) {
        auto line = CCLayerColor::create({0, 0, 0, 60});
        line->setContentSize({w, 1.f});
        line->setPosition({0.f, h - 1.f});
        card->addChild(line, 3);
    }

    // green edge marker on your own replies
    if (isMine) {
        auto marker = CCLayerColor::create({140, 255, 140, 200});
        marker->setContentSize({3.f, h});
        marker->setPosition({0.f, 0.f});
        card->addChild(marker, 3);
    }

    float row1Y = h - kPad - kRow1 / 2.f;
    float iconSize = 20.f;
    if (auto* icon = makeAuthorIcon(r.author, iconSize)) {
        icon->setPosition({iconSize / 2.f + 8.f, row1Y});
        card->addChild(icon, 5);
    }
    auto name = CCLabelBMFont::create(
        r.author.username.empty() ? "Anonymous" : r.author.username.c_str(),
        "goldFont.fnt"
    );
    name->setScale(0.34f);
    name->setAnchorPoint({0.f, 0.5f});
    name->setPosition({iconSize + 22.f, row1Y});
    card->addChild(name);

    if (isMine) {
        auto youBadge = CCLabelBMFont::create("you", "chatFont.fnt");
        youBadge->setScale(0.42f);
        youBadge->setColor({150, 220, 150});
        youBadge->setAnchorPoint({0.f, 0.5f});
        float youX = iconSize + 22.f + name->getScaledContentSize().width + 6.f;
        youBadge->setPosition({youX, row1Y});
        card->addChild(youBadge);
    }

    auto when = CCLabelBMFont::create(paimon::forum::formatRelativeTime(r.createdAt).c_str(), "chatFont.fnt");
    when->setScale(0.42f);
    when->setColor(kTextSoft);
    when->setAnchorPoint({1.f, 0.5f});
    when->setPosition({w - 10.f, row1Y});
    card->addChild(when);

    float row2Y = h - kPad - kRow1 - kRowGap - kRow2 / 2.f;
    std::string preview = r.content;
    if (preview.size() > 130) preview = preview.substr(0, 127) + "...";
    auto content = CCLabelBMFont::create(preview.empty() ? " " : preview.c_str(), "chatFont.fnt");
    content->setScale(0.55f);
    content->setColor({255, 250, 240});
    content->setAnchorPoint({0.f, 0.5f});
    content->setPosition({10.f, row2Y});
    if (content->getScaledContentSize().width > w - 20.f) {
        content->setScale(content->getScale() * (w - 20.f) / content->getScaledContentSize().width);
    }
    card->addChild(content);

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
        std::string lt = fmt::format("{}  {}", r.likedByMe ? "Liked" : "Like", r.likes);
        auto spr = ButtonSprite::create(lt.c_str(), "bigFont.fnt",
            r.likedByMe ? "GJ_button_01.png" : "GJ_button_04.png", 0.7f);
        spr->setScale(0.30f);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [self, replyId](CCMenuItemSpriteExtra*) {
            self->onLikeReplyById(replyId);
        });
        menu->addChild(btn);
    }
    {
        auto spr = ButtonSprite::create("Reply", "bigFont.fnt", "GJ_button_05.png", 0.7f);
        spr->setScale(0.30f);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [self, replyId](CCMenuItemSpriteExtra*) {
            self->onReplyToReply(replyId);
        });
        menu->addChild(btn);
    }
    {
        auto spr = ButtonSprite::create("Report", "bigFont.fnt", "GJ_button_06.png", 0.7f);
        spr->setScale(0.30f);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [self, replyId](CCMenuItemSpriteExtra*) {
            self->onReportReplyById(replyId);
        });
        menu->addChild(btn);
    }

    if (isMine) {
        auto spr = ButtonSprite::create("Delete", "bigFont.fnt", "GJ_button_06.png", 0.7f);
        spr->setScale(0.30f);
        spr->setColor({255, 110, 110});
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [self, replyId](CCMenuItemSpriteExtra*) {
            self->onDeleteReplyById(replyId);
        });
        menu->addChild(btn);
    }

    menu->updateLayout();

    if (!r.parentReplyId.empty()) {
        auto thread = CCLabelBMFont::create("in thread", "chatFont.fnt");
        thread->setScale(0.42f);
        thread->setColor({120, 200, 255});
        thread->setAnchorPoint({1.f, 0.5f});
        thread->setPosition({w - 8.f, kPad + kRow3 / 2.f});
        card->addChild(thread);
    }

    return card;
}

void PostDetailPopup::onLikePost(CCObject*) {
    auto postId = m_post.id;
    WeakRef<PostDetailPopup> self = this;
    ForumApi::get().togglePostLike(postId, [self, postId](paimon::forum::Result<bool>) {
        auto popup = self.lock();
        if (!popup) return;
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

void PostDetailPopup::onExit() {
    this->unscheduleUpdate();
    Popup::onExit();
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
