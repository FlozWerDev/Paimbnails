#include "EditorNowPlayingToast.hpp"

#include "../services/EditorMusicPlayer.hpp"
#include "../../menu-music/services/SongCoverCache.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/TextureBudget.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using paimon::menumusic::SongCoverCache;

namespace paimon::editormusic {

namespace {

constexpr char const* kToastNodeId = "paimon-editor-now-playing-toast";
constexpr float kCapsuleH = 24.f;
constexpr float kMinW = 126.f;
constexpr float kMaxW = 206.f;
constexpr float kDiscSize = 18.f;
constexpr int kToastZOrder = 9500;

float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }

float easeOutCubic(float t) {
    float const u = 1.f - clamp01(t);
    return 1.f - u * u * u;
}

float easeInCubic(float t) {
    t = clamp01(t);
    return t * t * t;
}

float easeOutQuint(float t) {
    float const u = 1.f - clamp01(t);
    return 1.f - u * u * u * u * u;
}

float easeOutBack(float t) {
    t = clamp01(t);
    constexpr float c1 = 1.55f;
    constexpr float c3 = c1 + 1.f;
    float const u = t - 1.f;
    return 1.f + c3 * u * u * u + c1 * u * u;
}

GLubyte toByte(float a) {
    return static_cast<GLubyte>(clamp01(a) * 255.f + 0.5f);
}

ccColor3B accentForSkin(int skin) {
    switch (skin) {
        case 1: return {105, 225, 255}; // Azul GD
        case 2: return {210, 155, 255}; // Morado GD
        case 3: return {130, 245, 160}; // Oscuro GD
        default: return {255, 212, 95}; // Clasico (Cafe)
    }
}

bool editorIsPlaytestingOrHidden() {
    auto* editor = LevelEditorLayer::get();
    if (!editor) return false;
    if (editor->m_playbackMode != PlaybackMode::Not) return true;
    if (editor->m_editorUI && !editor->m_editorUI->isVisible()) return true;
    return false;
}

} // namespace

void EditorNowPlayingToast::show(
    CCNode* parent,
    std::string const& title,
    std::string const& artist,
    std::string const& coverPath,
    int songId
) {
    if (!parent || title.empty() || editorIsPlaytestingOrHidden()) return;

    if (auto* existing = parent->getChildByID(kToastNodeId)) {
        existing->removeFromParent();
    }

    auto* toast = new EditorNowPlayingToast();
    if (toast && toast->init(title, artist, coverPath, songId)) {
        toast->autorelease();
        parent->addChild(toast, kToastZOrder);
    } else {
        CC_SAFE_DELETE(toast);
    }
}

