#pragma once

#include <Geode/Geode.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCScale9Sprite.h>
#include <Geode/ui/NineSlice.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class PaimonDrawNode;

namespace paimon::menumusic {

// entry and exit motion of the Now Playing toast. The ids are the values stored
// in the menuLoopNotificationStyle setting.
enum class ToastAnimation : std::uint8_t {
    Expand = 0,  // drops as a compact GD square badge and morphs into the full banner.
    Slide,       // full GD banner springs down from the top edge with velocity flex.
    Bounce,      // gravity drop with impact squash-and-stretch and damped rebound.
    Elastic,     // 2D harmonic spring pop-in with width and scale oscillation.
    Fade,        // quiet quintic drift and dissolve.
    None,        // no motion; appears and disappears instantly.
    Count,
};

char const* toastAnimationId(ToastAnimation animation);
char const* toastAnimationLabel(ToastAnimation animation);
ToastAnimation toastAnimationFromId(std::string const& id);
std::vector<std::string> toastAnimationLabels();

struct NowPlayingToastContent {
    std::string prefix;     // small gold label above the title; empty hides it
    std::string title;
    std::string subtitle;   // optional line below the title
    std::string coverPath;  // absolute path to the cover; empty shows the GD music icon
};

class NowPlayingToast : public cocos2d::CCNode {
public:
    struct Options {
        ToastAnimation style = ToastAnimation::Expand;
        float speed = 1.f;
        float intensity = 0.5f;
        float holdSeconds = 2.f;
    };

    using cocos2d::CCNode::init;

    // menu music track toast. Respects the Now Playing toggle.
    static void showForCurrent(cocos2d::CCNode* parent);
    // shows the content with the configured animation. The caller checks the toggle.
    static void showContent(cocos2d::CCNode* parent, NowPlayingToastContent const& content);
    // settings preview: the current track, or sample content when nothing plays.
    static void showPreview(cocos2d::CCNode* parent);

    void onExit() override;

private:
    enum class Phase : std::uint8_t { Wait, In, Hold, Out, Done };
    struct Pose;
    struct TextRow {
        cocos2d::CCLabelBMFont* label = nullptr;
        cocos2d::CCPoint basePos = {0.f, 0.f};
        float revealStart = 0.f;
        float revealEnd = 1.f;
        float slideX = 0.f;
        float slideY = 0.f;
        bool hasPrefixIcon = false;
    };

    static void present(
        cocos2d::CCNode* parent,
        NowPlayingToastContent const& content,
        int zOrder,
        float waitSeconds
    );
    static Options readOptions();

    bool init(NowPlayingToastContent const& content, Options const& options, float waitSeconds);
    void buildPanel();
    void buildText(NowPlayingToastContent const& content);
    void buildProgressBar();
    void buildEqualizer();
    void buildArtwork();
    void buildSparkles();
    void enter(Phase phase, float duration);
    void onTick(float dt);
    void finish();

    Pose poseAt(Phase phase, float t) const;
    void applyPose(Pose const& pose, float dt, float progress);
    void updateEqualizer(float dt, float eqReveal);
    void updateClipStencil(float width);
    void loadCover(std::string const& path);
    void applyCover(cocos2d::CCTexture2D* texture);

    float m_cardW = 296.f;
    float m_centerX = 0.f;
    float m_showY = 0.f;
    float m_hideY = 0.f;
    float m_layoutScale = 1.f;
    ToastAnimation m_style = ToastAnimation::Expand;
    float m_intensity = 0.5f;
    float m_inDuration = 0.f;
    float m_outDuration = 0.f;
    float m_holdDuration = 2.f;

    Phase m_phase = Phase::Wait;
    float m_phaseTime = 0.f;
    float m_phaseDuration = 0.f;
    float m_clock = 0.f;
    float m_holdClock = 0.f;

    float m_lastClipW = -1.f;
    float m_coverBlend = 0.f;
    bool m_hasCover = false;
    std::uint64_t m_coverGeneration = 0;

    std::array<float, 5> m_eqLevels = {0.22f, 0.35f, 0.45f, 0.32f, 0.24f};
    float m_audioEnergy = 0.f;

    // 100% native GD UI assets; m_contentClip and m_coverClip are siblings (stencil depth 1).
    geode::NineSlice* m_shadow = nullptr;
    geode::NineSlice* m_panel = nullptr;
    geode::NineSlice* m_innerWell = nullptr;

    PaimonDrawNode* m_clipStencil = nullptr;
    cocos2d::CCClippingNode* m_contentClip = nullptr;
    cocos2d::CCNode* m_textLayer = nullptr;
    cocos2d::CCSprite* m_prefixIcon = nullptr;
    cocos2d::CCPoint m_prefixIconBase = {0.f, 0.f};

    cocos2d::extension::CCScale9Sprite* m_progressGroove = nullptr;
    cocos2d::extension::CCScale9Sprite* m_progressFill = nullptr;
    float m_progressArtH = 20.f;
    float m_progressScale = 0.36f;

    std::array<geode::NineSlice*, 5> m_eqTracks = {};
    std::array<geode::NineSlice*, 5> m_eqBars = {};

    cocos2d::CCNode* m_artwork = nullptr;
    geode::NineSlice* m_artworkFrame = nullptr;
    cocos2d::CCSprite* m_fallbackIcon = nullptr;
    float m_fallbackBaseScale = 1.f;
    cocos2d::CCClippingNode* m_coverClip = nullptr;
    cocos2d::CCSprite* m_coverSprite = nullptr;

    std::array<cocos2d::CCSprite*, 2> m_sparkles = {};

    std::vector<TextRow> m_rows;
};

} // namespace paimon::menumusic
