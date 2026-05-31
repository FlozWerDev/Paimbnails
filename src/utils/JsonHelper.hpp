#pragma once

#include <matjson.hpp>
#include <utility>

namespace paimon::json {

// Iterates safely over a JSON array, calling `fn` for each element.
//
// This helper exists because `.asArray().unwrap()` will throw an unhandled
// exception if matjson cannot produce an array (corrupted disk file, server
// returning the wrong type, race in matjson internals, etc.). Calling code
// should not crash the game over a malformed payload.
//
// If `value` is not an array, or the array result is not Ok, this is a no-op.
//
// Example:
//   forEachInArray(payload["players"], [&](matjson::Value const& item) {
//       // safe iteration even if "players" is missing or wrong type
//   });
template <typename F>
inline void forEachInArray(matjson::Value const& value, F&& fn) {
    if (!value.isArray()) return;
    auto res = value.asArray();
    if (!res.isOk()) return;
    for (auto const& item : res.unwrap()) {
        fn(item);
    }
}

// Same as forEachInArray but takes an explicit array result.
// Useful when you already have the result of asArray().
template <typename F>
inline void forEachInArrayResult(geode::Result<std::vector<matjson::Value>> const& res, F&& fn) {
    if (!res.isOk()) return;
    for (auto const& item : res.unwrap()) {
        fn(item);
    }
}

// Helper that takes ownership of a value's array if available.
// Returns an empty vector if the value is not a valid array.
inline std::vector<matjson::Value> arrayOrEmpty(matjson::Value const& value) {
    if (!value.isArray()) return {};
    auto res = value.asArray();
    if (!res.isOk()) return {};
    return res.unwrap();
}

} // namespace paimon::json
