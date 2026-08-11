#include "ModlyCommentsPopup.hpp"
#include "ModlyProfilePopup.hpp"
#include "ModlyUIHelpers.hpp"
#include "../services/ModlyRepo.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/Scrollbar.hpp>
#include <Geode/ui/TextArea.hpp>

using namespace geode::prelude;

namespace paimon::compat_mods {

namespace {
    constexpr float kWidth = 400.f;
    constexpr float kHeight = 280.f;
    constexpr float kListW = 356.f;
    constexpr float kListH = 194.f;
    constexpr float kAvatar = 26.f;
}

bool ModlyCommentsPopup::init(ModlyMod const& mod) {
    if (!Popup::init(kWidth, kHeight)) return false;
    paimon::markDynamicPopup(this);

    m_mod = mod;
    this->setTitle(Localization::get().getString("modly.comments_title").c_str());

    auto subtitle = CCLabelBMFont::create(m_mod.name.c_str(), "goldFont.fnt");
    subtitle->setScale(0.4f);
    subtitle->setPosition({kWidth / 2.f, kHeight - 52.f});
    fitLabelWidth(subtitle, kListW);
    m_mainLayer->addChild(subtitle, 2);

    m_listHolder = CCNode::create();
    m_listHolder->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_listHolder, 2);

    load();
    return true;
}

void ModlyCommentsPopup::load() {
    showStatus(Localization::get().getString("modly.loading"));

    WeakRef<ModlyCommentsPopup> self = this;
    ModlyRepo::get().fetchComments(m_mod.id, false,
        [self](bool ok, std::vector<ModlyComment> const& comments) {
            auto popup = self.lock();
            if (!popup) return;

            if (!ok) {
                popup->showStatus(Localization::get().getString("modly.comments_error"));
                return;
            }
            if (comments.empty()) {
                popup->showStatus(Localization::get().getString("modly.comments_empty"));
                return;
            }
            popup->buildList(comments);
        });
}

void ModlyCommentsPopup::showStatus(std::string const& text) {
    if (!m_listHolder) return;
    m_listHolder->removeAllChildren();

    auto label = CCLabelBMFont::create(text.c_str(), "goldFont.fnt");
    label->setScale(0.5f);
    label->setOpacity(0);
    label->setPosition({kWidth / 2.f, 30.f + kListH / 2.f});
    fitLabelWidth(label, kListW);
    m_listHolder->addChild(label);
    label->runAction(CCFadeTo::create(0.25f, 200));
    m_status = label;
}

void ModlyCommentsPopup::buildList(std::vector<ModlyComment> const& comments) {
    if (!m_listHolder) return;
    m_listHolder->removeAllChildren();
    m_status = nullptr;

    auto scroll = ScrollLayer::create({kListW, kListH});
    scroll->setPosition({(kWidth - kListW) / 2.f, 30.f});
    m_listHolder->addChild(scroll);

    std::vector<CCNode*> cards;
    float totalH = 0.f;
    for (size_t i = 0; i < comments.size(); ++i) {
        auto* card = buildCommentCard(comments[i], kListW, static_cast<int>(i));
        cards.push_back(card);
        totalH += card->getContentSize().height + 4.f;
    }

    float contentH = std::max(totalH, kListH);
    scroll->m_contentLayer->setContentSize({kListW, contentH});
    scroll->m_contentLayer->setPositionY(kListH - contentH);

    float y = contentH;
    for (auto* card : cards) {
        y -= card->getContentSize().height;
        card->setPosition({0.f, y});
        scroll->m_contentLayer->addChild(card);
        y -= 4.f;
    }

    scroll->moveToTop();

    if (auto* bar = Scrollbar::create(scroll)) {
        bar->setContentSize({8.f, kListH - 8.f});
        bar->setPosition({(kWidth + kListW) / 2.f + 9.f, 30.f + kListH / 2.f});
        m_listHolder->addChild(bar);
    }
}

CCNode* ModlyCommentsPopup::buildCommentCard(ModlyComment const& comment, float width, int index) {
    auto& repo = ModlyRepo::get();
    auto const* author = repo.user(comment.authorUid);

    // The site prefers the profile name over the one stored on the comment so a
    // renamed user cannot keep an old display name.
    std::string authorName = author && !author->name.empty() ? author->name : comment.authorName;
    if (authorName.empty()) authorName = Localization::get().getString("modly.unknown_author");

    float textX = 12.f + kAvatar + 8.f;
    float textW = width - textX - 12.f;

    auto body = SimpleTextArea::create(comment.text, "chatFont.fnt", 0.5f, textW);
    body->setAnchorPoint({0.f, 1.f});
    body->setColor({255, 255, 255, 220});

    float bodyH = body->getContentSize().height;
    float cardH = std::max(bodyH + 30.f, kAvatar + 16.f);

    auto card = CCNode::create();
    card->setContentSize({width, cardH});
    card->setAnchorPoint({0.f, 0.f});

    auto bg = paimon::SpriteHelper::createRoundedRect(
        width, cardH, 6.f, {1.f, 1.f, 1.f, index % 2 == 0 ? 0.09f : 0.05f});
    if (bg) card->addChild(bg, 0);

    auto avatar = createAvatar(
        author ? repo.photoUrl(*author) : "",
        author && author->hasPhoto,
        authorName, kAvatar);
    avatar->setPosition({12.f + kAvatar / 2.f, cardH - 8.f - kAvatar / 2.f});
    card->addChild(avatar, 1);

    auto nameLabel = CCLabelBMFont::create(authorName.c_str(), "bigFont.fnt");
    nameLabel->setScale(0.36f);
    nameLabel->setAnchorPoint({0.f, 0.5f});
    nameLabel->setPosition({textX, cardH - 12.f});
    fitLabelWidth(nameLabel, textW * 0.5f);
    card->addChild(nameLabel, 1);

    float cursorX = textX + nameLabel->getContentSize().width * nameLabel->getScale() + 5.f;

    if (author) {
        if (auto* seal = createRankSeal(*author, 12.f)) {
            seal->setPosition({cursorX, cardH - 12.f});
            card->addChild(seal, 1);
            cursorX += 16.f;
        }
        if (!author->tags.empty()) {
            auto tag = createPill(translateTag(author->tags[0]), {90, 95, 125}, 0.28f);
            tag->setPosition({cursorX, cardH - 12.f});
            card->addChild(tag, 1);
        }
    }

    auto date = CCLabelBMFont::create(formatModlyDate(comment.date).c_str(), "chatFont.fnt");
    date->setScale(0.4f);
    date->setAnchorPoint({1.f, 0.5f});
    date->setOpacity(140);
    date->setPosition({width - 12.f, cardH - 12.f});
    card->addChild(date, 1);

    body->setPosition({textX, cardH - 24.f});
    card->addChild(body, 1);

    return card;
}

ModlyCommentsPopup* ModlyCommentsPopup::create(ModlyMod const& mod) {
    auto ret = new ModlyCommentsPopup();
    if (ret->init(mod)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

} // namespace paimon::compat_mods
