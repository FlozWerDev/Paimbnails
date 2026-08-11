#include "StatsChartNode.hpp"
#include "InfoBlocks.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::info {

namespace {

constexpr ccColor4F kNoBorder{0.f, 0.f, 0.f, 0.f};
constexpr float kPadX = 5.f;
constexpr float kPadTop = 4.f;
constexpr float kPadBottom = 4.f;
constexpr float kLaneH = 11.f;      // room for the axis labels
constexpr float kScaleLaneH = 11.f; // room for the "max N" caption
constexpr float kMaxBarW = 15.f;    // keeps two attempts from filling the chart
// Empty-state columns preserve the chart silhouette.
constexpr int kGhostSlots = 24;

// CCDrawNode draws premultiplied, so the rgb has to carry the alpha.
ccColor4F rgba(ccColor3B color, float alpha) {
    return {color.r / 255.f * alpha, color.g / 255.f * alpha, color.b / 255.f * alpha, alpha};
}

void fill(CCDrawNode* draw, float x, float y, float w, float h, ccColor4F const& color) {
    if (w <= 0.f || h <= 0.f) return;
    draw->drawRect({x, y}, {x + w, y + h}, color, 0.f, kNoBorder);
}

// Green marks late deaths; red marks early ones.
ccColor3B heatColor(float t) {
    t = std::clamp(t, 0.f, 1.f);
    if (t < 0.5f) {
        float k = t / 0.5f;
        return {static_cast<GLubyte>(120 + k * 135), static_cast<GLubyte>(215),
                static_cast<GLubyte>(120 - k * 40)};
    }
    float k = (t - 0.5f) / 0.5f;
    return {255, static_cast<GLubyte>(215 - k * 140), static_cast<GLubyte>(80 - k * 40)};
}

ccColor3B lighten(ccColor3B color, float k) {
    return {static_cast<GLubyte>(color.r + (255 - color.r) * k),
            static_cast<GLubyte>(color.g + (255 - color.g) * k),
            static_cast<GLubyte>(color.b + (255 - color.b) * k)};
}

ccColor3B shade(ccColor3B color, float k) {
    return {static_cast<GLubyte>(color.r * k), static_cast<GLubyte>(color.g * k),
            static_cast<GLubyte>(color.b * k)};
}

// Bars use fading slabs and an inset cap for depth and readable tops.
void drawBar(CCDrawNode* draw, float x, float w, float h, ccColor3B color, bool marked) {
    constexpr int kSlabs = 4;
    for (int s = 0; s < kSlabs; s++) {
        float y0 = h * static_cast<float>(s) / kSlabs;
        float y1 = std::min(h, h * static_cast<float>(s + 1) / kSlabs + 0.4f);
        fill(draw, x, y0, w, y1 - y0, rgba(color, 0.62f + 0.26f * (y0 / std::max(h, 1.f))));
    }

    float capH = std::min(2.f, h);
    float inset = w >= 6.f ? 1.f : 0.f;
    fill(draw, x + inset, h - capH, w - inset * 2.f, capH,
         rgba(lighten(color, marked ? 0.8f : 0.28f), marked ? 1.f : 0.94f));
}

}

