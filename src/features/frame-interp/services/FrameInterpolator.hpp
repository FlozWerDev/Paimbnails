#pragma once

// Interpolates between steps to remove micro-stutter. Restores all on draw.

#include "FrameInterpConfig.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

class GJBaseGameLayer;

namespace paimon::frameinterp {

struct Transform {
    cocos2d::CCPoint pos{0.f, 0.f};
    float rotX   = 0.f;
    float rotY   = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
};

class FrameInterpolator {
public:
    static FrameInterpolator& get();

    FrameInterpConfig& config() { return m_config; }
    FrameInterpConfig const& config() const { return m_config; }

    void init();
    void loadConfig();
    void saveConfig();
    void resetToDefaults();

    bool isEnabled() const;
    void setEnabled(bool enabled);

    // Stores the leftover without simulating; nothing arrives while paused.
    void onStepped(double stepped, double leftover);

    void beginVisit(GJBaseGameLayer* layer);
    void endVisit();

    void reset();

    bool isActive() const { return m_active; }
    float lastAlpha() const { return m_alpha; }
    float stepsPerFrame() const { return m_stepsPerFrame; }
    int trackedCount() const { return m_trackedCount; }

private:
    FrameInterpolator() = default;
    FrameInterpolator(FrameInterpolator const&) = delete;
    FrameInterpolator& operator=(FrameInterpolator const&) = delete;

    struct Slot {
        cocos2d::CCNode* node = nullptr;
        Transform prev;
        Transform cur;
        bool hasPrev = false;
        bool hasCur = false;
    };

    struct ObjectSlot {
        Transform prev;
        Transform cur;
        bool hasPrev = false;
        bool hasCur = false;
        uint32_t stamp = 0;
    };

    struct Pending {
        cocos2d::CCNode* node;
        Transform state;
        uint8_t mask;
    };

    std::filesystem::path configPath() const;
    void sanitize();

    bool shouldRun(GJBaseGameLayer* layer) const;
    void syncTracks(GJBaseGameLayer* layer);
    void syncObjects(GJBaseGameLayer* layer, bool advanced);
    void applyNode(cocos2d::CCNode* node, Transform const& authoritative,
                   Transform const& prev);

    FrameInterpConfig m_config;
    bool m_loaded = false;

    // Identity check for the current level; never dereferenced.
    GJBaseGameLayer* m_layer = nullptr;
    std::vector<Slot> m_tracked;
    std::vector<cocos2d::CCNode*> m_wanted;
    std::unordered_map<cocos2d::CCNode*, ObjectSlot> m_objects;
    std::vector<Pending> m_pending;

    bool m_stepPending = false;
    double m_stepped = 0.0;
    double m_leftover = 0.0;
    double m_span = 0.0;
    uint32_t m_frame = 0;

    bool m_active = false;
    float m_alpha = 1.f;
    float m_stepsPerFrame = 0.f;
    int m_trackedCount = 0;
};

} // namespace paimon::frameinterp
