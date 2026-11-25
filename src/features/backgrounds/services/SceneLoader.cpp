#include "SceneLoader.hpp"
#include "SceneJson.hpp"
#include "WallpaperLayout.hpp"
#include "../../../utils/stb_image.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <unordered_set>

namespace paimon::wallpaper {
namespace {

using namespace scenejson;

constexpr size_t kMaxObjects = 1500;
// ---------------------------------------------------------------------------
// package helpers (worker thread)

std::optional<matjson::Value> jsonFrom(ScenePackage const& package, std::string const& name) {
    auto bytes = package.read(name, 32 * 1024 * 1024);
    if (bytes.empty()) return std::nullopt;
    size_t offset = 0;
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) offset = 3;
    std::string text(reinterpret_cast<char const*>(bytes.data() + offset), bytes.size() - offset);
    auto parsed = matjson::parse(text);
    if (!parsed || !parsed.unwrap().isObject()) return std::nullopt;
    return std::move(parsed).unwrap();
}

struct ResolvedAsset {
    enum class Kind { Image, Solid, Skip } kind = Kind::Skip;
    std::string texture;
    bool additive = false;
};

// what a material pass tells the 2D renderer.
struct ResolvedMaterial {
    std::string texture;
    bool additive = false;
    // REFRACT combo: the colour is multiplied by the (distorted) background,
    // so a white albedo mostly tints what is behind it.
    bool refract = false;
};

// resolves model -> material -> texture chains once per asset; scenes reuse
// the same handful of models across many objects.
class AssetResolver {
public:
    explicit AssetResolver(ScenePackage const& package) : m_package(package) {}

    std::string findTexture(std::string const& name) const {
        if (name.empty() || name.starts_with("_rt_")) return {};
        std::string const prefixed = name.starts_with("materials/") ? name : "materials/" + name;
        for (auto const& candidate : {name, prefixed, name + ".tex", prefixed + ".tex",
                 name + ".png", prefixed + ".png", name + ".jpg", prefixed + ".jpg"}) {
            if (ScenePackage::safeName(candidate) && m_package.contains(candidate)) return candidate;
        }
        return {};
    }

    ResolvedMaterial material(std::string const& rawPath) {
        if (auto it = m_materials.find(rawPath); it != m_materials.end()) return it->second;
        ResolvedMaterial result;
        std::string path = rawPath;
        if (!m_package.contains(path)) {
            if (!path.starts_with("materials/")) path = "materials/" + path;
            if (!path.ends_with(".json")) path += ".json";
        }
        if (ScenePackage::safeName(path)) {
            if (auto json = jsonFrom(m_package, path)) {
                auto const& passes = (*json)["passes"];
                if (passes.isArray()) {
                    for (auto const& pass : passes.asArray().unwrap()) {
                        if (lowerOf(stringOf(pass["blending"])) == "additive") result.additive = true;
                        auto const& combos = pass["combos"];
                        if (combos.isObject() && combos.contains("REFRACT") && numberOf(combos["REFRACT"], 0.0) != 0.0) {
                            result.refract = true;
                        }
                        auto const& textures = pass["textures"];
                        if (textures.isArray()) {
                            for (auto const& entry : textures.asArray().unwrap()) {
                                std::string name = entry.isString() ? entry.asString().unwrapOr("")
                                    : entry.isObject() ? stringOf(entry["file"], stringOf(entry["name"])) : "";
                                auto found = findTexture(name);
                                if (!found.empty()) { result.texture = std::move(found); break; }
                            }
                        }
                        if (!result.texture.empty()) break;
                    }
                }
            }
        }
        m_materials.emplace(rawPath, result);
        return result;
    }

