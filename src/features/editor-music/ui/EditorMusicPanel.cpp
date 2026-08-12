#include "EditorMusicPanel.hpp"

#include "EditorMusicPickerPopup.hpp"
#include "../services/EditorMusicPlayer.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <fmt/format.h>

#include <algorithm>

using namespace geode::prelude;

namespace paimon::editormusic {

namespace {

constexpr float kPanelW = 180.f;
constexpr float kPanelH = 58.f;

constexpr float kPad = 8.f;
constexpr float kBarW = kPanelW - kPad * 2.f;
constexpr float kBarH = 4.f;
constexpr float kSeekY = 33.f;
constexpr float kRowY = 16.f;
constexpr float kTitleY = 48.f;
constexpr float kTitleMaxW = 96.f;

constexpr float kVolumeX = 126.f;
constexpr float kVolumeW = kPanelW - kPad - kVolumeX;
constexpr float kVolumeY = 14.f;

// Touch slack around the thin bars so they are not impossible to grab.
constexpr float kGrabSlack = 6.f;

constexpr int kMenuPriority = -260;
constexpr int kPanelPriority = -250;

constexpr char const* kOpenKey = "editor-music-open";
constexpr char const* kPosXKey = "editor-music-pos-x";
constexpr char const* kPosYKey = "editor-music-pos-y";

constexpr ccColor3B kAccent = {120, 200, 255};
constexpr ccColor3B kDim = {130, 138, 155};

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
    this->setContentSize({kPanelW, kPanelH});
    this->setTouchEnabled(true);

    EditorMusicPlayer::get().refreshQueue();

    buildUI();
    loadPosition();

    m_open = Mod::get()->getSavedValue<bool>(kOpenKey, false);
    applyVisibility();

    this->schedule(schedule_selector(EditorMusicPanel::tick), 0.05f);
    return true;
}

EditorMusicPanel::~EditorMusicPanel() {
    if (s_instance == this) s_instance = nullptr;
    // Leaving the editor takes the music with it; nothing else owns the channel.
    EditorMusicPlayer::get().stop();
}

void EditorMusicPanel::onExit() {
    CCLayer::onExit();
    if (s_instance == this) s_instance = nullptr;
}

void EditorMusicPanel::registerWithTouchDispatcher() {
    // After our own menu (so buttons win) but before the editor canvas.
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, kPanelPriority, true);
}

CCMenuItemSpriteExtra* EditorMusicPanel::iconButton(
    std::initializer_list<char const*> frames, float size, std::function<void()> onPress
) {
    auto* sprite = firstFrame(frames);
    if (!sprite) return nullptr;
    limitNodeSize(sprite, {size, size}, 2.f, 0.05f);
    return CCMenuItemExt::createSpriteExtra(sprite, [onPress = std::move(onPress)](auto*) {
        onPress();
    });
}

