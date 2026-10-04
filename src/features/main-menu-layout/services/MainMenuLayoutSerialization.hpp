#pragma once

#include "MainMenuLayoutManager.hpp"
#include "../../../utils/JsonHelper.hpp"

#include <algorithm>
#include <limits>

namespace paimon::menu_layout {

namespace serialization_detail {
inline float number(matjson::Value const& value, char const* key, float fallback) {
    return paimon::json::floatOr(value[key], fallback);
}
inline GLubyte color(matjson::Value const& value, char const* key, int fallback) {
    return static_cast<GLubyte>(std::clamp<int64_t>(value[key].asInt().unwrapOr(fallback), 0, 255));
}
inline int integer(matjson::Value const& value, char const* key) {
    return static_cast<int>(std::clamp<int64_t>(value[key].asInt().unwrapOr(0),
        std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
}
} // namespace serialization_detail

inline matjson::Value layoutToJson(std::string const& key, MenuButtonLayout const& layout) {
    matjson::Value value = matjson::makeObject({});
    value["key"] = key;
    value["x"] = layout.position.x;
    value["y"] = layout.position.y;
    value["scale"] = layout.scale;
    value["scaleX"] = layout.scaleX;
    value["scaleY"] = layout.scaleY;
    value["opacity"] = layout.opacity;
    value["hidden"] = layout.hidden;
    value["layer"] = layout.layer;
    value["linkGroup"] = layout.linkGroup;
    value["hasColor"] = layout.hasColor;
    value["r"] = layout.color.r;
    value["g"] = layout.color.g;
    value["b"] = layout.color.b;
    value["fontFile"] = layout.fontFile;
    return value;
}

inline MenuButtonLayout layoutFromJson(matjson::Value const& value) {
    MenuButtonLayout layout;
    layout.position.x = serialization_detail::number(value, "x", 0.0);
    layout.position.y = serialization_detail::number(value, "y", 0.0);
    layout.scale = serialization_detail::number(value, "scale", 1.0);
    layout.scaleX = serialization_detail::number(value, "scaleX", layout.scale);
    layout.scaleY = serialization_detail::number(value, "scaleY", layout.scale);
    layout.opacity = serialization_detail::number(value, "opacity", 1.0);
    layout.hidden = value["hidden"].asBool().unwrapOr(false);
    layout.layer = serialization_detail::integer(value, "layer");
    layout.linkGroup = value["linkGroup"].asString().unwrapOr("");
    layout.hasColor = value["hasColor"].asBool().unwrapOr(false);
    layout.color.r = serialization_detail::color(value, "r", 255);
    layout.color.g = serialization_detail::color(value, "g", 255);
    layout.color.b = serialization_detail::color(value, "b", 255);
    layout.fontFile = value["fontFile"].asString().unwrapOr("");
    return layout;
}

inline matjson::Value shapeToJson(DrawShapeLayout const& layout) {
    matjson::Value value = matjson::makeObject({});
    value["id"] = layout.id;
    value["kind"] = layout.kind == DrawShapeKind::Rectangle ? "rect" : layout.kind == DrawShapeKind::Circle ? "circle" : "round";
    value["x"] = layout.position.x;
    value["y"] = layout.position.y;
    value["scale"] = layout.scale;
    value["scaleX"] = layout.scaleX;
    value["scaleY"] = layout.scaleY;
    value["opacity"] = layout.opacity;
    value["hidden"] = layout.hidden;
    value["width"] = layout.width;
    value["height"] = layout.height;
    value["cornerRadius"] = layout.cornerRadius;
    value["r"] = layout.color.r;
    value["g"] = layout.color.g;
    value["b"] = layout.color.b;
    value["zOrder"] = layout.zOrder;
    value["layer"] = layout.layer;
    value["linkGroup"] = layout.linkGroup;
    return value;
}

inline DrawShapeLayout shapeFromJson(matjson::Value const& value) {
    DrawShapeLayout layout;
    layout.id = value["id"].asString().unwrapOr("");
    auto kind = value["kind"].asString().unwrapOr("round");
    layout.kind = kind == "rect" ? DrawShapeKind::Rectangle : kind == "circle" ? DrawShapeKind::Circle : DrawShapeKind::RoundedRect;
    layout.position.x = serialization_detail::number(value, "x", 0.0);
    layout.position.y = serialization_detail::number(value, "y", 0.0);
    layout.scale = serialization_detail::number(value, "scale", 1.0);
    layout.scaleX = serialization_detail::number(value, "scaleX", layout.scale);
    layout.scaleY = serialization_detail::number(value, "scaleY", layout.scale);
    layout.opacity = serialization_detail::number(value, "opacity", 0.75);
    layout.hidden = value["hidden"].asBool().unwrapOr(false);
    layout.width = serialization_detail::number(value, "width", 110.0);
    layout.height = serialization_detail::number(value, "height", 70.0);
    layout.cornerRadius = serialization_detail::number(value, "cornerRadius", 18.0);
    layout.color.r = serialization_detail::color(value, "r", 90);
    layout.color.g = serialization_detail::color(value, "g", 220);
    layout.color.b = serialization_detail::color(value, "b", 255);
    layout.zOrder = serialization_detail::integer(value, "zOrder");
    layout.layer = serialization_detail::integer(value, "layer");
    layout.linkGroup = value["linkGroup"].asString().unwrapOr("");
    return layout;
}

} // namespace paimon::menu_layout
