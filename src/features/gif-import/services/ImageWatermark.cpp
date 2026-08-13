#include "ImageWatermark.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace paimon::gifimport {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kSolidObjectSize = 30.f;
constexpr float kSplitA = 0.381966f;
constexpr float kSplitB = 0.438447f;
constexpr float kTurnsA = 2160.f;
constexpr float kTurnsB = 3960.f;
constexpr std::size_t kMaxPairs = 32;

bool canSplit(Primitive const& object) {
    return (object.kind == PrimitiveKind::Block || object.kind == PrimitiveKind::Stroke) &&
        object.width > 0.01f && object.height > 0.01f;
}

bool imageShape(int id) {
    return id == 211 || id == 3637 || id == 693 || id == 694;
}

std::uint32_t markerHash(Primitive const& object, std::size_t index) {
    std::uint32_t hash = 2166136261u;
    auto mix = [&](std::uint32_t value) {
        hash ^= value;
        hash *= 16777619u;
    };
    mix(static_cast<std::uint32_t>(std::lround(object.width * 1000.f)));
    mix(static_cast<std::uint32_t>(std::lround(object.height * 1000.f)));
    mix(static_cast<std::uint32_t>(object.color));
    mix(static_cast<std::uint32_t>(index));
    return hash;
}

std::array<Primitive, 2> splitPrimitive(Primitive const& object, std::size_t index) {
    auto first = object;
    auto second = object;
    auto const hash = markerHash(object, index);
    float const ratio = (hash & 1u) != 0 ? kSplitA : kSplitB;
    float const radians = object.rotation * kPi / 180.f;
    float const cosine = std::cos(radians);
    float const sine = std::sin(radians);
    bool const splitWidth = object.width > object.height ||
        (std::abs(object.width - object.height) < 0.001f && (hash & 2u) == 0);

    if (splitWidth) {
        first.width = object.width * ratio;
        second.width = object.width - first.width;
        float const firstShift = -second.width * 0.5f;
        float const secondShift = first.width * 0.5f;
        first.x += firstShift * cosine;
        first.y += firstShift * sine;
        second.x += secondShift * cosine;
        second.y += secondShift * sine;
    } else {
        first.height = object.height * ratio;
        second.height = object.height - first.height;
        float const firstShift = -second.height * 0.5f;
        float const secondShift = first.height * 0.5f;
        first.x -= firstShift * sine;
        first.y += firstShift * cosine;
        second.x -= secondShift * sine;
        second.y += secondShift * cosine;
    }
    return {first, second};
}

struct SavedObject {
    int id = 0;
    int color = 0;
    float x = 0.f;
    float y = 0.f;
    float rotation = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    std::uint8_t flags = 0;
    bool hasPosition = false;
    bool hasScale = false;
};

bool nextToken(std::string_view text, std::size_t& cursor, std::string_view& token) {
    if (cursor >= text.size()) return false;
    auto const end = text.find(',', cursor);
    if (end == std::string_view::npos) {
        token = text.substr(cursor);
        cursor = text.size();
    } else {
        token = text.substr(cursor, end - cursor);
        cursor = end + 1;
    }
    return true;
}

bool parseInt(std::string_view token, int& value) {
    auto const result = std::from_chars(token.data(), token.data() + token.size(), value);
    return result.ec == std::errc{} && result.ptr == token.data() + token.size();
}

bool parseFloat(std::string_view token, float& value) {
    auto const result = std::from_chars(token.data(), token.data() + token.size(), value);
    return result.ec == std::errc{} && result.ptr == token.data() + token.size();
}

