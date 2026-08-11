#include "ExtendedInfoPopup.hpp"
#include "InfoBlocks.hpp"
#include "LevelStatsPopup.hpp"
#include "../services/GDHistoryClient.hpp"
#include "../services/SearchObjectBuilder.hpp"
#include "../../thumbnails/services/ThumbnailLoader.hpp"
#include "../../../blur/BlurSystem.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/LevelMetadata.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/Shaders.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/GJDifficultySprite.hpp>
#include <Geode/binding/ProfilePage.hpp>
#include <Geode/utils/general.hpp>
#include <algorithm>
#include <cctype>

using namespace geode::prelude;
using namespace paimon::info::blocks;

namespace paimon::info {

namespace {

constexpr float kPopupW = 400.f;
constexpr float kPopupH = 276.f;
constexpr float kListW = 372.f;
constexpr float kTabY = 230.f;
constexpr float kFilterY = 206.f;
constexpr float kListTopFilter = 192.f;
constexpr float kListTopPlain = 218.f;
constexpr float kListTopFull = 238.f;
constexpr float kListBottomOpen = 48.f;
constexpr float kListBottomTucked = 16.f;
constexpr float kChromeRise = 32.f;
constexpr float kRowH = 26.f;
constexpr float kBlockGap = 4.f;

constexpr float kBackdropBlur = 13.f;
constexpr GLubyte kBackdropDim = 125;

constexpr int kZBackdrop = 1;
constexpr int kZListBg = 2;
constexpr int kZList = 3;
constexpr int kZChrome = 4;

constexpr ccColor3B kTabOn{255, 255, 255};
constexpr ccColor3B kTabOff{125, 125, 125};

constexpr int kSummaryTab = -1;

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

void fadeButton(ButtonSprite* sprite, GLubyte opacity) {
    if (!sprite) return;
    sprite->setOpacity(opacity);
    if (sprite->m_label) sprite->m_label->setOpacity(opacity);
    if (sprite->m_BGSprite) sprite->m_BGSprite->setOpacity(opacity);
}

CCNode* makeStatTile(float width, float height, std::vector<char const*> const& frames,
                     std::string const& value, char const* caption, float iconScale = 0.6f) {
    auto tile = CCNode::create();
    tile->setContentSize({width, height});

    if (auto icon = firstFrame(frames)) {
        icon->setScale(iconScale);
        icon->setPosition({width / 2.f, height - 13.f});
        tile->addChild(icon);
    }

    addText(tile, value.c_str(), "bigFont.fnt", 0.4f, kValue,
            {width / 2.f, height - 30.f}, {0.5f, 0.5f}, width - 4.f);
    addText(tile, caption, "chatFont.fnt", 0.36f, kLabel,
            {width / 2.f, 8.f}, {0.5f, 0.5f}, width - 2.f);
    return tile;
}

GJFeatureState featureStateOf(GJGameLevel* level) {
    switch (level->m_isEpic) {
        case 1: return GJFeatureState::Epic;
        case 2: return GJFeatureState::Legendary;
        case 3: return GJFeatureState::Mythic;
        default: break;
    }
    return level->m_featured > 0 ? GJFeatureState::Featured : GJFeatureState::None;
}

}

ExtendedInfoPopup* ExtendedInfoPopup::create(GJGameLevel* level) {
    auto ret = new ExtendedInfoPopup();
    if (ret && ret->init(level)) { ret->autorelease(); return ret; }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ExtendedInfoPopup::init(GJGameLevel* level) {
    if (!level) return false;
    if (!Popup::init(kPopupW, kPopupH)) return false;

    paimon::markDynamicPopup(this);

    m_level = level;
    m_facts = collectFacts(level);

    auto const content = m_mainLayer->getContentSize();
    float const cx = content.width / 2.f;

    this->setTitle(std::string(level->m_levelName).c_str(), "goldFont.fnt", 0.7f, 22.f);
    if (m_title) {
        m_title->limitLabelWidth(kPopupW - 100.f, 0.7f, 0.1f);
        m_title->setZOrder(kZChrome);
    }

    buildTabBar(cx, kTabY);

    m_filter = TextInput::create(kListW - 8.f, "Buscar campo...", "chatFont.fnt");
    m_filter->setPosition({cx, kFilterY});
    m_filter->setScale(0.68f);
    m_filter->setFilter(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 #._-");
    m_filter->setCallback(paimon::ui::safeTextInputCallback<ExtendedInfoPopup>(
        WeakRef<ExtendedInfoPopup>(this), &ExtendedInfoPopup::onFilterChanged));
    m_mainLayer->addChild(m_filter, kZChrome);

    m_listBg = paimon::SpriteHelper::safeCreateScale9("square02_001.png");
    if (m_listBg) {
        m_listBg->setColor({0, 0, 0});
        m_listBg->setOpacity(70);
        m_mainLayer->addChild(m_listBg, kZListBg);
    }

    m_scroll = ScrollLayer::create({kListW, kListTopPlain - kListBottomOpen});
    m_mainLayer->addChild(m_scroll, kZList);

    m_emptyLabel = CCLabelBMFont::create("Sin coincidencias", "bigFont.fnt");
    m_emptyLabel->setScale(0.42f);
    m_emptyLabel->setColor(kLabel);
    m_emptyLabel->setVisible(false);
    m_mainLayer->addChild(m_emptyLabel, kZList);

    buildActionRow(cx);
    applyLayout();

    requestThumbnail();
    rebuildContent();
    requestExactDate();
    return true;
}

void ExtendedInfoPopup::buildActionRow(float centerX) {
    m_actionRow = CCMenu::create();
    m_actionRow->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_actionRow, 101);

    auto copySpr = ButtonSprite::create("Copiar todo", "goldFont.fnt", "GJ_button_02.png", 0.65f);
    auto copyBtn = CCMenuItemSpriteExtra::create(
        copySpr, this, menu_selector(ExtendedInfoPopup::onCopyAll));
    copyBtn->setPosition({centerX - 66.f, 27.f});
    m_actionRow->addChild(copyBtn);

    auto statsSpr = ButtonSprite::create("Tus stats", "goldFont.fnt", "GJ_button_01.png", 0.65f);
    auto statsBtn = CCMenuItemSpriteExtra::create(
        statsSpr, this, menu_selector(ExtendedInfoPopup::onLevelStats));
    statsBtn->setPosition({centerX + 60.f, 27.f});
    m_actionRow->addChild(statsBtn);

    auto arrow = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_03_001.png");
    if (!arrow) return;
    arrow->setScale(0.62f);
    m_drawerBtn = CCMenuItemSpriteExtra::create(
        arrow, this, menu_selector(ExtendedInfoPopup::onDrawer));
    m_drawerBtn->setRotation(90.f);
    m_drawerBtn->setPosition({kPopupW - 24.f, kPopupH - 22.f});
    m_buttonMenu->addChild(m_drawerBtn);
}

bool ExtendedInfoPopup::filterActive() const {
    return !(m_tab == kSummaryTab && m_query.empty());
}

float ExtendedInfoPopup::listTop() const {
    float base = filterActive() ? kListTopFilter : kListTopPlain;
    return base + (kListTopFull - base) * m_drawer;
}

float ExtendedInfoPopup::listBottom() const {
    return kListBottomOpen + (kListBottomTucked - kListBottomOpen) * m_drawer;
}

float ExtendedInfoPopup::viewHeight() const {
    return listTop() - listBottom();
}

float ExtendedInfoPopup::contentFloor() const {
    return kListTopFull - kListBottomTucked;
}

void ExtendedInfoPopup::applyLayout() {
    float const cx = kPopupW / 2.f;
    float const bottom = listBottom();
    float const height = viewHeight();
    auto const chromeAlpha = static_cast<GLubyte>(
        255.f * std::clamp(1.f - m_drawer * 1.6f, 0.f, 1.f));

    if (m_scroll) {
        m_scroll->setContentSize({kListW, height});
        m_scroll->setPosition({cx - kListW / 2.f, bottom});

        if (auto* content = m_scroll->m_contentLayer) {
            float topOffset = height - content->getContentSize().height;
            content->setPositionY(std::clamp(content->getPositionY(),
                                             std::min(0.f, topOffset),
                                             std::max(0.f, topOffset)));
        }
    }
    if (m_listBg) {
        m_listBg->setContentSize({kListW + 6.f, height + 6.f});
        m_listBg->setPosition({cx, bottom + height / 2.f});
    }
    if (m_emptyLabel) m_emptyLabel->setPosition({cx, bottom + height / 2.f});

    if (m_tabMenu) {
        m_tabMenu->setPositionY(kTabY + kChromeRise * m_drawer);
        m_tabMenu->setVisible(m_drawer < 0.97f);
        for (auto* btn : m_tabButtons) {
            if (!btn) continue;
            float dim = btn->getTag() == m_tab ? 1.f : 0.78f;
            fadeButton(typeinfo_cast<ButtonSprite*>(btn->getNormalImage()),
                       static_cast<GLubyte>(chromeAlpha * dim));
        }
    }

    if (m_filter) {
        m_filter->setPositionY(kFilterY + kChromeRise * m_drawer);
        m_filter->setVisible(filterActive() && m_drawer < 0.5f);
    }

    if (m_actionRow) {
        m_actionRow->setPositionY(-20.f * m_drawer);
        m_actionRow->setVisible(m_drawer < 0.97f);
        for (auto* item : CCArrayExt<CCNode*>(m_actionRow->getChildren())) {
            if (auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(item)) {
                fadeButton(typeinfo_cast<ButtonSprite*>(btn->getNormalImage()), chromeAlpha);
            }
        }
    }
}

void ExtendedInfoPopup::onDrawer(CCObject*) {
    m_drawerTarget = m_drawerTarget > 0.5f ? 0.f : 1.f;

    if (m_drawerBtn) {
        m_drawerBtn->stopAllActions();
        m_drawerBtn->runAction(CCEaseInOut::create(
            CCRotateTo::create(0.25f, m_drawerTarget > 0.5f ? -90.f : 90.f), 2.f));
    }

    this->unschedule(schedule_selector(ExtendedInfoPopup::tickDrawer));
    this->schedule(schedule_selector(ExtendedInfoPopup::tickDrawer));
}

void ExtendedInfoPopup::tickDrawer(float dt) {
    m_drawer += (m_drawerTarget - m_drawer) * std::min(1.f, dt * 13.f);

    if (std::fabs(m_drawerTarget - m_drawer) < 0.01f) {
        m_drawer = m_drawerTarget;
        this->unschedule(schedule_selector(ExtendedInfoPopup::tickDrawer));
    }
    applyLayout();
}

void ExtendedInfoPopup::buildTabBar(float centerX, float y) {
    auto menu = CCMenu::create();
    menu->setPosition({centerX, y});
    menu->setContentSize({kListW, 26.f});
    menu->setLayout(RowLayout::create()->setGap(3.f)->setAxisAlignment(AxisAlignment::Center));
    m_mainLayer->addChild(menu, kZChrome);
    m_tabMenu = menu;

    auto addTab = [&](char const* name, int tag) {
        auto spr = ButtonSprite::create(name, "bigFont.fnt", "GJ_button_04.png", 0.7f);
        if (spr) spr->setScale(0.46f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ExtendedInfoPopup::onTab));
        btn->setTag(tag);
        menu->addChild(btn);
        m_tabButtons.push_back(btn);
    };

    addTab("Resumen", kSummaryTab);
    for (int i = 0; i < static_cast<int>(FactTab::Count); i++) {
        addTab(tabName(static_cast<FactTab>(i)), i);
    }

    menu->updateLayout();
    refreshTabButtons();
}

void ExtendedInfoPopup::refreshTabButtons() {
    for (auto* btn : m_tabButtons) {
        if (!btn) continue;
        bool selected = btn->getTag() == m_tab;
        btn->setColor(selected ? kTabOn : kTabOff);
        btn->stopAllActions();
        btn->runAction(CCEaseOut::create(
            CCScaleTo::create(0.14f, selected ? 1.08f : 1.f), 2.f));
    }
}


CCNode* ExtendedInfoPopup::makeHeaderBlock(float width) {
    auto* level = m_level.data();
    constexpr float h = 82.f;
    constexpr float faceX = 42.f;
    constexpr float starsY = 25.f;
    auto block = makeBlock(width, h);

    addThumbnailBanner(block, width, h);

    int const rated = level->m_stars.value();
    int const requested = rated > 0 ? 0 : requestedDifficultyValue(level);
    bool const showRequest = requested != 0;

    if (auto face = GJDifficultySprite::create(showRequest ? requested : difficultyValue(level),
                                               GJDifficultyName::Short)) {
        face->updateFeatureState(featureStateOf(level));
        face->setScale(0.82f);
        face->setPosition({faceX, 51.f});
        block->addChild(face, 1);
    }

    int stars = showRequest ? level->m_starsRequested : rated;
    if (stars > 0) {
        char const* frame = level->isPlatformer()
            ? "GJ_moonsIcon_001.png" : "GJ_starsIcon_001.png";
        auto icon = paimon::SpriteHelper::safeCreateWithFrameName(frame);
        if (icon) {
            icon->setScale(0.5f);
            if (showRequest) icon->setColor({170, 170, 170});
        }

        auto* count = addText(block, std::to_string(stars).c_str(), "bigFont.fnt", 0.4f,
                              showRequest ? kLabel : kValue, {0.f, starsY}, {0.f, 0.5f});
        float const countW = count ? count->getScaledContentSize().width : 0.f;
        float const iconW = icon ? icon->getScaledContentSize().width : 0.f;
        float const gap = icon && count ? 3.f : 0.f;
        float x = faceX - (countW + gap + iconW) / 2.f;

        if (count) count->setPositionX(x);
        x += countW + gap;
        if (icon) {
            icon->setAnchorPoint({0.f, 0.5f});
            icon->setPosition({x, starsY});
            block->addChild(icon, 1);
        }
    }

    if (showRequest) {
        addText(block, "solicitada", "chatFont.fnt", 0.34f, {205, 205, 205},
                {faceX, 10.f}, {0.5f, 0.5f}, 74.f);
    }

    float const textX = 84.f;
    float const textW = width - textX - 12.f;

    addText(block, std::string(level->m_levelName).c_str(), "bigFont.fnt", 0.52f,
            kValue, {textX, h - 20.f}, {0.f, 0.5f}, textW);

    auto creator = std::string(level->m_creatorName);
    if (creator.empty()) creator = "-";
    addText(block, fmt::format("por {}", creator).c_str(), "chatFont.fnt", 0.46f,
            {205, 205, 205}, {textX, h - 41.f}, {0.f, 0.5f}, textW);

    addText(block, fmt::format("ID {}", level->m_levelID.value()).c_str(), "chatFont.fnt",
            0.44f, kAccent, {textX, 19.f}, {0.f, 0.5f}, textW * 0.6f);

    if (level->m_coins > 0) {
        char const* coinFrame = level->m_coinsVerified.value() > 0
            ? "GJ_coinsIcon_001.png" : "GJ_coinsIcon2_001.png";
        float coinX = width - 12.f;
        for (int i = 0; i < std::min(level->m_coins, 3); i++) {
            auto coin = paimon::SpriteHelper::safeCreateWithFrameName(coinFrame);
            if (!coin) break;
            coin->setScale(0.45f);
            coin->setAnchorPoint({1.f, 0.5f});
            coin->setPosition({coinX, 19.f});
            block->addChild(coin, 1);
            coinX -= coin->getScaledContentSize().width + 3.f;
        }
    }

    return block;
}

CCNode* ExtendedInfoPopup::makeDescriptionBlock(float width) {
    auto desc = std::string(m_level->getUnpackedLevelDescription());
    if (desc.empty()) desc = std::string(m_level->m_levelDesc);
    if (desc.empty()) return nullptr;

    constexpr float scale = 0.5f;
    auto label = CCLabelBMFont::create(desc.c_str(), "chatFont.fnt",
                                       (width - 20.f) / scale, kCCTextAlignmentLeft);
    if (!label) return nullptr;

    label->setScale(scale);
    label->setAnchorPoint({0.f, 1.f});
    label->setColor({215, 215, 215});

    float textH = std::max(20.f, label->getContentSize().height * scale);
    float h = textH + 14.f;

    auto block = makeBlock(width, h);
    label->setPosition({10.f, h - 7.f});
    block->addChild(label);
    return block;
}

CCNode* ExtendedInfoPopup::makeStatGrid(float width) {
    auto* level = m_level.data();
    constexpr float h = 54.f;
    auto block = makeBlock(width, h);

    struct TileDef {
        std::vector<char const*> frames;
        std::string value;
        char const* caption;
    };

    int objects = level->m_objectCount.value();
    auto length = lengthName(level->m_levelLength);

    std::vector<TileDef> const tiles = {
        {{"GJ_downloadsIcon_001.png"}, formatThousands(level->m_downloads), "descargas"},
        {{"GJ_likesIcon_001.png", "GJ_like2Icon_001.png", "GJ_starsIcon_001.png"},
         formatThousands(level->m_likes), "likes"},
        {{"GJ_hammerIcon_001.png", "GJ_sBlocksIcon_001.png"},
         objects > 0 ? formatThousands(objects) : "?", "objetos"},
        {{"GJ_timeIcon_001.png"}, length.empty() ? "?" : length, "duracion"},
    };

    float tileW = width / static_cast<float>(tiles.size());
    for (size_t i = 0; i < tiles.size(); i++) {
        auto* tile = makeStatTile(tileW, h, tiles[i].frames, tiles[i].value, tiles[i].caption);
        tile->setPosition({i * tileW, 0.f});
        block->addChild(tile);
    }

    return block;
}

CCNode* ExtendedInfoPopup::makeLikeBar(float width) {
    auto* level = m_level.data();
    int votes = level->m_likes + level->m_dislikes;
    if (votes <= 0) return nullptr;

    constexpr float h = 30.f;
    auto block = makeBlock(width, h);

    float ratio = static_cast<float>(level->m_likes) / static_cast<float>(votes);

    addText(block, "Aprobacion", "chatFont.fnt", 0.44f, kLabel,
            {10.f, h - 10.f}, {0.f, 0.5f});
    addText(block, fmt::format("{:.1f}%  ({} / {})", ratio * 100.f,
                               formatThousands(level->m_likes),
                               formatThousands(votes)).c_str(),
            "chatFont.fnt", 0.44f, kValue, {width - 10.f, h - 10.f}, {1.f, 0.5f});

    float barW = width - 20.f;
    addBar(block, 10.f, 7.f, barW, 6.f, 1.f, {200, 70, 70});
    addBar(block, 10.f, 7.f, barW, 6.f, ratio, {110, 220, 120});
    return block;
}

void ExtendedInfoPopup::addThumbnailBanner(CCNode* block, float width, float height) {
    if (!m_thumbnail) return;

    auto* sprite = CCSprite::createWithTexture(m_thumbnail);
    auto* stencil = paimon::SpriteHelper::createRoundedRectStencil(width, height, 9.f);
    auto* clip = CCClippingNode::create();
    if (!sprite || !stencil || !clip) return;

    auto size = sprite->getContentSize();
    if (size.width <= 0.f || size.height <= 0.f) return;

    sprite->setScale(std::max(width / size.width, height / size.height));
    sprite->setAnchorPoint({0.5f, 0.5f});
    sprite->setPosition({width / 2.f, height / 2.f});
    sprite->setOpacity(0);
    sprite->runAction(CCEaseOut::create(CCFadeTo::create(0.35f, 255), 2.f));

    clip->setStencil(stencil);
    clip->setContentSize({width, height});
    clip->addChild(sprite);

    auto dark = CCLayerColor::create(ccc4(0, 0, 0, 170));
    dark->setContentSize({width, height});
    clip->addChild(dark, 1);

    block->addChild(clip, -1);
}

void ExtendedInfoPopup::requestThumbnail() {
    int levelID = m_level ? m_level->m_levelID.value() : 0;
    if (levelID <= 0) return;

    if (auto* cached = ThumbnailLoader::get().tryGetCachedTexture(levelID, false)) {
        m_thumbnail = cached;
        applyBackdrop(cached);
        return;
    }

    auto self = WeakRef<ExtendedInfoPopup>(this);
    ThumbnailLoader::get().requestLoad(levelID, fmt::format("{}.png", levelID),
        [self](CCTexture2D* texture, bool success) {
            if (!success || !texture) return;
            auto ref = self.lock();
            if (!ref || !ref->getParent()) return;
            auto* popup = static_cast<ExtendedInfoPopup*>(ref.data());
            popup->m_thumbnail = texture;
            popup->applyBackdrop(texture);
            popup->rebuildContent();
        },
        ThumbnailLoader::PriorityHero, false, ThumbnailLoader::Quality::High);
}

void ExtendedInfoPopup::applyBackdrop(CCTexture2D* texture) {
    if (!texture || m_hasBackdrop || !m_mainLayer) return;

    CCSize const area{kPopupW - 6.f, kPopupH - 6.f};

    if (auto* ready = Shaders::createPopupPaimonBlurredSprite(texture, area, kBackdropBlur)) {
        installBackdrop(ready, area, false);
        return;
    }

    Ref<CCTexture2D> raw = texture;
    auto self = WeakRef<ExtendedInfoPopup>(this);
    BlurSystem::getInstance()->buildPaimonBlurPriority(
        texture, area, kBackdropBlur, std::string{},
        [self, area, raw](CCSprite* blurred) {
            auto ref = self.lock();
            if (!ref) return;
            auto* sprite = blurred ? blurred : CCSprite::createWithTexture(raw);
            if (!sprite) return;
            static_cast<ExtendedInfoPopup*>(ref.data())->installBackdrop(sprite, area, true);
        });
}

void ExtendedInfoPopup::installBackdrop(CCSprite* blurred, CCSize const& area, bool fadeIn) {
    if (m_hasBackdrop || !m_mainLayer) return;

    auto* backdrop = makeBackdrop(blurred, area, kBackdropDim, fadeIn);
    if (!backdrop) return;

    backdrop->setPosition({kPopupW / 2.f, kPopupH / 2.f});
    m_mainLayer->addChild(backdrop, kZBackdrop);
    m_hasBackdrop = true;
}

CCNode* ExtendedInfoPopup::makeSongBlock(float width) {
    auto* level = m_level.data();
    auto songName = std::string(level->getSongName());
    if (songName.empty()) return nullptr;

    constexpr float h = 30.f;
    auto block = makeBlock(width, h);

    float textX = 10.f;
    if (auto icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_musicIcon_001.png")) {
        icon->setScale(0.55f);
        icon->setAnchorPoint({0.f, 0.5f});
        icon->setPosition({10.f, h / 2.f});
        block->addChild(icon);
        textX = 10.f + icon->getScaledContentSize().width + 6.f;
    }

    std::string right = level->m_songID > 0
        ? fmt::format("ID {}", level->m_songID)
        : fmt::format("track {}", level->m_audioTrack);

    addText(block, right.c_str(), "chatFont.fnt", 0.42f, kAccent,
            {width - 10.f, h / 2.f}, {1.f, 0.5f});
    addText(block, songName.c_str(), "chatFont.fnt", 0.46f, kValue,
            {textX, h / 2.f}, {0.f, 0.5f}, width - textX - 70.f);

    return block;
}

void ExtendedInfoPopup::buildSummary(CCNode* content) {
    std::vector<CCNode*> blocks;
    auto push = [&](CCNode* node) { if (node) blocks.push_back(node); };

    push(makeHeaderBlock(kListW));
    push(makeDescriptionBlock(kListW));
    push(makeStatGrid(kListW));
    push(makeLikeBar(kListW));
    push(makeSongBlock(kListW));

    float totalH = 2.f;
    for (auto* block : blocks) totalH += block->getContentSize().height + kBlockGap;
    totalH = std::max(contentFloor(), totalH);
    content->setContentSize({kListW, totalH});

    float y = totalH - 2.f;
    for (auto* block : blocks) {
        y -= block->getContentSize().height;
        block->setPosition({0.f, y});
        content->addChild(block);
        y -= kBlockGap;
    }
}


void ExtendedInfoPopup::buildFactRows(CCNode* content) {
    auto needle = lower(m_query);
    for (int i = 0; i < static_cast<int>(m_facts.size()); i++) {
        auto const& fact = m_facts[i];
        if (needle.empty()) {
            if (static_cast<int>(fact.tab) != m_tab) continue;
        } else if (lower(fact.label).find(needle) == std::string::npos
                && lower(fact.value).find(needle) == std::string::npos) {
            continue;
        }
        m_shown.push_back(i);
    }

    if (m_emptyLabel) m_emptyLabel->setVisible(m_shown.empty());

    float totalH = std::max(contentFloor(), m_shown.size() * kRowH + 4.f);
    content->setContentSize({kListW, totalH});

    auto rowMenu = CCMenu::create();
    rowMenu->setPosition({0.f, 0.f});
    rowMenu->setContentSize({kListW, totalH});
    content->addChild(rowMenu, 2);

    float y = totalH - 2.f;

    for (int slot = 0; slot < static_cast<int>(m_shown.size()); slot++) {
        auto const& fact = m_facts[m_shown[slot]];
        y -= kRowH;

        auto band = CCLayerColor::create(ccc4(255, 255, 255, slot % 2 == 0 ? 14 : 0));
        band->setContentSize({kListW, kRowH});
        band->setPosition({0.f, y});
        content->addChild(band, 0);

        addText(content, fact.label.c_str(), "chatFont.fnt", 0.52f, kLabel,
                {10.f, y + kRowH / 2.f}, {0.f, 0.5f}, kListW * 0.44f);
        addText(content, fact.value.c_str(), "chatFont.fnt", 0.56f,
                fact.accent ? kAccent : kValue,
                {kListW - 26.f, y + kRowH / 2.f}, {1.f, 0.5f}, kListW * 0.5f);

        auto hit = CCLayerColor::create(ccc4(255, 255, 255, 0));
        hit->setContentSize({kListW - 8.f, kRowH - 2.f});
        auto btn = CCMenuItemSpriteExtra::create(hit, this, menu_selector(ExtendedInfoPopup::onRow));
        btn->setContentSize({kListW - 8.f, kRowH - 2.f});
        btn->setAnchorPoint({0.f, 0.f});
        btn->setPosition({4.f, y + 1.f});
        btn->setTag(slot);
        rowMenu->addChild(btn);

        addText(content, fact.action == FactAction::None ? ">" : "->", "chatFont.fnt",
                0.44f, {130, 130, 130}, {kListW - 12.f, y + kRowH / 2.f}, {0.5f, 0.5f});
    }
}

void ExtendedInfoPopup::rebuildContent() {
    if (!m_scroll) return;

    auto* content = m_scroll->m_contentLayer;
    content->removeAllChildren();
    m_shown.clear();

    bool summary = !filterActive();
    if (m_emptyLabel) m_emptyLabel->setVisible(false);

    applyLayout();

    if (summary) buildSummary(content);
    else buildFactRows(content);

    m_scroll->moveToTop();

    content->stopAllActions();
    content->setPositionX(14.f);
    content->runAction(CCEaseOut::create(
        CCMoveTo::create(0.16f, {0.f, content->getPositionY()}), 2.f));
}

void ExtendedInfoPopup::onTab(CCObject* sender) {
    int tag = static_cast<CCNode*>(sender)->getTag();
    if (tag != kSummaryTab && (tag < 0 || tag >= static_cast<int>(FactTab::Count))) return;

    m_tab = tag;
    refreshTabButtons();
    rebuildContent();
}

void ExtendedInfoPopup::onLevelStats(CCObject*) {
    if (!m_level) return;
    if (auto* popup = LevelStatsPopup::create(m_level)) popup->show();
}

void ExtendedInfoPopup::onFilterChanged(std::string const& text) {
    m_query = text;
    rebuildContent();
}

void ExtendedInfoPopup::onRow(CCObject* sender) {
    int slot = static_cast<CCNode*>(sender)->getTag();
    if (slot < 0 || slot >= static_cast<int>(m_shown.size())) return;

    auto const& fact = m_facts[m_shown[slot]];
    if (fact.action != FactAction::None) {
        runAction(fact);
        return;
    }

    auto text = fact.copyText.empty() ? fact.value : fact.copyText;
    clipboard::write(text);
    PaimonNotify::create(fmt::format("{} copiado", fact.label), NotificationIcon::Success)->show();
}

void ExtendedInfoPopup::runAction(Fact const& fact) {
    switch (fact.action) {
        case FactAction::OpenOriginal:
            this->onClose(nullptr);
            openLevelByID(fact.actionArg);
            return;
        case FactAction::OpenSong:
            this->onClose(nullptr);
            openLevelsWithSong(fact.actionArg);
            return;
        case FactAction::OpenCreator:
            if (auto page = ProfilePage::create(fact.actionArg, false)) {
                page->show();
            }
            return;
        default:
            return;
    }
}

void ExtendedInfoPopup::onCopyAll(CCObject*) {
    if (!m_level) return;

    auto json = paimon::collectLevelMetadata(m_level.data());
    if (json.empty()) {
        PaimonNotify::create("No hay datos que copiar", NotificationIcon::Error)->show();
        return;
    }

    clipboard::write(json);
    PaimonNotify::create("Datos del nivel copiados como JSON", NotificationIcon::Success)->show();
}

void ExtendedInfoPopup::requestExactDate() {
    if (!m_level) return;

    auto self = WeakRef<ExtendedInfoPopup>(this);
    gdhistory::requestLevelDate(m_level->m_levelID.value(),
        [self](std::string const& date) {
            auto ref = self.lock();
            if (!ref) return;
            static_cast<ExtendedInfoPopup*>(ref.data())->applyExactDate(date);
        });
}

void ExtendedInfoPopup::applyExactDate(std::string const& date) {
    if (date.empty()) return;

    auto existing = std::find_if(m_facts.begin(), m_facts.end(),
        [](Fact const& fact) { return fact.label == "Fecha exacta"; });
    if (existing != m_facts.end()) return;

    Fact fact;
    fact.label = "Fecha exacta";
    fact.value = date;
    fact.tab = FactTab::Level;
    fact.accent = true;

    auto uploadRow = std::find_if(m_facts.begin(), m_facts.end(),
        [](Fact const& row) { return row.label == "Subido"; });
    if (uploadRow != m_facts.end()) m_facts.insert(uploadRow + 1, std::move(fact));
    else m_facts.push_back(std::move(fact));

    rebuildContent();
}

void ExtendedInfoPopup::onClose(CCObject* sender) {
    paimon::ui::detachGeodeTextInput(m_filter);
    m_filter = nullptr;
    Popup::onClose(sender);
}

}