    ResolvedAsset const& resolve(std::string const& ref) {
        if (auto it = m_assets.find(ref); it != m_assets.end()) return it->second;
        ResolvedAsset asset;
        auto const lower = lowerOf(ref);
        if (lower.find("util/composelayer") != std::string::npos ||
            lower.find("util/fullscreenlayer") != std::string::npos ||
            lower.find("util/projectlayer") != std::string::npos) {
            asset.kind = ResolvedAsset::Kind::Skip;
        } else if (lower.find("util/solidlayer") != std::string::npos) {
            asset.kind = ResolvedAsset::Kind::Solid;
        } else if (auto direct = findTexture(ref); !direct.empty() && !lower.ends_with(".json")) {
            asset.kind = ResolvedAsset::Kind::Image;
            asset.texture = std::move(direct);
        } else {
            std::string model = ref;
            if (!model.ends_with(".json") && model.find('.') == std::string::npos) model += ".json";
            if (!m_package.contains(model) && m_package.contains("models/" + model)) model = "models/" + model;
            std::optional<matjson::Value> json = m_package.contains(model) ? jsonFrom(m_package, model) : std::nullopt;
            if (json && (*json).contains("passes")) {
                // the reference is a material itself
                auto resolved = material(model);
                asset.texture = std::move(resolved.texture);
                asset.additive = resolved.additive;
                asset.kind = asset.texture.empty() ? ResolvedAsset::Kind::Skip : ResolvedAsset::Kind::Image;
            } else if (json) {
                if (boolOf((*json)["solidlayer"], false)) {
                    asset.kind = ResolvedAsset::Kind::Solid;
                } else if (boolOf((*json)["passthrough"], false) || boolOf((*json)["fullscreen"], false)) {
                    asset.kind = ResolvedAsset::Kind::Skip;
                } else {
                    auto const matPath = stringOf((*json)["material"]);
                    if (!matPath.empty()) {
                        auto resolved = material(matPath);
                        asset.texture = std::move(resolved.texture);
                        asset.additive = resolved.additive;
                    }
                    asset.kind = asset.texture.empty() ? ResolvedAsset::Kind::Skip : ResolvedAsset::Kind::Image;
                }
            }
        }
        return m_assets.emplace(ref, std::move(asset)).first->second;
    }

private:
    ScenePackage const& m_package;
    std::unordered_map<std::string, ResolvedAsset> m_assets;
    std::unordered_map<std::string, ResolvedMaterial> m_materials;
};

void collectObjects(matjson::Value const& val, std::vector<matjson::Value const*>& out) {
    if (val.isArray()) {
        for (auto const& elem : val.asArray().unwrap()) collectObjects(elem, out);
    } else if (val.isObject()) {
        out.push_back(&val);
        if (val.contains("objects")) collectObjects(val["objects"], out);
    }
}

int64_t idOf(matjson::Value const& value) {
    auto const& v = valueOf(value);
    if (v.isNumber()) return static_cast<int64_t>(v.asDouble().unwrapOr(-1.0));
    if (v.isString()) {
        auto const text = v.asString().unwrapOr("");
        int64_t parsed = -1;
        auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (ec == std::errc() && ptr != text.data()) return parsed;
    }
    return -1;
}

bool looksLikeTime(std::string const& text) {
    size_t digits = 0;
    bool separator = false;
    for (char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c))) ++digits;
        else if (c == ':' || c == '.') separator = true;
    }
    return separator && digits >= 3 && digits <= 6;
}

// colorBlendMode follows the BLENDMODE numbering of common_blending.h.
SceneBlend blendOfMode(int mode) {
    switch (mode) {
        case 9:  // add
        case 31: // A + B * opacity
            return SceneBlend::Additive;
        case 6:  // lighten
        case 7:  // screen
        case 8:  // color dodge
        case 10: // max
        case 22: // glow
            return SceneBlend::Screen;
        case 1: // darken
        case 2: // multiply
        case 3: // color burn
        case 4: // linear burn
        case 5: // min
            return SceneBlend::Multiply;
        default:
            return SceneBlend::Normal;
    }
}

// the opacity effect is static: a constant alpha and an optional painted
// mask (`textures[1]`), both multiplied into the layer's alpha.
struct OpacityEffects {
    float alpha = 1.f;
    std::vector<std::string> masks;
};

OpacityEffects opacityEffectsOf(matjson::Value const& object, AssetResolver const& resolver) {
    OpacityEffects out;
    auto const& effects = object["effects"];
    if (!effects.isArray()) return out;
    for (auto const& effect : effects.asArray().unwrap()) {
        if (!effect.isObject() || !boolOf(effect["visible"], true)) continue;
        auto const file = lowerOf(ScenePackage::normalizeName(stringOf(effect["file"])));
        if (file != "effects/opacity/effect.json" && !file.ends_with("/opacity/effect.json")) continue;
        auto const& passes = effect["passes"];
        if (!passes.isArray() || passes.asArray().unwrap().empty()) continue;
        auto const& pass = passes.asArray().unwrap().front();
        auto const& constants = pass["constantshadervalues"];
        if (constants.isObject() && constants.contains("alpha")) {
            out.alpha *= std::clamp(static_cast<float>(numberOf(constants["alpha"], 1.0)), 0.f, 1.f);
        }
        auto const& textures = pass["textures"];
        if (textures.isArray() && textures.asArray().unwrap().size() > 1) {
            auto found = resolver.findTexture(stringOf(textures.asArray().unwrap()[1]));
            if (!found.empty()) out.masks.push_back(std::move(found));
        }
    }
    return out;
}

// order of year/month/day in the script's `value = ...` expression.
std::array<uint8_t, 3> dateOrderOf(std::string const& script) {
    auto const lower = lowerOf(script);
    std::array<uint8_t, 3> order{0, 1, 2};
    for (size_t at = lower.find("value"); at != std::string::npos; at = lower.find("value", at + 5)) {
        size_t const assign = lower.find_first_not_of(" \t", at + 5);
        if (assign == std::string::npos || lower[assign] != '=' || lower.compare(assign, 2, "==") == 0) continue;
        size_t const end = lower.find_first_of(";\n", assign);
        auto const expression = lower.substr(assign, end == std::string::npos ? std::string::npos : end - assign);
        size_t const positions[3] = {expression.find("year"), expression.find("month"), expression.find("day")};
        if (positions[1] == std::string::npos || positions[2] == std::string::npos) continue;
        std::array<std::pair<size_t, uint8_t>, 3> ranked{{{positions[0], 0}, {positions[1], 1}, {positions[2], 2}}};
        std::sort(ranked.begin(), ranked.end());
        for (size_t i = 0; i < 3; ++i) order[i] = ranked[i].second;
        break;
    }
    return order;
}