bool parseObject(std::string_view save, SavedObject& object) {
    std::size_t cursor = 0;
    std::string_view keyToken;
    std::string_view valueToken;
    bool hasX = false;
    bool hasY = false;
    bool hasScaleX = false;
    bool hasScaleY = false;
    while (nextToken(save, cursor, keyToken) && nextToken(save, cursor, valueToken)) {
        int key = 0;
        if (!parseInt(keyToken, key)) continue;
        switch (key) {
            case 1: parseInt(valueToken, object.id); break;
            case 2: hasX = parseFloat(valueToken, object.x); break;
            case 3: hasY = parseFloat(valueToken, object.y); break;
            case 6: parseFloat(valueToken, object.rotation); break;
            case 21: parseInt(valueToken, object.color); break;
            case 64:
                if (valueToken == "1") object.flags |= 1u;
                break;
            case 67:
                if (valueToken == "1") object.flags |= 2u;
                break;
            case 121:
                if (valueToken == "1") object.flags |= 4u;
                break;
            case 128: hasScaleX = parseFloat(valueToken, object.scaleX); break;
            case 129: hasScaleY = parseFloat(valueToken, object.scaleY); break;
            case 134:
                if (valueToken == "1") object.flags |= 8u;
                break;
            default: break;
        }
    }
    object.hasPosition = hasX && hasY;
    object.hasScale = hasScaleX && hasScaleY;
    return imageShape(object.id) && object.color > 0 && std::isfinite(object.rotation) &&
        (!object.hasPosition || (std::isfinite(object.x) && std::isfinite(object.y))) &&
        (!object.hasScale || (std::isfinite(object.scaleX) && std::isfinite(object.scaleY)));
}

bool close(float left, float right, float absolute = 0.002f, float relative = 0.001f) {
    return std::abs(left - right) <= absolute + std::max(std::abs(left), std::abs(right)) * relative;
}

bool sameRotation(float left, float right) {
    float difference = std::fmod(std::abs(left - right), 360.f);
    difference = std::min(difference, 360.f - difference);
    return difference < 0.01f;
}

std::uint8_t markerRatio(float first, float second) {
    first = std::abs(first);
    second = std::abs(second);
    float const total = first + second;
    if (total <= 0.0001f) return 0;
    float const ratio = std::min(first, second) / total;
    if (std::abs(ratio - kSplitA) < 0.003f) return 1u;
    if (std::abs(ratio - kSplitB) < 0.003f) return 2u;
    return 0;
}

bool rotationMark(float rotation) {
    if (rotation < 0.f) return false;
    int const turns = static_cast<int>(std::floor(rotation / 360.f + 0.0001f));
    return turns == 6 || turns == 7 || turns == 11 || turns == 12;
}

std::uint8_t matchesPair(SavedObject const& first, SavedObject const& second) {
    if (first.color != second.color || !sameRotation(first.rotation, second.rotation)) {
        return 0;
    }

    std::uint8_t const widthVariant = close(
        std::abs(first.scaleY), std::abs(second.scaleY))
        ? markerRatio(first.scaleX, second.scaleX) : 0;
    std::uint8_t const heightVariant = close(
        std::abs(first.scaleX), std::abs(second.scaleX))
        ? markerRatio(first.scaleY, second.scaleY) : 0;
    if (widthVariant == 0 && heightVariant == 0) return 0;

    float const radians = first.rotation * kPi / 180.f;
    float const cosine = std::cos(radians);
    float const sine = std::sin(radians);
    float const dx = second.x - first.x;
    float const dy = second.y - first.y;
    float const localX = dx * cosine - dy * sine;
    float const localY = dx * sine + dy * cosine;

    if (widthVariant != 0) {
        float const expected = kSolidObjectSize *
            (std::abs(first.scaleX) + std::abs(second.scaleX)) * 0.5f;
        float const tolerance = 0.012f + expected * 0.001f;
        if (std::abs(std::abs(localX) - expected) <= tolerance &&
            std::abs(localY) <= tolerance) {
            return widthVariant;
        }
    }

    if (heightVariant != 0) {
        float const expected = kSolidObjectSize *
            (std::abs(first.scaleY) + std::abs(second.scaleY)) * 0.5f;
        float const tolerance = 0.012f + expected * 0.001f;
        if (std::abs(std::abs(localY) - expected) <= tolerance &&
            std::abs(localX) <= tolerance) {
            return heightVariant;
        }
    }
    return 0;
}

} // namespace

