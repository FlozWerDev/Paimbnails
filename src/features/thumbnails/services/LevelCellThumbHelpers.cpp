#include "LevelCellThumbHelpers.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <random>
#include <string_view>
#include <unordered_set>
#include <utility>
#include "../../../utils/Constants.hpp"
#include "../../../utils/SpriteHelper.hpp"

using namespace geode::prelude;

namespace paimon::levelcell {

float safeCoverScale(float targetWidth, float targetHeight, float contentWidth, float contentHeight, float fallback) {
    if (targetWidth <= 0.0f || targetHeight <= 0.0f || contentWidth <= 0.0f || contentHeight <= 0.0f) {
        return fallback;
    }
    float scale = std::max(targetWidth / contentWidth, targetHeight / contentHeight);
    if (scale <= 0.0f) return fallback;
    return std::clamp(scale, 0.01f, 64.0f);
}

float getLevelCellThumbWidthFactor() {
    float widthFactor = static_cast<float>(Mod::get()->getSettingValue<double>("level-thumb-width"));
    return std::clamp(widthFactor, PaimonConstants::MIN_THUMB_WIDTH_FACTOR, PaimonConstants::MAX_THUMB_WIDTH_FACTOR);
}

float calculateLevelCellThumbCoverScale(CCSprite* sprite, float bgWidth, float bgHeight, float widthFactor, float fallback) {
    if (!sprite) {
        return fallback;
    }

    return safeCoverScale(
        bgWidth * widthFactor,
        bgHeight,
        sprite->getContentSize().width,
        sprite->getContentSize().height,
        fallback
    );
}

std::vector<ThumbnailAPI::ThumbnailInfo> normalizeLevelCellGalleryThumbnails(
    int32_t levelID,
    std::vector<ThumbnailAPI::ThumbnailInfo> thumbnails
) {
    std::erase_if(thumbnails, [](ThumbnailAPI::ThumbnailInfo const& thumb) {
        return thumb.url.empty();
    });

    std::stable_sort(thumbnails.begin(), thumbnails.end(), [](ThumbnailAPI::ThumbnailInfo const& a, ThumbnailAPI::ThumbnailInfo const& b) {
        if (a.position != b.position) return a.position < b.position;
        if (a.id != b.id) return a.id < b.id;
        return a.url < b.url;
    });

    // Elimina duplicados por URL
    std::unordered_set<std::string_view> seenUrls;
    seenUrls.reserve(thumbnails.size());
    std::vector<ThumbnailAPI::ThumbnailInfo> normalized;
    normalized.reserve(thumbnails.size() + 1);

    for (auto& thumb : thumbnails) {
        if (!seenUrls.insert(thumb.url).second) {
            continue;
        }
        normalized.push_back(std::move(thumb));
    }

    if (normalized.empty() && levelID > 0) {
        ThumbnailAPI::ThumbnailInfo mainThumb;
        mainThumb.id = "0";
        mainThumb.url = ThumbnailAPI::get().getThumbnailURL(levelID);
        mainThumb.type = "static";
        mainThumb.position = 0;
        normalized.push_back(std::move(mainThumb));
    }

    return normalized;
}

std::string makeLevelCellBlurCacheKey(int32_t levelID, int galleryIndex, float blurIntensity, bool isBackground) {
    return fmt::format(
        "levelcell:{}:{}:{}:{}",
        isBackground ? "bg" : "thumb",
        levelID,
        galleryIndex,
        static_cast<int>(std::round(blurIntensity * 2.0f))
    );
}

PaimonAnimType parseAnimType(std::string const& s) {
    static constexpr std::pair<std::string_view, PaimonAnimType> table[] = {
        {"zoom-slide", PaimonAnimType::ZoomSlide}, {"zoom", PaimonAnimType::Zoom},
        {"slide", PaimonAnimType::Slide}, {"bounce", PaimonAnimType::Bounce},
        {"rotate", PaimonAnimType::Rotate}, {"rotate-content", PaimonAnimType::RotateContent},
        {"shake", PaimonAnimType::Shake}, {"pulse", PaimonAnimType::Pulse},
        {"swing", PaimonAnimType::Swing},
    };
    for (auto const& [key, val] : table) {
        if (key == s) return val;
    }
    return PaimonAnimType::None;
}

PaimonAnimEffect parseAnimEffect(std::string const& s) {
    static constexpr std::pair<std::string_view, PaimonAnimEffect> table[] = {
        {"brightness", PaimonAnimEffect::Brightness}, {"darken", PaimonAnimEffect::Darken},
        {"sepia", PaimonAnimEffect::Sepia}, {"sharpen", PaimonAnimEffect::Sharpen},
        {"edge-detection", PaimonAnimEffect::EdgeDetection}, {"vignette", PaimonAnimEffect::Vignette},
        {"pixelate", PaimonAnimEffect::Pixelate}, {"posterize", PaimonAnimEffect::Posterize},
        {"chromatic", PaimonAnimEffect::Chromatic}, {"scanlines", PaimonAnimEffect::Scanlines},
        {"solarize", PaimonAnimEffect::Solarize}, {"rainbow", PaimonAnimEffect::Rainbow},
        {"red", PaimonAnimEffect::Red}, {"blue", PaimonAnimEffect::Blue},
        {"gold", PaimonAnimEffect::Gold}, {"fade", PaimonAnimEffect::Fade},
        {"grayscale", PaimonAnimEffect::Grayscale}, {"invert", PaimonAnimEffect::Invert},
        {"blur", PaimonAnimEffect::Blur}, {"glitch", PaimonAnimEffect::Glitch},
    };
    for (auto const& [key, val] : table) {
        if (key == s) return val;
    }
    return PaimonAnimEffect::None;
}

PaimonBgType parseBgType(std::string const& s) {
    return s == "thumbnail" ? PaimonBgType::Thumbnail : PaimonBgType::Gradient;
}

PaimonGalleryTransition parseGalleryTransition(std::string const& s) {
    static constexpr std::pair<std::string_view, PaimonGalleryTransition> table[] = {
        {"crossfade", PaimonGalleryTransition::Crossfade}, {"slide-left", PaimonGalleryTransition::SlideLeft},
        {"slide-right", PaimonGalleryTransition::SlideRight}, {"slide-up", PaimonGalleryTransition::SlideUp},
        {"slide-down", PaimonGalleryTransition::SlideDown}, {"zoom-in", PaimonGalleryTransition::ZoomIn},
        {"zoom-out", PaimonGalleryTransition::ZoomOut}, {"flip-horizontal", PaimonGalleryTransition::FlipHorizontal},
        {"flip-vertical", PaimonGalleryTransition::FlipVertical}, {"rotate-cw", PaimonGalleryTransition::RotateCW},
        {"rotate-ccw", PaimonGalleryTransition::RotateCCW}, {"cube", PaimonGalleryTransition::Cube},
        {"dissolve", PaimonGalleryTransition::Dissolve}, {"swipe", PaimonGalleryTransition::Swipe},
        {"bounce", PaimonGalleryTransition::Bounce}, {"elastic-slide", PaimonGalleryTransition::ElasticSlide},
        {"directional-elastic", PaimonGalleryTransition::DirectionalElastic},
        {"spiral", PaimonGalleryTransition::Spiral}, {"wave", PaimonGalleryTransition::Wave},
        {"pop", PaimonGalleryTransition::Pop}, {"random", PaimonGalleryTransition::Random},
    };
    for (auto const& [key, val] : table) {
        if (key == s) return val;
    }
    return PaimonGalleryTransition::Crossfade;
}

PaimonGalleryTransition resolveRandomTransition() {
    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 19); // Excluye Random
    return static_cast<PaimonGalleryTransition>(dist(rng));
}

