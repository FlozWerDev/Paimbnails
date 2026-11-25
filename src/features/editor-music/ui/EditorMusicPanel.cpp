#include "EditorMusicPanel.hpp"

#include "EditorMusicPickerPopup.hpp"
#include "EditorNowPlayingToast.hpp"
#include "../services/EditorMusicPlayer.hpp"
#include "../../menu-music/services/MenuMusicLibrary.hpp"
#include "../../menu-music/services/SongCoverCache.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/TextureBudget.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCScale9Sprite.h>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using paimon::menumusic::MenuMusicLibrary;
using paimon::menumusic::SongCoverCache;

namespace paimon::editormusic {

namespace {

constexpr float kDeckW = 264.f;
constexpr float kDeckH = 92.f;
constexpr float kMiniW = 198.f;
constexpr float kMiniH = 38.f;

constexpr float kArtDeckSize = 48.f;
constexpr float kArtMiniSize = 24.f;

constexpr float kSeekX = 66.f;
constexpr float kSeekW = 186.f;
constexpr float kSeekY = 40.f;
constexpr float kSeekVisualH = 7.f;

constexpr float kVolX = 205.f;
constexpr float kVolW = 47.f;
constexpr float kVolY = 18.f;
constexpr float kVolVisualH = 6.f;

constexpr float kGrabSlack = 7.f;
constexpr int kMenuPriority = -260;
constexpr int kPanelPriority = -250;

constexpr char const* kOpenKey = "editor-music-open";
constexpr char const* kCollapsedKey = "editor-music-collapsed";
constexpr char const* kPosXKey = "editor-music-pos-x";
constexpr char const* kPosYKey = "editor-music-pos-y";

constexpr ccColor3B kAccentGold = {255, 215, 95};
constexpr ccColor3B kAccentAqua = {105, 225, 255};
constexpr ccColor3B kDimText = {165, 178, 202};
constexpr float kPi = 3.14159265358979323846f;

float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }

float easeOutCubic(float t) {
    float const u = 1.f - clamp01(t);
    return 1.f - u * u * u;
}

float easeInOutCubic(float t) {
    t = clamp01(t);
    return t < 0.5f ? 4.f * t * t * t : 1.f - 0.5f * std::pow(-2.f * t + 2.f, 3.f);
}

float easeOutBack(float t) {
    t = clamp01(t);
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.f;
    float const u = t - 1.f;
    return 1.f + c3 * u * u * u + c1 * u * u;
}

ccColor3B mixColor(ccColor3B a, ccColor3B b, float t) {
    t = clamp01(t);
    return {
        static_cast<GLubyte>(lerp(static_cast<float>(a.r), static_cast<float>(b.r), t) + 0.5f),
        static_cast<GLubyte>(lerp(static_cast<float>(a.g), static_cast<float>(b.g), t) + 0.5f),
        static_cast<GLubyte>(lerp(static_cast<float>(a.b), static_cast<float>(b.b), t) + 0.5f),
    };
}

GLubyte toByte(float a) {
    return static_cast<GLubyte>(clamp01(a) * 255.f + 0.5f);
}

std::string formatTime(int ms) {
    if (ms < 0) ms = 0;
    int total = ms / 1000;
    return fmt::format("{}:{:02}", total / 60, total % 60);
}

CCSprite* firstFrame(std::initializer_list<char const*> frames) {
    for (auto const* frame : frames) {
        if (auto* sprite = SpriteHelper::safeCreateWithFrameName(frame)) return sprite;
    }
    return nullptr;
}

void replaceButtonIcon(CCMenuItemSpriteExtra* btn, CCSprite* sprite, float size) {
    if (!btn || !sprite) return;
    limitNodeSize(sprite, {size, size}, 2.f, 0.05f);
    btn->setNormalImage(sprite);
    auto const sz = sprite->getScaledContentSize();
    btn->setContentSize(sz);
    sprite->setAnchorPoint({0.5f, 0.5f});
    sprite->setPosition({sz.width * 0.5f, sz.height * 0.5f});
}

char const* panelTextureForSkin(int skin) {
    switch (skin) {
        case 1: return "GJ_square02.png";
        case 2: return "GJ_square04.png";
        case 3: return "GJ_square05.png";
        default: return "GJ_square01.png";
    }
}

cocos2d::extension::CCScale9Sprite* createProgressCapsule(float& outArtHeight) {
    constexpr char const* kBarFile = "GJ_progressBar_001.png";
    auto* tex = CCTextureCache::sharedTextureCache()->addImage(kBarFile, false);
    if (!tex) return nullptr;
    CCSize const sz = tex->getContentSize();
    if (sz.width <= 4.f || sz.height <= 4.f) return nullptr;
    outArtHeight = sz.height;
    float const capX = std::min(12.f, sz.width * 0.25f);
    float const capY = std::min(6.f, sz.height * 0.30f);
    return cocos2d::extension::CCScale9Sprite::create(
        kBarFile,
        CCRectMake(0.f, 0.f, sz.width, sz.height),
        CCRectMake(capX, capY, std::max(1.f, sz.width - capX * 2.f), std::max(1.f, sz.height - capY * 2.f))
    );
}

void setNodeTreeOpacity(CCNode* node, GLubyte opacity) {
    if (!node) return;
    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node)) {
        rgba->setOpacity(opacity);
    }
    if (auto* children = node->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            setNodeTreeOpacity(child, opacity);
        }
    }
}

} // namespace

EditorMusicPanel* EditorMusicPanel::s_instance = nullptr;

EditorMusicPanel* EditorMusicPanel::get() {
    return s_instance;
}

