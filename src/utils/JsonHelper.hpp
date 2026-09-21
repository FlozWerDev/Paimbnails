#pragma once

#include <matjson.hpp>
#include <utility>

namespace paimon::json {

// Itera un array sin reventar si viene roto.
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

// Array o vacio si no hay.
inline std::vector<matjson::Value> arrayOrEmpty(matjson::Value const& value) {
    if (!value.isArray()) return {};
    auto res = value.asArray();
    if (!res.isOk()) return {};
    return res.unwrap();
}

} // namespace paimon::json
