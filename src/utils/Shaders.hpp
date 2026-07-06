#pragma once
#include <Geode/cocos/platform/CCGL.h>
#include <Geode/cocos/shaders/CCGLProgram.h>
#include <Geode/cocos/shaders/CCShaderCache.h>
#include <Geode/utils/cocos.hpp>
#include <functional>
#include <vector>

// Don't use 'using namespace' in headers — it pollutes every TU. Functions and
// classes in the Shaders namespace use explicit cocos2d:: types, or live in the .cpp.

// Common shaders used throughout; kept together to avoid copy/paste.

namespace Shaders {

    cocos2d::CCGLProgram* getOrCreateShader(char const* key, char const* vertexSrc, char const* fragmentSrc);


// All inline shaders moved to resources/shaders/*.glsl; see GLSLLoader.hpp for the API.

    void applyBlurPass(cocos2d::CCSprite* input, cocos2d::CCRenderTexture* output, cocos2d::CCGLProgram* program, cocos2d::CCSize const& size, float radius);

    float intensityToBlurRadius(float intensity);

    cocos2d::CCSprite* createBlurredSprite(cocos2d::CCTexture2D* texture, cocos2d::CCSize const& targetSize, float intensity, bool useDirectRadius = false);

    /// Tuned version for fullscreen overlays / popup backgrounds.
    cocos2d::CCSprite* createPopupBlurredSprite(cocos2d::CCTexture2D* texture, cocos2d::CCSize const& targetSize, float intensity);

    /// Premium popup blur: fixed Dual Kawase with mips 1/2, 1/4, 1/8.
    cocos2d::CCSprite* createPopupPaimonBlurredSprite(cocos2d::CCTexture2D* texture, cocos2d::CCSize const& targetSize, float intensity);

    cocos2d::CCGLProgram* getBlurCellShader();

    cocos2d::CCGLProgram* getBlurSinglePassShader();

//  PAIMON BLUR — Dual Kawase multi-pass (downsample + upsample)

// The Dual Kawase down/up and realtime single-pass shaders for GIFs live in
// resources/shaders/kawase_{down,up,realtime}.glsl, loaded via
// paimon::shaders::getKawase*Shader. Helpers cache through CCShaderCache, so
// only the first use pays the compile.

    /// Multi-pass Dual Kawase blur for static thumbnails
    cocos2d::CCSprite* createPaimonBlurSprite(cocos2d::CCTexture2D* texture, cocos2d::CCSize const& targetSize, float intensity);

    /// Real-time PaimonBlur shader for GIFs
    cocos2d::CCGLProgram* getPaimonBlurShader();

//  UNIQUE ANIMATED EFFECTS

// Rain — raindrops (optimized: unrolled loop, 1 texture read)
constexpr auto fragmentShaderRain = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

float rHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float rainLayer(vec2 uv, float speed, float density, float layer) {
    vec2 r = uv * vec2(density, 1.0);
    r.y += u_time * speed + rHash(vec2(floor(r.x), layer)) * 100.0;
    float drop = smoothstep(0.0, 0.02, fract(r.y * 0.1) - 0.97);
    float mask = smoothstep(0.45, 0.5, abs(fract(r.x) - 0.5));
    return drop * (1.0 - mask);
}

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float str = u_intensity * 0.15;
    // 3 layers unrolled
    float rain = rainLayer(v_texCoord, 4.0, 80.0, 0.0)
               + rainLayer(v_texCoord, 6.0, 120.0, 1.0) * 0.75
               + rainLayer(v_texCoord, 8.0, 160.0, 2.0) * 0.5;
    color.rgb += vec3(0.7, 0.8, 1.0) * rain * str * str;
    color.rgb *= 1.0 - str * 0.2;
    gl_FragColor = color * v_fragmentColor;
})";

// Matrix — digital rain (optimized: 1 texture read, 2 hash calls)
constexpr auto fragmentShaderMatrix = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

float mHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float str = u_intensity * 0.2;
    float cols = 30.0 + u_intensity * 10.0;
    vec2 cell = floor(v_texCoord * vec2(cols, cols * 2.0));
    
    float spd = 2.0 + mHash(vec2(cell.x, 0.0)) * 4.0;
    float fall = fract(cell.y / (cols * 2.0) - u_time * spd * 0.1);
    float trail = smoothstep(0.0, 0.4, fall) * smoothstep(1.0, 0.5, fall);
    float head = smoothstep(0.38, 0.42, fall);
    float flick = step(0.3, mHash(cell + floor(u_time * 8.0)));
    
    float glow = (trail * 0.6 + head) * flick;
    color.rgb = mix(color.rgb, color.rgb * 0.7 + vec3(0.1, 1.0, 0.3) * glow * str, str);
    gl_FragColor = color * v_fragmentColor;
})";

// Neon Pulse — pulsing neon glow (optimized: 4 texture reads, precomputed lum)
constexpr auto fragmentShaderNeonPulse = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float str = u_intensity * 0.15;
    
    // edge detection: 4 neighbors, skip center lum (not needed for gradient)
    const vec3 lw = vec3(0.299, 0.587, 0.114);
    float px = 1.0 / 512.0;
    float lL = dot(texture2D(u_texture, v_texCoord + vec2(-px, 0.0)).rgb, lw);
    float lR = dot(texture2D(u_texture, v_texCoord + vec2( px, 0.0)).rgb, lw);
    float lU = dot(texture2D(u_texture, v_texCoord + vec2(0.0,  px)).rgb, lw);
    float lD = dot(texture2D(u_texture, v_texCoord + vec2(0.0, -px)).rgb, lw);
    float edge = smoothstep(0.02, 0.15, abs(lL - lR) + abs(lU - lD));
    
    // hue cycle + pulse in one pass
    float h = (u_time * 0.5 + v_texCoord.x * 0.3 + v_texCoord.y * 0.2) * 6.2832;
    vec3 neon = 0.5 + 0.5 * sin(vec3(h, h + 2.094, h + 4.189));
    float pulse = 0.7 + 0.3 * sin(u_time * 3.0);
    
    color.rgb += neon * edge * pulse * str * 3.0;
    gl_FragColor = color * v_fragmentColor;
})";

// Wave Distortion — underwater wavy effect
constexpr auto fragmentShaderWaveDistortion = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

void main() {
    float strength = u_intensity * 0.02;
    vec2 uv = v_texCoord;
    
    // multiple sine waves for organic distortion
    uv.x += sin(uv.y * 15.0 + u_time * 2.0) * strength;
    uv.y += cos(uv.x * 12.0 + u_time * 1.7) * strength * 0.8;
    uv.x += sin(uv.y * 25.0 + u_time * 3.3) * strength * 0.4;
    uv.y += cos(uv.x * 20.0 + u_time * 2.5) * strength * 0.3;
    
    // slight color shift for underwater feel
    vec4 color = texture2D(u_texture, uv);
    float caustic = sin(uv.x * 30.0 + u_time * 4.0) * sin(uv.y * 30.0 + u_time * 3.0);
    caustic = smoothstep(0.3, 1.0, caustic) * strength * 2.0;
    color.rgb += vec3(0.1, 0.3, 0.5) * caustic;
    
    // subtle blue tint
    color.rgb = mix(color.rgb, color.rgb * vec3(0.9, 0.95, 1.1), u_intensity * 0.05);
    
    gl_FragColor = color * v_fragmentColor;
})";

// CRT — old TV effect (optimized: 3 texture reads total)
constexpr auto fragmentShaderCRT = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

