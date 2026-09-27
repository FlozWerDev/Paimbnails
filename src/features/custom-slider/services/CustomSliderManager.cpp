#include "CustomSliderManager.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/utils/file.hpp>
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../../../utils/ShapeStencil.hpp"
#include "../../../utils/EditorContext.hpp"
#include "../../icon-gradients/GradientUtils.hpp"

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::slider;

namespace {
constexpr int kMaxThumbTextureSize = 256;

struct IconMapEntry {
    SliderIconType slider;
    IconType gd;
    int (GameManager::*playerGetter)();
};

IconMapEntry const kIconMap[] = {
    {SliderIconType::Cube,   IconType::Cube,   &GameManager::getPlayerFrame},
    {SliderIconType::Ship,   IconType::Ship,   &GameManager::getPlayerShip},
    {SliderIconType::Ball,   IconType::Ball,   &GameManager::getPlayerBall},
    {SliderIconType::Ufo,    IconType::Ufo,    &GameManager::getPlayerBird},
    {SliderIconType::Wave,   IconType::Wave,   &GameManager::getPlayerDart},
    {SliderIconType::Robot,  IconType::Robot,  &GameManager::getPlayerRobot},
    {SliderIconType::Spider, IconType::Spider, &GameManager::getPlayerSpider},
    {SliderIconType::Swing,  IconType::Swing,  &GameManager::getPlayerSwing},
};
// Slider-thumb shader namespace; never share uniforms with garage/icons.
constexpr int kGradientExtra = 909;

ImageLoadHelper::LoadedImage loadThumbTexture(std::filesystem::path const& path) {
    auto bytes = ImageLoadHelper::readBinaryFile(path, 10);
    if (bytes.empty()) return {};

    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()),
            &width, &height, &channels) ||
        width <= 0 || height <= 0 ||
        width > ImageLoadHelper::kMaxImageDim || height > ImageLoadHelper::kMaxImageDim) return {};

    auto* pixels = stbi_load_from_memory(
        bytes.data(), static_cast<int>(bytes.size()), &width, &height, nullptr, 4);
    if (!pixels) return {};

    ImageLoadHelper::LoadedImage result;
    if (width > kMaxThumbTextureSize || height > kMaxThumbTextureSize) {
        auto resized = ImageLoadHelper::downsampleForCache(
            pixels, width, height, kMaxThumbTextureSize);
        if (!resized.pixels.empty()) {
            result = ImageLoadHelper::createFromRGBA(
                resized.pixels.data(), resized.width, resized.height, false);
        }
    } else {
        result = ImageLoadHelper::createFromRGBA(pixels, width, height, false);
    }

    stbi_image_free(pixels);
    return result;
}
}

CustomSliderManager& CustomSliderManager::get() {
    static CustomSliderManager instance;
    return instance;
}

std::filesystem::path CustomSliderManager::configPath() const {
    return Mod::get()->getSaveDir() / "custom-slider.json";
}

