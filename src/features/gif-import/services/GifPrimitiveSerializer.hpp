#pragma once

#include "../GifImportTypes.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <string>

namespace paimon::gifimport {

struct ObjectShape {
    int id = 211;
    float width = 30.f;
    float height = 30.f;
    int zLayer = 0;
};

using ShapeTable = std::array<ObjectShape, kPrimitiveKinds>;

inline std::size_t shapeIndex(PrimitiveKind kind) {
    return static_cast<std::size_t>(kind);
}

inline ShapeTable defaultShapes() {
    ShapeTable shapes{};
    shapes[shapeIndex(PrimitiveKind::Circle)] = {3637, 50.f, 50.f};
    shapes[shapeIndex(PrimitiveKind::Triangle)] = {693, 30.f, 30.f};
    shapes[shapeIndex(PrimitiveKind::WideTriangle)] = {694, 60.f, 30.f};
    return shapes;
}

struct PrimitiveStyle {
    ObjectShape shape;
    int channel = 1;
    PlanStamp const* stamp = nullptr;
};

struct PrimitivePlacement {
    float pixelSize = 1.f;
    float originX = 0.f;
    float originY = 0.f;
    int imageHeight = 0;
    int group = 0;
    bool layered = true;
    int zLayer = -1;
    int backdropZLayer = 0;
};

namespace serialization {

inline void field(std::string& output, int key, int value) {
    output += ',' + std::to_string(key) + ',' + std::to_string(value);
}

inline void field(std::string& output, int key, float value) {
    std::array<char, 64> buffer{};
    auto const result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                      value, std::chars_format::fixed, 6);
    if (result.ec != std::errc{}) {
        // non-finite coordinates cannot be serialized; emit 0 so the primitive
        // stays parseable instead of unwinding through the editor path.
        output += ',' + std::to_string(key) + ",0";
        return;
    }
    output += ',' + std::to_string(key) + ',';
    output.append(buffer.data(), result.ptr);
}

} // namespace serialization

// used by the editor and by the native-texture regression renderer.
inline void appendSerializedPrimitive(std::string& output, Primitive const& object,
                                      PrimitiveStyle const& style,
                                      PrimitivePlacement const& placement) {
    auto const& shape = style.shape;
    auto const* stamp = style.stamp;
    float const boxWidth = object.width * placement.pixelSize;
    float const boxHeight = object.height * placement.pixelSize;
    float const offsetX = stamp ? stamp->offsetX * object.width : 0.f;
    float const offsetY = stamp ? stamp->offsetY * object.height : 0.f;
    float const x = placement.originX + (object.x + offsetX) * placement.pixelSize;
    float const y = placement.originY +
        (placement.imageHeight - object.y - offsetY) * placement.pixelSize;
    float scaleX = boxWidth / shape.width;
    float scaleY = boxHeight / shape.height;
    if (stamp) {
        bool const quarter = std::abs(std::fmod(std::abs(stamp->rotation), 180.f) - 90.f) < 0.5f;
        scaleX = (quarter ? boxHeight : boxWidth) / (quarter ? stamp->baseHeight : stamp->baseWidth);
        scaleY = (quarter ? boxWidth : boxHeight) / (quarter ? stamp->baseWidth : stamp->baseHeight);
    }
    float const rotation = stamp ? stamp->rotation : object.rotation +
        ((object.kind == PrimitiveKind::Triangle || object.kind == PrimitiveKind::WideTriangle) ? 180.f : 0.f);
    output += "1," + std::to_string(stamp ? stamp->objectId : shape.id);
    serialization::field(output, 2, x);
    serialization::field(output, 3, y);
    serialization::field(output, 21, style.channel);
    output += ",64,1,67,1,121,1,134,1";
    serialization::field(output, 128, scaleX);
    serialization::field(output, 129, scaleY);
    if (std::abs(rotation) > 0.001f) serialization::field(output, 6, rotation);
    if (!stamp && std::abs(object.skew) > 0.001f) {
        serialization::field(output, 131, rotation + object.skew);
        serialization::field(output, 132, rotation);
    }
    if (stamp && stamp->flipX) output += ",4,1";
    if (placement.layered) {
        serialization::field(output, 25, std::clamp<int>(object.layer, -999, 999));
        int const layer = placement.backdropZLayer != 0 && object.layer == -999
            ? placement.backdropZLayer : placement.zLayer;
        serialization::field(output, 24, layer != 0 ? layer : -1);
    }
    if (placement.group > 0) serialization::field(output, 57, placement.group);
    output += ';';
}

} // namespace paimon::gifimport