void calculateLevelCellThumbScale(CCSprite* sprite, float bgWidth, float bgHeight, float widthFactor, float& outScaleX, float& outScaleY) {
    if (!sprite) return;

    const float contentWidth = sprite->getContentSize().width;
    const float contentHeight = sprite->getContentSize().height;
    if (contentWidth <= 0.f || contentHeight <= 0.f || bgWidth <= 0.f || bgHeight <= 0.f) {
        outScaleX = 1.f;
        outScaleY = 1.f;
        return;
    }
    const float desiredWidth = bgWidth * widthFactor;

    outScaleY = bgHeight / contentHeight;

    float minScaleX = outScaleY;
    float desiredScaleX = desiredWidth / contentWidth;
    outScaleX = std::max(minScaleX, desiredScaleX);
}

CCClippingNode* createThumbnailClippingNode(CCNode* bg, CCSprite* sprite, float& outCoverScale) {
    if (!bg || !sprite) {
        log::warn("[LevelCell] createThumbnailClippingNode: null bg or sprite");
        return nullptr;
    }

    float kThumbWidthFactor = getLevelCellThumbWidthFactor();
    const float bgWidth = bg->getContentWidth();
    const float bgHeight = bg->getContentHeight();
    const float desiredWidth = bgWidth * kThumbWidthFactor;

    float scaleX, scaleY;
    calculateLevelCellThumbScale(sprite, bgWidth, bgHeight, kThumbWidthFactor, scaleX, scaleY);
    outCoverScale = calculateLevelCellThumbCoverScale(sprite, bgWidth, bgHeight, kThumbWidthFactor);
    sprite->setScale(outCoverScale);
    log::debug("[LevelCell] createThumbnailClippingNode: bgSize=({:.1f},{:.1f}) widthFactor={:.2f} coverScale={:.4f} scaleX={:.4f} scaleY={:.4f}",
        bgWidth, bgHeight, kThumbWidthFactor, outCoverScale, scaleX, scaleY);

    CCSize scaledSize{ desiredWidth, bgHeight };
    const float kDiagonalSkew = 35.f; // Desplazamiento diagonal del borde izquierdo
    auto drawMask = paimon::SpriteHelper::createDiagonalStencil(scaledSize.width, scaledSize.height, kDiagonalSkew);
    if (!drawMask) return nullptr;
    drawMask->setAnchorPoint({1,0});
    drawMask->ignoreAnchorPointForPosition(true);

    auto clippingNode = CCClippingNode::create();
    if (!clippingNode) return nullptr;

    clippingNode->setStencil(drawMask);
    clippingNode->setContentSize(scaledSize);
    clippingNode->setAnchorPoint({1,0});
    clippingNode->setPosition({ bgWidth, 0.f });
    clippingNode->setID("paimon-clipping-node"_spr);
    clippingNode->setZOrder(-1);

    sprite->setPosition(clippingNode->getContentSize() * 0.5f);
    clippingNode->addChild(sprite);
    return clippingNode;
}

} // namespace paimon::levelcell