void main() {
    float s = u_intensity * 0.1;
    
    // barrel distortion
    vec2 uv = v_texCoord * 2.0 - 1.0;
    uv *= 1.0 + dot(uv, uv) * s * 0.5;
    uv = uv * 0.5 + 0.5;
    
    // vignette + clamp
    float vig = smoothstep(0.0, 0.02, uv.x) * smoothstep(1.0, 0.98, uv.x)
              * smoothstep(0.0, 0.02, uv.y) * smoothstep(1.0, 0.98, uv.y);
    uv = clamp(uv, 0.0, 1.0);
    
    // 3 reads: center (g+a), left (r), right (b)
    float subpx = s * 0.003;
    vec4 center = texture2D(u_texture, uv);
    float r = texture2D(u_texture, uv + vec2(subpx, 0.0)).r;
    float b = texture2D(u_texture, uv - vec2(subpx, 0.0)).b;
    
    vec3 col = vec3(r, center.g, b);
    
    // scanlines + flicker + grain combined
    col *= mix(1.0, sin(uv.y * 800.0) * 0.5 + 0.5, s * 0.3);
    col *= 1.0 - 0.03 * s * sin(u_time * 8.0 + sin(u_time * 13.0) * 2.0);
    col += fract(sin(dot(v_texCoord * 500.0 + u_time, vec2(127.1, 311.7))) * 43758.5453) * s * 0.15;
    col *= vig;
    
    gl_FragColor = vec4(col, center.a) * v_fragmentColor;
})";

// ShaderBgSprite — a CCSprite that re-applies its uniforms every frame. Without
// it, custom uniforms are lost when the active shader changes.

class ShaderBgSprite : public cocos2d::CCSprite {
public:
    float m_shaderIntensity = 0.5f;
    float m_screenW = 0.f;
    float m_screenH = 0.f;
    float m_shaderTime = 0.f;
    float m_cursorX = 0.5f;
    float m_cursorY = 0.5f;
    float m_clickState = 0.f;

    // Audio reactivity multipliers (set per-instance from BeatShaderManager).
    // When the beat-shaders feature is off these stay at zero so audio
    // uniforms are pushed as 0.0 and shaders that read them behave like
    // their non-reactive counterparts.
    float m_audioReactive = 0.f;   // master 0..1 — gate for audio uniforms
    float m_bassMult      = 1.f;
    float m_midMult       = 1.f;
    float m_trebleMult    = 1.f;
    float m_beatMult      = 1.f;
    float m_energyMult    = 1.f;

