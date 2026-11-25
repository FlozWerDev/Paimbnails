#pragma once

#include "GlassGeometry.hpp"
#include "GlassQuality.hpp"

#include <Geode/Geode.hpp>
#include <array>
#include <chrono>
#include <optional>
#include <utility>
#include <vector>

namespace paimon::glass {

struct GlassTarget {
    GLuint tex = 0;
    GLuint fbo = 0; // 0 for the shared copy texture
    int width = 0;  // allocated texels
    int height = 0;
};

// everything the plate shader needs to sample one capture.
struct CaptureView {
    GLuint sharpTex = 0;
    GLuint blurTex = 0;
    bool sharpValid = false;
    float sharpMap[4] = {};   // xy: framebuffer px of texel 0, zw: 1 / texture size
    float sharpClamp[4] = {}; // uv min (xy) and max (zw) of this capture
    float blurMap[4] = {};    // xy: region origin (fb px), zw: fb px -> blur uv
    float blurClamp[4] = {};  // uv min (xy) and max (zw) of the blurred content
};

// copies the framebuffer behind glass plates while the scene draws and
// blurs it with a short dual-kawase chain. The panels of one popup share a
// copy, as do the buttons of one menu, and a copy only covers their
// pixels, so the cost follows the amount of glass on screen instead of
// the window size. Main thread only.
class GlassCapture {
public:
    static GlassCapture& get();

    void setProfile(GlassProfile const& profile, float blurSpread);

    // which copy a plate of `root` uses this frame. Layered profiles give
    // a plate drawn overlapping another glass panel of the same root a
    // newer copy that includes it, while non-overlapping sibling panels
    // share ordinal 0 (one copy).
    int ordinalFor(void const* root, GlassRect const& plate);
    void notePanelDrawn(void const* root, GlassRect const& plate);

    // called from a plate's draw() with the bound framebuffer and viewport.
    std::optional<CaptureView> acquire(void const* root, int ordinal, GlassRect const& plate,
                                       GLint framebuffer, GlassRect const& viewport);

    // forget every GL handle. Delete them only while their context is current.
    void releaseGL(bool deleteHandles);
    // slow timer: frees copies that no plate used for a while.
    void trimIdle();

private:
    static constexpr int kMaxLevels = 5;
    // each button menu of a list row keeps its own copy (see GlassNode::resolveRoot).
    static constexpr std::size_t kMaxSlots = 16;

    struct Slot {
        void const* root = nullptr;
        int ordinal = 0;
        unsigned lastUsedFrame = 0;
        unsigned capturedFrame = 0;
        double capturedAt = -1.0;
        bool valid = false;
        bool sharpValid = false;
        GLint framebuffer = -1;
        GlassRect viewport;
        GlassRect region;
        GlassRect unionThis;
        GlassRect unionPrev;
        unsigned unionFrame = 0;
        int levelCount = 0;
        std::array<GlassTarget, kMaxLevels> levels{};
        std::array<int, kMaxLevels> contentW{};
        std::array<int, kMaxLevels> contentH{};
    };

    GlassCapture() = default;
    void beginFrame();
    Slot* findSlot(void const* root, int ordinal);
    Slot* allocateSlot(void const* root, int ordinal);
    bool capture(Slot& slot, GlassRect const& region, GLint framebuffer, GlassRect const& viewport);
    bool ensureTarget(GlassTarget& target, int width, int height, bool renderable, int step);
    bool ensurePrograms();
    void blurPass(GLuint source, float const uv[4], float texelW, float texelH,
                  GlassTarget const& dest, int destW, int destH, bool up);
    CaptureView viewFor(Slot const& slot) const;
    void releaseTarget(GlassTarget& target, bool deleteHandles);
    double now() const;

    struct RootPanels {
        void const* root = nullptr;
        std::vector<GlassRect> rects;
    };

    GlassProfile m_profile;
    float m_spread = 1.9f;
    std::vector<Slot> m_slots;
    GlassTarget m_sharp;
    std::vector<RootPanels> m_roots; // panel rects drawn per root this frame
    unsigned m_frame = 0;
    bool m_frameStarted = false;
    int m_capturesThisFrame = 0;
    // a plate wanted a copy but the frame budget was spent.
    bool m_starvedThisFrame = false;
    bool m_starvedLastFrame = false;
    unsigned m_lastActiveFrame = 0;
    void const* m_contextToken = nullptr;
    bool m_validated = false;
    bool m_broken = false;
    geode::Ref<cocos2d::CCGLProgram> m_blurProgram;
    GLint m_locStep = -1;
    GLint m_locClamp = -1;
    GLint m_locUp = -1;
};

} // namespace paimon::glass