void applyImageWatermark(ImportPlan& plan, int objectBudget) {
    std::size_t eligible = 0;
    for (auto const& object : plan.staticObjects) eligible += canSplit(object);
    for (auto const& track : plan.tracks) {
        for (auto const& object : track.objects) eligible += canSplit(object);
    }
    std::size_t const budgetPairs = std::clamp<std::size_t>(
        static_cast<std::size_t>(std::max(objectBudget, 1)) / 250, 3, kMaxPairs);
    std::size_t const target = std::min(eligible, budgetPairs);
    std::size_t seen = 0;
    std::size_t marked = 0;

    auto rewrite = [&](std::vector<Primitive>& objects) {
        std::vector<Primitive> watermarked;
        watermarked.reserve(objects.size() + target);
        for (auto const& object : objects) {
            if (!canSplit(object)) {
                watermarked.push_back(object);
                continue;
            }
            ++seen;
            std::size_t const desired = seen * target / eligible;
            if (desired <= marked) {
                watermarked.push_back(object);
                continue;
            }
            auto split = splitPrimitive(object, marked);
            watermarked.push_back(split[0]);
            watermarked.push_back(split[1]);
            ++marked;
        }
        objects = std::move(watermarked);
    };

    if (target > 0) {
        rewrite(plan.staticObjects);
        for (auto& track : plan.tracks) rewrite(track.objects);
    }

    std::size_t visualObjects = plan.staticObjects.size();
    for (auto const& track : plan.tracks) visualObjects += track.objects.size();
    std::size_t const turnTarget = std::min(visualObjects, budgetPairs);
    seen = 0;
    marked = 0;
    auto addTurns = [&](std::vector<Primitive>& objects) {
        for (auto& object : objects) {
            ++seen;
            std::size_t const desired = seen * turnTarget / visualObjects;
            if (desired <= marked) continue;
            float rotation = std::fmod(object.rotation, 360.f);
            if (rotation < 0.f) rotation += 360.f;
            object.rotation = rotation +
                ((markerHash(object, marked) & 1u) != 0 ? kTurnsA : kTurnsB);
            ++marked;
        }
    };
    if (turnTarget > 0) {
        addTurns(plan.staticObjects);
        for (auto& track : plan.tracks) addTurns(track.objects);
    }
}

WatermarkEvidence inspectImageWatermark(std::string_view levelString) {
    WatermarkEvidence evidence;
    std::size_t cursor = 0;
    SavedObject previous;
    bool hasPrevious = false;
    std::size_t pairCount = 0;
    std::size_t signedPairCount = 0;
    std::uint8_t ratioVariants = 0;
    while (cursor < levelString.size()) {
        auto const end = levelString.find(';', cursor);
        auto const save = levelString.substr(
            cursor, end == std::string_view::npos ? levelString.size() - cursor : end - cursor);
        SavedObject object;
        if (parseObject(save, object)) {
            if (rotationMark(object.rotation)) {
                ++evidence.rotationMarks;
                if (std::popcount(object.flags) >= 3) ++evidence.signedRotationMarks;
                if (evidence.detected()) return evidence;
            }
            bool const geometryCandidate = object.id == 211 && object.hasPosition && object.hasScale;
            if (geometryCandidate) {
                auto const variant = hasPrevious ? matchesPair(previous, object) : 0;
                if (variant != 0) {
                    ++pairCount;
                    ratioVariants |= variant;
                    if (std::popcount(previous.flags) >= 3 &&
                        std::popcount(object.flags) >= 3) {
                        ++signedPairCount;
                    }
                    if (ratioVariants == 3u) {
                        evidence.geometryPairs = pairCount;
                        evidence.signedPairs = signedPairCount;
                        if (evidence.detected()) return evidence;
                    }
                    hasPrevious = false;
                    if (end == std::string_view::npos) break;
                    cursor = end + 1;
                    continue;
                }
                previous = object;
                hasPrevious = true;
            } else {
                hasPrevious = false;
            }
        } else {
            hasPrevious = false;
        }
        if (end == std::string_view::npos) break;
        cursor = end + 1;
    }

    return evidence;
}

WatermarkEvidence inspectStoredImageWatermark(
    std::string_view storedLevelString,
    std::string_view unpackedLevelString
) {
    return inspectImageWatermark(
        unpackedLevelString.empty() ? storedLevelString : unpackedLevelString);
}

} // namespace paimon::gifimport
