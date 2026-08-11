#pragma once

#include <Geode/Geode.hpp>

#include <unordered_set>
#include <vector>

namespace paimon::icon_gradients {

constexpr char const* kAnimationModuleId = "paimbnails.gradientanimation.global";

enum class GradientAnimationType {
    Flow = 1,
    Pulse,
    Spin,
    Orbit,
    Swing,
    Custom,
};

// A custom layer is one movement applied to the gradient. These values go
// straight into the shader (animMotion), so don't renumber them.
enum class GradientMotion {
    SlideX = 0,
    SlideY,
    Zoom,
    Rotate,
    Orbit,
    RippleX,
    RippleY,
    Twist,
};

// How the layer's value travels over time (animWave in the shader). Same rule:
// the value is the shader's branch index.
enum class GradientWave {
    Smooth = 0,
    Even,
    Loop,
    Snap,
    Bounce,
    Random,
};

constexpr int kGradientMotionCount = 8;
constexpr int kGradientWaveCount = 6;

// Matches the uniform array size in the gradient shaders.
constexpr size_t kMaxCustomLayers = 4;

constexpr float kLayerSpeedMin = 0.05f;
constexpr float kLayerSpeedMax = 4.f;

struct GradientAnimationLayer {
    GradientMotion motion = GradientMotion::SlideX;
    GradientWave wave = GradientWave::Smooth;
    float amount = 0.5f; // 0..1, multiplied by the master intensity
    float speed = 1.f;   // multiplies the master speed
    float phase = 0.f;   // 0..1 offset inside the layer's own cycle
};

// A ready-made stack the user can load into the editor and then tweak.
struct GradientAnimationPreset {
    char const* name;
    char const* description;
    std::vector<GradientAnimationLayer> layers;
};

struct GradientAnimationConfig {
    GradientAnimationType type = GradientAnimationType::Flow;
    float speed = 1.f;
    float intensity = 0.6f;
    bool reverse = false;
    std::vector<GradientAnimationLayer> custom;
};

class GradientAnimationManager {
public:
    static GradientAnimationManager& get();

    GradientAnimationConfig const& config() const;

    bool isEnabled() const;

    void setEnabled(bool enabled);
    void setType(GradientAnimationType type);
    void setSpeed(float speed);
    void setIntensity(float intensity);
    void setReverse(bool reverse);
    void reset();

    // Custom stack. Every mutation persists and pushes the new uniforms, so the
    // preview in the editor reacts on the same frame.
    std::vector<GradientAnimationLayer> const& customLayers() const;
    bool addCustomLayer();
    bool duplicateCustomLayer(size_t index);
    void updateCustomLayer(size_t index, GradientAnimationLayer const& layer);
    void removeCustomLayer(size_t index);
    // Returns where the layer ended up (unchanged when it can't move).
    size_t moveCustomLayer(size_t index, int delta);
    void setCustomLayers(std::vector<GradientAnimationLayer> layers);
    void clearCustomLayers();

    void track(cocos2d::CCGLProgram* program);
    void refreshPrograms();

    static char const* nameFor(GradientAnimationType type);
    static char const* descriptionFor(GradientAnimationType type);
    static char const* nameFor(GradientMotion motion);
    static char const* descriptionFor(GradientMotion motion);
    static char const* nameFor(GradientWave wave);
    static char const* descriptionFor(GradientWave wave);

    static std::vector<GradientAnimationPreset> const& customPresets();

private:
    GradientAnimationManager();

    void load();
    void save();
    void saveCustom();
    void apply(cocos2d::CCGLProgram* program) const;

    GradientAnimationConfig m_config;
    std::unordered_set<cocos2d::CCGLProgram*> m_programs;
};

} // namespace paimon::icon_gradients