bool EditorNowPlayingToast::init(
    std::string const& title,
    std::string const& artist,
    std::string const& coverPath,
    int songId
) {
    if (!CCNode::init()) return false;

    auto& player = EditorMusicPlayer::get();
    m_animated = player.animationsEnabled();
    m_accent = accentForSkin(player.panelSkin());

    this->setID(kToastNodeId);
    this->setAnchorPoint({0.5f, 0.5f});
    this->ignoreAnchorPointForPosition(false);

    auto const win = CCDirector::get()->getWinSize();
    this->setPosition({win.width * 0.5f, 106.f});

    // Subtle dark pill backdrop + thin accent border
    m_border = SpriteHelper::safeCreateNineSliceFromFile("square02b_001.png");
    if (m_border) {
        m_border->setScaleMultiplier(0.28f);
        m_border->setAnchorPoint({0.5f, 0.5f});
        m_border->setColor(m_accent);
        m_border->setOpacity(110);
        this->addChild(m_border, 0);
    }

    m_bg = SpriteHelper::safeCreateNineSliceFromFile("square02b_001.png");
    if (m_bg) {
        m_bg->setScaleMultiplier(0.26f);
        m_bg->setAnchorPoint({0.5f, 0.5f});
        m_bg->setColor({14, 17, 26});
        m_bg->setOpacity(225);
        this->addChild(m_bg, 1);
    }

    // Bottom hairline progress drain
    m_progressBar = SpriteHelper::safeCreateNineSliceFromFile("square02b_001.png");
    if (m_progressBar) {
        m_progressBar->setScaleMultiplier(0.12f);
        m_progressBar->setAnchorPoint({0.f, 0.f});
        m_progressBar->setColor(m_accent);
        m_progressBar->setOpacity(210);
        this->addChild(m_progressBar, 4);
    }

    // Left mini spinning disc / cover art
    m_discRoot = CCNode::create();
    m_discRoot->setContentSize({kDiscSize, kDiscSize});
    m_discRoot->setAnchorPoint({0.5f, 0.5f});
    this->addChild(m_discRoot, 3);

    if (auto* ring = SpriteHelper::safeCreateNineSliceFromFile("square02b_001.png")) {
        ring->setScaleMultiplier(0.22f);
        ring->setContentSize({kDiscSize + 2.f, kDiscSize + 2.f});
        ring->setPosition({kDiscSize * 0.5f, kDiscSize * 0.5f});
        ring->setColor({28, 34, 48});
        ring->setOpacity(235);
        m_discRoot->addChild(ring, 0);
    }

    m_discRotor = CCNode::create();
    m_discRotor->setContentSize({kDiscSize, kDiscSize});
    m_discRotor->setAnchorPoint({0.5f, 0.5f});
    m_discRotor->setPosition({kDiscSize * 0.5f, kDiscSize * 0.5f});
    m_discRoot->addChild(m_discRotor, 1);

    if (auto* stencil = SpriteHelper::createRoundedRectStencil(kDiscSize, kDiscSize, kDiscSize * 0.5f)) {
        m_coverClip = CCClippingNode::create();
        m_coverClip->setStencil(stencil);
        m_coverClip->setAlphaThreshold(0.05f);
        m_coverClip->setContentSize({kDiscSize, kDiscSize});
        m_coverClip->setAnchorPoint({0.5f, 0.5f});
        m_coverClip->setPosition({kDiscSize * 0.5f, kDiscSize * 0.5f});
        m_discRotor->addChild(m_coverClip, 0);
    }

    m_fallbackIcon = SpriteHelper::safeCreateWithFrameName("GJ_musicOnBtn_001.png");
    if (!m_fallbackIcon) m_fallbackIcon = SpriteHelper::safeCreateWithFrameName("GJ_playMusicBtn_001.png");
    if (m_fallbackIcon) {
        limitNodeSize(m_fallbackIcon, {13.f, 13.f}, 2.f, 0.05f);
        m_fallbackIcon->setPosition({kDiscSize * 0.5f, kDiscSize * 0.5f});
        m_discRotor->addChild(m_fallbackIcon, 2);
    }

    // Clipped title + artist labels
    m_textStencil = PaimonDrawNode::create();
    m_textClip = CCClippingNode::create();
    if (m_textClip && m_textStencil) {
        m_textClip->setStencil(m_textStencil);
        m_textClip->setAlphaThreshold(0.05f);
        m_textClip->setAnchorPoint({0.f, 0.5f});
        this->addChild(m_textClip, 3);
    }

    bool const hasArtist = !artist.empty();
    float const maxTextW = kMaxW - 50.f;

    m_titleLabel = CCLabelBMFont::create(title.c_str(), "bigFont.fnt");
    if (m_titleLabel) {
        m_titleLabel->setAnchorPoint({0.f, 0.5f});
        m_titleLabel->limitLabelWidth(maxTextW, 0.28f, 0.17f);
        m_titleLabel->setColor({255, 255, 255});
        m_titleLabel->setPosition({1.f, hasArtist ? 14.5f : 11.f});
        if (m_textClip) m_textClip->addChild(m_titleLabel, 1);
    }

    float titleVisW = m_titleLabel ? m_titleLabel->getScaledContentSize().width : 60.f;
    float artistVisW = 0.f;
    if (hasArtist) {
        m_artistLabel = CCLabelBMFont::create(artist.c_str(), "chatFont.fnt");
        if (m_artistLabel) {
            m_artistLabel->setAnchorPoint({0.f, 0.5f});
            m_artistLabel->limitLabelWidth(maxTextW, 0.35f, 0.22f);
            m_artistLabel->setColor({178, 192, 214});
            m_artistLabel->setPosition({1.f, 6.f});
            artistVisW = m_artistLabel->getScaledContentSize().width;
            if (m_textClip) m_textClip->addChild(m_artistLabel, 1);
        }
    }

    float const neededTextW = std::max(titleVisW, artistVisW);
    m_targetW = std::clamp(neededTextW + 52.f, kMinW, kMaxW);

    // 3 micro EQ bars on right end
    for (int i = 0; i < 3; ++i) {
        auto* bar = SpriteHelper::safeCreateNineSliceFromFile("square02b_001.png");
        if (!bar) continue;
        bar->setScaleMultiplier(0.14f);
        bar->setAnchorPoint({0.5f, 0.f});
        bar->setContentSize({2.4f, 3.f});
        bar->setColor(m_accent);
        this->addChild(bar, 3);
        m_eqBars[static_cast<std::size_t>(i)] = bar;
    }

    loadCover(coverPath, songId);

    if (m_animated) {
        enter(Phase::Pop, 0.20f);
        applyLayout(kCapsuleH, 0.f, 0.f, 1.f);
    } else {
        enter(Phase::Hold, 2.20f);
        applyLayout(m_targetW, 1.f, 1.f, 1.f);
    }

    this->schedule(schedule_selector(EditorNowPlayingToast::tick));
    return true;
}