std::string textureKeyOf(std::string const& path, std::vector<std::string> const& masks, bool premultiply) {
    std::string key = path;
    for (auto const& mask : masks) key += "|mask=" + mask;
    if (premultiply) key += "|premultiplied";
    return key;
}

} // namespace

// cocos-free scene.json extraction; safe on a worker thread.
std::shared_ptr<SceneData> extractSceneData(ScenePackage const& package) {
    auto document = jsonFrom(package, "scene.json");
    if (!document) return nullptr;
    auto const& scene = *document;
    auto const& general = scene["general"];

    float sceneW = 0.f, sceneH = 0.f;
    bool autoProjection = false;
    if (general.isObject()) {
        auto const& projection = general["orthogonalprojection"];
        if (projection.isObject()) {
            sceneW = static_cast<float>(numberOf(projection["width"]));
            sceneH = static_cast<float>(numberOf(projection["height"]));
            autoProjection = boolOf(projection["auto"], false);
        }
        if ((sceneW <= 0.f || sceneH <= 0.f) && general["viewport"].isObject()) {
            sceneW = static_cast<float>(numberOf(general["viewport"]["width"]));
            sceneH = static_cast<float>(numberOf(general["viewport"]["height"]));
        }
    }

    std::vector<matjson::Value const*> objects;
    if (scene.contains("objects")) collectObjects(scene["objects"], objects);
    if (objects.size() > kMaxObjects) objects.resize(kMaxObjects);

    if (autoProjection || sceneW <= 0.f || sceneH <= 0.f) {
        float maxX = 0.f, maxY = 0.f;
        for (auto const* obj : objects) {
            if (idOf((*obj)["parent"]) >= 0) continue;
            auto const size = vecOf((*obj)["size"]);
            auto const origin = vecOf((*obj)["origin"]);
            auto const scale = vecOf((*obj)["scale"], {1.f, 1.f, 1.f});
            maxX = std::max(maxX, origin.x + std::abs(size.x * scale.x) * 0.5f);
            maxY = std::max(maxY, origin.y + std::abs(size.y * scale.y) * 0.5f);
        }
        if (maxX > 0.f && maxY > 0.f) {
            sceneW = sceneExtentFromEdges(maxX);
            sceneH = sceneExtentFromEdges(maxY);
        }
    }
    if (sceneW <= 0.f || sceneH <= 0.f) { sceneW = 1920.f; sceneH = 1080.f; }
    if (sceneW > 40000.f || sceneH > 40000.f) return nullptr;

    auto data = std::make_shared<SceneData>();
    data->sceneW = sceneW;
    data->sceneH = sceneH;
    auto const clear = colorOf(general["clearcolor"], {0.f, 0.f, 0.f});
    data->clear[0] = clear.x; data->clear[1] = clear.y; data->clear[2] = clear.z;
    if (general.isObject()) {
        data->cameraParallax = boolOf(general["cameraparallax"], false);
        data->parallaxAmount = std::clamp(static_cast<float>(numberOf(general["cameraparallaxamount"], 0.5)), 0.f, 2.f);
        data->parallaxDelay = std::clamp(static_cast<float>(numberOf(general["cameraparallaxdelay"], 0.1)), 0.f, 2.f);
        data->perspective = !general.contains("orthogonalprojection") || general["orthogonalprojection"].isNull();
    }

    AssetResolver resolver(package);
    std::unordered_set<std::string> seenTextures;
    data->objects.reserve(objects.size());

    for (auto const* objectPtr : objects) {
        auto const& object = *objectPtr;
        SceneObject desc;
        desc.id = idOf(object["id"]);
        desc.parentId = idOf(object["parent"]);
        desc.visible = boolOf(object["visible"], true);

        auto const origin = vecOf(object["origin"]);
        auto const scale = vecOf(object["scale"], {1.f, 1.f, 1.f});
        auto const size = vecOf(object["size"]);
        auto const angles = vecOf(object["angles"]);
        desc.origin[0] = origin.x; desc.origin[1] = origin.y;
        desc.scale[0] = scale.x; desc.scale[1] = scale.y;
        desc.size[0] = size.x; desc.size[1] = size.y;
        desc.angle = angles.z;

        auto const color = colorOf(object["color"]);
        float const brightness = static_cast<float>(numberOf(object["brightness"], 1.0));
        desc.color[0] = std::clamp(color.x * brightness, 0.f, 1.f);
        desc.color[1] = std::clamp(color.y * brightness, 0.f, 1.f);
        desc.color[2] = std::clamp(color.z * brightness, 0.f, 1.f);
        desc.alpha = std::clamp(static_cast<float>(numberOf(object["alpha"], 1.0)), 0.f, 1.f);
        auto const depth = vecOf(object["parallaxDepth"], {0.f, 0.f, 0.f});
        desc.depth[0] = depth.x; desc.depth[1] = depth.y;

        std::string const horizontal = stringOf(object["horizontalalign"], stringOf(object["alignment"]));
        desc.anchor = anchorForAlignment(horizontal, stringOf(object["verticalalign"]));

        std::string const particle = stringOf(object["particle"]);
        std::string const image = stringOf(object["image"]);
        bool const hasText = object.contains("text") && !object["text"].isNull();
        SceneBlend const layerBlend = blendOfMode(static_cast<int>(numberOf(object["colorBlendMode"], 0.0)));

        if (!particle.empty()) {
            desc.kind = SceneObjectKind::Particle;
            auto system = jsonFrom(package, particle);
            if (system) {
                auto parsed = parseSceneParticles(*system, object["instanceoverride"]);
                parsed.tint = {parsed.tint.x * desc.color[0], parsed.tint.y * desc.color[1], parsed.tint.z * desc.color[2]};
                parsed.alphaScale *= desc.alpha;
                auto const matPath = stringOf((*system)["material"]);
                if (!matPath.empty()) {
                    auto resolved = resolver.material(matPath);
                    if (resolved.refract) {
                        // colour *= background: multiply (translucent) or
                        // brighten (additive), both on premultiplied texels.
                        desc.blend = resolved.additive ? SceneBlend::Brighten : SceneBlend::Multiply;
                        if (!resolved.texture.empty()) {
                            desc.texturePath = textureKeyOf(resolved.texture, {}, true);
                            data->textureSources.try_emplace(desc.texturePath,
                                SceneTextureSource{resolved.texture, {}, true});
                        }
                    } else {
                        desc.texturePath = std::move(resolved.texture);
                        desc.blend = resolved.additive ? SceneBlend::Additive : SceneBlend::Normal;
                    }
                }
                desc.particles = std::make_shared<SceneParticleDesc const>(std::move(parsed));
            } else {
                desc.kind = SceneObjectKind::Group;
            }
        } else if (hasText) {
            desc.kind = SceneObjectKind::Text;
            desc.text = stringOf(object["text"]);
            desc.pointSize = static_cast<float>(numberOf(object["pointsize"], 32.0));
            desc.padding = static_cast<float>(numberOf(object["padding"], 0.0));
            auto const& textNode = object["text"];
            if (textNode.isObject() && textNode.contains("script")) {
                auto const script = stringOf(textNode["script"]);
                auto const& props = textNode["scriptproperties"];
                bool const usesDate = script.find("Date") != std::string::npos;
                bool const timeScript = usesDate &&
                    (script.find("getHours") != std::string::npos ||
                     script.find("getMinutes") != std::string::npos ||
                     script.find("toLocaleTimeString") != std::string::npos);
                bool const dateScript = usesDate && script.find("getFullYear") != std::string::npos &&
                    script.find("getMonth") != std::string::npos && script.find("getDate") != std::string::npos;
                auto delimiterOf = [&props](char const* fallback) {
                    auto delimiter = props.isObject() ? stringOf(props["delimiter"], fallback) : std::string(fallback);
                    return delimiter.empty() || delimiter.size() > 3 ? std::string(fallback) : delimiter;
                };
                if (timeScript && looksLikeTime(desc.text)) {
                    desc.clock = SceneTextClock::Time;
                    desc.clock24 = props.isObject() && props.contains("use24hFormat")
                        ? boolOf(props["use24hFormat"], true) : true;
                    desc.clockSeconds = props.isObject() && boolOf(props["showSeconds"], false);
                    desc.clockDelimiter = delimiterOf(":");
                } else if (dateScript && !timeScript) {
                    // these scripts print '\0' (nothing) when an alternate
                    // mode is ticked.
                    bool const blanked = props.isObject() &&
                        (boolOf(props["showDate"], false) || boolOf(props["showCD"], false));
                    if (!blanked) {
                        desc.clock = SceneTextClock::Date;
                        desc.clockDelimiter = delimiterOf("/");
                        desc.dateOrder = dateOrderOf(script);
                        desc.dateYear = !props.isObject() || boolOf(props["showYear"], true);
                    } else {
                        desc.text.clear();
                    }
                }
            }
            if (desc.text.empty() && desc.clock == SceneTextClock::None) desc.kind = SceneObjectKind::Group;
        } else if (!image.empty()) {
            auto const& asset = resolver.resolve(image);
            if (asset.kind == ResolvedAsset::Kind::Solid) {
                desc.kind = SceneObjectKind::Solid;
                desc.blend = layerBlend;
                desc.alpha *= opacityEffectsOf(object, resolver).alpha;
            } else if (asset.kind == ResolvedAsset::Kind::Image) {
                desc.kind = SceneObjectKind::Sprite;
                desc.blend = asset.additive ? SceneBlend::Additive : layerBlend;
                auto const opacity = opacityEffectsOf(object, resolver);
                desc.alpha *= opacity.alpha;
                bool const premultiply = desc.blend == SceneBlend::Screen || desc.blend == SceneBlend::Multiply;
                desc.texturePath = textureKeyOf(asset.texture, opacity.masks, premultiply);
                if (desc.texturePath != asset.texture) {
                    data->textureSources.try_emplace(desc.texturePath,
                        SceneTextureSource{asset.texture, opacity.masks, premultiply});
                }
            } else {
                desc.kind = SceneObjectKind::Group;
            }
        }

        data->objects.push_back(std::move(desc));
    }

    // hierarchy: world transforms, top-level ancestors and inherited visibility.
    auto& list = data->objects;
    std::unordered_map<int64_t, size_t> byId;
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].id >= 0) byId.emplace(list[i].id, i);
    }
    std::vector<uint8_t> state(list.size(), 0);
    auto resolve = [&](auto&& self, size_t i) -> void {
        if (state[i] == 2) return;
        auto& o = list[i];
        auto const local = sceneLocalTransform(o.origin[0], o.origin[1], o.angle, o.scale[0], o.scale[1]);
        state[i] = 1;
        auto parent = o.parentId >= 0 ? byId.find(o.parentId) : byId.end();
        if (parent != byId.end() && parent->second != i && state[parent->second] != 1) {
            self(self, parent->second);
            auto const& p = list[parent->second];
            o.world = sceneMultiply(p.world, local);
            o.root = p.root;
            o.shown = o.visible && p.shown;
        } else {
            o.world = local;
            o.root = static_cast<int>(i);
            o.shown = o.visible;
        }
        state[i] = 2;
    };
    for (size_t i = 0; i < list.size(); ++i) resolve(resolve, i);

    for (auto& o : list) {
        if (!o.shown || o.texturePath.empty()) continue;
        if (o.kind != SceneObjectKind::Sprite && o.kind != SceneObjectKind::Particle) continue;
        float const axisX = std::hypot(o.world.a, o.world.b);
        float const axisY = std::hypot(o.world.c, o.world.d);
        SceneTextureNeed need;
        if (o.kind == SceneObjectKind::Particle && o.particles) {
            float const extent = o.particles->maxSize() * std::max(axisX, axisY);
            need = {extent, extent};
        } else if (o.size[0] > 0.f && o.size[1] > 0.f) {
            need = {std::abs(o.size[0]) * axisX, std::abs(o.size[1]) * axisY};
        } else {
            need = {-1.f, -1.f}; // drawn at its native size: keep level 0
        }
        auto [it, inserted] = data->textureNeeds.try_emplace(o.texturePath, need);
        if (!inserted) {
            auto& current = it->second;
            if (current.width < 0.f || need.width < 0.f) current = {-1.f, -1.f};
            else current = {std::max(current.width, need.width), std::max(current.height, need.height)};
        }
        if (seenTextures.insert(o.texturePath).second) data->textureOrder.push_back(o.texturePath);
    }
    return data;
}