EditorMusicPanel* EditorMusicPanel::create(LevelEditorLayer* editor) {
    auto* ret = new EditorMusicPanel();
    if (ret->init(editor)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool EditorMusicPanel::init(LevelEditorLayer* editor) {
    if (!CCLayer::init()) return false;

    m_editor = editor;
    s_instance = this;

    this->setID("editor-music-panel"_spr);
    this->ignoreAnchorPointForPosition(false);
    this->setAnchorPoint({0.5f, 0.5f});
    this->setTouchEnabled(true);

    auto* mod = Mod::get();
    m_open = mod->getSavedValue<bool>(kOpenKey, false);
    m_collapsed = mod->getSavedValue<bool>(kCollapsedKey, false);
    m_openAnim = m_open ? 1.f : 0.f;
    m_collapseAnim = m_collapsed ? 1.f : 0.f;
    m_playtestAnim = 1.f;

    this->setContentSize({
        m_collapsed ? kMiniW : kDeckW,
        m_collapsed ? kMiniH : kDeckH
    });

    auto& player = EditorMusicPlayer::get();
    player.refreshQueue();

    buildUI();
    loadPosition();

    m_playerListenerToken = player.addListener([this]() {
        if (this->getParent()) {
            this->refreshTrackInfo(false);
            this->updateTransport();
        }
    });

    applyVisibilityImmediate();
    this->schedule(schedule_selector(EditorMusicPanel::tick));
    return true;
}

EditorMusicPanel::~EditorMusicPanel() {
    if (s_instance == this) s_instance = nullptr;
    if (m_playerListenerToken) {
        EditorMusicPlayer::get().removeListener(m_playerListenerToken);
        m_playerListenerToken = 0;
    }
    EditorMusicPlayer::get().stop();
}

void EditorMusicPanel::onExit() {
    ++m_coverGen;
    if (m_playerListenerToken) {
        EditorMusicPlayer::get().removeListener(m_playerListenerToken);
        m_playerListenerToken = 0;
    }
    CCLayer::onExit();
    if (s_instance == this) s_instance = nullptr;
}

void EditorMusicPanel::registerWithTouchDispatcher() {
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, kPanelPriority, true);
}

CCMenuItemSpriteExtra* EditorMusicPanel::iconButton(
    std::initializer_list<char const*> frames,
    float size,
    std::function<void()> onPress
) {
    auto* sprite = firstFrame(frames);
    if (!sprite) return nullptr;
    limitNodeSize(sprite, {size, size}, 2.f, 0.05f);
    return CCMenuItemExt::createSpriteExtra(sprite, [onPress = std::move(onPress)](auto*) {
        onPress();
    });
}

void EditorMusicPanel::rebuildSkinBackdrop() {
    int const skin = EditorMusicPlayer::get().panelSkin();
    if (m_panelBg && m_appliedSkin == skin) return;
    m_appliedSkin = skin;

    if (m_panelBg) {
        m_panelBg->removeFromParent();
        m_panelBg = nullptr;
    }

    m_panelBg = SpriteHelper::safeCreateNineSliceFromFile(panelTextureForSkin(skin));
    if (!m_panelBg) m_panelBg = NineSlice::create(panelTextureForSkin(skin));
    if (m_panelBg) {
        m_panelBg->setScaleMultiplier(0.62f);
        m_panelBg->setAnchorPoint({0.f, 0.f});
        m_panelBg->setPosition({0.f, 0.f});
        this->addChild(m_panelBg, -3);
    }
}

void EditorMusicPanel::buildUI() {
    rebuildSkinBackdrop();

    m_innerWell = SpriteHelper::safeCreateNineSliceFromFile("square02b_001.png");
    if (m_innerWell) {
        m_innerWell->setScaleMultiplier(0.45f);
        m_innerWell->setAnchorPoint({0.f, 0.f});
        m_innerWell->setColor({0, 0, 0});
        m_innerWell->setOpacity(115);
        this->addChild(m_innerWell, -2);
    }

    for (std::size_t i = 0; i < m_corners.size(); ++i) {
        auto* corner = SpriteHelper::safeCreateWithFrameName("dailyLevelCorner_001.png");
        if (!corner) continue;
        // Match m_panelBg's 0.62x scaleMultiplier so the corner's white border merges flush with GJ_square.
        corner->setScale(0.62f);
        corner->setFlipX(i == 1);
        corner->setAnchorPoint({0.5f, 0.5f});
        corner->setOpacity(255);
        this->addChild(corner, -1);
        m_corners[i] = corner;
    }

    // Left artwork slot (spinning cover or vinyl note inside GJ_square05)
    m_artMenu = CCMenu::create();
    m_artMenu->setPosition({0.f, 0.f});
    m_artMenu->setTouchPriority(kMenuPriority);
    this->addChild(m_artMenu, 5);

    auto* artContainer = CCNode::create();
    artContainer->setContentSize({kArtDeckSize, kArtDeckSize});
    artContainer->setAnchorPoint({0.5f, 0.5f});

    m_artFrame = SpriteHelper::safeCreateNineSliceFromFile("GJ_square05.png");
    if (m_artFrame) {
        m_artFrame->setScaleMultiplier(0.50f);
        m_artFrame->setContentSize({kArtDeckSize, kArtDeckSize});
        m_artFrame->setPosition({kArtDeckSize * 0.5f, kArtDeckSize * 0.5f});
        artContainer->addChild(m_artFrame, 0);
    }

    auto* artWell = SpriteHelper::safeCreateNineSliceFromFile("square02b_001.png");
    if (artWell) {
        artWell->setScaleMultiplier(0.35f);
        artWell->setContentSize({kArtDeckSize - 8.f, kArtDeckSize - 8.f});
        artWell->setPosition({kArtDeckSize * 0.5f, kArtDeckSize * 0.5f});
        artWell->setColor({14, 18, 30});
        artWell->setOpacity(225);
        artContainer->addChild(artWell, 1);
    }

    m_discRotor = CCNode::create();
    m_discRotor->setContentSize({kArtDeckSize - 10.f, kArtDeckSize - 10.f});
    m_discRotor->setAnchorPoint({0.5f, 0.5f});
    m_discRotor->setPosition({kArtDeckSize * 0.5f, kArtDeckSize * 0.5f});
    artContainer->addChild(m_discRotor, 2);

    float const discInner = kArtDeckSize - 10.f;
    if (auto* stencil = SpriteHelper::createRoundedRectStencil(discInner, discInner, discInner * 0.5f)) {
        m_artClip = CCClippingNode::create();
        m_artClip->setStencil(stencil);
        m_artClip->setAlphaThreshold(0.05f);
        m_artClip->setContentSize({discInner, discInner});
        m_artClip->setAnchorPoint({0.5f, 0.5f});
        m_artClip->setPosition({discInner * 0.5f, discInner * 0.5f});
        m_discRotor->addChild(m_artClip, 0);
    }

    m_artFallbackIcon = firstFrame({"GJ_musicOnBtn_001.png", "GJ_playMusicBtn_001.png"});
    if (m_artFallbackIcon) {
        limitNodeSize(m_artFallbackIcon, {22.f, 22.f}, 2.f, 0.05f);
        m_artFallbackIcon->setPosition({discInner * 0.5f, discInner * 0.5f});
        m_discRotor->addChild(m_artFallbackIcon, 2);
    }

    m_artBtn = CCMenuItemExt::createSpriteExtra(artContainer, [this](auto*) {
        openPicker(0);
    });
    if (m_artBtn) {
        m_artBtn->setID("editor-music-artwork-btn"_spr);
        m_artMenu->addChild(m_artBtn);
    }

    // Clipped title + subtitle + timestamp
    m_titleClipStencil = PaimonDrawNode::create();
    m_titleClip = CCClippingNode::create();
    if (m_titleClip && m_titleClipStencil) {
        m_titleClip->setStencil(m_titleClipStencil);
        m_titleClip->setAlphaThreshold(0.05f);
        m_titleClip->setAnchorPoint({0.f, 0.5f});
        this->addChild(m_titleClip, 4);
    }

    m_titleLabel = CCLabelBMFont::create("Sin cancion", "bigFont.fnt");
    if (m_titleLabel) {
        m_titleLabel->setAnchorPoint({0.f, 0.5f});
        m_titleLabel->setScale(0.36f);
        m_titleLabel->setColor({255, 255, 255});
        if (m_titleClip) m_titleClip->addChild(m_titleLabel, 1);
        else this->addChild(m_titleLabel, 4);
    }

    m_subtitleLabel = CCLabelBMFont::create("Ctrl+M para abrir o cerrar", "chatFont.fnt");
    if (m_subtitleLabel) {
        m_subtitleLabel->setAnchorPoint({0.f, 0.5f});
        m_subtitleLabel->setScale(0.41f);
        m_subtitleLabel->setColor(kDimText);
        this->addChild(m_subtitleLabel, 4);
    }

    m_timeLabel = CCLabelBMFont::create("0:00 / 0:00", "chatFont.fnt");
    if (m_timeLabel) {
        m_timeLabel->setAnchorPoint({1.f, 0.5f});
        m_timeLabel->setScale(0.38f);
        m_timeLabel->setColor(kDimText);
        this->addChild(m_timeLabel, 4);
    }

    // 5-bar native GD equalizer
    for (int i = 0; i < 5; ++i) {
        auto* track = SpriteHelper::safeCreateNineSliceFromFile("square02b_001.png");
        if (track) {
            track->setScaleMultiplier(0.18f);
            track->setContentSize({3.2f, 16.f});
            track->setAnchorPoint({0.5f, 0.5f});
            track->setColor({0, 0, 0});
            track->setOpacity(110);
            this->addChild(track, 3);
            m_eqTracks[static_cast<std::size_t>(i)] = track;
        }
        auto* bar = SpriteHelper::safeCreateNineSliceFromFile("square02b_001.png");
        if (bar) {
            bar->setScaleMultiplier(0.18f);
            bar->setContentSize({3.2f, 4.f});
            bar->setAnchorPoint({0.5f, 0.f});
            bar->setColor({110, 245, 85});
            this->addChild(bar, 4);
            m_eqBars[static_cast<std::size_t>(i)] = bar;
        }
    }

    // Native GD seek bar (GJ_progressBar_001 + sliderthumb)
    m_seekGroove = createProgressCapsule(m_seekBarArtH);
    if (m_seekGroove) {
        m_seekGroove->setAnchorPoint({0.f, 0.5f});
        m_seekGroove->setScale(kSeekVisualH / std::max(1.f, m_seekBarArtH));
        m_seekGroove->setContentSize({kSeekW / std::max(0.01f, m_seekGroove->getScale()), m_seekBarArtH});
        m_seekGroove->setColor({18, 22, 34});
        m_seekGroove->setOpacity(195);
        m_seekGroove->setPosition({kSeekX, kSeekY});
        this->addChild(m_seekGroove, 2);
    }
    m_seekFill = createProgressCapsule(m_seekBarArtH);
    if (m_seekFill) {
        m_seekFill->setAnchorPoint({0.f, 0.5f});
        m_seekFill->setScale(kSeekVisualH / std::max(1.f, m_seekBarArtH));
        m_seekFill->setColor({115, 245, 100});
        m_seekFill->setPosition({kSeekX + 1.f, kSeekY});
        this->addChild(m_seekFill, 3);
    }
    m_seekThumb = SpriteHelper::safeCreate("sliderthumb.png");
    if (m_seekThumb) {
        m_seekThumb->setScale(0.32f);
        m_seekThumb->setPosition({kSeekX, kSeekY});
        this->addChild(m_seekThumb, 4);
    }

    // Native GD volume capsule
    float volArtH = 20.f;
    m_volGroove = createProgressCapsule(volArtH);
    if (m_volGroove) {
        m_volGroove->setAnchorPoint({0.f, 0.5f});
        m_volGroove->setScale(kVolVisualH / std::max(1.f, volArtH));
        m_volGroove->setContentSize({kVolW / std::max(0.01f, m_volGroove->getScale()), m_seekBarArtH});
        m_volGroove->setColor({18, 22, 34});
        m_volGroove->setOpacity(195);
        m_volGroove->setPosition({kVolX, kVolY});
        this->addChild(m_volGroove, 2);
    }
    m_volFill = createProgressCapsule(volArtH);
    if (m_volFill) {
        m_volFill->setAnchorPoint({0.f, 0.5f});
        m_volFill->setScale(kVolVisualH / std::max(1.f, volArtH));
        m_volFill->setColor(kAccentAqua);
        m_volFill->setPosition({kVolX + 1.f, kVolY});
        this->addChild(m_volFill, 3);
    }
    m_volThumb = SpriteHelper::safeCreate("sliderthumb.png");
    if (m_volThumb) {
        m_volThumb->setScale(0.28f);
        m_volThumb->setPosition({kVolX, kVolY});
        this->addChild(m_volThumb, 4);
    }

    // Header mini buttons (FX, Menu, Collapse, Close)
    m_headerMenu = CCMenu::create();
    m_headerMenu->setPosition({0.f, 0.f});
    m_headerMenu->setTouchPriority(kMenuPriority);
    this->addChild(m_headerMenu, 6);

    m_fxBtn = iconButton({"GJ_fxOnBtn_001.png", "GJ_optionsBtn02_001.png"}, 13.f, [this] {
        openPicker(2);
    });
    if (m_fxBtn) {
        m_fxBtn->setID("editor-music-fx-btn"_spr);
        m_headerMenu->addChild(m_fxBtn);
    }

    m_menuBtn = iconButton({"GJ_musicOnBtn_001.png", "GJ_plusBtn_001.png"}, 13.f, [this] {
        openPicker(0);
    });
    if (m_menuBtn) {
        m_menuBtn->setID("editor-music-menu-btn"_spr);
        m_headerMenu->addChild(m_menuBtn);
    }

    m_collapseBtn = iconButton({"edit_downBtn_001.png", "GJ_arrow_02_001.png"}, 12.f, [this] {
        m_collapsed = !m_collapsed;
        Mod::get()->setSavedValue<bool>(kCollapsedKey, m_collapsed);
        if (m_collapseBtn) triggerButtonPop(m_collapseBtn, 1.22f);
    });
    if (m_collapseBtn) {
        m_collapseBtn->setID("editor-music-collapse-btn"_spr);
        m_headerMenu->addChild(m_collapseBtn);
    }

    m_closeBtn = iconButton({"GJ_closeBtn_001.png", "GJ_deleteIcon_001.png"}, 13.f, [this] {
        if (m_open) toggleOpen();
    });
    if (m_closeBtn) {
        m_closeBtn->setID("editor-music-close-btn"_spr);
        m_headerMenu->addChild(m_closeBtn);
    }

    // Deck transport menu
    m_deckMenu = CCMenu::create();
    m_deckMenu->setPosition({0.f, 0.f});
    m_deckMenu->setTouchPriority(kMenuPriority);
    this->addChild(m_deckMenu, 6);

    constexpr float kRowY = 18.f;
    m_favBtn = iconButton({"GJ_starBtn_001.png", "GJ_starsIcon_001.png"}, 12.f, [this] {
        auto& player = EditorMusicPlayer::get();
        if (player.trackId().empty()) return;
        auto const* track = MenuMusicLibrary::get().findTrack(player.trackId());
        if (!track) return;
        bool const nextFav = !track->favorite;
        MenuMusicLibrary::get().setFavorite(track->id, nextFav);
        if (m_favBtn) triggerButtonPop(m_favBtn, 1.35f);
        updateTransport();
    });
    if (m_favBtn) {
        m_favIcon = typeinfo_cast<CCSprite*>(m_favBtn->getNormalImage());
        m_favBtn->setPosition({72.f, kRowY});
        m_deckMenu->addChild(m_favBtn);
    }

    if (auto* prev = iconButton({"GJ_arrow_02_001.png"}, 11.f, [] {
            EditorMusicPlayer::get().playPrevious();
        })) {
        prev->setPosition({92.f, kRowY});
        m_deckMenu->addChild(prev);
    }

    m_playBtn = iconButton({"GJ_playBtn2_001.png", "GJ_playBtn_001.png"}, 16.f, [this] {
        EditorMusicPlayer::get().togglePause();
        if (m_playBtn) triggerButtonPop(m_playBtn, 1.25f);
    });
    if (m_playBtn) {
        m_playBtn->setPosition({113.f, kRowY});
        m_deckMenu->addChild(m_playBtn);
    }

    if (auto* next = iconButton({"GJ_arrow_02_001.png"}, 11.f, [] {
            EditorMusicPlayer::get().playNext();
        })) {
        if (auto* sprite = typeinfo_cast<CCSprite*>(next->getNormalImage())) sprite->setFlipX(true);
        next->setPosition({134.f, kRowY});
        m_deckMenu->addChild(next);
    }

    auto* shuffle = iconButton({"GJ_sortIcon_001.png", "GJ_reloadBtn_001.png"}, 12.f, [this] {
        EditorMusicPlayer::get().toggleShuffle();
        updateTransport();
        if (auto* item = m_shuffleIcon ? m_shuffleIcon->getParent() : nullptr) triggerButtonPop(item, 1.22f);
    });
    if (shuffle) {
        m_shuffleIcon = typeinfo_cast<CCSprite*>(shuffle->getNormalImage());
        shuffle->setPosition({154.f, kRowY});
        m_deckMenu->addChild(shuffle);
    }

    auto* repeat = iconButton({"GJ_updateBtn_001.png", "GJ_reloadBtn_001.png"}, 12.f, [this] {
        EditorMusicPlayer::get().cycleRepeat();
        updateTransport();
        if (auto* item = m_repeatIcon ? m_repeatIcon->getParent() : nullptr) triggerButtonPop(item, 1.22f);
    });
    if (repeat) {
        m_repeatIcon = typeinfo_cast<CCSprite*>(repeat->getNormalImage());
        m_repeatBadge = CCLabelBMFont::create("1", "bigFont.fnt");
        m_repeatBadge->setScale(0.24f);
        m_repeatBadge->setColor(kAccentGold);
        m_repeatBadge->setPosition({
            repeat->getContentSize().width * 0.82f,
            repeat->getContentSize().height * 0.22f
        });
        m_repeatBadge->setVisible(false);
        repeat->addChild(m_repeatBadge, 2);
        repeat->setPosition({173.f, kRowY});
        m_deckMenu->addChild(repeat);
    }

    m_muteBtn = iconButton({"GJ_audioOnBtn_001.png", "GJ_musicOnBtn_001.png"}, 12.f, [this] {
        EditorMusicPlayer::get().toggleMute();
        updateTransport();
        if (m_muteBtn) triggerButtonPop(m_muteBtn, 1.22f);
    });
    if (m_muteBtn) {
        m_muteIcon = typeinfo_cast<CCSprite*>(m_muteBtn->getNormalImage());
        m_muteBtn->setPosition({194.f, kRowY});
        m_deckMenu->addChild(m_muteBtn);
    }

    // Mini-bar controls (shown when collapsed)
    m_miniMenu = CCMenu::create();
    m_miniMenu->setPosition({0.f, 0.f});
    m_miniMenu->setTouchPriority(kMenuPriority);
    this->addChild(m_miniMenu, 6);

    constexpr float kMiniRowY = kMiniH * 0.5f;
    m_miniPlayBtn = iconButton({"GJ_playBtn2_001.png", "GJ_playBtn_001.png"}, 14.f, [this] {
        EditorMusicPlayer::get().togglePause();
        if (m_miniPlayBtn) triggerButtonPop(m_miniPlayBtn, 1.25f);
    });
    if (m_miniPlayBtn) {
        m_miniPlayBtn->setPosition({132.f, kMiniRowY});
        m_miniMenu->addChild(m_miniPlayBtn);
    }

    m_miniNextBtn = iconButton({"GJ_arrow_02_001.png"}, 10.f, [] {
        EditorMusicPlayer::get().playNext();
    });
    if (m_miniNextBtn) {
        if (auto* sprite = typeinfo_cast<CCSprite*>(m_miniNextBtn->getNormalImage())) sprite->setFlipX(true);
        m_miniNextBtn->setPosition({151.f, kMiniRowY});
        m_miniMenu->addChild(m_miniNextBtn);
    }

    m_miniExpandBtn = iconButton({"edit_upBtn_001.png", "GJ_plusBtn_001.png"}, 12.f, [this] {
        m_collapsed = false;
        Mod::get()->setSavedValue<bool>(kCollapsedKey, false);
        if (m_miniExpandBtn) triggerButtonPop(m_miniExpandBtn, 1.22f);
    });
    if (m_miniExpandBtn) {
        m_miniExpandBtn->setPosition({169.f, kMiniRowY});
        m_miniMenu->addChild(m_miniExpandBtn);
    }

    m_miniCloseBtn = iconButton({"GJ_closeBtn_001.png", "GJ_deleteIcon_001.png"}, 12.f, [this] {
        if (m_open) toggleOpen();
    });
    if (m_miniCloseBtn) {
        m_miniCloseBtn->setPosition({185.f, kMiniRowY});
        m_miniMenu->addChild(m_miniCloseBtn);
    }

    m_lastWin = CCDirector::get()->getWinSize();
    refreshTrackInfo(false);
    updateTransport();
    updateBars(0.f);
    applyPanelLayoutAndOpacity();
}

void EditorMusicPanel::loadPosition() {
    auto win = CCDirector::get()->getWinSize();
    auto* mod = Mod::get();
    float const defX = 14.f + kDeckW * 0.5f;
    float const defY = win.height - 46.f - kDeckH * 0.5f;
    float x = static_cast<float>(mod->getSavedValue<double>(kPosXKey, defX));
    float y = static_cast<float>(mod->getSavedValue<double>(kPosYKey, defY));
    this->setPosition({x, y});
    clampToScreen();
}

void EditorMusicPanel::savePosition() {
    auto* mod = Mod::get();
    mod->setSavedValue<double>(kPosXKey, this->getPositionX());
    mod->setSavedValue<double>(kPosYKey, this->getPositionY());
}

void EditorMusicPanel::resetPosition() {
    auto win = CCDirector::get()->getWinSize();
    float const defX = 14.f + kDeckW * 0.5f;
    float const defY = win.height - 46.f - kDeckH * 0.5f;
    this->setPosition({defX, defY});
    clampToScreen();
    savePosition();
    if (EditorMusicPlayer::get().animationsEnabled()) {
        triggerButtonPop(this, 1.06f);
    }
}

void EditorMusicPanel::clampToScreen() {
    auto win = CCDirector::get()->getWinSize();
    auto const sz = this->getContentSize();
    float const hx = sz.width * 0.5f;
    float const hy = sz.height * 0.5f;
    auto pos = this->getPosition();
    pos.x = std::clamp(pos.x, hx, std::max(hx, win.width - hx));
    pos.y = std::clamp(pos.y, hy, std::max(hy, win.height - hy));
    this->setPosition(pos);
}

bool EditorMusicPanel::toggleOpen() {
    m_open = !m_open;
    Mod::get()->setSavedValue<bool>(kOpenKey, m_open);
    if (m_open) {
        EditorMusicPlayer::get().refreshQueue();
        refreshTrackInfo(false);
        this->setVisible(true);
    }
    if (!EditorMusicPlayer::get().animationsEnabled()) {
        m_openAnim = m_open ? 1.f : 0.f;
        applyPanelLayoutAndOpacity();
    }
    return m_open;
}

void EditorMusicPanel::applyVisibilityImmediate() {
    bool playtesting = m_editor && m_editor->m_playbackMode != PlaybackMode::Not;
    bool uiHidden = m_editor && m_editor->m_editorUI && !m_editor->m_editorUI->isVisible();
    m_playtestAnim = (playtesting || uiHidden) ? 0.f : 1.f;
    applyPanelLayoutAndOpacity();
}

void EditorMusicPanel::refreshSkin() {
    rebuildSkinBackdrop();
    applyPanelLayoutAndOpacity();
}

void EditorMusicPanel::triggerButtonPop(CCNode* node, float peakScale) {
    if (!node || !EditorMusicPlayer::get().animationsEnabled()) return;
    constexpr int kPopTag = 0x454d504f;
    node->stopActionByTag(kPopTag);
    float base = 1.f;
    if (auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(node)) {
        base = item->m_baseScale > 0.01f ? item->m_baseScale : 1.f;
    }
    node->setScale(base);
    auto* seq = CCSequence::create(
        CCEaseBackOut::create(CCScaleTo::create(0.11f, base * peakScale)),
        CCEaseSineOut::create(CCScaleTo::create(0.14f, base)),
        nullptr
    );
    seq->setTag(kPopTag);
    node->runAction(seq);
}

void EditorMusicPanel::refreshTrackInfo(bool animateChange) {
    auto& player = EditorMusicPlayer::get();
    std::string const newId = player.trackId();
    bool const trackChanged = (newId != m_shownTrackId);
    m_shownTrackId = newId;

    auto name = player.trackName();
    if (name.empty()) name = "Sin cancion";
    if (m_titleLabel) {
        m_titleLabel->setString(name.c_str());
        m_marqueeOffset = 0.f;
        m_marqueePause = 1.2f;
        m_marqueeDir = -1;
    }

    if (m_subtitleLabel) {
        std::string sub;
        if (newId.empty()) {
            sub = "Ctrl+M abrir/cerrar  |  Click disco: Menu";
        } else {
            sub = player.trackArtist();
            std::string modeTag = queueSourceName(player.source());
            if (player.source() == EditorQueueSource::Playlist) {
                if (auto* pl = MenuMusicLibrary::get().findPlaylist(player.playlistId())) {
                    modeTag = pl->name;
                }
            }
            if (player.fxConfig().enabled && !player.isFxBypassed()) {
                modeTag += fmt::format(" | {}", fxPresetName(player.fxConfig().preset));
            }
            sub = sub.empty() ? modeTag : fmt::format("{}  -  {}", sub, modeTag);
        }
        m_subtitleLabel->setString(sub.c_str());
        m_subtitleLabel->limitLabelWidth(116.f, 0.41f, 0.24f);
    }

    if (trackChanged) {
        loadCoverTexture(player.trackCoverPath(), player.trackSongId());
        if (animateChange || (m_firstSyncDone && !newId.empty())) {
            m_trackSlideAnim = 0.f;
            if (m_discRotor) triggerButtonPop(m_discRotor, 1.16f);
            if (m_firstSyncDone && !newId.empty() && player.isPlaying() && player.showToastOnChange()) {
                if (auto* scene = CCDirector::get()->getRunningScene()) {
                    EditorNowPlayingToast::show(
                        scene, name, player.trackArtist(),
                        player.trackCoverPath(), player.trackSongId());
                }
            }
        }
        m_firstSyncDone = true;
    }
}

void EditorMusicPanel::loadCoverTexture(std::string const& path, int songId) {
    if (path == m_loadedCoverPath && m_artCoverSprite) return;
    m_loadedCoverPath = path;
    auto const gen = ++m_coverGen;

    if (path.empty()) {
        applyCoverTexture(nullptr);
        if (songId > 0 && Mod::get()->getSavedValue<bool>("menuMusicThumbnailCovers", true)) {
            WeakRef<EditorMusicPanel> weak = this;
            SongCoverCache::get().requestCovers(songId, [weak, gen](std::vector<std::string> const& paths, bool ok) {
                auto self = weak.lock();
                if (!self || self->m_coverGen != gen || !ok || paths.empty()) return;
                self->loadCoverTexture(paths.front(), 0);
            });
        }
        return;
    }

    auto* cached = CCTextureCache::sharedTextureCache()->textureForKey(path.c_str());
    if (cached) {
        applyCoverTexture(cached);
        return;
    }

    WeakRef<EditorMusicPanel> weak = this;
    paimon::image::loadBudgetedAsync(path, [weak, gen](CCTexture2D* tex) {
        auto self = weak.lock();
        if (!self || self->m_coverGen != gen) return;
        self->applyCoverTexture(tex);
    });
}

void EditorMusicPanel::applyCoverTexture(CCTexture2D* tex) {
    if (m_artCoverSprite) {
        m_artCoverSprite->removeFromParent();
        m_artCoverSprite = nullptr;
    }
    if (!tex || !m_artClip) {
        if (m_artFallbackIcon) m_artFallbackIcon->setVisible(true);
        return;
    }
    auto* spr = CCSprite::createWithTexture(tex);
    if (!spr) {
        if (m_artFallbackIcon) m_artFallbackIcon->setVisible(true);
        return;
    }
    auto const clipSz = m_artClip->getContentSize();
    auto const texSz = spr->getContentSize();
    float const coverScale = std::max(
        clipSz.width / std::max(1.f, texSz.width),
        clipSz.height / std::max(1.f, texSz.height));
    spr->setScale(coverScale);
    spr->setPosition(clipSz / 2.f);
    m_artClip->addChild(spr, 1);
    m_artCoverSprite = spr;
    if (m_artFallbackIcon) m_artFallbackIcon->setVisible(false);
}

void EditorMusicPanel::updateTransport() {
    auto& player = EditorMusicPlayer::get();
    bool const playing = player.isPlaying();

    auto updatePlaySprite = [](CCMenuItemSpriteExtra* btn, bool isPlayState, bool& cacheState, float size) {
        if (!btn || isPlayState == cacheState) return;
        auto* sprite = isPlayState
            ? firstFrame({"GJ_pauseBtn_001.png", "GJ_pauseEditorBtn_001.png"})
            : firstFrame({"GJ_playBtn2_001.png", "GJ_playBtn_001.png"});
        if (sprite) {
            replaceButtonIcon(btn, sprite, size);
        }
        cacheState = isPlayState;
    };
    updatePlaySprite(m_playBtn, playing, m_shownPlaying, 16.f);
    updatePlaySprite(m_miniPlayBtn, playing, m_shownMiniPlaying, 14.f);

    if (m_shuffleIcon) {
        m_shuffleIcon->setColor(player.shuffle() ? kAccentGold : kDimText);
        m_shuffleIcon->setOpacity(player.shuffle() ? 255 : 150);
    }
    if (m_repeatIcon) {
        bool const on = player.repeat() != RepeatMode::Off;
        m_repeatIcon->setColor(on ? kAccentAqua : kDimText);
        m_repeatIcon->setOpacity(on ? 255 : 150);
    }
    if (m_repeatBadge) m_repeatBadge->setVisible(player.repeat() == RepeatMode::One);

    bool isFav = false;
    if (!player.trackId().empty()) {
        if (auto const* t = MenuMusicLibrary::get().findTrack(player.trackId())) {
            isFav = t->favorite;
        }
    }
    m_shownFav = isFav;
    if (m_favIcon) {
        m_favIcon->setColor(isFav ? kAccentGold : kDimText);
        m_favIcon->setOpacity(isFav ? 255 : 155);
    }

    bool const muted = player.isMuted() || player.volume() <= 0.005f;
    if (m_muteIcon && muted != m_shownMuted) {
        m_shownMuted = muted;
        auto* nextSpr = firstFrame({muted ? "GJ_audioOffBtn_001.png" : "GJ_audioOnBtn_001.png"});
        if (nextSpr && m_muteBtn) {
            replaceButtonIcon(m_muteBtn, nextSpr, 12.f);
            m_muteIcon = nextSpr;
        }
    }
    if (m_muteIcon) {
        m_muteIcon->setColor(muted ? ccColor3B{255, 115, 115} : ccColor3B{255, 255, 255});
    }
}

void EditorMusicPanel::updateBars(float) {
    auto& player = EditorMusicPlayer::get();
    int const length = player.lengthMs();
    int const position = (m_grab == Grab::Seek && length > 0)
        ? static_cast<int>(m_seekPreviewRatio * static_cast<float>(length))
        : player.positionMs();
    float const progress = (m_grab == Grab::Seek)
        ? m_seekPreviewRatio
        : (length > 0 ? clamp01(static_cast<float>(position) / static_cast<float>(length)) : 0.f);

    float const deckAlpha = (1.f - easeInOutCubic(m_collapseAnim)) * easeOutCubic(m_openAnim) * m_playtestAnim;

    auto applyFillCapsule = [&](cocos2d::extension::CCScale9Sprite* fill, float visualH, float fillPx, bool& outVisible) {
        outVisible = (fillPx >= 2.f);
        if (!fill) return;
        bool const show = outVisible && deckAlpha > 0.02f;
        fill->setVisible(show);
        if (!show) return;
        float const baseScale = visualH / std::max(1.f, m_seekBarArtH);
        constexpr float kMinUnscaledW = 24.f;
        float const minVisualW = kMinUnscaledW * baseScale;
        float const innerH = std::max(4.f, m_seekBarArtH - 4.f);
        if (fillPx < minVisualW) {
            fill->setScaleX(fillPx / kMinUnscaledW);
            fill->setScaleY(baseScale);
            fill->setContentSize({kMinUnscaledW, innerH});
        } else {
            fill->setScaleX(baseScale);
            fill->setScaleY(baseScale);
            fill->setContentSize({fillPx / baseScale, innerH});
        }
    };

    applyFillCapsule(m_seekFill, kSeekVisualH, (kSeekW - 2.f) * progress, m_seekFillVisible);
    if (m_seekThumb) {
        m_seekThumb->setPosition({kSeekX + kSeekW * progress, kSeekY});
        float const targetScale = (m_grab == Grab::Seek) ? 0.42f : 0.30f;
        m_seekThumb->setScale(lerp(m_seekThumb->getScale(), targetScale, 0.35f));
    }

    float const volRatio = player.isMuted() ? 0.f : player.volume();
    applyFillCapsule(m_volFill, kVolVisualH, (kVolW - 2.f) * volRatio, m_volFillVisible);
    if (m_volThumb) {
        m_volThumb->setPosition({kVolX + kVolW * volRatio, kVolY});
        float const targetScale = (m_grab == Grab::Volume) ? 0.38f : 0.27f;
        m_volThumb->setScale(lerp(m_volThumb->getScale(), targetScale, 0.35f));
    }

    if (m_timeLabel) {
        int const posSec = std::max(0, position) / 1000;
        int const lenSec = std::max(0, length) / 1000;
        if (posSec != m_lastTimePosSec || lenSec != m_lastTimeLenSec) {
            m_lastTimePosSec = posSec;
            m_lastTimeLenSec = lenSec;
            m_timeLabel->setString(fmt::format("{} / {}", formatTime(position), formatTime(length)).c_str());
        }
    }
}

void EditorMusicPanel::updateMarquee(float dt) {
    if (!m_titleLabel || !m_titleClip) return;
    float const labelW = m_titleLabel->getContentSize().width * m_titleLabel->getScaleX();
    float const overflow = labelW - m_titleClipW;

    if (overflow <= 2.f) {
        m_marqueeOffset = 0.f;
    } else {
        if (m_marqueePause > 0.f) {
            m_marqueePause -= dt;
        } else {
            m_marqueeOffset += static_cast<float>(m_marqueeDir) * 26.f * dt;
            if (m_marqueeOffset <= -overflow) {
                m_marqueeOffset = -overflow;
                m_marqueeDir = 1;
                m_marqueePause = 1.0f;
            } else if (m_marqueeOffset >= 0.f) {
                m_marqueeOffset = 0.f;
                m_marqueeDir = -1;
                m_marqueePause = 1.2f;
            }
        }
    }

    float const slideX = (1.f - easeOutCubic(m_trackSlideAnim)) * 24.f;
    m_titleLabel->setPosition({m_marqueeOffset + slideX + 2.f, 8.f});
}

void EditorMusicPanel::updateEqualizer(float dt) {
    auto& player = EditorMusicPlayer::get();
    bool const playing = player.isPlaying();
    float const peak = player.outputPeak();

    constexpr std::array<float, 5> kFreqA = {6.2f, 8.4f, 7.1f, 9.3f, 5.8f};
    constexpr std::array<float, 5> kPhase = {0.4f, 1.7f, 2.9f, 4.2f, 5.5f};
    constexpr std::array<float, 5> kProfile = {0.72f, 0.96f, 1.0f, 0.88f, 0.68f};

    float sum = 0.f;
    float const smooth = std::min(1.f, dt * 16.f);
    for (std::size_t i = 0; i < m_eqLevels.size(); ++i) {
        float target = 0.14f;
        if (playing) {
            float const w1 = 0.5f + 0.5f * std::sin(m_clock * kFreqA[i] + kPhase[i]);
            float const w2 = 0.5f + 0.5f * std::sin(m_clock * (kFreqA[i] * 0.61f) + kPhase[i] * 1.7f);
            float const synth = 0.25f + 0.45f * w1 + 0.30f * w2;
            float const drive = peak > 0.01f ? lerp(synth, std::min(1.f, peak * 1.45f), 0.62f) : synth;
            target = std::clamp(drive * kProfile[i], 0.14f, 1.f);
        }
        m_eqLevels[i] = lerp(m_eqLevels[i], target, smooth);
        sum += m_eqLevels[i];
    }
    m_audioEnergy = lerp(m_audioEnergy, playing ? (sum / 5.f) : 0.05f, std::min(1.f, dt * 10.f));
}

void EditorMusicPanel::updateArtwork(float dt) {
    bool const playing = EditorMusicPlayer::get().isPlaying();
    float const targetVel = playing ? 48.f : 0.f;
    m_spinVel = lerp(m_spinVel, targetVel, std::min(1.f, dt * 5.f));
    m_spinAngle = std::fmod(m_spinAngle + m_spinVel * dt, 360.f);

    if (m_discRotor) {
        m_discRotor->setRotation(m_spinAngle);
    }
    if (m_artBtn) {
        float const morph = easeInOutCubic(m_collapseAnim);
        float const baseScale = lerp(1.f, kArtMiniSize / kArtDeckSize, morph);
        float const pulse = playing ? (1.f + 0.032f * m_audioEnergy) : 1.f;
        m_artBtn->setScale(baseScale * pulse);
        m_artBtn->m_baseScale = baseScale;
    }
}

void EditorMusicPanel::updateAnimations(float dt) {
    bool const anim = EditorMusicPlayer::get().animationsEnabled();
    float const openSpeed = anim ? 5.2f : 100.f;
    float const collapseSpeed = anim ? 5.0f : 100.f;
    float const ptSpeed = anim ? 6.5f : 100.f;

    float const openTarget = m_open ? 1.f : 0.f;
    if (m_openAnim < openTarget) m_openAnim = std::min(openTarget, m_openAnim + dt * openSpeed);
    else if (m_openAnim > openTarget) m_openAnim = std::max(openTarget, m_openAnim - dt * openSpeed);

    float const collapseTarget = m_collapsed ? 1.f : 0.f;
    if (m_collapseAnim < collapseTarget) m_collapseAnim = std::min(collapseTarget, m_collapseAnim + dt * collapseSpeed);
    else if (m_collapseAnim > collapseTarget) m_collapseAnim = std::max(collapseTarget, m_collapseAnim - dt * collapseSpeed);

    bool const playtesting = m_editor && m_editor->m_playbackMode != PlaybackMode::Not;
    bool const uiHidden = m_editor && m_editor->m_editorUI && !m_editor->m_editorUI->isVisible();
    float const ptTarget = (playtesting || uiHidden) ? 0.f : 1.f;
    if (m_playtestAnim < ptTarget) m_playtestAnim = std::min(ptTarget, m_playtestAnim + dt * ptSpeed);
    else if (m_playtestAnim > ptTarget) m_playtestAnim = std::max(ptTarget, m_playtestAnim - dt * ptSpeed);

    if (m_trackSlideAnim < 1.f) {
        m_trackSlideAnim = std::min(1.f, m_trackSlideAnim + dt * (anim ? 5.5f : 100.f));
    }
}

void EditorMusicPanel::applyPanelLayoutAndOpacity() {
    float const openEased = m_open ? easeOutBack(m_openAnim) : easeOutCubic(m_openAnim);
    float const openAlpha = easeOutCubic(m_openAnim);
    float const masterAlpha = clamp01(openAlpha * m_playtestAnim);

    if (masterAlpha <= 0.005f && (!m_open || m_playtestAnim <= 0.005f)) {
        this->setVisible(false);
        if (m_artMenu) m_artMenu->setEnabled(false);
        if (m_headerMenu) m_headerMenu->setEnabled(false);
        if (m_deckMenu) m_deckMenu->setEnabled(false);
        if (m_miniMenu) m_miniMenu->setEnabled(false);
        return;
    }
    this->setVisible(true);
    if (m_artMenu) m_artMenu->setEnabled(masterAlpha > 0.45f);

    float const morph = easeInOutCubic(m_collapseAnim);
    float const curW = lerp(kDeckW, kMiniW, morph);
    float const curH = lerp(kDeckH, kMiniH, morph);
    float const deckAlpha = clamp01(1.f - morph * 1.4f) * masterAlpha;
    float const miniAlpha = clamp01((morph - 0.35f) * 1.6f) * masterAlpha;

    this->setContentSize({curW, curH});
    float const popScale = lerp(0.80f, 1.f, openEased) * lerp(0.90f, 1.f, m_playtestAnim);
    this->setScale(std::max(0.1f, popScale));

    if (m_panelBg) {
        m_panelBg->setContentSize({curW, curH});
        m_panelBg->setOpacity(toByte(masterAlpha));
    }
    if (m_innerWell) {
        m_innerWell->setContentSize({std::max(12.f, curW - 14.f), std::max(12.f, curH - 14.f)});
        m_innerWell->setPosition({7.f, 7.f});
        m_innerWell->setOpacity(toByte(0.45f * masterAlpha));
    }
    constexpr float kCornerOffset = 24.75f * 0.62f;
    for (std::size_t i = 0; i < m_corners.size(); ++i) {
        if (!m_corners[i]) continue;
        m_corners[i]->setPosition({i == 1 ? curW - kCornerOffset : kCornerOffset, kCornerOffset});
        m_corners[i]->setOpacity(toByte(deckAlpha));
        m_corners[i]->setVisible(deckAlpha > 0.02f);
    }

    if (m_artMenu) {
        m_artMenu->setContentSize({curW, curH});
        m_artMenu->setOpacity(toByte(masterAlpha));
    }
    if (m_artBtn) {
        float const artX = lerp(34.f, 20.f, morph);
        float const artY = lerp(46.f, kMiniH * 0.5f, morph);
        m_artBtn->setPosition({artX, artY});
    }

    // Title clip position and width morph smoothly between Deck and Mini
    m_titleClipW = lerp(128.f, 82.f, morph);
    float const clipX = lerp(66.f, 38.f, morph);
    float const clipY = lerp(74.f, kMiniH * 0.5f, morph);
    if (m_titleClip && m_titleClipStencil) {
        m_titleClip->setContentSize({m_titleClipW, 16.f});
        m_titleClip->setPosition({clipX, clipY});
        auto* draw = static_cast<PaimonDrawNode*>(m_titleClipStencil);
        draw->clear();
        CCPoint rect[4] = {
            ccp(0.f, 0.f), ccp(m_titleClipW, 0.f),
            ccp(m_titleClipW, 16.f), ccp(0.f, 16.f)
        };
        draw->drawPolygon(rect, 4, {1.f, 1.f, 1.f, 1.f}, 0.f, {0.f, 0.f, 0.f, 0.f});
    }
    if (m_titleLabel) {
        m_titleLabel->setOpacity(toByte(masterAlpha * easeOutCubic(m_trackSlideAnim)));
    }
    if (m_subtitleLabel) {
        m_subtitleLabel->setPosition({66.f, 59.f});
        m_subtitleLabel->setVisible(deckAlpha > 0.02f);
        m_subtitleLabel->setOpacity(toByte(deckAlpha * easeOutCubic(m_trackSlideAnim)));
    }
    if (m_timeLabel) {
        m_timeLabel->setPosition({252.f, 49.f});
        m_timeLabel->setVisible(deckAlpha > 0.02f);
        m_timeLabel->setOpacity(toByte(deckAlpha));
    }

    // Equalizer bars (Deck mode top-right next to subtitle)
    float const eqStartX = 188.f;
    float const eqBaseY = 55.f;
    for (int i = 0; i < 5; ++i) {
        float const x = eqStartX + static_cast<float>(i) * 4.8f;
        auto* track = m_eqTracks[static_cast<std::size_t>(i)];
        auto* bar = m_eqBars[static_cast<std::size_t>(i)];
        if (track) {
            track->setVisible(deckAlpha > 0.02f);
            track->setPosition({x, eqBaseY + 7.f});
            track->setOpacity(toByte(0.45f * deckAlpha));
        }
        if (bar) {
            bar->setVisible(deckAlpha > 0.02f);
            float const lvl = m_eqLevels[static_cast<std::size_t>(i)];
            float const h = 3.f + 11.f * lvl;
            bar->setContentSize({3.2f, h});
            bar->setPosition({x, eqBaseY});
            bar->setColor(mixColor({105, 245, 70}, {245, 255, 115}, lvl * 0.85f));
            bar->setOpacity(toByte(deckAlpha));
        }
    }

    // Seek & volume bars (Deck mode)
    auto applyBarOpacity = [&](CCNode* n, float a) {
        if (!n) return;
        n->setVisible(a > 0.02f);
        setNodeTreeOpacity(n, toByte(a));
    };
    applyBarOpacity(m_seekGroove, deckAlpha * 0.8f);
    applyBarOpacity(m_seekFill, m_seekFillVisible ? deckAlpha : 0.f);
    applyBarOpacity(m_seekThumb, deckAlpha);
    applyBarOpacity(m_volGroove, deckAlpha * 0.8f);
    applyBarOpacity(m_volFill, m_volFillVisible ? deckAlpha : 0.f);
    applyBarOpacity(m_volThumb, deckAlpha);

    // Header buttons (Deck mode)
    if (m_headerMenu) {
        m_headerMenu->setContentSize({curW, curH});
        m_headerMenu->setVisible(deckAlpha > 0.02f);
        m_headerMenu->setEnabled(deckAlpha > 0.45f);
        setNodeTreeOpacity(m_headerMenu, toByte(deckAlpha));
        float const hy = kDeckH - 13.f;
        if (m_fxBtn)       m_fxBtn->setPosition({kDeckW - 60.f, hy});
        if (m_menuBtn)     m_menuBtn->setPosition({kDeckW - 44.f, hy});
        if (m_collapseBtn) m_collapseBtn->setPosition({kDeckW - 28.f, hy});
        if (m_closeBtn)    m_closeBtn->setPosition({kDeckW - 13.f, hy});
    }

    if (m_deckMenu) {
        m_deckMenu->setContentSize({curW, curH});
        m_deckMenu->setVisible(deckAlpha > 0.02f);
        m_deckMenu->setEnabled(deckAlpha > 0.45f);
        setNodeTreeOpacity(m_deckMenu, toByte(deckAlpha));
    }

    if (m_miniMenu) {
        m_miniMenu->setContentSize({curW, curH});
        m_miniMenu->setVisible(miniAlpha > 0.02f);
        m_miniMenu->setEnabled(miniAlpha > 0.45f);
        setNodeTreeOpacity(m_miniMenu, toByte(miniAlpha));
    }
}

void EditorMusicPanel::tick(float dt) {
    float const safeDt = std::clamp(dt, 0.f, 0.05f);
    m_clock += safeDt;

    auto& player = EditorMusicPlayer::get();
    player.tick(safeDt);

    bool const levelRunning = m_editor && m_editor->m_playbackMode != PlaybackMode::Not;
    if (levelRunning && !player.isSuspended()) player.suspend();
    else if (!levelRunning && player.isSuspended()) player.resumeFromSuspend();

    updateAnimations(safeDt);

    auto win = CCDirector::get()->getWinSize();
    if (!win.equals(m_lastWin)) {
        m_lastWin = win;
        clampToScreen();
    }

    if (this->isVisible() || m_open) {
        if (player.trackId() != m_shownTrackId) refreshTrackInfo(true);
        updateTransport();
        updateEqualizer(safeDt);
        updateArtwork(safeDt);
        updateMarquee(safeDt);
        updateBars(safeDt);
    }

    applyPanelLayoutAndOpacity();
}

void EditorMusicPanel::openPicker(int initialTab) {
    auto* scene = CCDirector::get()->getRunningScene();
    if (scene && scene->getChildByID("editor-music-picker"_spr)) return;
    if (auto* popup = EditorMusicPickerPopup::create(initialTab)) popup->show();
}

void EditorMusicPanel::applySeekAt(CCPoint local) {
    m_seekPreviewRatio = std::clamp((local.x - kSeekX) / kSeekW, 0.f, 1.f);
    updateBars(0.f);
}

void EditorMusicPanel::applyVolumeAt(CCPoint local) {
    float ratio = std::clamp((local.x - kVolX) / kVolW, 0.f, 1.f);
    EditorMusicPlayer::get().setVolume(ratio, false);
    updateBars(0.f);
}

bool EditorMusicPanel::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!this->isVisible() || !m_open || m_openAnim < 0.4f || m_playtestAnim < 0.4f) return false;

    auto const sz = this->getContentSize();
    auto local = this->convertToNodeSpace(touch->getLocation());
    if (local.x < 0.f || local.y < 0.f || local.x > sz.width || local.y > sz.height) return false;

    if (!m_collapsed && m_collapseAnim < 0.3f) {
        CCRect seekArea{kSeekX - 4.f, kSeekY - kGrabSlack, kSeekW + 8.f, kSeekVisualH + kGrabSlack * 2.f};
        CCRect volumeArea{kVolX - 4.f, kVolY - kGrabSlack, kVolW + 8.f, kVolVisualH + kGrabSlack * 2.f};

        if (seekArea.containsPoint(local)) {
            m_grab = Grab::Seek;
            applySeekAt(local);
            return true;
        }
        if (volumeArea.containsPoint(local)) {
            m_grab = Grab::Volume;
            applyVolumeAt(local);
            return true;
        }
    }

    m_grab = Grab::Move;
    auto* parent = this->getParent();
    auto world = touch->getLocation();
    auto parentLocal = parent ? parent->convertToNodeSpace(world) : world;
    m_grabOffset = parentLocal - this->getPosition();
    return true;
}