void EditorNowPlayingToast::onExit() {
    ++m_coverGen;
    this->unschedule(schedule_selector(EditorNowPlayingToast::tick));
    CCNode::onExit();
}

void EditorNowPlayingToast::enter(Phase phase, float duration) {
    m_phase = phase;
    m_phaseTime = 0.f;
    m_phaseDuration = std::max(0.001f, duration);
}

void EditorNowPlayingToast::finish() {
    m_phase = Phase::Done;
    this->unschedule(schedule_selector(EditorNowPlayingToast::tick));
    this->removeFromParent();
}

void EditorNowPlayingToast::updateClipStencil(float clipW) {
    if (!m_textStencil || std::abs(clipW - m_lastClipW) < 0.35f) return;
    m_lastClipW = clipW;
    m_textStencil->clear();
    if (clipW <= 0.5f) return;
    CCPoint rect[4] = {
        ccp(0.f, 0.f), ccp(clipW, 0.f),
        ccp(clipW, kCapsuleH - 2.f), ccp(0.f, kCapsuleH - 2.f)
    };
    m_textStencil->drawPolygon(rect, 4, {1.f, 1.f, 1.f, 1.f}, 0.f, {0.f, 0.f, 0.f, 0.f});
}

void EditorNowPlayingToast::applyLayout(float curW, float masterAlpha, float contentAlpha, float holdProgress) {
    this->setContentSize({curW, kCapsuleH});

    if (m_border) {
        m_border->setContentSize({curW + 1.6f, kCapsuleH + 1.6f});
        m_border->setPosition({curW * 0.5f, kCapsuleH * 0.5f});
        m_border->setOpacity(toByte(0.45f * masterAlpha));
    }
    if (m_bg) {
        m_bg->setContentSize({curW, kCapsuleH});
        m_bg->setPosition({curW * 0.5f, kCapsuleH * 0.5f});
        m_bg->setOpacity(toByte(0.88f * masterAlpha));
    }

    if (m_discRoot) {
        m_discRoot->setPosition({kCapsuleH * 0.5f, kCapsuleH * 0.5f});
        if (m_coverSprite) m_coverSprite->setOpacity(toByte(masterAlpha));
        if (m_fallbackIcon) m_fallbackIcon->setOpacity(toByte(masterAlpha));
    }

    float const clipW = std::max(0.f, curW - 46.f);
    if (m_textClip) {
        m_textClip->setContentSize({clipW, kCapsuleH - 2.f});
        m_textClip->setPosition({26.f, kCapsuleH * 0.5f});
        updateClipStencil(clipW);
    }
    float const textSlideX = (1.f - contentAlpha) * -6.f;
    if (m_titleLabel) {
        m_titleLabel->setPositionX(1.f + textSlideX);
        m_titleLabel->setOpacity(toByte(masterAlpha * contentAlpha));
    }
    if (m_artistLabel) {
        m_artistLabel->setPositionX(1.f + textSlideX);
        m_artistLabel->setOpacity(toByte(masterAlpha * contentAlpha * 0.92f));
    }

    // 3 micro EQ bars on right
    float const eqAlpha = masterAlpha * clamp01((contentAlpha - 0.25f) * 1.35f);
    for (int i = 0; i < 3; ++i) {
        auto* bar = m_eqBars[static_cast<std::size_t>(i)];
        if (!bar) continue;
        bar->setVisible(eqAlpha > 0.02f && curW > 64.f);
        bar->setPosition({curW - 15.f + static_cast<float>(i) * 4.0f, 6.5f});
        bar->setContentSize({2.3f, 2.5f + 8.5f * m_eqLevels[static_cast<std::size_t>(i)]});
        bar->setOpacity(toByte(eqAlpha));
    }

    // Bottom hairline progress drain
    if (m_progressBar) {
        float const maxBarW = std::max(0.f, curW - 14.f);
        float const barW = maxBarW * clamp01(holdProgress);
        bool const showBar = (barW >= 2.f) && (contentAlpha > 0.2f) && (masterAlpha > 0.05f);
        m_progressBar->setVisible(showBar);
        if (showBar) {
            m_progressBar->setPosition({7.f, 1.2f});
            m_progressBar->setContentSize({barW, 1.5f});
            m_progressBar->setOpacity(toByte(0.80f * masterAlpha * contentAlpha));
        }
    }
}