// ---------------------------------------------------------------------------
// texture decode (worker threads)

SceneCoverage coverageOf(SceneTexture const& texture) {
    SceneCoverage c;
    if (texture.video) {
        c.cellW = texture.visibleWidth;
        c.cellH = texture.visibleHeight;
        c.full = true;
        return c;
    }
    if (!texture.frames.empty()) {
        auto const& f = texture.frames.front();
        c.cellW = static_cast<int>(std::abs(f.width != 0.f ? f.width : f.heightX));
        c.cellH = static_cast<int>(std::abs(f.height != 0.f ? f.height : f.widthY));
    }
    if (c.cellW <= 1 || c.cellH <= 1) {
        c.cellW = texture.visibleWidth;
        c.cellH = texture.visibleHeight;
    }
    c.full = texture.visibleWidth + 1 >= texture.sourceWidth && texture.visibleHeight + 1 >= texture.sourceHeight;
    return c;
}

float pixelsPerSceneUnit(SceneData const& data, SceneDecodeCaps const& caps) {
    auto const layout = fitWallpaper(data.sceneW, data.sceneH, caps.viewportW, caps.viewportH, caps.fitMode);
    float const pointsPerUnit = std::max(layout.width / std::max(data.sceneW, 1.f),
        layout.height / std::max(data.sceneH, 1.f));
    return pointsPerUnit * caps.pixelsPerPoint * caps.overscan;
}

