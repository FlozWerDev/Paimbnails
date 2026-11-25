#pragma once

#include "../engine/LiveTones.hpp"
#include "../persist/TextureProject.hpp"

#include <atomic>
#include <array>
#include <filesystem>
#include <limits>
#include <memory>
#include <unordered_map>

namespace paimon::texture_studio {

// paints GD's menu UI live from a palette: a shader swaps in during
// cCSprite/CCSpriteBatchNode draws, reading a per-texture role mask that a
// background worker builds once (and caches on disk). Scene backgrounds and
// brown list/cell fills are painted without analysis.
class LiveSlotRuntime final {
public:
    static LiveSlotRuntime& get();
    static void normalize(TextureProject& project);
    static LiveTonePalette tonesFor(TextureProject const& project);
    void restoreSaved();
    void start() { if (!m_restored) restoreSaved(); }
    void showOriginal() { m_enabled = false; }
    geode::Result<> preview(TextureProject project);
    // drops cached masks (memory and disk) and analyzes again.
    geode::Result<> reanalyze(TextureProject project);
    geode::Result<> activate(TextureProject const& project);
    geode::Result<> disable();
    void onTextureLoaded(char const* path, cocos2d::CCTexture2D* texture, bool skipSuffix);
    void refreshTextures();
    cocos2d::CCGLProgram* prepareDraw(cocos2d::CCTexture2D* texture,
                                     cocos2d::CCGLProgram* original);
    // repaints brown CCLayerColor fills (list backgrounds, cells) in place.
    bool remapLayerColors(cocos2d::ccColor4F* colors, int count);
    void onGLContextReload();
    std::string status() const;
    // 0..1 while textures are being analyzed, negative when idle.
    float progress() const;
    bool enabled() const { return m_enabled; }
    std::uintmax_t diskCacheBytes() const;

    struct Progress {
        std::atomic<int> done{0};
        std::atomic<int> total{0};
    };

private:
    enum class Uniform : std::size_t {
        Texture, RoleMask,
        TonePrimary, ToneSecondary, ToneBlue, TonePink, ToneRed, ToneSurface, ToneNeutral, ToneBackground,
        DetailColor, GlowColor, Brightness, Saturation, Contrast, DarkThreshold, GlowReplace,
        ApplyDetail, Strength, GlowStrength, Premultiplied, MaskScaleX, MaskScaleY, FullPaint, Count
    };
    enum class SourceKind : std::uint8_t {
        Masked,          // analyzed role mask
        Background,      // scene gradient: whole texture takes the background tone
        MenuBackground,  // menu-only background art shared with levels
    };
    struct Source {
        std::string png, plist, base;
        SourceKind kind = SourceKind::Masked;
        // live-only standalone chrome the shared catalog keeps vanilla for packs.
        bool forceTint = false;
        std::vector<geode::Ref<cocos2d::CCTexture2D>> textures;
        geode::Ref<cocos2d::CCTexture2D> mask;
        int width = 0, height = 0;
        std::size_t maskBytes = 0;
        unsigned int lastUsedFrame = 0;
        bool requested = false;
        bool analyzed = false;
    };
    void collectSources();
    void checkResourceSources();
    void releaseSources();
    void prepareMasks();
    void requestMaskBuild();
    void startNextMask();
    void requestTextureRefresh();
    void trimUnusedMasks(unsigned int frame, std::size_t incomingBytes);
    void requestMaskTrim(unsigned int frame);
    void cacheShaderState();
    void unbindMasks();
    void beginFrame(unsigned int frame);
    void updateUniforms(cocos2d::CCTexture2D* texture, Source const& source, bool fullPaint);

    geode::Ref<cocos2d::CCGLProgram> m_shader;
    std::array<geode::Ref<cocos2d::CCGLProgram>, 3> m_spriteShaders;
    std::array<GLint, static_cast<std::size_t>(Uniform::Count)> m_uniformLocations{};
    bool m_uniformsDirty = true;
    bool m_premultiplied = false;
    bool m_fullPaint = false;
    float m_maskScaleX = 1.f, m_maskScaleY = 1.f;
    TextureProject m_project;
    LiveTonePalette m_tones{};
    std::vector<Source> m_sources;
    std::unordered_map<std::string, std::size_t> m_sourcesByName;
    std::unordered_map<std::string, std::size_t> m_sourcesByPath;
    std::size_t m_sourceTexturePackCount = 0;
    float m_sourceContentScale = 0.f;
    std::vector<std::string> m_sourceSearchPaths;
    std::unordered_map<cocos2d::CCTexture2D*, std::size_t> m_textures;
    unsigned int m_cachedTextureCount = 0;
    unsigned int m_lastCacheCheckFrame = std::numeric_limits<unsigned int>::max();
    unsigned int m_lastMaskTrimFrame = 0;
    bool m_inGameplay = false;
    bool m_textureRefreshQueued = false;
    bool m_maskBuildQueued = false;
    bool m_workerRunning = false;
    std::size_t m_maskBytes = 0;
    int m_maxTextureDimension = 4096;
    std::filesystem::path m_cacheDir;
    std::shared_ptr<Progress> m_progress = std::make_shared<Progress>();
    std::string m_analyzing;
    std::shared_ptr<std::atomic<unsigned>> m_generation =
        std::make_shared<std::atomic<unsigned>>(0);
    bool m_enabled = false;
    bool m_loggedTintApplied = false;
    bool m_restored = false;
    bool m_pending = false;
    std::size_t m_failures = 0;
    std::size_t m_cacheHits = 0;
    std::string m_error;
};

}