void EditorMusicPanel::ccTouchMoved(CCTouch* touch, CCEvent*) {
    switch (m_grab) {
        case Grab::Seek:
            applySeekAt(this->convertToNodeSpace(touch->getLocation()));
            break;
        case Grab::Volume:
            applyVolumeAt(this->convertToNodeSpace(touch->getLocation()));
            break;
        case Grab::Move: {
            auto* parent = this->getParent();
            auto world = touch->getLocation();
            auto parentLocal = parent ? parent->convertToNodeSpace(world) : world;
            this->setPosition(parentLocal - m_grabOffset);
            clampToScreen();
            break;
        }
        case Grab::None:
            break;
    }
}

void EditorMusicPanel::ccTouchEnded(CCTouch*, CCEvent*) {
    if (m_grab == Grab::Move) {
        savePosition();
    } else if (m_grab == Grab::Seek) {
        auto& player = EditorMusicPlayer::get();
        int const length = player.lengthMs();
        if (length > 0) {
            player.seekMs(static_cast<int>(m_seekPreviewRatio * static_cast<float>(length)));
        }
    } else if (m_grab == Grab::Volume) {
        auto& player = EditorMusicPlayer::get();
        player.setVolume(player.volume(), true);
    }
    m_grab = Grab::None;
    updateBars(0.f);
}

void EditorMusicPanel::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    ccTouchEnded(touch, event);
}

} // namespace paimon::editormusic