std::pair<int, int> sceneTextureTarget(SceneData const& data, std::string const& path, float pixelsPerUnit) {
    auto it = data.textureNeeds.find(path);
    if (it == data.textureNeeds.end() || it->second.width <= 0.f || it->second.height <= 0.f) return {0, 0};
    return {std::max(1, static_cast<int>(std::ceil(it->second.width * pixelsPerUnit))),
        std::max(1, static_cast<int>(std::ceil(it->second.height * pixelsPerUnit)))};
}

namespace {

// 2x2 box average; drops the odd trailing row/column.
void halveRgba(std::vector<uint8_t>& pixels, int& width, int& height) {
    int const halfW = width / 2, halfH = height / 2;
    if (halfW < 1 || halfH < 1) return;
    for (int y = 0; y < halfH; ++y) {
        uint8_t const* row0 = pixels.data() + static_cast<size_t>(y * 2) * width * 4;
        uint8_t const* row1 = row0 + static_cast<size_t>(width) * 4;
        uint8_t* out = pixels.data() + static_cast<size_t>(y) * halfW * 4;
        for (int x = 0; x < halfW; ++x) {
            for (int k = 0; k < 4; ++k) {
                out[x * 4 + k] = static_cast<uint8_t>(
                    (row0[x * 8 + k] + row0[x * 8 + 4 + k] + row1[x * 8 + k] + row1[x * 8 + 4 + k] + 2) / 4);
            }
        }
    }
    width = halfW;
    height = halfH;
    pixels.resize(static_cast<size_t>(width) * height * 4);
}

bool decodeEncoded(SceneTexture& texture) {
    if (!texture.encoded) return true;
    int width = 0, height = 0, components = 0;
    auto* pixels = stbi_load_from_memory(texture.pixels.data(),
        static_cast<int>(texture.pixels.size()), &width, &height, &components, 4);
    if (!pixels) return false;
    if (width < 1 || height < 1 || width > 16384 || height > 16384 ||
        uint64_t(width) * height > 64ull * 1024 * 1024) {
        stbi_image_free(pixels);
        return false;
    }
    float const sx = texture.width > 0 ? static_cast<float>(width) / texture.width : 1.f;
    float const sy = texture.height > 0 ? static_cast<float>(height) / texture.height : 1.f;
    texture.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    texture.width = width;
    texture.height = height;
    texture.visibleWidth = std::clamp(static_cast<int>(std::lround(texture.visibleWidth * sx)), 1, width);
    texture.visibleHeight = std::clamp(static_cast<int>(std::lround(texture.visibleHeight * sy)), 1, height);
    for (auto& f : texture.frames) {
        f.x *= sx; f.width *= sx; f.heightX *= sx;
        f.y *= sy; f.height *= sy; f.widthY *= sy;
    }
    texture.encoded = false;
    texture.format = SceneTextureFormat::RGBA8;
    return true;
}

std::optional<SceneTexture> loadTexture(ScenePackage const& package, std::string const& path,
    SceneTextureRequest const& request) {
    auto bytes = package.read(path);
    if (bytes.empty()) return std::nullopt;
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "TEXV", 4) == 0) {
        auto texture = decodeSceneTexture(bytes, request);
        if (!texture) return std::nullopt;
        if (!texture->video && !decodeEncoded(*texture)) return std::nullopt;
        return texture;
    }
    int w = 0, h = 0, comp = 0;
    auto* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &comp, 4);
    if (!pixels) return std::nullopt;
    if (w < 1 || h < 1 || w > 16384 || h > 16384 || uint64_t(w) * h > 64ull * 1024 * 1024) {
        stbi_image_free(pixels);
        return std::nullopt;
    }
    SceneTexture result;
    result.width = result.visibleWidth = result.sourceWidth = w;
    result.height = result.visibleHeight = result.sourceHeight = h;
    result.pixels.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
    stbi_image_free(pixels);
    return result;
}

