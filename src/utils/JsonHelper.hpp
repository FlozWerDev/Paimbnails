#pragma once

#include <matjson.hpp>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <utility>

namespace paimon::json {

inline float floatOr(matjson::Value const& value, float fallback = 0.f) {
    auto number = value.asDouble().unwrapOr(static_cast<double>(fallback));
    if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max()) return fallback;
    return static_cast<float>(number);
}

template <std::signed_integral T>
inline T integerOr(matjson::Value const& value, T fallback = 0) {
    if (value.isExactlyInt()) {
        auto number = value.asInt().unwrapOr(0);
        if (number < std::numeric_limits<T>::min() || number > std::numeric_limits<T>::max()) return fallback;
        return static_cast<T>(number);
    }
    if (value.isExactlyUInt()) {
        auto number = value.asUInt().unwrapOr(0);
        if (number > static_cast<uintmax_t>(std::numeric_limits<T>::max())) return fallback;
        return static_cast<T>(number);
    }
    if (!value.isNumber()) return fallback;
    auto number = value.asDouble().unwrapOr(0.0);
    // the upper bound is exclusive: int64_max rounds up when stored as double.
    auto upper = std::ldexp(1.0, std::numeric_limits<T>::digits);
    if (!std::isfinite(number) || std::trunc(number) != number || number < -upper || number >= upper) return fallback;
    return static_cast<T>(number);
}

// iterate an array without crashing on malformed input.
template <typename F>
inline void forEachInArray(matjson::Value const& value, F&& fn) {
    if (!value.isArray()) return;
    for (auto const& item : value) {
        fn(item);
    }
}

template <typename F>
inline void forEachInArrayResult(geode::Result<std::vector<matjson::Value>> const& res, F&& fn) {
    if (!res.isOk()) return;
    for (auto const& item : res.unwrap()) {
        fn(item);
    }
}

// array, or empty when missing.
inline std::vector<matjson::Value> arrayOrEmpty(matjson::Value const& value) {
    if (!value.isArray()) return {};
    auto res = value.asArray();
    if (!res.isOk()) return {};
    return res.unwrap();
}

} // namespace paimon::json