void EditorMusicPanel::buildUI() {
    if (auto* bg = SpriteHelper::createColorPanel(kPanelW, kPanelH, {10, 13, 24}, 210, 8.f)) {
        bg->setAnchorPoint({0.f, 0.f});
        bg->setPosition({0.f, 0.f});
        this->addChild(bg, -1);
    }

    m_titleLabel = CCLabelBMFont::create("Sin cancion", "bigFont.fnt");
    m_titleLabel->setAnchorPoint({0.f, 0.5f});
    m_titleLabel->setPosition({kPad, kTitleY});
    m_titleLabel->setScale(0.3f);
    this->addChild(m_titleLabel);

    m_timeLabel = CCLabelBMFont::create("0:00 / 0:00", "chatFont.fnt");
    m_timeLabel->setAnchorPoint({1.f, 0.5f});
    m_timeLabel->setPosition({kPanelW - 22.f, kTitleY});
    m_timeLabel->setScale(0.32f);
    m_timeLabel->setColor(kDim);
    this->addChild(m_timeLabel);

    auto* seekBg = CCLayerColor::create({255, 255, 255, 40}, kBarW, kBarH);
    seekBg->setPosition({kPad, kSeekY});
    this->addChild(seekBg);

    m_seekFill = CCLayerColor::create({kAccent.r, kAccent.g, kAccent.b, 235}, 0.f, kBarH);
    m_seekFill->setPosition({kPad, kSeekY});
    this->addChild(m_seekFill);

    auto* volumeBg = CCLayerColor::create({255, 255, 255, 40}, kVolumeW, kBarH);
    volumeBg->setPosition({kVolumeX, kVolumeY});
    this->addChild(volumeBg);

    m_volumeFill = CCLayerColor::create({255, 255, 255, 190}, 0.f, kBarH);
    m_volumeFill->setPosition({kVolumeX, kVolumeY});
    this->addChild(m_volumeFill);

    if (auto* speaker = firstFrame({"GJ_musicOnBtn_001.png", "GJ_playMusicBtn_001.png"})) {
        limitNodeSize(speaker, {9.f, 9.f}, 2.f, 0.05f);
        speaker->setPosition({kVolumeX - 9.f, kVolumeY + kBarH / 2.f});
        speaker->setOpacity(170);
        this->addChild(speaker);
    }

    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    m_menu->setContentSize({kPanelW, kPanelH});
    m_menu->setTouchPriority(kMenuPriority);
    this->addChild(m_menu);

    float x = 14.f;
    auto place = [&](CCMenuItemSpriteExtra* button, float step) {
        if (!button) return;
        button->setPosition({x, kRowY});
        m_menu->addChild(button);
        x += step;
    };

    place(iconButton({"GJ_arrow_02_001.png"}, 10.f, [] {
        EditorMusicPlayer::get().playPrevious();
    }), 18.f);

    m_playButton = iconButton({"GJ_playBtn2_001.png", "GJ_playBtn_001.png"}, 14.f, [] {
        EditorMusicPlayer::get().togglePause();
    });
    place(m_playButton, 19.f);

    if (auto* next = iconButton({"GJ_arrow_02_001.png"}, 10.f, [] {
            EditorMusicPlayer::get().playNext();
        })) {
        if (auto* sprite = typeinfo_cast<CCSprite*>(next->getNormalImage())) sprite->setFlipX(true);
        place(next, 19.f);
    }

    auto* shuffle = iconButton({"GJ_chanceBtn_001.png", "GJ_reloadBtn_001.png"}, 11.f, [this] {
        EditorMusicPlayer::get().toggleShuffle();
        updateTransport();
    });
    if (shuffle) m_shuffleIcon = typeinfo_cast<CCSprite*>(shuffle->getNormalImage());
    place(shuffle, 18.f);

    auto* repeat = iconButton({"GJ_updateBtn_001.png", "GJ_reloadBtn_001.png"}, 11.f, [this] {
        EditorMusicPlayer::get().cycleRepeat();
        updateTransport();
    });
    if (repeat) {
        m_repeatIcon = typeinfo_cast<CCSprite*>(repeat->getNormalImage());
        m_repeatBadge = CCLabelBMFont::create("1", "bigFont.fnt");
        m_repeatBadge->setScale(0.24f);
        m_repeatBadge->setPosition({repeat->getContentSize().width * 0.8f,
                                    repeat->getContentSize().height * 0.22f});
        m_repeatBadge->setVisible(false);
        repeat->addChild(m_repeatBadge, 2);
    }
    place(repeat, 18.f);

    place(iconButton({"GJ_musicOnBtn_001.png", "GJ_plusBtn_001.png"}, 11.f, [this] {
        openPicker();
    }), 18.f);

    if (auto* close = iconButton({"GJ_deleteIcon_001.png", "GJ_closeBtn_001.png"}, 9.f, [this] {
            if (m_open) toggleOpen();
        })) {
        close->setPosition({kPanelW - 10.f, kPanelH - 9.f});
        m_menu->addChild(close);
    }

    m_lastWin = CCDirector::get()->getWinSize();
    updateTransport();
    updateBars();
    refreshTrackInfo();
}

void EditorMusicPanel::loadPosition() {
    auto win = CCDirector::get()->getWinSize();
    auto* mod = Mod::get();
    float x = static_cast<float>(mod->getSavedValue<double>(kPosXKey, 8.0));
    float y = static_cast<float>(mod->getSavedValue<double>(kPosYKey, win.height - 44.f - kPanelH));
    this->setPosition({x, y});
    clampToScreen();
}

void EditorMusicPanel::savePosition() {
    auto* mod = Mod::get();
    mod->setSavedValue<double>(kPosXKey, this->getPositionX());
    mod->setSavedValue<double>(kPosYKey, this->getPositionY());
}

void EditorMusicPanel::clampToScreen() {
    auto win = CCDirector::get()->getWinSize();
    auto pos = this->getPosition();
    pos.x = std::clamp(pos.x, 0.f, std::max(0.f, win.width - kPanelW));
    pos.y = std::clamp(pos.y, 0.f, std::max(0.f, win.height - kPanelH));
    this->setPosition(pos);
}

bool EditorMusicPanel::toggleOpen() {
    m_open = !m_open;
    Mod::get()->setSavedValue<bool>(kOpenKey, m_open);
    if (m_open) {
        EditorMusicPlayer::get().refreshQueue();
        refreshTrackInfo();
    }
    applyVisibility();
    return m_open;
}

void EditorMusicPanel::applyVisibility() {
    // Paused counts as playtesting too, or the panel would pop back up over the
    // pause menu halfway through a run.
    bool playtesting = m_editor && m_editor->m_playbackMode != PlaybackMode::Not;
    bool uiHidden = m_editor && m_editor->m_editorUI && !m_editor->m_editorUI->isVisible();
    this->setVisible(m_open && !playtesting && !uiHidden);
}

void EditorMusicPanel::refreshTrackInfo() {
    auto& player = EditorMusicPlayer::get();
    m_shownTrackId = player.trackId();

    auto name = player.trackName();
    if (name.empty()) name = "Sin cancion";
    m_titleLabel->setString(name.c_str());
    m_titleLabel->limitLabelWidth(kTitleMaxW, 0.3f, 0.16f);
}