// RGBA levels far larger than what reaches the screen waste upload time and
// vram; halve them (and their sheet cells) while both axes stay oversized.
void shrinkToTarget(SceneTexture& texture, int targetW, int targetH, int maxDimension) {
    if (texture.video || texture.format != SceneTextureFormat::RGBA8 || texture.pixels.empty()) return;
    auto cell = coverageOf(texture);
    float factor = 1.f;
    bool const sized = targetW > 0 && targetH > 0;
    while (texture.width / 2 >= 64 && texture.height / 2 >= 64 &&
        ((sized && cell.cellW >= targetW * 2 && cell.cellH >= targetH * 2) ||
         texture.width > maxDimension || texture.height > maxDimension)) {
        halveRgba(texture.pixels, texture.width, texture.height);
        factor *= 0.5f;
        cell.cellW /= 2;
        cell.cellH /= 2;
    }
    if (factor == 1.f) return;
    texture.visibleWidth = std::max(1, static_cast<int>(texture.visibleWidth * factor));
    texture.visibleHeight = std::max(1, static_cast<int>(texture.visibleHeight * factor));
    for (auto& f : texture.frames) {
        f.x *= factor; f.width *= factor; f.heightX *= factor;
        f.y *= factor; f.height *= factor; f.widthY *= factor;
    }
}

