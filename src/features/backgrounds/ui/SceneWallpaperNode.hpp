#pragma once

#include <Geode/Geode.hpp>
#include "../services/SceneData.hpp"
#include "../services/SceneTexture.hpp"
#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct LayerBgConfig;

namespace paimon::video { class VideoPlayer; }

namespace paimon::wallpaper {

class SceneParticleNode;

// native 2D renderer for Wallpaper Engine scenes: image, solid, text (with
// live clocks and dates), video and particle layers, parent hierarchies with
// rotation/scale, additive/screen/multiply composition, opacity masks,
// animated TEXS sprite sheets and camera parallax.
//
// loading is asynchronous: scene.json parsing and texture decoding (only the
// mip level the screen needs; DXT kept compressed for the gpu) run on worker
// threads, uploads are spread over frames, and fully static scenes collapse
// into one cached texture.
class SceneWallpaperNode : public cocos2d::CCNode {
public:
    static SceneWallpaperNode* create(std::filesystem::path const& project,
        cocos2d::CCSize viewport, LayerBgConfig const& config);
    ~SceneWallpaperNode() override;
    void onEnter() override;
    void onExit() override;
    void update(float dt) override;
    // drop gl-owned textures and caches while the old context is still alive.
    static void onGLContextReload();

private:
    struct AnimationFrame {
        cocos2d::CCRect rect;
        float duration = 0.033f;
    };
    struct Drawable {
        cocos2d::CCNode* node = nullptr;
        cocos2d::CCSprite* sprite = nullptr;
        cocos2d::CCLabelBMFont* label = nullptr;
        // world transform of the drawable (scene units) before parallax.
        SceneAffine world;
        float sizeX = 0.f, sizeY = 0.f;
        float fitX = 1.f, fitY = 1.f;
        // current parallax displacement (scene units).
        float offsetX = 0.f, offsetY = 0.f;
        int root = -1;
        std::vector<AnimationFrame> animation;
        size_t frame = 0;
        float elapsed = 0.f;
        size_t clock = static_cast<size_t>(-1);
        // video layers: the extracted MP4 and its player once it opened.
        std::filesystem::path videoFile;
        std::shared_ptr<paimon::video::VideoPlayer> video;
        bool videoStarted = false;
        bool videoPaused = false;
        bool videoShown = false;
    };
    struct ClockText {
        size_t drawable = 0;
        SceneTextClock kind = SceneTextClock::Time;
        bool use24 = true;
        bool seconds = false;
        std::string delimiter = ":";
        std::array<uint8_t, 3> dateOrder{0, 1, 2};
        bool dateYear = true;
        std::string current;
        float boxW = 0.f, boxH = 0.f;
    };
    struct RootDepth {
        float x = 0.f, y = 0.f;
    };

    bool initScene(std::filesystem::path const& project, cocos2d::CCSize viewport,
        LayerBgConfig const& config);
    void startAsyncLoad(std::shared_ptr<SceneData const> descriptors);
    void queueUploads(std::shared_ptr<SceneData const> data,
        std::vector<std::pair<std::string, SceneTexture>>&& decoded, bool extracted);
    void uploadStep(float);
    void buildScene(std::shared_ptr<SceneData const> data);
    void applyTransform(Drawable& drawable);
    void updateClocks(bool force);
    void tickClocks(float);
    void startVideos();
    void attachVideo(size_t index, std::shared_ptr<paimon::video::VideoPlayer> player);
    void pumpVideos(float dt);
    void pauseVideos();
    bool bakeStaticScene();
    bool showBaked();
    void showStatus(std::string const& message, bool error);
    void clearLayers();
    void invalidateTextures(cocos2d::CCTexture2D* dummy);
    void restartAfterContextLoss();
    bool visibleInHierarchy() const;
    void requestPoster();
    void showPoster(geode::Ref<cocos2d::CCTexture2D> texture);
    void queuePosterSave(geode::Ref<cocos2d::CCRenderTexture> surface);
    geode::Ref<cocos2d::CCRenderTexture> renderSurface(bool opaque);
    float pixelsPerPoint() const;

    std::filesystem::path m_root;
    std::string m_rootKey;
    std::string m_posterKey;
    std::string m_fitMode = "cover";
    bool m_parallax = false;
    float m_strength = 0.f;
    // authored camera parallax: displacement scale and smoothing rate.
    float m_parallaxScale = 1.f;
    float m_parallaxRate = 8.f;
    float m_currentParallaxX = 0.f;
    float m_currentParallaxY = 0.f;
    cocos2d::CCPoint m_lastPointer{0.f, 0.f};
    bool m_hasPointer = false;
    bool m_parallaxSettled = false;
    bool m_rebuildPending = false;
    bool m_posterShown = false;
    bool m_posterSaved = false;
    bool m_uploadScheduled = false;
    bool m_clockScheduled = false;
    // one reload when the texture cache lost entries between decode and build.
    bool m_retriedMissing = false;
    uint64_t m_loadGeneration = 0;
    uint64_t m_buildGeneration = 0;
    std::shared_ptr<std::atomic<bool>> m_cancelFlag;

    cocos2d::CCLayerColor* m_background = nullptr;
    cocos2d::CCSprite* m_poster = nullptr;
    cocos2d::CCSprite* m_baked = nullptr;
    cocos2d::CCNode* m_world = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;

    std::vector<Drawable> m_drawables;
    std::vector<RootDepth> m_roots;
    std::vector<size_t> m_animated;
    std::vector<size_t> m_videos;
    std::vector<SceneParticleNode*> m_particles;
    std::vector<ClockText> m_clocks;
    std::vector<geode::Ref<cocos2d::CCTexture2D>> m_textureRefs;

    // staged uploads, spread over several frames.
    std::shared_ptr<SceneData const> m_pendingData;
    std::vector<std::pair<std::string, SceneTexture>> m_pendingUploads;
    size_t m_uploadIndex = 0;
};

} // namespace paimon::wallpaper