void EditorMusicPanel::updateTransport() {
    auto& player = EditorMusicPlayer::get();

    bool playing = player.isPlaying();
    if (m_playButton && playing != m_shownPlaying) {
        auto* sprite = playing
            ? firstFrame({"GJ_pauseBtn_001.png", "GJ_pauseEditorBtn_001.png"})
            : firstFrame({"GJ_playBtn2_001.png", "GJ_playBtn_001.png"});
        if (sprite) {
            limitNodeSize(sprite, {14.f, 14.f}, 2.f, 0.05f);
            m_playButton->setNormalImage(sprite);
        }
        m_shownPlaying = playing;
    }

    if (m_shuffleIcon) {
        m_shuffleIcon->setColor(player.shuffle() ? kAccent : kDim);
        m_shuffleIcon->setOpacity(player.shuffle() ? 255 : 140);
    }
    if (m_repeatIcon) {
        bool on = player.repeat() != RepeatMode::Off;
        m_repeatIcon->setColor(on ? kAccent : kDim);
        m_repeatIcon->setOpacity(on ? 255 : 140);
    }
    if (m_repeatBadge) m_repeatBadge->setVisible(player.repeat() == RepeatMode::One);
}

void EditorMusicPanel::updateBars() {
    auto& player = EditorMusicPlayer::get();

    int length = player.lengthMs();
    int position = player.positionMs();
    float progress = length > 0 ? std::clamp(static_cast<float>(position) / length, 0.f, 1.f) : 0.f;

    if (m_seekFill) m_seekFill->setContentSize({kBarW * progress, kBarH});
    if (m_volumeFill) m_volumeFill->setContentSize({kVolumeW * player.volume(), kBarH});
    if (m_timeLabel) {
        m_timeLabel->setString(
            fmt::format("{} / {}", formatTime(position), formatTime(length)).c_str());
    }
}

void EditorMusicPanel::tick(float) {
    auto& player = EditorMusicPlayer::get();
    player.tick();

    // Backstop for the playtest hooks: whatever put the editor in playback mode,
    // the level's own audio owns the room until it drops back out.
    bool levelRunning = m_editor && m_editor->m_playbackMode != PlaybackMode::Not;
    if (levelRunning && !player.isSuspended()) player.suspend();
    else if (!levelRunning && player.isSuspended()) player.resumeFromSuspend();

    applyVisibility();
    if (!this->isVisible()) return;

    auto win = CCDirector::get()->getWinSize();
    if (!win.equals(m_lastWin)) {
        m_lastWin = win;
        clampToScreen();
    }

    if (player.trackId() != m_shownTrackId) refreshTrackInfo();
    updateTransport();
    updateBars();
}

void EditorMusicPanel::openPicker() {
    auto* scene = CCDirector::get()->getRunningScene();
    if (scene && scene->getChildByID("editor-music-picker"_spr)) return;
    if (auto* popup = EditorMusicPickerPopup::create()) popup->show();
}

void EditorMusicPanel::applySeekAt(CCPoint local) {
    auto& player = EditorMusicPlayer::get();
    int length = player.lengthMs();
    if (length <= 0) return;
    float ratio = std::clamp((local.x - kPad) / kBarW, 0.f, 1.f);
    player.seekMs(static_cast<int>(ratio * length));
    updateBars();
}

void EditorMusicPanel::applyVolumeAt(CCPoint local) {
    float ratio = std::clamp((local.x - kVolumeX) / kVolumeW, 0.f, 1.f);
    EditorMusicPlayer::get().setVolume(ratio);
    updateBars();
}

bool EditorMusicPanel::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!this->isVisible()) return false;

    auto local = this->convertToNodeSpace(touch->getLocation());
    if (local.x < 0.f || local.y < 0.f || local.x > kPanelW || local.y > kPanelH) return false;

    CCRect seekArea{kPad, kSeekY - kGrabSlack, kBarW, kBarH + kGrabSlack * 2.f};
    CCRect volumeArea{kVolumeX, kVolumeY - kGrabSlack, kVolumeW, kBarH + kGrabSlack * 2.f};

    if (seekArea.containsPoint(local)) {
        m_grab = Grab::Seek;
        applySeekAt(local);
    } else if (volumeArea.containsPoint(local)) {
        m_grab = Grab::Volume;
        applyVolumeAt(local);
    } else {
        m_grab = Grab::Move;
        m_grabOffset = local;
    }
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
            auto local = parent ? parent->convertToNodeSpace(world) : world;
            this->setPosition(local - m_grabOffset);
            clampToScreen();
            break;
        }
        case Grab::None:
            break;
    }
}

void EditorMusicPanel::ccTouchEnded(CCTouch*, CCEvent*) {
    if (m_grab == Grab::Move) savePosition();
    m_grab = Grab::None;
}

void EditorMusicPanel::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    ccTouchEnded(touch, event);
}

} // namespace paimon::editormusic