// pixel rectangles one layer displays: every sheet cell, or the image.
std::vector<std::array<int, 4>> displayedCells(SceneTexture const& texture) {
    std::vector<std::array<int, 4>> cells;
    float const sheetW = static_cast<float>(texture.width), sheetH = static_cast<float>(texture.height);
    if (texture.frames.size() >= 2) {
        for (auto const& f : texture.frames) {
            float fw = f.width != 0.f ? f.width : f.heightX;
            float fh = f.height != 0.f ? f.height : f.widthY;
            float fx = f.x, fy = f.y;
            if (std::abs(fw) <= 1.0001f && std::abs(fh) <= 1.0001f && std::abs(fx) <= 1.0001f && std::abs(fy) <= 1.0001f) {
                fx *= sheetW; fy *= sheetH; fw *= sheetW; fh *= sheetH;
            }
            int const x0 = std::clamp(static_cast<int>(std::lround(std::min(fx, fx + fw))), 0, texture.width);
            int const y0 = std::clamp(static_cast<int>(std::lround(std::min(fy, fy + fh))), 0, texture.height);
            int const x1 = std::clamp(static_cast<int>(std::lround(std::max(fx, fx + fw))), 0, texture.width);
            int const y1 = std::clamp(static_cast<int>(std::lround(std::max(fy, fy + fh))), 0, texture.height);
            if (x1 - x0 < 1 || y1 - y0 < 1) continue;
            std::array<int, 4> const cell{x0, y0, x1 - x0, y1 - y0};
            if (std::find(cells.begin(), cells.end(), cell) == cells.end()) cells.push_back(cell);
        }
    }
    if (cells.empty()) cells.push_back({0, 0, std::min(texture.visibleWidth, texture.width), std::min(texture.visibleHeight, texture.height)});
    return cells;
}

// opacity effect: alpha *= mask.r, the mask stretched over each displayed
// cell (the effect runs on the composed layer, i.e. one sheet frame).
void applyOpacityMask(SceneTexture& texture, SceneTexture const& mask) {
    if (texture.video || mask.video || texture.format != SceneTextureFormat::RGBA8 ||
        mask.format != SceneTextureFormat::RGBA8 || texture.pixels.empty() || mask.pixels.empty()) return;
    // R8 levels decode as white + alpha; every other format keeps it in red.
    int const channel = mask.sourceFormat == 9 ? 3 : 0;
    int const maskW = std::min(mask.visibleWidth, mask.width), maskH = std::min(mask.visibleHeight, mask.height);
    if (maskW < 1 || maskH < 1) return;
    auto maskAt = [&](int x, int y) {
        return static_cast<float>(mask.pixels[(static_cast<size_t>(y) * mask.width + x) * 4 + channel]);
    };
    auto sample = [&](float u, float v) {
        float const x = std::clamp(u * maskW - 0.5f, 0.f, static_cast<float>(maskW - 1));
        float const y = std::clamp(v * maskH - 0.5f, 0.f, static_cast<float>(maskH - 1));
        int const x0 = static_cast<int>(x), y0 = static_cast<int>(y);
        int const x1 = std::min(x0 + 1, maskW - 1), y1 = std::min(y0 + 1, maskH - 1);
        float const fx = x - x0, fy = y - y0;
        float const top = maskAt(x0, y0) + (maskAt(x1, y0) - maskAt(x0, y0)) * fx;
        float const bottom = maskAt(x0, y1) + (maskAt(x1, y1) - maskAt(x0, y1)) * fx;
        return (top + (bottom - top) * fy) / 255.f;
    };
    for (auto const& [cx, cy, cw, ch] : displayedCells(texture)) {
        for (int y = cy; y < cy + ch; ++y) {
            float const v = (static_cast<float>(y - cy) + 0.5f) / static_cast<float>(ch);
            uint8_t* row = texture.pixels.data() + static_cast<size_t>(y) * texture.width * 4;
            for (int x = cx; x < cx + cw; ++x) {
                float const u = (static_cast<float>(x - cx) + 0.5f) / static_cast<float>(cw);
                uint8_t& alpha = row[x * 4 + 3];
                alpha = static_cast<uint8_t>(std::lround(alpha * sample(u, v)));
            }
        }
    }
}

void premultiplyAlpha(SceneTexture& texture) {
    if (texture.video || texture.format != SceneTextureFormat::RGBA8) return;
    for (size_t i = 0; i + 3 < texture.pixels.size(); i += 4) {
        unsigned const a = texture.pixels[i + 3];
        for (size_t k = 0; k < 3; ++k) texture.pixels[i + k] = static_cast<uint8_t>((texture.pixels[i + k] * a + 127) / 255);
    }
}

std::string hex64(uint64_t value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i, value >>= 4) out[static_cast<size_t>(i)] = digits[value & 15];
    return out;
}

