#include "GifSourceScaler.hpp"

#include "GifParallel.hpp"

#include "../../../utils/GLSLLoader.hpp"

#include <Geode/Geode.hpp>
#include <Geode/cocos/platform/CCGL.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace geode::prelude;

namespace paimon::gifimport {

namespace {

// four pixels per grid cell cover the finest tracing grid and its area samples.
constexpr int kWorkingFactor = 4;
constexpr int kMinWorking = 128;
// retry on the cpu when gpu alpha falls below the importer threshold.
constexpr std::uint8_t kImportAlphaThreshold = 96;

bool hasVisibleAlpha(SourceAnimation const& animation) {
    for (auto const& frame : animation.frames) {
        for (std::size_t index = 3; index < frame.rgba.size(); index += 4) {
            if (frame.rgba[index] >= kImportAlphaThreshold) return true;
        }
    }
    return false;
}

// a local autorelease pool frees render targets between frames instead of after the whole video.
struct FramePool {
    FramePool() { CCPoolManager::sharedPoolManager()->push(); }
    ~FramePool() { CCPoolManager::sharedPoolManager()->pop(); }
};

void reduceFrame(
    std::uint8_t const* source,
    int sourceWidth,
    int sourceHeight,
    std::uint8_t* target,
    int width,
    int height
) {
    for (int y = 0; y < height; ++y) {
        int const fromY = y * sourceHeight / height;
        int const toY = std::max(fromY + 1, (y + 1) * sourceHeight / height);
        for (int x = 0; x < width; ++x) {
            int const fromX = x * sourceWidth / width;
            int const toX = std::max(fromX + 1, (x + 1) * sourceWidth / width);
            std::uint32_t color[3]{};
            std::uint32_t alpha = 0;
            std::uint32_t weight = 0;
            for (int sampleY = fromY; sampleY < toY; ++sampleY) {
                auto const* row = source +
                    (static_cast<std::size_t>(sampleY) * sourceWidth + fromX) * 4;
                for (int sampleX = fromX; sampleX < toX; ++sampleX, row += 4) {
                    color[0] += static_cast<std::uint32_t>(row[0]) * row[3];
                    color[1] += static_cast<std::uint32_t>(row[1]) * row[3];
                    color[2] += static_cast<std::uint32_t>(row[2]) * row[3];
                    alpha += row[3];
                    ++weight;
                }
            }
            auto* pixel = target + (static_cast<std::size_t>(y) * width + x) * 4;
            pixel[3] = static_cast<std::uint8_t>(alpha / std::max(weight, 1u));
            // without alpha weighting, transparent edge black bleeds into color
            // and the drawing grows a dark orla.
            for (int channel = 0; channel < 3; ++channel) {
                pixel[channel] = alpha > 0
                    ? static_cast<std::uint8_t>(color[channel] / alpha)
                    : 0;
            }
        }
    }
}

CCRenderTexture* renderPass(
    CCTexture2D* texture,
    CCSize const& sourceSize,
    int width,
    int height,
    bool flipped,
    CCGLProgram* shader,
    CCPoint tapStep = {1.f, 1.f}
) {
    auto* canvas = CCRenderTexture::create(
        width, height, kCCTexture2DPixelFormat_RGBA8888);
    if (!canvas) return nullptr;
    auto* sprite = CCSprite::createWithTexture(texture);
    if (!sprite) return nullptr;

    sprite->setAnchorPoint({0.f, 0.f});
    sprite->setPosition({0.f, 0.f});
    sprite->setScaleX(static_cast<float>(width) / sourceSize.width);
    sprite->setScaleY(static_cast<float>(height) / sourceSize.height);
    // render-target textures come upside down; flipping back keeps the pass
    // chain in one orientation.
    sprite->setFlipY(flipped);
    // destination alpha must be the shader's write, not a blend against void:
    // tracing reads that channel.
    sprite->setBlendFunc({GL_ONE, GL_ZERO});
    sprite->setShaderProgram(shader);

    shader->use();
    shader->setUniformsForBuiltins();
    GLint const texel = shader->getUniformLocationForName("u_texel");
    if (texel != -1) {
        shader->setUniformLocationWith2f(
            texel, tapStep.x / sourceSize.width, tapStep.y / sourceSize.height);
    }

    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    texture->setTexParameters(&params);

    canvas->beginWithClear(0.f, 0.f, 0.f, 0.f);
    sprite->visit();
    canvas->end();
    return canvas;
}

bool reduceOnGpu(
    SourceAnimation const& source,
    SourceAnimation& target,
    int width,
    int height
) {
    auto* shader = paimon::shaders::getGifDownscaleShader();
    if (!shader) return false;
    auto* director = CCDirector::get();
    if (!director || !director->getOpenGLView()) return false;

    CCSize const full{
        static_cast<float>(source.width), static_cast<float>(source.height)};
    for (std::size_t index = 0; index < source.frames.size(); ++index) {
        FramePool const pool;
        auto* texture = new CCTexture2D();
        bool const uploaded = texture->initWithData(
            source.frames[index].rgba.data(), kCCTexture2DPixelFormat_RGBA8888,
            source.width, source.height, full);
        if (!uploaded) {
            texture->release();
            return false;
        }

        CCRenderTexture* canvas = nullptr;
        CCTexture2D* input = texture;
        CCSize step = full;
        bool flipped = false;
        // halve while it fits: one big bilinear downsample reads four texels
        // and skips nearly the whole image.
        while (true) {
            int const nextWidth = std::max(width, static_cast<int>(step.width) / 2);
            int const nextHeight = std::max(height, static_cast<int>(step.height) / 2);
            canvas = renderPass(input, step, nextWidth, nextHeight, flipped, shader);
            if (!canvas) {
                texture->release();
                return false;
            }
            step = CCSize(
                static_cast<float>(nextWidth), static_cast<float>(nextHeight));
            input = canvas->getSprite()->getTexture();
            flipped = true;
            if (nextWidth <= width && nextHeight <= height) break;
        }

        auto* image = canvas->newCCImage(true);
        texture->release();
        if (!image) return false;
        bool const usable = image->getData() &&
            image->getWidth() >= width && image->getHeight() >= height;
        if (usable) {
            auto& frame = target.frames[index];
            auto const* pixels = image->getData();
            int const stride = image->getWidth();
            for (int y = 0; y < height; ++y) {
                std::memcpy(
                    frame.rgba.data() + static_cast<std::size_t>(y) * width * 4,
                    pixels + static_cast<std::size_t>(y) * stride * 4,
                    static_cast<std::size_t>(width) * 4);
            }
        }
        image->release();
        if (!usable) return false;
    }
    // some drivers return transparent readback from a completed pass; retry with cpu reduction.
    if (hasVisibleAlpha(source) && !hasVisibleAlpha(target)) {
        log::warn("[GifImport] GPU downscale returned no visible alpha; using CPU fallback");
        return false;
    }
    return true;
}

std::shared_ptr<SourceAnimation> blurSource(
    std::shared_ptr<SourceAnimation> source, float radius
) {
    auto target = std::make_shared<SourceAnimation>(*source);
    auto* director = CCDirector::get();
    auto* shader = director && director->getOpenGLView()
        ? paimon::shaders::getGifBlurShader() : nullptr;
    CCSize const size{static_cast<float>(source->width), static_cast<float>(source->height)};
    bool gpu = shader != nullptr;
    if (gpu) for (std::size_t index = 0; index < source->frames.size(); ++index) {
        FramePool const pool;
        auto* texture = new CCTexture2D();
        if (!texture->initWithData(source->frames[index].rgba.data(),
            kCCTexture2DPixelFormat_RGBA8888, source->width, source->height, size)) {
            texture->release(); gpu = false; break;
        }
        auto* horizontal = renderPass(texture, size, source->width, source->height,
            false, shader, {radius, 0.f});
        auto* vertical = horizontal ? renderPass(horizontal->getSprite()->getTexture(),
            size, source->width, source->height, true, shader, {0.f, radius}) : nullptr;
        auto* image = vertical ? vertical->newCCImage(true) : nullptr;
        texture->release();
        if (!image) { gpu = false; break; }
        gpu = image->getData() && image->getWidth() >= source->width && image->getHeight() >= source->height;
        if (gpu) for (int y = 0; y < source->height; ++y) {
            std::memcpy(target->frames[index].rgba.data() + static_cast<std::size_t>(y) * source->width * 4,
                image->getData() + static_cast<std::size_t>(y) * image->getWidth() * 4,
                static_cast<std::size_t>(source->width) * 4);
        }
        image->release();
        if (!gpu) break;
    }
    if (gpu && (!hasVisibleAlpha(*source) || hasVisibleAlpha(*target))) return target;
    // same kernel and clamp-to-edge sampling if gl is unavailable.
    parallelFor(source->frames.size(), [&](std::size_t index) {
        auto input = source->frames[index].rgba;
        auto& output = target->frames[index].rgba;
        constexpr float weights[]{1.f, 4.f, 6.f, 4.f, 1.f};
        for (int axis = 0; axis < 2; ++axis) {
            for (int y = 0; y < source->height; ++y) for (int x = 0; x < source->width; ++x) {
                float sum[4]{};
                for (int tap = -2; tap <= 2; ++tap) {
                    float const offset = (axis == 0 ? x : y) + tap * radius;
                    int const limit = (axis == 0 ? source->width : source->height) - 1;
                    float const clamped = std::clamp(offset, 0.f, static_cast<float>(limit));
                    int const lo = static_cast<int>(clamped), hi = std::min(lo + 1, limit);
                    float const fraction = clamped - lo;
                    auto const a = (static_cast<std::size_t>(axis == 0 ? y : lo) * source->width + (axis == 0 ? lo : x)) * 4;
                    auto const b = (static_cast<std::size_t>(axis == 0 ? y : hi) * source->width + (axis == 0 ? hi : x)) * 4;
                    float const alpha = input[a + 3] * (1.f - fraction) + input[b + 3] * fraction;
                    float const weight = weights[tap + 2];
                    sum[3] += alpha * weight;
                    for (int c = 0; c < 3; ++c) sum[c] +=
                        (input[a + c] * (1.f - fraction) + input[b + c] * fraction) * alpha * weight;
                }
                auto const at = (static_cast<std::size_t>(y) * source->width + x) * 4;
                for (int c = 0; c < 3; ++c) output[at + c] = static_cast<std::uint8_t>(
                    std::clamp(sum[3] > 0.f ? sum[c] / sum[3] : 0.f, 0.f, 255.f));
                output[at + 3] = static_cast<std::uint8_t>(std::clamp(sum[3] / 16.f, 0.f, 255.f));
            }
            if (axis == 0) input = output;
        }
    });
    return target;
}

} // namespace

std::shared_ptr<SourceAnimation> prescaleSource(
    std::shared_ptr<SourceAnimation> source,
    int maxDimension,
    float blurRadius
) {
    if (!source || source->frames.empty() || source->width <= 0 || source->height <= 0) {
        return source;
    }
    int const working = std::max(kMinWorking, maxDimension * kWorkingFactor);
    int const longest = std::max(source->width, source->height);
    auto filter = [&](std::shared_ptr<SourceAnimation> image) {
        return blurRadius > 0.f ? blurSource(image,
            std::clamp(blurRadius, 0.35f, 2.f) * std::max(image->width, image->height) /
                std::max(maxDimension, 1)) : image;
    };
    if (longest <= working) return filter(source);

    auto reduced = std::make_shared<SourceAnimation>();
    reduced->width = source->width >= source->height
        ? working
        : std::max(1, working * source->width / source->height);
    reduced->height = source->width >= source->height
        ? std::max(1, working * source->height / source->width)
        : working;
    reduced->frames.resize(source->frames.size());
    for (std::size_t index = 0; index < source->frames.size(); ++index) {
        reduced->frames[index].delayMs = source->frames[index].delayMs;
        reduced->frames[index].rgba.assign(
            static_cast<std::size_t>(reduced->width) * reduced->height * 4, 0);
    }

    if (!reduceOnGpu(*source, *reduced, reduced->width, reduced->height)) {
        parallelFor(source->frames.size(), [&](std::size_t index) {
            reduceFrame(
                source->frames[index].rgba.data(), source->width, source->height,
                reduced->frames[index].rgba.data(), reduced->width, reduced->height);
        });
    }
    log::info(
        "[GifImport] Fuente reducida de {}x{} a {}x{} ({} frames)",
        source->width, source->height, reduced->width, reduced->height,
        reduced->frames.size());
    return filter(reduced);
}

} // namespace paimon::gifimport
