#pragma once

#include <matjson.hpp>
#include <utility>

namespace paimon::json {

// iterate an array without crashing on malformed input.
template <typename F>
inline void forEachInArray(matjson::Value const& value, F&& fn) {
    if (!value.isArray()) return;
    auto res = value.asArray();
    if (!res.isOk()) return;
    for (auto const& item : res.unwrap()) {
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