std::filesystem::path CustomSliderManager::imagesDir() const {
    auto dir = Mod::get()->getSaveDir() / "slider-images";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void CustomSliderManager::loadConfig() {
    invalidateImageCache();

    auto path = configPath();
    std::error_code ec;
    auto const size = std::filesystem::file_size(path, ec);
    if (ec || size > 2ull * 1024 * 1024) return;
    auto res = file::readFromJson<matjson::Value>(path);
    if (!res) return;

    auto json = res.unwrap();

    m_config.enabled         = json["enabled"].asBool().unwrapOr(false);
    m_config.thumbMode       = static_cast<SliderThumbMode>(
        std::clamp<int64_t>(json["thumbMode"].asInt().unwrapOr(0), 0,
            static_cast<int64_t>(SliderThumbMode::Gif)));
    m_config.iconType        = static_cast<SliderIconType>(
        std::clamp<int64_t>(json["iconType"].asInt().unwrapOr(0), 0,
            static_cast<int64_t>(SliderIconType::Swing)));
    m_config.usePlayerIcon   = json["usePlayerIcon"].asBool().unwrapOr(true);
    m_config.customIconId    = json["customIconId"].asInt().unwrapOr(1);
    m_config.iconScale       = static_cast<float>(json["iconScale"].asDouble().unwrapOr(0.55));
    m_config.iconRotation    = static_cast<float>(json["iconRotation"].asDouble().unwrapOr(0.0));
    m_config.iconOpacity     = json["iconOpacity"].asInt().unwrapOr(255);
    m_config.usePlayerColors = json["usePlayerColors"].asBool().unwrapOr(true);
    m_config.enableGlow      = json["enableGlow"].asBool().unwrapOr(false);
    m_config.useGradients    = json["useGradients"].asBool().unwrapOr(false);
    m_config.customImagePath = json["customImagePath"].asString().unwrapOr("");
    m_config.containerEnabled = json["containerEnabled"].asBool().unwrapOr(true);
    m_config.containerShape = json["containerShape"].asString().unwrapOr("circle");
    m_config.containerBorderEnabled = json["containerBorderEnabled"].asBool().unwrapOr(false);
    m_config.containerBorderThickness = static_cast<float>(json["containerBorderThickness"].asDouble().unwrapOr(2.0));
    m_config.animateOnDrag   = json["animateOnDrag"].asBool().unwrapOr(true);
    m_config.animType        = static_cast<SliderAnimType>(
        std::clamp<int64_t>(json["animType"].asInt().unwrapOr(3), 0,
            static_cast<int64_t>(SliderAnimType::BounceRotate)));
    m_config.animBounceScale = static_cast<float>(json["animBounceScale"].asDouble().unwrapOr(1.25));
    m_config.animRotateDeg   = static_cast<float>(json["animRotateDeg"].asDouble().unwrapOr(22.0));
    m_config.animDuration    = static_cast<float>(json["animDuration"].asDouble().unwrapOr(0.15));

    if (json.contains("color1") && json["color1"].isArray()) {
        auto arrRes = json["color1"].asArray();
        if (arrRes.isOk()) {
            auto arr = arrRes.unwrap();
            if (arr.size() >= 3) {
                m_config.color1 = {
                    static_cast<GLubyte>(arr[0].asInt().unwrapOr(0)),
                    static_cast<GLubyte>(arr[1].asInt().unwrapOr(255)),
                    static_cast<GLubyte>(arr[2].asInt().unwrapOr(100))
                };
            }
        }
    }
    if (json.contains("color2") && json["color2"].isArray()) {
        auto arrRes = json["color2"].asArray();
        if (arrRes.isOk()) {
            auto arr = arrRes.unwrap();
            if (arr.size() >= 3) {
                m_config.color2 = {
                    static_cast<GLubyte>(arr[0].asInt().unwrapOr(255)),
                    static_cast<GLubyte>(arr[1].asInt().unwrapOr(255)),
                    static_cast<GLubyte>(arr[2].asInt().unwrapOr(255))
                };
            }
        }
    }

    if (json.contains("containerBorderColor") && json["containerBorderColor"].isArray()) {
        auto arrRes = json["containerBorderColor"].asArray();
        if (arrRes.isOk()) {
            auto arr = arrRes.unwrap();
            if (arr.size() >= 3) {
                m_config.containerBorderColor = {
                    static_cast<GLubyte>(arr[0].asInt().unwrapOr(255)),
                    static_cast<GLubyte>(arr[1].asInt().unwrapOr(255)),
                    static_cast<GLubyte>(arr[2].asInt().unwrapOr(255))
                };
            }
        }
    }

    if (json.contains("targets") && json["targets"].isObject()) {
        auto t = json["targets"];
        m_config.targets.optionsSliders = t["optionsSliders"].asBool().unwrapOr(true);
        m_config.targets.editorSliders  = t["editorSliders"].asBool().unwrapOr(true);
        m_config.targets.garageSliders  = t["garageSliders"].asBool().unwrapOr(false);
    }
}

void CustomSliderManager::saveConfig() {
    auto json = matjson::Value::object();

    json["enabled"]         = m_config.enabled;
    json["thumbMode"]       = static_cast<int>(m_config.thumbMode);
    json["iconType"]        = static_cast<int>(m_config.iconType);
    json["usePlayerIcon"]   = m_config.usePlayerIcon;
    json["customIconId"]    = m_config.customIconId;
    json["iconScale"]       = static_cast<double>(m_config.iconScale);
    json["iconRotation"]    = static_cast<double>(m_config.iconRotation);
    json["iconOpacity"]     = m_config.iconOpacity;
    json["usePlayerColors"] = m_config.usePlayerColors;
    json["enableGlow"]      = m_config.enableGlow;
    json["useGradients"]    = m_config.useGradients;
    json["customImagePath"] = m_config.customImagePath;
    json["containerEnabled"] = m_config.containerEnabled;
    json["containerShape"] = m_config.containerShape;
    json["containerBorderEnabled"] = m_config.containerBorderEnabled;
    json["containerBorderThickness"] = static_cast<double>(m_config.containerBorderThickness);
    json["animateOnDrag"]   = m_config.animateOnDrag;
    json["animType"]        = static_cast<int>(m_config.animType);
    json["animBounceScale"] = static_cast<double>(m_config.animBounceScale);
    json["animRotateDeg"]   = static_cast<double>(m_config.animRotateDeg);
    json["animDuration"]    = static_cast<double>(m_config.animDuration);

    auto color1Arr = matjson::Value::array();
    color1Arr.push(static_cast<int>(m_config.color1.r));
    color1Arr.push(static_cast<int>(m_config.color1.g));
    color1Arr.push(static_cast<int>(m_config.color1.b));
    json["color1"] = color1Arr;

    auto color2Arr = matjson::Value::array();
    color2Arr.push(static_cast<int>(m_config.color2.r));
    color2Arr.push(static_cast<int>(m_config.color2.g));
    color2Arr.push(static_cast<int>(m_config.color2.b));
    json["color2"] = color2Arr;

    auto cBorderArr = matjson::Value::array();
    cBorderArr.push(static_cast<int>(m_config.containerBorderColor.r));
    cBorderArr.push(static_cast<int>(m_config.containerBorderColor.g));
    cBorderArr.push(static_cast<int>(m_config.containerBorderColor.b));
    json["containerBorderColor"] = cBorderArr;

    auto targets = matjson::Value::object();
    targets["optionsSliders"] = m_config.targets.optionsSliders;
    targets["editorSliders"]  = m_config.targets.editorSliders;
    targets["garageSliders"]  = m_config.targets.garageSliders;
    json["targets"] = targets;

    if (auto result = file::writeToJson(configPath(), json); result.isErr()) {
        log::warn("[CustomSlider] failed to save config: {}", result.unwrapErr());
    }
}

void CustomSliderManager::resetToDefaults() {
    invalidateImageCache();
    m_config = CustomSliderConfig{};
    saveConfig();
}

void CustomSliderManager::invalidateImageCache() {
    auto clearPath = [](std::string const& path) {
        if (path.empty()) return;
        CCTextureCache::sharedTextureCache()->removeTextureForKey(path.c_str());
        AnimatedGIFSprite::remove(path);
    };

    clearPath(m_imageTexturePath);
    if (m_config.customImagePath != m_imageTexturePath) clearPath(m_config.customImagePath);
    m_imageTexture = nullptr;
    m_imageTexturePath.clear();
}

CCNode* CustomSliderManager::createIconNode(bool isSelected) {
    auto* gm = GameManager::get();
    if (!gm) return nullptr;

    int iconId = m_config.customIconId;
    IconType gdIconType = IconType::Cube;
    int (GameManager::*playerGetter)() = nullptr;
    for (auto const& entry : kIconMap) {
        if (entry.slider != m_config.iconType) continue;
        gdIconType = entry.gd;
        playerGetter = entry.playerGetter;
        break;
    }

    if (m_config.usePlayerIcon && playerGetter) {
        iconId = (gm->*playerGetter)();
    }

    auto* player = SimplePlayer::create(iconId);
    if (!player) return nullptr;

    player->updatePlayerFrame(iconId, gdIconType);

    ccColor3B color1;
    ccColor3B color2;
    if (m_config.usePlayerColors) {
        color1 = gm->colorForIdx(gm->getPlayerColor());
        color2 = gm->colorForIdx(gm->getPlayerColor2());
    } else {
        color1 = m_config.color1;
        color2 = m_config.color2;
    }

    if (isSelected) {
        auto lighten = [](ccColor3B color) {
            color.r = std::min(255, static_cast<int>(color.r) * 3 / 4 + 80);
            color.g = std::min(255, static_cast<int>(color.g) * 3 / 4 + 80);
            color.b = std::min(255, static_cast<int>(color.b) * 3 / 4 + 80);
            return color;
        };
        color1 = lighten(color1);
        color2 = lighten(color2);
    }

    player->setColor(color1);
    player->setSecondColor(color2);

    if (m_config.usePlayerColors) {
        if (gm->getPlayerGlow()) {
            player->setGlowOutline(gm->colorForIdx(gm->getPlayerGlowColor()));
        } else {
            player->disableGlowOutline();
        }
    } else {
        if (m_config.enableGlow) {
            player->setGlowOutline(color2);
        } else {
            player->disableGlowOutline();
        }
    }

    if (m_config.useGradients && paimon::icon_gradients::moduleEnabled()) {
        using namespace paimon::icon_gradients;
        GradientUtils::applyGradient(
            player, GradientUtils::getGradient(gdIconType, false), false, false, kGradientExtra);
    }

    player->setScale(m_config.iconScale);
    player->setRotation(m_config.iconRotation);

    return player;
}

static cocos2d::CCNode* wrapInShapeContainer(
    cocos2d::CCNode* imageNode,
    paimon::slider::CustomSliderConfig const& cfg
) {
    if (!imageNode) return nullptr;
    if (!cfg.containerEnabled) return imageNode;

    using namespace cocos2d;
    using namespace geode::prelude;

    constexpr float kTargetSize = 30.f;

    auto* container = CCNode::create();
    container->setContentSize({kTargetSize, kTargetSize});
    container->setAnchorPoint({0.5f, 0.5f});
    container->ignoreAnchorPointForPosition(false);

    std::string shape = cfg.containerShape.empty() ? std::string("circle") : cfg.containerShape;

    auto* stencil = createShapeStencil(shape, kTargetSize);
    if (!stencil) stencil = createShapeStencil("circle", kTargetSize);
    if (!stencil) {
        imageNode->setPosition({kTargetSize / 2.f, kTargetSize / 2.f});
        container->addChild(imageNode);
        return container;
    }
    stencil->setPosition({0, 0});

    auto* clipper = CCClippingNode::create();
    clipper->setStencil(stencil);
    clipper->setAlphaThreshold(-1.0f);
    clipper->setContentSize({kTargetSize, kTargetSize});

    float iw = std::max(imageNode->getContentWidth(), 1.f);
    float ih = std::max(imageNode->getContentHeight(), 1.f);
    float coverScale = std::max(kTargetSize / iw, kTargetSize / ih);
    imageNode->setScale(coverScale);
    imageNode->setAnchorPoint({0.5f, 0.5f});
    imageNode->ignoreAnchorPointForPosition(false);
    imageNode->setPosition({kTargetSize / 2.f, kTargetSize / 2.f});
    clipper->addChild(imageNode);
    container->addChild(clipper);

    if (cfg.containerBorderEnabled) {
        float thick = std::clamp(cfg.containerBorderThickness, 0.5f, 8.f);
        float borderSize = kTargetSize + thick * 2.f;
        if (auto* border = createShapeBorder(shape, borderSize, thick, cfg.containerBorderColor, 255)) {
            border->setAnchorPoint({0.5f, 0.5f});
            border->setPosition({kTargetSize / 2.f, kTargetSize / 2.f});
            container->addChild(border, 5);
        }
    }

    container->setScale(cfg.iconScale);
    container->setRotation(cfg.iconRotation);
    return container;
}

CCTexture2D* CustomSliderManager::imageTexture() {
    if (m_config.customImagePath.empty()) return nullptr;

    std::filesystem::path imgPath = paimon::assets::pathFromUtf8(m_config.customImagePath);
    std::error_code ec;
    if (!std::filesystem::exists(imgPath, ec)) return nullptr;

    std::string pathStr = geode::utils::string::pathToString(imgPath);
    if (pathStr != m_imageTexturePath) {
        invalidateImageCache();
        m_imageTexturePath = pathStr;
    }
    if (m_imageTexture) return m_imageTexture.data();

    auto loaded = loadThumbTexture(imgPath);
    if (loaded.success && loaded.texture) {
        m_imageTexture = loaded.texture;
        loaded.texture->release();
    } else {
        m_imageTexture = CCTextureCache::sharedTextureCache()->addImage(pathStr.c_str(), false);
    }
    return m_imageTexture.data();
}

CCNode* CustomSliderManager::finishThumbNode(CCSprite* node) {
    if (!node) return nullptr;
    if (m_config.containerEnabled) {
        node->setScale(1.f);
        node->setRotation(0.f);
        node->setOpacity(static_cast<GLubyte>(m_config.iconOpacity));
        return wrapInShapeContainer(node, m_config);
    }

    float maxDim = std::max(node->getContentSize().width, node->getContentSize().height);
    if (maxDim > 0.f) {
        node->setScale(30.f / maxDim * m_config.iconScale);
    } else {
        node->setScale(m_config.iconScale);
    }
    node->setRotation(m_config.iconRotation);
    node->setOpacity(static_cast<GLubyte>(m_config.iconOpacity));
    return node;
}

CCNode* CustomSliderManager::createImageNode() {
    auto* texture = imageTexture();
    if (!texture) return nullptr;

    auto* spr = CCSprite::createWithTexture(texture);
    if (!spr) return nullptr;

    return finishThumbNode(spr);
}

CCNode* CustomSliderManager::createGifNode(bool isSelected) {
    if (m_config.customImagePath.empty()) return nullptr;

    if (isSelected) return createImageNode();

    std::filesystem::path gifPath = paimon::assets::pathFromUtf8(m_config.customImagePath);
    std::error_code ec;
    if (!std::filesystem::exists(gifPath, ec)) return nullptr;

    std::string pathStr = geode::utils::string::pathToString(gifPath);
    auto* gifSpr = AnimatedGIFSprite::create(pathStr);
    if (!gifSpr) {
        return createImageNode();
    }

    return finishThumbNode(gifSpr);
}

CCNode* CustomSliderManager::createThumbNode(bool isSelected) {
    switch (m_config.thumbMode) {
        case SliderThumbMode::Image:
            return createImageNode();
        case SliderThumbMode::Gif:
            return createGifNode(isSelected);
        case SliderThumbMode::Icon:
        default:
            return createIconNode(isSelected);
    }
}

bool CustomSliderManager::shouldAffectSlider(CCNode* slider) {
    if (!slider) return false;
    if (!slider->getParent()) return false;

    // Native editor sliders rebuild color state on close; skinning corrupts pointers, walk parents.
    if (paimon::isEditorScene()) {
        for (auto* p = slider->getParent(); p; p = p->getParent()) {
            if (std::string(typeid(*p).name()).find("CustomSliderPopup") != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    // Native color/HSV editors stay excluded even with every target on.
    for (auto* p = slider->getParent(); p; p = p->getParent()) {
        if (typeinfo_cast<CustomizeObjectLayer*>(p) ||
            typeinfo_cast<ColorSelectPopup*>(p) ||
            typeinfo_cast<ConfigureHSVWidget*>(p) ||
            typeinfo_cast<HSVWidgetPopup*>(p)) {
            return false;
        }
    }

    auto* parent = slider->getParent();
    while (parent) {
        auto className = std::string(typeid(*parent).name());

        if (className.find("CustomSliderPopup") != std::string::npos) {
            return true;
        }

        if (m_config.targets.optionsSliders) {
            if (className.find("OptionsLayer") != std::string::npos ||
                className.find("MoreOptionsLayer") != std::string::npos ||
                className.find("VideoOptionsLayer") != std::string::npos ||
                className.find("AudioOptionsLayer") != std::string::npos) {
                return true;
            }
        }

        if (m_config.targets.editorSliders) {
            if (className.find("EditorUI") != std::string::npos ||
                className.find("EditorPauseLayer") != std::string::npos ||
                className.find("SetupTrigger") != std::string::npos ||
                className.find("LevelEditorLayer") != std::string::npos) {
                return true;
            }
        }

        if (m_config.targets.garageSliders) {
            if (className.find("GJGarageLayer") != std::string::npos ||
                className.find("CharacterColor") != std::string::npos) {
                return true;
            }
        }

        parent = parent->getParent();
    }

    if (m_config.targets.optionsSliders &&
        m_config.targets.editorSliders &&
        m_config.targets.garageSliders) {
        return true;
    }

    return false;
}

void CustomSliderManager::addIconToNode(CCNode* baseNode, bool isSelected) {
    if (!baseNode) return;
    if (auto* node = createThumbNode(isSelected)) baseNode->addChild(node);
}