    static ShaderBgSprite* createWithTexture(cocos2d::CCTexture2D* tex) {
        auto ret = new ShaderBgSprite();
        if (ret && ret->initWithTexture(tex)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void draw() override;
    void updateShaderTime(float dt);
};

cocos2d::CCGLProgram* getBgShaderProgram(std::string const& shaderName);

// Distinct procedural background shaders generated fully on GPU.
cocos2d::CCGLProgram* getProceduralBgShaderProgram(std::string const& shaderName);

// Beat-reactive shaders authored for the BeatShaders feature. These read
// u_bass / u_mid / u_treble / u_beat / u_energy uniforms and are intended to
// be applied as an overlay on top of any existing background. Returns nullptr
// when the name is unknown.
cocos2d::CCGLProgram* getBeatShaderProgram(std::string const& shaderName);

// Global gate for audio reactivity. When enabled, every ShaderBgSprite in
// the scene pushes audio uniforms (bass / mid / treble / beat / energy) to
// its shader and ticks PaimonAudio once per frame. Disabled by default.
namespace ShaderBgSpriteAudioGate {
    void setEnabled(bool e);
    bool isEnabled();
}

/// Pre-compile all LevelInfoLayer shaders during menu idle to avoid stutter on
/// the first level entry. MUST run on the main (GL) thread.
void prewarmLevelInfoShaders();

/// Like prewarmLevelInfoShaders but spreads GL compiles across frames (~2 per
/// tick) so it doesn't block the main thread at once.
void prewarmLevelInfoShadersStaggered();

/// Pre-compile ONLY the background shaders the user has configured on some layer
/// (menu, levelinfo, profile, etc.), avoiding the stutter of compiling a dynamic
/// shader (rain, matrix, crt, ...) on first use, without wasting time on the 30+
/// unused shaders. MUST run on the main thread.
void prewarmConfiguredBackgroundShaders();

/// Staggered variant (1 shader per frame) of prewarmConfiguredBackgroundShaders.
void prewarmConfiguredBackgroundShadersStaggered();

/// Progressive blur engine that spreads FBO passes across frames to avoid
/// freezes. Inherits CCObject for Ref<> lifecycle.
class ProgressiveBlurJob : public cocos2d::CCObject {
public:
    using CompletionCallback = std::function<void(cocos2d::CCSprite* result)>;

    /// Create a progressive Gaussian blur (2-4 passes spread across frames)
    static ProgressiveBlurJob* createGaussian(
        cocos2d::CCTexture2D* texture,
        cocos2d::CCSize const& targetSize,
        float intensity,
        CompletionCallback onComplete);

    /// Create a progressive Dual Kawase blur (2-16 passes spread across frames)
    static ProgressiveBlurJob* createPaimonBlur(
        cocos2d::CCTexture2D* texture,
        cocos2d::CCSize const& targetSize,
        float intensity,
        CompletionCallback onComplete);

    void start();    ///< register tick with CCScheduler
    void cancel();   ///< stop processing and free resources
    bool isDone() const { return m_done; }
    bool isCancelled() const { return m_cancelled; }

    /// Fast mode: first tick is synchronous and chains phases in the same frame.
    /// Use only for single-shot jobs where latency matters (fullscreen overlays).
    /// Keep it OFF for N parallel jobs (list cells), or each pushes 20+ FBO passes
    /// in one frame.
    void setFastMode(bool enabled) { m_fastMode = enabled; }

    ~ProgressiveBlurJob() override;

private:
    enum class Phase : uint8_t {
        Setup,
        Downsample,
        Upsample,
        GaussianH1,
        GaussianV1,
        GaussianH2,
        GaussianV2,
        Complete
    };

    enum class BlurType : uint8_t { Gaussian, PaimonBlur };

    struct MipLevel {
        geode::Ref<cocos2d::CCRenderTexture> rt;
        cocos2d::CCSize size;
    };

    bool initGaussian(cocos2d::CCTexture2D* texture, cocos2d::CCSize const& targetSize,
                      float intensity, CompletionCallback onComplete);
    bool initPaimonBlur(cocos2d::CCTexture2D* texture, cocos2d::CCSize const& targetSize,
                        float intensity, CompletionCallback onComplete);

    void tick(float dt);
    void tickPaimonBlur();
    void tickGaussian();
    void finish(cocos2d::CCSprite* result);
    cocos2d::CCSprite* capSourceTexture(cocos2d::CCTexture2D* texture,
                                        cocos2d::CCSize const& targetSize);

    BlurType m_blurType = BlurType::Gaussian;
    Phase m_phase = Phase::Setup;
    bool m_done = false;
    bool m_cancelled = false;
    bool m_started = false;
    bool m_fastMode = false;

    CompletionCallback m_onComplete;
    cocos2d::CCSize m_targetSize;
    float m_intensity = 5.0f;

    // Gaussian state
    geode::Ref<cocos2d::CCRenderTexture> m_rtA;
    geode::Ref<cocos2d::CCRenderTexture> m_rtB;
    cocos2d::CCGLProgram* m_blurH = nullptr;
    cocos2d::CCGLProgram* m_blurV = nullptr;
    float m_radius = 0.1f;

    // PaimonBlur state
    cocos2d::CCGLProgram* m_blurDown = nullptr;
    cocos2d::CCGLProgram* m_blurUp = nullptr;
    int m_totalPasses = 4;
    int m_currentPass = 0;
    std::vector<MipLevel> m_mips;
    geode::Ref<cocos2d::CCSprite> m_currentSprite;
    cocos2d::CCSize m_currentSize;

    // retained source texture
    geode::Ref<cocos2d::CCTexture2D> m_sourceTexture;
};

}
