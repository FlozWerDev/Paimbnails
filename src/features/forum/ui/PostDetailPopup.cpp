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
    constexpr float POPUP_W = 460.f;
    constexpr float POPUP_H = 320.f;
    constexpr float SCROLL_W = 430.f;

    static constexpr ccColor3B kAccent = {130, 180, 255};

    // build the author's icon sprite (SimplePlayer)
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

    static CCNode* makeDarkPanel(float w, float h, GLubyte alpha = 70) {
        // Dark interior + thin near-white border to match the rest of the forum.
        return paimon::SpriteHelper::createRoundedRect(
            w, h, 5.f,
            {0.06f, 0.07f, 0.11f, alpha / 255.f},
            {0.92f, 0.94f, 1.0f, 0.85f},
            1.0f
        );
    }
}

bool PostDetailPopup::init(Post const& post, CopyableFunction<void()> onChanged) {
    if (!Popup::init(POPUP_W, POPUP_H)) return false;
    m_post = post;
    m_onChanged = std::move(onChanged);

    this->setTitle(m_post.title.c_str());

    // Replace the vanilla brown frame with the mod's dark rounded panel.
    if (m_bgSprite) m_bgSprite->setVisible(false);

    auto popupSize = m_mainLayer->getContentSize();
    auto darkBg = paimon::SpriteHelper::createRoundedRect(
        popupSize.width, popupSize.height, 8.f,
        {10/255.f, 10/255.f, 18/255.f, 245/255.f},
        {kAccent.r/255.f, kAccent.g/255.f, kAccent.b/255.f, 130/255.f},
        1.4f
    );
    if (darkBg) {
        darkBg->setPosition({0.f, 0.f});
        darkBg->setZOrder(-1);
        m_mainLayer->addChild(darkBg);
    }

    rebuild();
    paimon::markDynamicPopup(this);
    this->scheduleUpdate();
    return true;
}

void PostDetailPopup::rebuild() {
    auto contentSize = m_mainLayer->getContentSize();
    float cx = contentSize.width / 2.f;

    // Clear previous children (except title and close, managed by Popup).
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

    // Layout, top to bottom: header, tags, description, action bar, replies, reply input.
    constexpr float kRowGap   = 7.f;
    constexpr float kHeaderH  = 34.f;
    constexpr float kTagsH    = 18.f;
    constexpr float kDescH    = 50.f;
    constexpr float kActionH  = 28.f;
    constexpr float kReplyLblH = 16.f;
    constexpr float kInputH   = 30.f;

    // Header
    float headerBot = contentSize.height - kHeaderH - 32.f;
    auto headerRow = makeAuthorRow(m_post.author, m_post.createdAt, contentSize.width - 36.f);
    headerRow->setPosition({18.f, headerBot + kHeaderH});
    headerRow->setID("rebuild-block"_spr);
    m_mainLayer->addChild(headerRow);

    // Tags row (only when there are tags)
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
        // Skip the row entirely so descriptions can use that vertical space.
        tagsBot = headerBot - kRowGap;
    }

    // Description box
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

    // Action bar (sticks left, balanced sizing)
    float actionY = descBot - kRowGap - kActionH / 2.f;
    {
        auto* acc = GJAccountManager::get();
        int myId = acc ? acc->m_accountID : 0;
        bool canDelete = myId > 0 && myId == m_post.author.accountID;

        // CCMenu row layout keeps spacing uniform regardless of label widths.
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

        // Like — "Liked X" stays gold-filled when active so the toggle is obvious.
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

    // Replies
    float actionBot = actionY - kActionH / 2.f;
    float replyLblBot = actionBot - kRowGap - kReplyLblH;
    float inputBot = 8.f;
    float scrollBot = inputBot + kInputH + kRowGap;
    float scrollH = replyLblBot - scrollBot;
    if (scrollH < 30.f) scrollH = 30.f;

    {
        auto repliesLbl = CCLabelBMFont::create(
            fmt::format("Replies  ({})", static_cast<int>(m_post.replies.size())).c_str(),
            "bigFont.fnt"
        );
        repliesLbl->setScale(0.32f);
        repliesLbl->setAnchorPoint({0.f, 0.5f});
        repliesLbl->setPosition({18.f, replyLblBot + kReplyLblH / 2.f});
        repliesLbl->setColor({200, 210, 230});
        repliesLbl->setID("rebuild-block"_spr);
        m_mainLayer->addChild(repliesLbl);

        if (auto scrollBg = makeDarkPanel(SCROLL_W, scrollH, 60)) {
            scrollBg->setPosition({(contentSize.width - SCROLL_W) / 2.f, scrollBot});
            scrollBg->setID("rebuild-block"_spr);
            m_mainLayer->addChild(scrollBg);
        }

        m_scroll = ScrollLayer::create({SCROLL_W, scrollH});
        m_scroll->setPosition({(contentSize.width - SCROLL_W) / 2.f, scrollBot});
        m_scroll->setID("rebuild-block"_spr);
        m_mainLayer->addChild(m_scroll, 5);

        // Build replies
        float cardGap = 5.f;
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
            auto empty = CCLabelBMFont::create(
                "No replies yet — be the first to chime in!", "bigFont.fnt");
            empty->setScale(0.32f);
            empty->setColor({150, 150, 170});
            empty->setPosition({SCROLL_W / 2.f, scrollH / 2.f});
            m_scroll->m_contentLayer->addChild(empty);
        }
    }

    // Reply input + send
    if (!m_post.locked) {
        float inputW = SCROLL_W - 75.f;
        float inputCenterY = inputBot + kInputH / 2.f;

        // Subtle backing panel makes the input row feel grouped with the send btn.
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

        // Cooldown label (above the input row, only shown when active)
        m_cooldownLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_cooldownLabel->setScale(0.42f);
        m_cooldownLabel->setPosition({contentSize.width / 2.f, inputBot + kInputH + 8.f});
        m_cooldownLabel->setColor({255, 180, 80});
        m_cooldownLabel->setVisible(false);
        m_cooldownLabel->setID("rebuild-block"_spr);
        m_mainLayer->addChild(m_cooldownLabel, 10);
        updateCooldownLabel();
    } else {
        // Locked posts: show a banner instead of the input row.
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
    // Header layout: [icon] [name] [relative] ... [absolute]
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
    dateLbl->setColor({170, 180, 210});
    dateLbl->setAnchorPoint({0.f, 0.5f});
    dateLbl->setPosition({nameX, h / 2.f - 8.f});
    row->addChild(dateLbl);

    auto absLbl = CCLabelBMFont::create(
        paimon::forum::formatAbsoluteTime(when).c_str(),
        "chatFont.fnt"
    );
    absLbl->setScale(0.42f);
    absLbl->setColor({130, 140, 160});
    absLbl->setAnchorPoint({1.f, 0.5f});
    absLbl->setPosition({w - 4.f, h / 2.f});
    row->addChild(absLbl);

    return row;
}

