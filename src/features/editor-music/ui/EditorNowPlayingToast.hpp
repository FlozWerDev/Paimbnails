#pragma once

// Ultra-compact, non-intrusive Now Playing pill exclusively for the level editor.
// Pops in as a mini spinning disc above the bottom toolbar, unfolds horizontally
// into a slim capsule with live micro-EQ and a draining hairline bar, then folds back.

#include <Geode/Geode.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <array>
#include <cstdint>
#include <string>

class PaimonDrawNode;

namespace paimon::editormusic {

class EditorNowPlayingToast : public cocos2d::CCNode {
public:
    using cocos2d::CCNode::init;

    static void show(
        cocos2d::CCNode* parent,
        std::string const& title,
        std::string const& artist,
        std::string const& coverPath,
        int songId = 0
    );

    void onExit() override;

private:
    enum class Phase : std::uint8_t { Pop, Unfold, Hold, Fold, Vanish, Done };

    bool init(std::string const& title, std::string const& artist, std::string const& coverPath, int songId);
    void enter(Phase phase, float duration);
    void tick(float dt);
    void applyLayout(float curW, float masterAlpha, float contentAlpha, float holdProgress);
    void updateClipStencil(float clipW);
    void loadCover(std::string const& path, int songId);
    void applyCover(cocos2d::CCTexture2D* tex);
    void finish();

    Phase m_phase = Phase::Pop;
    float m_phaseTime = 0.f;
    float m_phaseDuration = 0.2f;
    float m_clock = 0.f;
    float m_spinAngle = 0.f;
    float m_targetW = 160.f;
    float m_lastClipW = -1.f;
    bool m_animated = true;
    cocos2d::ccColor3B m_accent{115, 225, 255};

    geode::NineSlice* m_bg = nullptr;
    geode::NineSlice* m_border = nullptr;
    geode::NineSlice* m_progressBar = nullptr;

    cocos2d::CCNode* m_discRoot = nullptr;
    cocos2d::CCNode* m_discRotor = nullptr;
    cocos2d::CCClippingNode* m_coverClip = nullptr;
    cocos2d::CCSprite* m_coverSprite = nullptr;
    cocos2d::CCSprite* m_fallbackIcon = nullptr;
    std::uint64_t m_coverGen = 0;

    PaimonDrawNode* m_textStencil = nullptr;
    cocos2d::CCClippingNode* m_textClip = nullptr;
    cocos2d::CCLabelBMFont* m_titleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_artistLabel = nullptr;

    std::array<geode::NineSlice*, 3> m_eqBars = {};
    std::array<float, 3> m_eqLevels = {0.25f, 0.45f, 0.30f};
};

} // namespace paimon::editormusic
