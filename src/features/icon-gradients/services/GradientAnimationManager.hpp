#pragma once

#include <Geode/Geode.hpp>

#include <unordered_map>
#include <vector>

namespace paimon::icon_gradients {

constexpr char const* kAnimationModuleId = "paimbnails.gradientanimation.global";

// 1..6 map one-to-one to shader branches in animateGradient. 7+ have no shader
// branch, so apply() realizes them through the custom-layer engine (branch 6).
// never renumber the first six: saved data stores the raw int.
enum class GradientAnimationType {
    Flow = 1,
    Pulse,
    Spin,
    Orbit,
    Swing,
    Custom,
    Wave,
    Breathe,
    Rainbow,
    Shimmer,
    Bounce,
    Zigzag,
    Heartbeat,
    Strobe,
    PingPong,
    Spiral,
    Twinkle,
    GlitchFx,
    Ripple,
};

constexpr int kFirstBuiltRecipeType = static_cast<int>(GradientAnimationType::Wave);
constexpr int kLastAnimationType = static_cast<int>(GradientAnimationType::Ripple);

// a custom layer is one movement applied to the gradient. these values go
// straight into the shader (animmotion), so don't renumber them.
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

// how the layer's value travels over time (animwave in the shader). same rule:
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

// matches the uniform array size in the gradient shaders.
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

// a ready-made stack the user can load into the editor and then tweak.
struct GradientAnimationPreset {
    char const* name;
    char const* description;
    std::vector<GradientAnimationLayer> layers;
};

// shapes the wave used by recipe presets (types 7+). the first six types are
// shader-native and ignore this.
enum class GradientEasing {
    Smooth = 0,
    Linear,
    Snap,
    Soft,
    Sharp,
};

constexpr int kGradientEasingCount = 5;

struct GradientAnimationConfig {
    GradientAnimationType type = GradientAnimationType::Flow;
    float speed = 1.f;
    float intensity = 0.6f;
    bool reverse = false;
    float phaseOffset = 0.f; // 0..1 extra start offset, applied to recipe presets
    bool pingPong = false;   // recipe presets swing back instead of looping
    GradientEasing easing = GradientEasing::Smooth;
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
    void setPhaseOffset(float phase);
    void setPingPong(bool pingPong);
    void setEasing(GradientEasing easing);
    void randomize();
    void reset();

    // custom stack. every mutation persists and pushes the new uniforms, so the
    // preview in the editor reacts on the same frame.
    std::vector<GradientAnimationLayer> const& customLayers() const;
    bool addCustomLayer();
    bool duplicateCustomLayer(size_t index);
    void updateCustomLayer(size_t index, GradientAnimationLayer const& layer);
    void removeCustomLayer(size_t index);
    // returns where the layer ended up (unchanged when it can't move).
    size_t moveCustomLayer(size_t index, int delta);
    void setCustomLayers(std::vector<GradientAnimationLayer> layers);
    void clearCustomLayers();

    void track(cocos2d::CCGLProgram* program);
    void refreshPrograms();
    void shutdown();

    static char const* nameFor(GradientAnimationType type);
    static char const* descriptionFor(GradientAnimationType type);
    static char const* nameFor(GradientMotion motion);
    static char const* descriptionFor(GradientMotion motion);
    static char const* nameFor(GradientWave wave);
    static char const* descriptionFor(GradientWave wave);
    static char const* nameFor(GradientEasing easing);

    // every built-in type exposed to the picker, in display order.
    static std::vector<GradientAnimationType> const& builtInTypes();
    // the layer recipe a recipe-type (7+) expands into. empty for shader-native
    // types. used by apply() and by the preview to show the real movement.
    static std::vector<GradientAnimationLayer> recipeFor(GradientAnimationType type);

    static std::vector<GradientAnimationPreset> const& customPresets();

private:
    GradientAnimationManager();

    void load();
    void save();
    void saveCustom();
    void apply(cocos2d::CCGLProgram* program) const;

    GradientAnimationConfig m_config;
    std::unordered_map<cocos2d::CCGLProgram*, geode::WeakRef<cocos2d::CCGLProgram>> m_programs;
    bool m_stopped = false;
};

} // namespace paimon::icon_gradients
