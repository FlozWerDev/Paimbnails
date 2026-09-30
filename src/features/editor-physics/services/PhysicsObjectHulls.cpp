#include "PhysicsObjectHulls.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GameObject.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

namespace paimon::editorphysics {

namespace {

constexpr int kSampleSize = 48;
constexpr unsigned char kAlphaFloor = 40;
// above this the outline is the bounding box itself, and a plain box is both
// cheaper and steadier for the solver than a hull with rounding error in it.
constexpr float kBoxCoverage = 0.92f;
// a corner enclosing less than this is the sampling grid stepping around a
// diagonal, not a face the object actually has.
constexpr float kFlatCorner = 0.002f * kSampleSize * kSampleSize;

std::unordered_map<int, Silhouette> g_traced;

float cross(CCPoint origin, CCPoint a, CCPoint b) {
    return (a.x - origin.x) * (b.y - origin.y) - (a.y - origin.y) * (b.x - origin.x);
}

std::vector<CCPoint> convexHull(std::vector<CCPoint> points) {
    std::sort(points.begin(), points.end(), [](CCPoint a, CCPoint b) {
        return a.x == b.x ? a.y < b.y : a.x < b.x;
    });
    points.erase(
        std::unique(points.begin(), points.end(), [](CCPoint a, CCPoint b) {
            return a.x == b.x && a.y == b.y;
        }),
        points.end()
    );
    if (points.size() < 3) return {};

    std::vector<CCPoint> hull(points.size() * 2);
    std::size_t count = 0;
    for (auto const& point : points) {
        while (count >= 2 && cross(hull[count - 2], hull[count - 1], point) <= 0.f) --count;
        hull[count++] = point;
    }
    std::size_t const lower = count + 1;
    for (std::size_t i = points.size() - 1; i > 0; --i) {
        auto const& point = points[i - 1];
        while (count >= lower && cross(hull[count - 2], hull[count - 1], point) <= 0.f) --count;
        hull[count++] = point;
    }
    hull.resize(count - 1);
    return hull;
}

// drop the corner that encloses the least area until the hull fits a fixture,
// and keep going while a corner is barely a corner at all.
void simplify(std::vector<CCPoint>& hull) {
    while (hull.size() > 3) {
        std::size_t flattest = 0;
        float smallest = std::numeric_limits<float>::max();
        for (std::size_t i = 0; i < hull.size(); ++i) {
            std::size_t const previous = (i + hull.size() - 1) % hull.size();
            std::size_t const next = (i + 1) % hull.size();
            float const area = std::abs(cross(hull[previous], hull[i], hull[next]));
            if (area < smallest) {
                smallest = area;
                flattest = i;
            }
        }
        if (hull.size() <= static_cast<std::size_t>(kMaxVertices) && smallest > kFlatCorner) break;
        hull.erase(hull.begin() + static_cast<std::ptrdiff_t>(flattest));
    }
}

float polygonArea(std::vector<CCPoint> const& hull) {
    float area = 0.f;
    for (std::size_t i = 0; i < hull.size(); ++i) {
        auto const& current = hull[i];
        auto const& next = hull[(i + 1) % hull.size()];
        area += current.x * next.y - next.x * current.y;
    }
    return area * 0.5f;
}

std::vector<CCPoint> extremePixels(unsigned char const* rgba, int width, int height) {
    struct Span {
        int low = std::numeric_limits<int>::max();
        int high = -1;
    };
    std::vector<Span> rows(static_cast<std::size_t>(height));
    std::vector<Span> columns(static_cast<std::size_t>(width));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (rgba[(static_cast<std::size_t>(y) * width + x) * 4 + 3] < kAlphaFloor) continue;
            auto& row = rows[static_cast<std::size_t>(y)];
            auto& column = columns[static_cast<std::size_t>(x)];
            row.low = std::min(row.low, x);
            row.high = std::max(row.high, x);
            column.low = std::min(column.low, y);
            column.high = std::max(column.high, y);
        }
    }

    // the image rows run top down, so the y axis is flipped back here.
    std::vector<CCPoint> points;
    points.reserve(static_cast<std::size_t>(width + height) * 2);
    for (int y = 0; y < height; ++y) {
        auto const& row = rows[static_cast<std::size_t>(y)];
        if (row.high < 0) continue;
        float const flipped = static_cast<float>(height - 1 - y);
        points.push_back({static_cast<float>(row.low), flipped});
        points.push_back({static_cast<float>(row.high), flipped});
    }
    for (int x = 0; x < width; ++x) {
        auto const& column = columns[static_cast<std::size_t>(x)];
        if (column.high < 0) continue;
        points.push_back({static_cast<float>(x), static_cast<float>(height - 1 - column.low)});
        points.push_back({static_cast<float>(x), static_cast<float>(height - 1 - column.high)});
    }
    return points;
}

Silhouette trace(GameObject* object) {
    auto* frame = object->displayFrame();
    if (!frame) return {};
    auto* sprite = CCSprite::createWithSpriteFrame(frame);
    if (!sprite) return {};

    auto const content = sprite->getContentSize();
    if (content.width < 1.f || content.height < 1.f) return {};
    sprite->setAnchorPoint({0.5f, 0.5f});
    sprite->setScaleX(kSampleSize / content.width);
    sprite->setScaleY(kSampleSize / content.height);
    sprite->setPosition({kSampleSize * 0.5f, kSampleSize * 0.5f});
    sprite->setRotation(0.f);

    auto* canvas = CCRenderTexture::create(
        kSampleSize, kSampleSize, kCCTexture2DPixelFormat_RGBA8888
    );
    if (!canvas) return {};
    canvas->beginWithClear(0.f, 0.f, 0.f, 0.f);
    sprite->visit();
    canvas->end();

    auto* image = canvas->newCCImage(true);
    if (!image) return {};
    auto hull = convexHull(extremePixels(image->getData(), image->getWidth(), image->getHeight()));
    image->release();
    if (hull.size() < 3) return {};
    simplify(hull);

    float minX = hull.front().x;
    float minY = hull.front().y;
    float maxX = minX;
    float maxY = minY;
    for (auto const& point : hull) {
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    }
    float const width = maxX - minX;
    float const height = maxY - minY;
    if (width < 2.f || height < 2.f) return {};

    for (auto& point : hull) {
        point.x = (point.x - minX) / width - 0.5f;
        point.y = (point.y - minY) / height - 0.5f;
    }
    float area = polygonArea(hull);
    if (area < 0.f) {
        std::reverse(hull.begin(), hull.end());
        area = -area;
    }
    if (area > kBoxCoverage) return {};

    Silhouette silhouette;
    silhouette.vertexCount = static_cast<int>(hull.size());
    for (std::size_t i = 0; i < hull.size(); ++i) {
        silhouette.vertices[i] = {hull[i].x, hull[i].y};
    }
    return silhouette;
}

} // namespace

Silhouette const& silhouetteOf(GameObject* object) {
    static Silhouette const box;
    if (!object) return box;
    int const id = object->m_objectID;
    if (auto found = g_traced.find(id); found != g_traced.end()) return found->second;
    return g_traced.emplace(id, trace(object)).first->second;
}

} // namespace paimon::editorphysics