StatsChartNode* StatsChartNode::create(std::vector<float> const& values, CCSize const& size,
                                       ChartOptions const& options) {
    auto ret = new StatsChartNode();
    if (ret && ret->init(values, size, options)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool StatsChartNode::init(std::vector<float> const& values, CCSize const& size,
                          ChartOptions const& options) {
    if (!CCNode::init()) return false;
    if (size.width <= 0.f || size.height <= 0.f) return false;

    this->setContentSize(size);
// Set the anchor explicitly because callers place charts by center.
    this->ignoreAnchorPointForPosition(false);
    this->setAnchorPoint({0.5f, 0.5f});

    if (auto bg = paimon::SpriteHelper::safeCreateScale9("square02_001.png")) {
        bg->setContentSize(size);
        bg->setColor({0, 0, 0});
// Keep bars darker than stat tiles for thumbnail contrast.
        bg->setOpacity(145);
        bg->setAnchorPoint({0.f, 0.f});
        this->addChild(bg, -1);
    }

    bool const hasLane = !options.axisLabels.empty() || !options.axisNote.empty();
    float const laneH = hasLane ? kLaneH : kPadBottom;
    float const topH = options.showScale ? kScaleLaneH : kPadTop;

    CCRect plot{kPadX, laneH, size.width - kPadX * 2.f, size.height - laneH - topH};
    if (plot.size.width <= 4.f || plot.size.height <= 6.f) return false;

    float const peak = values.empty()
        ? 0.f : *std::max_element(values.begin(), values.end());
    bool const empty = peak <= 0.f;

    Geometry geo;
    geo.count = empty ? kGhostSlots : static_cast<int>(values.size());
    geo.slot = plot.size.width / static_cast<float>(geo.count);
    if (!options.stretch) geo.slot = std::min(geo.slot, kMaxBarW);

    float const gap = std::clamp(geo.slot * 0.2f, 0.6f, 3.f);
    geo.barW = std::max(2.f, geo.slot - gap);
// Leave unused columns on the left so the newest attempt stays aligned.
    geo.startX = plot.origin.x
        + std::max(0.f, plot.size.width - geo.slot * static_cast<float>(geo.count));

    auto* grid = CCDrawNode::create();
    this->addChild(grid);
    drawGrid(grid, plot, geo, empty);
    drawAxis(grid, options, plot, laneH);

    if (empty) {
        if (!options.emptyText.empty()) {
            blocks::addText(this, options.emptyText.c_str(), "chatFont.fnt", 0.4f, blocks::kLabel,
                            {size.width / 2.f, plot.origin.y + plot.size.height / 2.f},
                            {0.5f, 0.5f}, plot.size.width - 12.f);
        }
        return true;
    }

    auto* bars = CCDrawNode::create();
// Grow bars from the baseline so animation stays above the axis.
    bars->setPosition({0.f, plot.origin.y});
    this->addChild(bars, 1);
    drawBars(bars, values, options, plot, geo, peak);

    bars->setScaleY(0.08f);
    bars->runAction(CCEaseOut::create(CCScaleTo::create(0.3f, 1.f, 1.f), 2.2f));

    if (options.showScale) {
        blocks::addText(this, fmt::format("max {}", static_cast<int64_t>(std::lround(peak))).c_str(),
                        "chatFont.fnt", 0.3f, {150, 150, 150},
                        {plot.origin.x + plot.size.width, size.height - topH / 2.f - 1.f},
                        {1.f, 0.5f}, plot.size.width * 0.5f);
    }
    return true;
}

void StatsChartNode::drawGrid(CCDrawNode* draw, CCRect const& plot, Geometry const& geo,
                              bool ghost) {
// Keep faint columns for empty slots so gaps remain meaningful.
    float const trackAlpha = ghost ? 0.045f : 0.07f;
    for (int i = 0; i < geo.count; i++) {
        float x = geo.startX + static_cast<float>(i) * geo.slot;
        fill(draw, x, plot.origin.y, geo.barW, plot.size.height,
             rgba({255, 255, 255}, trackAlpha));
    }

// Quarter, half, and three-quarter guides make heights readable without labels.
    for (int i = 1; i <= 3; i++) {
        float y = plot.origin.y + plot.size.height * (0.25f * static_cast<float>(i));
        fill(draw, plot.origin.x, y, plot.size.width, 1.f, rgba({255, 255, 255}, 0.07f));
    }
}

void StatsChartNode::drawBars(CCDrawNode* draw, std::vector<float> const& values,
                              ChartOptions const& options, CCRect const& plot,
                              Geometry const& geo, float peak) {
    int highlight = options.highlight;
    if (highlight < 0 || highlight >= static_cast<int>(values.size())) {
        highlight = static_cast<int>(
            std::max_element(values.begin(), values.end()) - values.begin());
    }

    float const height = plot.size.height;

// Reference line marks how far the level has been reached.
    if (options.marker >= 0.f && options.marker <= 1.f) {
        float x = plot.origin.x + plot.size.width * options.marker;
        for (float y = 0.f; y < height; y += 5.f) {
            fill(draw, x - 0.5f, y, 1.f, std::min(3.f, height - y), rgba(blocks::kAccent, 0.45f));
        }
    }

    for (size_t i = 0; i < values.size(); i++) {
        if (values[i] <= 0.f) continue;

        float ratio = values[i] / peak;
        float h = std::max(3.f, height * ratio);
        float x = geo.startX + static_cast<float>(i) * geo.slot;

        auto color = options.heat ? heatColor(ratio) : options.color;
        if (i < options.tints.size()) {
            color = shade(color, 0.55f + 0.45f * std::clamp(options.tints[i], 0.f, 1.f));
        }
        drawBar(draw, x, geo.barW, h, color, static_cast<int>(i) == highlight);
    }

    if (options.average) {
        float sum = 0.f;
        for (float value : values) sum += value;
        float y = height * (sum / static_cast<float>(values.size()) / peak);

        for (float x = plot.origin.x; x < plot.origin.x + plot.size.width; x += 7.f) {
            float w = std::min(4.f, plot.origin.x + plot.size.width - x);
            fill(draw, x, y, w, 1.f, rgba({255, 255, 255}, 0.45f));
        }
    }
}

void StatsChartNode::drawAxis(CCDrawNode* draw, ChartOptions const& options,
                             CCRect const& plot, float laneY) {
    fill(draw, plot.origin.x, laneY, plot.size.width, 1.f, rgba({255, 255, 255}, 0.32f));

    auto const count = static_cast<int>(options.axisLabels.size());
    for (int i = 0; i < count; i++) {
        float t = count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 0.f;
        float x = plot.origin.x + plot.size.width * t;

        fill(draw, x - 0.5f, laneY - 2.5f, 1.f, 2.5f, rgba({255, 255, 255}, 0.32f));

        CCPoint anchor{t <= 0.f ? 0.f : (t >= 1.f ? 1.f : 0.5f), 0.5f};
        blocks::addText(this, options.axisLabels[i].c_str(), "chatFont.fnt", 0.28f,
                        {150, 150, 150}, {x, laneY / 2.f - 1.f}, anchor);
    }

    if (!options.axisNote.empty()) {
        blocks::addText(this, options.axisNote.c_str(), "chatFont.fnt", 0.28f, {130, 130, 130},
                        {plot.origin.x + plot.size.width, laneY / 2.f - 1.f}, {1.f, 0.5f});
    }
}

}
