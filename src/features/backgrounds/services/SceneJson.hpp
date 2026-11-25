#pragma once

#include <matjson.hpp>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>

namespace paimon::wallpaper::scenejson {

// wallpaper Engine wraps user-bindable properties as {"user": ..., "value": v}
// and script-driven ones as {"script": ..., "value": v}. The authored default
// always lives in "value".
inline matjson::Value const& valueOf(matjson::Value const& val) {
    matjson::Value const* current = &val;
    for (int depth = 0; depth < 4 && current->isObject() && current->contains("value"); ++depth) {
        current = &(*current)["value"];
    }
    return *current;
}

inline bool parseFloat(std::string_view text, float& out) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    if (text.empty()) return false;
    auto const* begin = text.data();
    auto const* end = text.data() + text.size();
    if (*begin == '+') ++begin;
    auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc() && ptr != begin && std::isfinite(out);
}

inline double numberOf(matjson::Value const& raw, double fallback = 0.0) {
    auto const& v = valueOf(raw);
    if (v.isNumber()) {
        auto const d = v.asDouble().unwrapOr(fallback);
        return std::isfinite(d) ? d : fallback;
    }
    if (v.isBool()) return v.asBool().unwrapOr(false) ? 1.0 : 0.0;
    if (v.isString()) {
        float parsed = 0.f;
        auto const text = v.asString().unwrapOr("");
        if (parseFloat(text, parsed)) return parsed;
    }
    return fallback;
}

inline bool boolOf(matjson::Value const& raw, bool fallback = true) {
    auto const& v = valueOf(raw);
    if (v.isBool()) return v.asBool().unwrapOr(fallback);
    if (v.isNumber()) return v.asDouble().unwrapOr(fallback ? 1.0 : 0.0) != 0.0;
    if (v.isString()) {
        auto s = v.asString().unwrapOr("");
        if (s == "true" || s == "1") return true;
        if (s == "false" || s == "0") return false;
    }
    return fallback;
}

inline std::string stringOf(matjson::Value const& raw, std::string fallback = {}) {
    auto const& v = valueOf(raw);
    if (v.isString()) return v.asString().unwrapOr(fallback);
    return fallback;
}

inline std::string lowerOf(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

struct Vec3 { float x = 0.f, y = 0.f, z = 0.f; };

// vectors arrive as "x y z" strings, arrays, {x,y,z} objects or a scalar.
inline Vec3 vecOf(matjson::Value const& raw, Vec3 fallback = {}) {
    auto const& v = valueOf(raw);
    if (v.isString()) {
        auto const text = v.asString().unwrapOr("");
        float values[3] = {fallback.x, fallback.y, fallback.z};
        size_t count = 0;
        size_t pos = 0;
        while (pos < text.size() && count < 3) {
            while (pos < text.size() && (std::isspace(static_cast<unsigned char>(text[pos])) || text[pos] == ',')) ++pos;
            size_t end = pos;
            while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) && text[end] != ',') ++end;
            if (end == pos) break;
            float parsed = 0.f;
            if (!parseFloat(std::string_view(text).substr(pos, end - pos), parsed)) return fallback;
            values[count++] = parsed;
            pos = end;
        }
        if (count == 0) return fallback;
        if (count == 1) return {values[0], values[0], values[0]};
        return {values[0], values[1], count > 2 ? values[2] : fallback.z};
    }
    if (v.isArray()) {
        auto const& arr = v.asArray().unwrap();
        Vec3 res = fallback;
        if (arr.size() > 0) res.x = static_cast<float>(numberOf(arr[0], fallback.x));
        if (arr.size() > 1) res.y = static_cast<float>(numberOf(arr[1], fallback.y));
        if (arr.size() > 2) res.z = static_cast<float>(numberOf(arr[2], fallback.z));
        return res;
    }
    if (v.isObject()) {
        Vec3 res = fallback;
        bool any = false;
        for (auto const& [key, slot] : {std::pair{"x", &res.x}, std::pair{"y", &res.y}, std::pair{"z", &res.z}}) {
            if (v.contains(key)) { *slot = static_cast<float>(numberOf(v[key], *slot)); any = true; }
        }
        return any ? res : fallback;
    }
    if (v.isNumber()) {
        float const f = static_cast<float>(numberOf(v, fallback.x));
        return {f, f, f};
    }
    return fallback;
}

// colours are 0..1 floats in scene.json; particles use 0..255. Returns 0..1.
inline Vec3 colorOf(matjson::Value const& raw, Vec3 fallback = {1.f, 1.f, 1.f}) {
    auto v = vecOf(raw, {-1.f, -1.f, -1.f});
    if (v.x < 0.f && v.y < 0.f && v.z < 0.f) return fallback;
    if (v.x > 1.001f || v.y > 1.001f || v.z > 1.001f) {
        v.x /= 255.f; v.y /= 255.f; v.z /= 255.f;
    }
    return {std::clamp(v.x, 0.f, 1.f), std::clamp(v.y, 0.f, 1.f), std::clamp(v.z, 0.f, 1.f)};
}

} // namespace paimon::wallpaper::scenejson