CCNode* PostDetailPopup::makeReplyCard(Reply const& r, float w) {
    // Reply card: row1 author + time, row2 content, row3 action buttons.
    constexpr float kRow1   = 22.f;
    constexpr float kRow2   = 26.f;
    constexpr float kRow3   = 24.f;
    constexpr float kPad    = 6.f;
    constexpr float kRowGap = 4.f;
    float h = kRow1 + kRow2 + kRow3 + kPad * 2.f + kRowGap * 2.f;

    auto card = CCNode::create();
    card->setContentSize({w, h});
    card->setAnchorPoint({0.f, 0.f});

    // Highlight the current user's own replies with a softer border.
    auto* acc = GJAccountManager::get();
    int myId = acc ? acc->m_accountID : 0;
    bool isMine = myId > 0 && myId == r.author.accountID;

    auto bg = paimon::SpriteHelper::createRoundedRect(
        w, h, 5.f,
        {0.06f, 0.07f, 0.12f, 0.95f},
        isMine
            ? cocos2d::ccColor4F{0.55f, 0.95f, 0.55f, 0.95f}
            : cocos2d::ccColor4F{0.92f, 0.94f, 1.0f, 0.85f},
        1.0f
    );
    if (bg) {
        bg->setPosition({0.f, 0.f});
        card->addChild(bg, 0);
    }

    // Row 1: icon + name + relative time
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
    when->setColor({150, 160, 185});
    when->setAnchorPoint({1.f, 0.5f});
    when->setPosition({w - 10.f, row1Y});
    card->addChild(when);

    // Row 2: content (single-line truncated for predictable height)
    float row2Y = h - kPad - kRow1 - kRowGap - kRow2 / 2.f;
    std::string preview = r.content;
    if (preview.size() > 130) preview = preview.substr(0, 127) + "...";
    auto content = CCLabelBMFont::create(preview.empty() ? " " : preview.c_str(), "chatFont.fnt");
    content->setScale(0.55f);
    content->setColor({230, 235, 245});
    content->setAnchorPoint({0.f, 0.5f});
    content->setPosition({10.f, row2Y});
    if (content->getScaledContentSize().width > w - 20.f) {
        content->setScale(content->getScale() * (w - 20.f) / content->getScaledContentSize().width);
    }
    card->addChild(content);

    // Row 3: action buttons row
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

// Actions

void PostDetailPopup::onLikePost(CCObject*) {
    auto postId = m_post.id;
    WeakRef<PostDetailPopup> self = this;
    ForumApi::get().togglePostLike(postId, [self, postId](paimon::forum::Result<bool>) {
        auto popup = self.lock();
        if (!popup) return;
        // local toggle (cache already updated by ForumApi)
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
        // reload post from cache
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
