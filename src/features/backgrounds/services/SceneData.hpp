#pragma once

#include "SceneLayout.hpp"
#include "SceneParticles.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::wallpaper {

// cocos-free scene description extracted from scene.json on a worker thread,
// cached per project root and consumed on the main thread.
enum class SceneObjectKind { Group, Sprite, Solid, Text, Particle };

// how a layer composes over what is below it. Wallpaper Engine has ~30 modes;
// these are the ones fixed-function blending can reproduce (or approximate).
// screen, Multiply and Brighten use premultiplied texels.
enum class SceneBlend : uint8_t { Normal, Additive, Screen, Multiply, Brighten };

// live text driven by a Wallpaper Engine text script.
enum class SceneTextClock : uint8_t { None, Time, Date };

struct SceneObject {
    SceneObjectKind kind = SceneObjectKind::Group;
    int64_t id = -1;
    int64_t parentId = -1;
    // own flag; the effective state also needs every ancestor visible.
    bool visible = true;
    float origin[2] = {0.f, 0.f};
    float scale[2] = {1.f, 1.f};
    // counter-clockwise radians, like the authored `angles.z`.
    float angle = 0.f;
    float size[2] = {0.f, 0.f};
    SceneAnchor anchor;
    float color[3] = {1.f, 1.f, 1.f};
    float alpha = 1.f;
    float depth[2] = {0.f, 0.f};
    SceneBlend blend = SceneBlend::Normal;
    // texture cache key of sprites and particles: the package path, plus the
    // opacity masks baked into it (see SceneData::textureSources).
    std::string texturePath;
    // text layers
    std::string text;
    float pointSize = 32.f;
    float padding = 0.f;
    SceneTextClock clock = SceneTextClock::None;
    bool clock24 = true;
    bool clockSeconds = false;
    std::string clockDelimiter = ":";
    // date order as indices into {year, month, day}.
    std::array<uint8_t, 3> dateOrder{0, 1, 2};
    bool dateYear = true;
    // particle layers
    std::shared_ptr<SceneParticleDesc const> particles;
    // resolved at extraction: world transform (scene units), index of the
    // top-level ancestor (parallax) and visibility including ancestors.
    SceneAffine world;
    int root = -1;
    bool shown = true;
};

// largest on-screen extent a texture is drawn at, in scene units. Drives the
// mip level picked at decode time; negative means "keep full resolution".
struct SceneTextureNeed {
    float width = 0.f;
    float height = 0.f;
};

// what one texture cache key is built from.
struct SceneTextureSource {
    std::string path;
    // opacity-effect masks multiplied into the alpha channel.
    std::vector<std::string> masks;
    // layer composed with screen/multiply: rgb premultiplied by alpha.
    bool premultiply = false;
};

struct SceneData {
    float sceneW = 0.f;
    float sceneH = 0.f;
    float clear[3] = {0.f, 0.f, 0.f};
    // general.cameraparallax: Wallpaper Engine only moves layers when the
    // author enabled it.
    bool cameraParallax = false;
    float parallaxAmount = 0.5f;
    float parallaxDelay = 0.1f;
    // no orthogonal projection: a 3D scene (models seen through a camera).
    bool perspective = false;
    std::vector<SceneObject> objects;
    // unique texture keys in first-use order.
    std::vector<std::string> textureOrder;
    std::unordered_map<std::string, SceneTextureNeed> textureNeeds;
    // keys that are not a plain package path.
    std::unordered_map<std::string, SceneTextureSource> textureSources;
};

} // namespace paimon::wallpaper
