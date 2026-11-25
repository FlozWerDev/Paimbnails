#include "PaimbnailsNewsPanel.hpp"

#include "NewsRichText.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../compat-mods/ui/ModlyUIHelpers.hpp"

#include <Geode/ui/LazySprite.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/utils/web.hpp>
#include <algorithm>

using namespace geode::prelude;
using paimon::loc::tr;

namespace paimon::news {

namespace {

constexpr float kSideMargin = 6.f;   // list edge to card
constexpr float kPadding = 8.f;      // card edge to content
constexpr float kItemGap = 6.f;
constexpr float kRowGap = 5.f;
constexpr float kAvatarSize = 16.f;
constexpr float kAuthorScale = 0.5f;
constexpr float kDateScale = 0.42f;
constexpr float kTitleScale = 0.55f;
constexpr float kBodyScale = 0.5f;
constexpr float kImageMinHeight = 50.f;
constexpr float kImageMaxHeight = 170.f;

// link previews, laid out like discord's embeds.
constexpr float kEmbedBarWidth = 3.f;
constexpr float kEmbedPadding = 6.f;
constexpr float kEmbedRowGap = 3.f;
constexpr float kEmbedThumbSize = 44.f;
constexpr float kEmbedImageMaxHeight = 150.f;
constexpr float kPlayButtonSize = 34.f;
constexpr float kEmbedProviderScale = 0.4f;
constexpr float kEmbedAuthorScale = 0.45f;
constexpr float kEmbedTitleScale = 0.5f;
constexpr float kEmbedTextScale = 0.45f;
constexpr float kEmbedFooterScale = 0.38f;
constexpr ccColor3B kEmbedBackground{43, 45, 49};
constexpr ccColor3B kEmbedDefaultBar{79, 84, 92};
constexpr ccColor3B kEmbedText{219, 222, 225};

// link confirmation: the address shown is cut so it fits the popup.
constexpr std::size_t kLinkPreviewChars = 40;
constexpr float kLinkPopupWidth = 360.f;

// a node placed later, once the card height is known.
struct Placed {
    CCNode* node;
    float x;
    float top;  // distance from the card's top edge to the node's top edge
};

void placeAll(CCNode* parent, std::vector<Placed> const& placed, float height) {
    for (auto const& p : placed) {
        float const anchorY = p.node->getAnchorPoint().y;
        float const nodeHeight = p.node->getScaledContentSize().height;
        // convert the top offset into the node's own anchor.
        p.node->setPosition({p.x, height - p.top - nodeHeight * (1.f - anchorY)});
        parent->addChild(p.node);
    }
}

// box with the picture's own aspect ratio, as large as the limits allow.
CCSize fitImage(int width, int height, float maxWidth, float maxHeight) {
    float const aspect = width > 0 && height > 0
        ? static_cast<float>(height) / static_cast<float>(width)
        : 9.f / 16.f;
    CCSize size{maxWidth, maxWidth * aspect};
    if (size.height > maxHeight) {
        size.height = maxHeight;
        size.width = std::min(maxWidth, maxHeight / aspect);
    }
    size.height = std::max(size.height, std::min(kImageMinHeight, maxHeight));
    return size;
}

// "host/path" without the scheme or query, short enough for one popup line.
std::string linkPreview(std::string const& url) {
    std::string_view view = url;
    if (auto const scheme = view.find("://"); scheme != std::string_view::npos) view.remove_prefix(scheme + 3);
    view = view.substr(0, view.find_first_of("?#"));
    if (view.ends_with('/')) view.remove_suffix(1);
    std::string out;
    for (char c : view) {
        if (c != '<' && c != '>') out.push_back(c);
    }
    if (out.size() > kLinkPreviewChars) {
        out.resize(kLinkPreviewChars - 3);
        out += "...";
    }
    return out;
}

void loadImageInto(CCNode* box, std::string const& url, CCSize const& size) {
    auto* sprite = LazySprite::create(size, false);
    if (!sprite) return;
    sprite->setPosition({size.width / 2.f, size.height / 2.f});
    sprite->setOpacity(0);
    box->addChild(sprite, 1);

    auto* spinner = LoadingSpinner::create(std::min(24.f, size.height * 0.5f));
    spinner->setPosition({size.width / 2.f, size.height / 2.f});
    box->addChild(spinner, 2);

    WeakRef<LazySprite> weakSprite = sprite;
    WeakRef<CCNode> weakBox = box;
    WeakRef<LoadingSpinner> weakSpinner = spinner;
    sprite->setLoadCallback([weakSprite, weakBox, weakSpinner, size](Result<> result) {
        if (auto spin = weakSpinner.lock()) spin->removeFromParent();
        auto spr = weakSprite.lock();
        auto owner = weakBox.lock();
        if (!spr || !owner || !spr->getParent()) return;
        if (!result.isOk()) {
            spr->removeFromParent();
            auto* failed = CCLabelBMFont::create(
                tr("pai.news.image_failed", "Image unavailable").c_str(), "chatFont.fnt");
            failed->setScale(0.45f);
            failed->setColor(paimon::ui::palette::dim);
            failed->setPosition({size.width / 2.f, size.height / 2.f});
            owner->addChild(failed, 1);
            return;
        }
        auto const content = spr->getContentSize();
        if (content.width <= 0.f || content.height <= 0.f) return;
        spr->setScale(std::min(size.width / content.width, size.height / content.height));
        spr->runAction(CCFadeIn::create(0.2f));
    });
    sprite->loadFromUrl(url);
}

CCNode* createImageBox(std::string const& url, CCSize const& size) {
    // the inset cascades its opacity, so the image sits beside it, not inside.
    auto* box = CCNode::create();
    box->setContentSize(size);
    box->setAnchorPoint({0.f, 1.f});
    box->addChild(paimon::ui::makeInset(size, 70), 0);
    loadImageInto(box, url, size);
    return box;
}

} // namespace

PaimbnailsNewsPanel* PaimbnailsNewsPanel::create(CCSize const& size, std::string channelId) {
    auto* ret = new PaimbnailsNewsPanel();
    if (ret->init(size, std::move(channelId))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool PaimbnailsNewsPanel::init(CCSize const& size, std::string channelId) {
    if (!CCNode::init()) return false;
    m_channelId = std::move(channelId);
    this->setContentSize(size);
    this->setAnchorPoint({0.f, 0.f});

    // same frame the vanilla popup uses for its pages.
    m_frame = GJCommentListLayer::create(nullptr, "", {0, 0, 0, 50}, size.width, size.height, false);
    if (!m_frame) return false;
    m_frame->setID("frame"_spr);
    m_frame->setPosition({0.f, 0.f});
    this->addChild(m_frame);

    m_scroll = ScrollLayer::create(size);
    if (m_scroll) m_scroll->setAnchorPoint({0.f, 0.f});
    m_scroll->setID("scroll"_spr);
    m_scroll->setPosition({0.f, 0.f});
    // links are buttons: a drag that starts on one still scrolls the list.
    m_scroll->setStealingTouches(true);
    // the vanilla page keeps the wheel until this tab is opened.
    m_scroll->setMouseEnabled(false);
    m_frame->addChild(m_scroll, 6);

    m_status = CCLabelBMFont::create("", "bigFont.fnt");
    m_status->setID("status"_spr);
    m_status->setScale(0.5f);
    m_status->setPosition({size.width / 2.f, size.height / 2.f + 10.f});
    m_status->setVisible(false);
    m_frame->addChild(m_status, 7);

    m_statusMenu = CCMenu::create();
    m_statusMenu->setID("status-menu"_spr);
    m_statusMenu->setPosition({size.width / 2.f, size.height / 2.f - 18.f});
    m_statusMenu->setContentSize({0.f, 0.f});
    m_statusMenu->setVisible(false);
    m_frame->addChild(m_statusMenu, 7);

    auto* retrySprite = CCSprite::createWithSpriteFrameName("GJ_updateBtn_001.png");
    retrySprite->setScale(0.7f);
    auto* retry = CCMenuItemSpriteExtra::create(
        retrySprite, this, menu_selector(PaimbnailsNewsPanel::onRetry));
    retry->setID("retry-button"_spr);
    m_statusMenu->addChild(retry);

    // prefetch so the tab is usually ready by the time it is opened.
    requestFeed(false);
    return true;
}

void PaimbnailsNewsPanel::setActive(bool active) {
    this->setVisible(active);
    if (m_scroll) m_scroll->setMouseEnabled(active);
    if (active) requestFeed(false);
}

void PaimbnailsNewsPanel::requestFeed(bool force) {
    if (m_loading) return;
    m_loading = true;
    if (m_shownRevision == 0) {
        showStatus(tr("pai.news.loading", "Loading news..."), false);
    }

    // the service may answer synchronously from its cache, even during init().
    WeakRef<PaimbnailsNewsPanel> weak = this;
    NewsService::get().fetch(m_channelId, force, [weak](bool ok, NewsFeed const& feed) {
        if (auto self = weak.lock()) self->onFeed(ok, feed);
    });
}

void PaimbnailsNewsPanel::onFeed(bool ok, NewsFeed const& feed) {
    m_loading = false;
    if (!ok) {
        // keep whatever is already on screen; only an empty panel shows the error.
        if (m_shownRevision == 0) {
            showStatus(tr("pai.news.error", "Could not load the news"), true);
        }
        return;
    }
    if (feed.revision == m_shownRevision) return;

    rebuildList(feed);
    m_shownRevision = feed.revision;
    if (feed.items.empty()) {
        showStatus(tr("pai.news.empty", "No news yet"), false);
    } else {
        hideStatus();
    }
}

void PaimbnailsNewsPanel::showStatus(std::string const& text, bool withRetry) {
    if (!m_status) return;
    m_status->setString(text.c_str());
    m_status->limitLabelWidth(this->getContentSize().width - 30.f, 0.5f, 0.2f);
    m_status->setVisible(true);
    if (m_statusMenu) m_statusMenu->setVisible(withRetry);
}

void PaimbnailsNewsPanel::hideStatus() {
    if (m_status) m_status->setVisible(false);
    if (m_statusMenu) m_statusMenu->setVisible(false);
}

void PaimbnailsNewsPanel::onRetry(CCObject*) {
    requestFeed(true);
}

void PaimbnailsNewsPanel::rebuildList(NewsFeed const& feed) {
    if (!m_scroll) return;
    auto* content = m_scroll->m_contentLayer;
    content->removeAllChildren();

    float const width = m_scroll->getContentSize().width;
    float const viewHeight = m_scroll->getContentSize().height;

    std::vector<CCNode*> cards;
    float total = kItemGap;
    for (auto const& item : feed.items) {
        if (auto* card = createItemNode(item, width)) {
            cards.push_back(card);
            total += card->getContentSize().height + kItemGap;
        }
    }

    float const contentHeight = std::max(viewHeight, total);
    content->setContentSize({width, contentHeight});
    float y = contentHeight - kItemGap;
    for (auto* card : cards) {
        y -= card->getContentSize().height;
        card->setPosition({0.f, y});
        content->addChild(card);
        y -= kItemGap;
    }
    m_scroll->scrollToTop();
}

CCNode* PaimbnailsNewsPanel::createItemNode(NewsItem const& item, float width) {
    float const cardWidth = width - kSideMargin * 2.f;
    float const textWidth = cardWidth - kPadding * 2.f;
    float const left = kSideMargin + kPadding;

    auto* card = CCNode::create();
    card->setAnchorPoint({0.f, 0.f});

    std::vector<Placed> placed;
    float cursor = kPadding;

    // header: avatar, author and date on one row.
    auto author = toChatFontText(item.author, 120);
    if (author.empty()) author = "Paimbnails";
    float textLeft = left;
    if (auto* avatar = paimon::compat_mods::createAvatar(
            item.avatarUrl, !item.avatarUrl.empty(), author, kAvatarSize)) {
        avatar->setAnchorPoint({0.f, 1.f});
        placed.push_back({avatar, left, cursor});
        textLeft += kAvatarSize + 5.f;
    }

    auto* date = CCLabelBMFont::create(formatLocalDate(item.timestamp).c_str(), "chatFont.fnt");
    date->setScale(kDateScale);
    date->setColor(paimon::ui::palette::dim);
    date->setAnchorPoint({1.f, 0.5f});

    auto* authorLabel = CCLabelBMFont::create(author.c_str(), "chatFont.fnt");
    authorLabel->setScale(kAuthorScale);
    authorLabel->setColor(paimon::ui::palette::gold);
    authorLabel->setAnchorPoint({0.f, 0.5f});
    float const authorMax = textWidth - (textLeft - left) - date->getScaledContentSize().width - 8.f;
    paimon::ui::shrinkLabelToFit(authorLabel, std::max(30.f, authorMax));

    // centre both labels on the avatar row.
    float const rowMiddle = cursor + kAvatarSize / 2.f;
    placed.push_back({authorLabel, textLeft, rowMiddle - authorLabel->getScaledContentSize().height / 2.f});
    placed.push_back({date, left + textWidth, rowMiddle - date->getScaledContentSize().height / 2.f});
    cursor += kAvatarSize + kRowGap;

    auto const title = toAsciiFontText(item.title, 400);
    if (!title.empty()) {
        auto* label = CCLabelBMFont::create(
            title.c_str(), "goldFont.fnt", textWidth / kTitleScale, kCCTextAlignmentLeft);
        label->setScale(kTitleScale);
        label->setAnchorPoint({0.f, 1.f});
        placed.push_back({label, left, cursor});
        cursor += label->getScaledContentSize().height + kRowGap;
    }

    RichTextStyle bodyStyle;
    bodyStyle.scale = kBodyScale;
    if (auto* body = createRichText(item.body, textWidth, bodyStyle, linkHandler())) {
        placed.push_back({body, left, cursor});
        cursor += body->getContentSize().height + kRowGap;
    }

    // discord order: attachments first, then link previews.
    if (!item.imageUrl.empty()) {
        auto* box = createImageBox(
            item.imageUrl, fitImage(item.imageWidth, item.imageHeight, textWidth, kImageMaxHeight));
        placed.push_back({box, left, cursor});
        cursor += box->getContentSize().height + kRowGap;
    }

    for (auto const& embed : item.embeds) {
        if (auto* node = createEmbedNode(embed, textWidth)) {
            placed.push_back({node, left, cursor});
            cursor += node->getContentSize().height + kRowGap;
        }
    }

    float const height = cursor - kRowGap + kPadding;
    card->setContentSize({width, height});

    auto* background = paimon::ui::makeInset({cardWidth, height}, 80);
    background->setPosition({kSideMargin, 0.f});
    card->addChild(background, -1);

    placeAll(card, placed, height);
    return card;
}

CCNode* PaimbnailsNewsPanel::createEmbedNode(NewsEmbed const& embed, float width) {
    auto const provider = toChatFontText(embed.provider, 400);
    auto const author = toChatFontText(embed.author, 400);
    auto const title = toChatFontText(embed.title, 400);
    auto footer = toChatFontText(embed.footer, 400);
    if (auto const date = formatLocalDate(embed.timestamp); !date.empty()) {
        footer = footer.empty() ? date : footer + " \xE2\x80\xA2 " + date;
    }

    bool const hasText = !provider.empty() || !author.empty() || !title.empty()
        || !embed.description.empty() || !footer.empty();
    if (!hasText) {
        // discord shows a bare image/gif/video embed as the picture alone.
        bool const useImage = !embed.imageUrl.empty();
        auto const& url = useImage ? embed.imageUrl : embed.thumbnailUrl;
        if (url.empty()) return nullptr;
        auto const size = fitImage(
            useImage ? embed.imageWidth : embed.thumbnailWidth,
            useImage ? embed.imageHeight : embed.thumbnailHeight, width, kImageMaxHeight);
        auto* box = createImageBox(url, size);
        if (useImage && embed.video) addPlayButton(box, size, embed.url);
        return box;
    }

    float const left = kEmbedBarWidth + kEmbedPadding;
    float const inner = width - left - kEmbedPadding;
    bool const hasThumb = !embed.thumbnailUrl.empty();
    // the thumbnail sits top-right and the text wraps beside it.
    float const textWidth = hasThumb ? inner - kEmbedThumbSize - kEmbedPadding : inner;
    auto const onLink = linkHandler();

    std::vector<Placed> placed;
    float cursor = kEmbedPadding;
    auto addText = [&](std::vector<TextSpan> const& spans, float scale, ccColor3B color, ccColor3B linkColor) {
        RichTextStyle style;
        style.scale = scale;
        style.color = color;
        style.linkColor = linkColor;
        if (auto* node = createRichText(spans, textWidth, style, onLink)) {
            placed.push_back({node, left, cursor});
            cursor += node->getContentSize().height + kEmbedRowGap;
        }
    };
    auto const white = paimon::ui::palette::text;
    auto const dim = paimon::ui::palette::dim;
    if (!provider.empty()) addText({{provider, {}}}, kEmbedProviderScale, dim, dim);
    if (!author.empty()) addText({{author, embed.authorUrl}}, kEmbedAuthorScale, white, white);
    if (!title.empty()) addText({{title, embed.url}}, kEmbedTitleScale, white, kLinkColor);
    addText(embed.description, kEmbedTextScale, kEmbedText, kLinkColor);

    if (hasThumb) {
        auto const size = fitImage(embed.thumbnailWidth, embed.thumbnailHeight, kEmbedThumbSize, kEmbedThumbSize);
        auto* thumb = createImageBox(embed.thumbnailUrl, size);
        placed.push_back({thumb, left + inner - size.width, kEmbedPadding});
        cursor = std::max(cursor, kEmbedPadding + size.height + kEmbedRowGap);
    }

    if (!embed.imageUrl.empty()) {
        auto const size = fitImage(embed.imageWidth, embed.imageHeight, inner, kEmbedImageMaxHeight);
        auto* box = createImageBox(embed.imageUrl, size);
        if (embed.video) addPlayButton(box, size, embed.url);
        placed.push_back({box, left, cursor});
        cursor += size.height + kEmbedRowGap;
    }

    if (!footer.empty()) {
        auto* label = CCLabelBMFont::create(footer.c_str(), "chatFont.fnt");
        label->setScale(kEmbedFooterScale);
        label->setColor(dim);
        label->setAnchorPoint({0.f, 1.f});
        paimon::ui::shrinkLabelToFit(label, inner);
        placed.push_back({label, left, cursor});
        cursor += label->getScaledContentSize().height + kEmbedRowGap;
    }

    float const height = cursor - kEmbedRowGap + kEmbedPadding;
    auto* node = CCNode::create();
    node->setAnchorPoint({0.f, 1.f});
    node->setContentSize({width, height});

    node->addChild(paimon::ui::makeInset({width, height}, 150, kEmbedBackground), -1);
    ccColor3B const bar = embed.color == 0
        ? kEmbedDefaultBar
        : ccColor3B{
            static_cast<GLubyte>((embed.color >> 16) & 0xFF),
            static_cast<GLubyte>((embed.color >> 8) & 0xFF),
            static_cast<GLubyte>(embed.color & 0xFF)};
    auto* stripe = CCLayerColor::create({bar.r, bar.g, bar.b, 255}, kEmbedBarWidth, std::max(0.f, height - 4.f));
    stripe->setPosition({1.f, 2.f});
    node->addChild(stripe, 0);

    placeAll(node, placed, height);
    return node;
}

void PaimbnailsNewsPanel::addPlayButton(CCNode* box, CCSize const& size, std::string const& url) {
    auto* sprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_playBtn2_001.png");
    if (!sprite) return;
    float const side = std::min({kPlayButtonSize, size.width * 0.5f, size.height * 0.5f});
    sprite->setScale(side / std::max(1.f, sprite->getContentSize().width));

    auto* menu = paimon::ui::makeZeroMenu();
    box->addChild(menu, 3);
    // discord plays the video in place; the closest thing here is opening the post.
    auto* button = CCMenuItemExt::createSpriteExtra(
        sprite, [onLink = linkHandler(), url](CCMenuItemSpriteExtra* sender) {
            if (!url.empty()) onLink(url, sender);
        });
    button->setEnabled(!url.empty());
    button->setPosition({size.width / 2.f, size.height / 2.f});
    menu->addChild(button);
}

LinkCallback PaimbnailsNewsPanel::linkHandler() {
    WeakRef<PaimbnailsNewsPanel> weak = this;
    return [weak](std::string const& url, CCNode* sender) {
        if (auto self = weak.lock()) self->openLink(url, sender);
    };
}

void PaimbnailsNewsPanel::openLink(std::string const& url, CCNode* sender) {
    if (!isAllowedLinkUrl(url)) return;
    // a link scrolled out of view can still sit under a touch next to the list.
    if (sender && m_scroll) {
        auto const& size = sender->getContentSize();
        auto const world = sender->convertToWorldSpace({size.width / 2.f, size.height / 2.f});
        auto const local = m_scroll->convertToNodeSpace(world);
        if (!CCRect{CCPointZero, m_scroll->getContentSize()}.containsPoint(local)) return;
    }

    auto const body = fmt::format(
        fmt::runtime(tr("pai.news.link.body", "This opens in your browser:\n<cy>{}</c>")), linkPreview(url));
    geode::createQuickPopup(
        tr("pai.news.link.title", "Open link").c_str(), body,
        tr("pai.news.link.cancel", "Cancel").c_str(), tr("pai.news.link.open", "Open").c_str(),
        kLinkPopupWidth,
        [url](FLAlertLayer*, bool confirmed) {
            if (confirmed) web::openLinkInBrowser(url);
        });
}

} // namespace paimon::news