void EditorNowPlayingToast::tick(float dt) {
    if (m_phase == Phase::Done) return;
    if (editorIsPlaytestingOrHidden()) {
        finish();
        return;
    }

    float const safeDt = std::clamp(dt, 0.f, 0.05f);
    m_clock += safeDt;
    m_phaseTime += safeDt;

    auto& player = EditorMusicPlayer::get();
    bool const playing = player.isPlaying();
    float const peak = player.outputPeak();

    // Spin disc softly (extra spin burst during Pop/Unfold)
    float spinSpeed = playing ? 55.f : 0.f;
    if (m_phase == Phase::Pop || m_phase == Phase::Unfold) spinSpeed += 140.f;
    m_spinAngle = std::fmod(m_spinAngle + spinSpeed * safeDt, 360.f);
    if (m_discRotor) m_discRotor->setRotation(m_spinAngle);

    // Update 3 micro EQ bars
    constexpr std::array<float, 3> kFreq = {7.2f, 9.8f, 6.4f};
    constexpr std::array<float, 3> kPhase = {0.3f, 2.1f, 4.0f};
    for (std::size_t i = 0; i < m_eqBars.size(); ++i) {
        float target = 0.16f;
        if (playing) {
            float const wave = 0.5f + 0.5f * std::sin(m_clock * kFreq[i] + kPhase[i]);
            float const synth = 0.28f + 0.72f * wave;
            target = peak > 0.01f ? std::clamp(lerp(synth, peak * 1.35f, 0.55f), 0.16f, 1.f) : synth;
        }
        m_eqLevels[i] += (target - m_eqLevels[i]) * std::min(1.f, safeDt * 18.f);
    }

    float const t = clamp01(m_phaseTime / m_phaseDuration);
    switch (m_phase) {
        case Phase::Pop: {
            float const s = lerp(0.35f, 1.f, easeOutBack(t));
            this->setScale(std::max(0.05f, s));
            applyLayout(kCapsuleH, easeOutCubic(t), 0.f, 1.f);
            if (t >= 1.f) {
                this->setScale(1.f);
                enter(Phase::Unfold, 0.28f);
            }
            break;
        }
        case Phase::Unfold: {
            float const e = easeOutQuint(t);
            float const w = lerp(kCapsuleH, m_targetW, e);
            float const contentA = clamp01((t - 0.18f) / 0.82f);
            this->setScale(1.f);
            applyLayout(w, 1.f, easeOutCubic(contentA), 1.f);
            if (t >= 1.f) {
                enter(Phase::Hold, 2.25f);
            }
            break;
        }
        case Phase::Hold: {
            this->setScale(1.f);
            applyLayout(m_targetW, 1.f, 1.f, 1.f - t);
            if (t >= 1.f) {
                if (m_animated) enter(Phase::Fold, 0.22f);
                else finish();
            }
            break;
        }
        case Phase::Fold: {
            float const e = easeInCubic(t);
            float const w = lerp(m_targetW, kCapsuleH, e);
            float const contentA = clamp01(1.f - t * 1.8f);
            this->setScale(1.f);
            applyLayout(w, 1.f, contentA, 0.f);
            if (t >= 1.f) {
                enter(Phase::Vanish, 0.16f);
            }
            break;
        }
        case Phase::Vanish: {
            float const e = easeInCubic(t);
            this->setScale(std::max(0.05f, lerp(1.f, 0.45f, e)));
            applyLayout(kCapsuleH, 1.f - e, 0.f, 0.f);
            if (t >= 1.f) {
                finish();
            }
            break;
        }
        case Phase::Done:
            break;
    }
}