uint64_t fnv1a(std::string_view text) {
    uint64_t hash = 14695981039346656037ull;
    for (char c : text) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

// keeps the newest extracted videos; files the decoder still has open fail
// to delete on Windows and simply stay.
void pruneVideoCache(std::filesystem::path const& folder, std::filesystem::path const& keep) {
    constexpr size_t kMaxFiles = 12;
    constexpr uintmax_t kMaxBytes = 1536ull * 1024 * 1024;
    std::error_code ec;
    struct File { std::filesystem::path path; std::filesystem::file_time_type time; uintmax_t size; };
    std::vector<File> files;
    uintmax_t total = 0;
    for (auto const& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        auto const& path = entry.path();
        if (path.extension() == ".tmp") {
            // a crash mid-write leaves these behind.
            if (std::filesystem::file_time_type::clock::now() - entry.last_write_time(ec) > std::chrono::hours(1)) {
                std::filesystem::remove(path, ec);
            }
            continue;
        }
        if (path.extension() != ".mp4") continue;
        auto const size = entry.file_size(ec);
        files.push_back({path, entry.last_write_time(ec), size});
        total += size;
    }
    std::sort(files.begin(), files.end(), [](File const& a, File const& b) { return a.time < b.time; });
    size_t count = files.size();
    for (auto const& file : files) {
        if (count <= kMaxFiles && total <= kMaxBytes) break;
        if (file.path == keep) continue;
        if (std::filesystem::remove(file.path, ec)) {
            --count;
            total -= std::min(total, file.size);
        }
    }
}

// the decoder needs a file: extract the MP4 once per package entry.
std::filesystem::path extractVideo(ScenePackage const& package, std::string const& path,
    std::vector<uint8_t> const& bytes, std::filesystem::path const& folder) {
    if (folder.empty() || bytes.empty()) return {};
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    auto const location = package.location().lexically_normal().u8string();
    auto const identity = std::string(location.begin(), location.end()) + "|" + path + "|" + std::to_string(bytes.size());
    auto const target = folder / (hex64(fnv1a(identity)) + ".mp4");
    if (std::filesystem::is_regular_file(target, ec) && std::filesystem::file_size(target, ec) == bytes.size()) {
        std::filesystem::last_write_time(target, std::filesystem::file_time_type::clock::now(), ec);
        return target;
    }
    auto const temporary = folder / (hex64(fnv1a(identity + "|" +
        std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())))) + ".tmp");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
            out.close();
            std::filesystem::remove(temporary, ec);
            return {};
        }
    }
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        // another loader won the race; its copy is identical.
        std::error_code exists;
        if (!(std::filesystem::is_regular_file(target, exists) && std::filesystem::file_size(target, exists) == bytes.size())) return {};
    }
    pruneVideoCache(folder, target);
    return target;
}

} // namespace

bool decodeSceneTextures(ScenePackage const& package, SceneData const& data,
    std::unordered_map<std::string, SceneCoverage> const& cached, SceneDecodeCaps const& caps,
    std::function<bool()> const& cancelled, std::vector<std::pair<std::string, SceneTexture>>& out) {
    float const ppu = pixelsPerSceneUnit(data, caps);
    struct Job { std::string key; SceneTextureSource source; int targetW; int targetH; };
    std::vector<Job> jobs;
    for (auto const& key : data.textureOrder) {
        auto const [targetW, targetH] = sceneTextureTarget(data, key, ppu);
        if (auto it = cached.find(key); it != cached.end() && it->second.satisfies(targetW, targetH)) continue;
        auto source = data.textureSources.find(key);
        jobs.push_back({key, source != data.textureSources.end() ? source->second : SceneTextureSource{key, {}, false},
            targetW, targetH});
    }
    if (jobs.empty()) return true;

    unsigned const hardware = std::max(1u, std::thread::hardware_concurrency());
    unsigned const workers = std::min<unsigned>({4u, std::max(1u, hardware - 1), static_cast<unsigned>(jobs.size())});
    std::atomic<size_t> next{0};
    std::atomic<bool> stop{false};
    std::mutex outMutex;

    auto work = [&] {
        for (;;) {
            size_t const index = next.fetch_add(1, std::memory_order_relaxed);
            if (index >= jobs.size() || stop.load(std::memory_order_relaxed)) return;
            if (cancelled && cancelled()) {
                stop.store(true);
                return;
            }
            auto const& job = jobs[index];
            bool const editsPixels = !job.source.masks.empty() || job.source.premultiply;
            SceneTextureRequest request;
            request.targetWidth = job.targetW;
            request.targetHeight = job.targetH;
            // masks and premultiplication rewrite texels: keep those uncompressed.
            request.keepCompressed = caps.keepCompressed && !editsPixels;
            request.maxDimension = caps.maxDimension;
            auto texture = loadTexture(package, job.source.path, request);
            if (!texture) continue;
            if (texture->video) {
                texture->videoFile = extractVideo(package, job.source.path, texture->pixels, caps.videoCacheDir);
                texture->pixels = {};
                if (texture->videoFile.empty()) continue;
            } else {
                shrinkToTarget(*texture, job.targetW, job.targetH, caps.maxDimension);
                for (auto const& maskPath : job.source.masks) {
                    SceneTextureRequest maskRequest;
                    maskRequest.targetWidth = texture->visibleWidth;
                    maskRequest.targetHeight = texture->visibleHeight;
                    maskRequest.maxDimension = caps.maxDimension;
                    if (auto mask = loadTexture(package, maskPath, maskRequest)) applyOpacityMask(*texture, *mask);
                }
                if (job.source.premultiply) premultiplyAlpha(*texture);
            }
            std::lock_guard lock(outMutex);
            out.emplace_back(job.key, std::move(*texture));
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(workers > 0 ? workers - 1 : 0);
    for (unsigned i = 1; i < workers; ++i) {
        threads.emplace_back(work);
    }
    work();
    for (auto& thread : threads) thread.join();
    return !stop.load(std::memory_order_relaxed);
}

} // namespace paimon::wallpaper
