#pragma once

#include <matjson.hpp>
#include "SafeText.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace paimon::ban {

struct Verdict {
    bool banned;
    std::string reason;
};

// optional echoes bind the answer to the exact request. When the server sends
// them they must match; when it does not, compatibility is preserved.
inline bool echoedAccountMatches(matjson::Value const& value, std::int32_t expectedAccountID) {
    if (expectedAccountID <= 0) return true;
    if (value.isNumber()) {
        return value.asInt().unwrapOr(0) == expectedAccountID;
    }
    if (value.isString()) {
        auto const text = value.asString().unwrapOr("");
        std::int32_t parsed = 0;
        auto const* begin = text.data();
        auto const* end = begin + text.size();
        auto result = std::from_chars(begin, end, parsed);
        return result.ec == std::errc{} && result.ptr == end && parsed == expectedAccountID;
    }
    return false;
}

inline bool echoedCheckMatches(matjson::Value const& value, std::string_view expectedCheck) {
    if (expectedCheck.empty()) return true;
    return value.isString() && value.asString().unwrapOr("") == expectedCheck;
}

inline std::optional<Verdict> parseBanVerdict(std::string_view response,
                                              std::int32_t expectedAccountID = 0,
                                              std::string_view expectedCheck = {}) {
    if (response.size() > 4096) return std::nullopt;
    auto parsed = matjson::parse(response);
    if (!parsed.isOk()) return std::nullopt;
    auto const& json = parsed.unwrap();
    if (!json.isObject() || !json.contains("banned") || !json["banned"].isBool()) {
        return std::nullopt;
    }
    if (json.contains("success") &&
        (!json["success"].isBool() || !json["success"].asBool().unwrapOr(false))) {
        return std::nullopt;
    }
    if (json.contains("accountID") && !echoedAccountMatches(json["accountID"], expectedAccountID)) {
        return std::nullopt;
    }
    for (auto const* key : {"check", "nonce", "_check"}) {
        if (json.contains(key) && !echoedCheckMatches(json[key], expectedCheck)) {
            return std::nullopt;
        }
    }
    std::string reason;
    if (json.contains("reason") && !json["reason"].isNull()) {
        if (!json["reason"].isString()) return std::nullopt;
        reason = paimon::text::sanitizeDisplayText(
            json["reason"].asString().unwrapOr(""), std::size_t{512});
    }
    return Verdict{json["banned"].asBool().unwrap(), std::move(reason)};
}

} // namespace paimon::ban