void EditorNowPlayingToast::loadCover(std::string const& path, int songId) {
    auto const gen = ++m_coverGen;
    if (path.empty()) {
        applyCover(nullptr);
        if (songId > 0 && Mod::get()->getSavedValue<bool>("menuMusicThumbnailCovers", true)) {
            WeakRef<EditorNowPlayingToast> weak = this;
            SongCoverCache::get().requestCovers(songId, [weak, gen](std::vector<std::string> const& paths, bool ok) {
                auto self = weak.lock();
                if (!self || self->m_coverGen != gen || !ok || paths.empty()) return;
                self->loadCover(paths.front(), 0);
            });
        }
        return;
    }

    if (auto* cached = CCTextureCache::sharedTextureCache()->textureForKey(path.c_str())) {
        applyCover(cached);
        return;
    }

    WeakRef<EditorNowPlayingToast> weak = this;
    paimon::image::loadBudgetedAsync(path, [weak, gen](CCTexture2D* tex) {
        auto self = weak.lock();
        if (!self || self->m_coverGen != gen) return;
        self->applyCover(tex);
    });
}

void EditorNowPlayingToast::applyCover(CCTexture2D* tex) {
    if (m_coverSprite) {
        m_coverSprite->removeFromParent();
        m_coverSprite = nullptr;
    }
    if (!tex || !m_coverClip) {
        if (m_fallbackIcon) m_fallbackIcon->setVisible(true);
        return;
    }
    auto* spr = CCSprite::createWithTexture(tex);
    if (!spr) {
        if (m_fallbackIcon) m_fallbackIcon->setVisible(true);
        return;
    }
    auto const clipSz = m_coverClip->getContentSize();
    auto const texSz = spr->getContentSize();
    float const scale = std::max(
        clipSz.width / std::max(1.f, texSz.width),
        clipSz.height / std::max(1.f, texSz.height));
    spr->setScale(scale);
    spr->setPosition(clipSz / 2.f);
    m_coverClip->addChild(spr, 1);
    m_coverSprite = spr;
    if (m_fallbackIcon) m_fallbackIcon->setVisible(false);
}

} // namespace paimon::editormusic
