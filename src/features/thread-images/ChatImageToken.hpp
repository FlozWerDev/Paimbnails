#pragma once

#include <algorithm>
#include <charconv>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::chatimg {

struct ChatImageRef {
    std::string id;   // 24 lowercase hex chars (96-bit random per upload)
    std::string ext;  // jpg | png | webp | gif
    int width = 0;    // 1..1920
    int height = 0;   // 1..1920
    std::string key;  // 22-char unpadded base64url 128-bit encryption seed
    std::string sig;  // 16 lowercase hex chars (64-bit server HMAC binding id:ext:threadID)

    std::string cacheKey() const {
        return id + "." + ext;
    }

    bool operator==(ChatImageRef const&) const = default;
};

struct ParsedChatMessage {
    std::string displayText;
    std::vector<ChatImageRef> images;
};

struct DisplaySize {
    float width = 0.f;
    float height = 0.f;
};

namespace detail {

inline bool isLowerHex(std::string_view s, size_t expectedLen) {
    if (s.size() != expectedLen) return false;
    for (char ch : s) {
        bool ok = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
        if (!ok) return false;
    }
    return true;
}

inline bool isBase64Url22(std::string_view s) {
    if (s.size() != 22) return false;
    for (char ch : s) {
        bool ok = (ch >= 'A' && ch <= 'Z')
               || (ch >= 'a' && ch <= 'z')
               || (ch >= '0' && ch <= '9')
               || ch == '-' || ch == '_';
        if (!ok) return false;
    }
    // 16 bytes -> 128 bits = 21*6 + 2 bits; last base64url char must have bottom 4 bits == 0
    char last = s.back();
    int val = -1;
    if (last >= 'A' && last <= 'Z') val = last - 'A';
    else if (last >= 'a' && last <= 'z') val = 26 + (last - 'a');
    else if (last >= '0' && last <= '9') val = 52 + (last - '0');
    else if (last == '-') val = 62;
    else if (last == '_') val = 63;
    return val >= 0 && (val & 0x0F) == 0;
}

inline bool isAllowedExt(std::string_view ext) {
    return ext == "jpg" || ext == "png" || ext == "webp" || ext == "gif";
}

inline bool parsePositiveInt(std::string_view s, int maxVal, int& out) {
    if (s.empty() || s.size() > 4) return false;
    for (char ch : s) {
        if (ch < '0' || ch > '9') return false;
    }
    int v = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr != s.data() + s.size()) return false;
    if (v < 1 || v > maxVal) return false;
    out = v;
    return true;
}

inline bool tryParseTokenBody(std::string_view inner, ChatImageRef& out) {
    // Exact v2 format: <24hex>.<ext>:<W>x<H>:<22b64url>:<16hex>
    auto dot = inner.find('.');
    if (dot != 24) return false;
    auto idPart = inner.substr(0, 24);
    if (!isLowerHex(idPart, 24)) return false;

    auto rest = inner.substr(25);
    auto colon1 = rest.find(':');
    if (colon1 == std::string_view::npos) return false;
    auto extPart = rest.substr(0, colon1);
    if (!isAllowedExt(extPart)) return false;

    rest = rest.substr(colon1 + 1);
    auto colon2 = rest.find(':');
    if (colon2 == std::string_view::npos) return false;
    auto dims = rest.substr(0, colon2);

    auto xPos = dims.find('x');
    if (xPos == std::string_view::npos || dims.find('x', xPos + 1) != std::string_view::npos) {
        return false;
    }
    int width = 0;
    int height = 0;
    if (!parsePositiveInt(dims.substr(0, xPos), 1920, width)) return false;
    if (!parsePositiveInt(dims.substr(xPos + 1), 1920, height)) return false;

    rest = rest.substr(colon2 + 1);
    auto colon3 = rest.find(':');
    if (colon3 == std::string_view::npos) return false;
    auto keyPart = rest.substr(0, colon3);
    auto sigPart = rest.substr(colon3 + 1);
    if (!isBase64Url22(keyPart) || !isLowerHex(sigPart, 16)) return false;

    out.id = std::string(idPart);
    out.ext = std::string(extPart);
    out.width = width;
    out.height = height;
    out.key = std::string(keyPart);
    out.sig = std::string(sigPart);
    return true;
}

inline std::string cleanDisplayText(std::string_view raw) {
    std::string normalized;
    normalized.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\r') {
            if (i + 1 < raw.size() && raw[i + 1] == '\n') continue;
            normalized.push_back('\n');
        } else {
            normalized.push_back(raw[i]);
        }
    }

    std::string perLineTrimmed;
    perLineTrimmed.reserve(normalized.size());
    size_t pos = 0;
    while (pos <= normalized.size()) {
        auto nl = normalized.find('\n', pos);
        bool hasNl = nl != std::string::npos;
        auto line = hasNl ? std::string_view(normalized).substr(pos, nl - pos)
                          : std::string_view(normalized).substr(pos);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
            line.remove_suffix(1);
        }
        perLineTrimmed.append(line);
        if (!hasNl) break;
        perLineTrimmed.push_back('\n');
        pos = nl + 1;
    }

    std::string collapsed;
    collapsed.reserve(perLineTrimmed.size());
    int nlRun = 0;
    for (char ch : perLineTrimmed) {
        if (ch == '\n') {
            ++nlRun;
            if (nlRun <= 2) collapsed.push_back('\n');
        } else {
            nlRun = 0;
            collapsed.push_back(ch);
        }
    }

    size_t start = 0;
    while (start < collapsed.size() &&
           (collapsed[start] == ' ' || collapsed[start] == '\t' || collapsed[start] == '\n')) {
        ++start;
    }
    size_t end = collapsed.size();
    while (end > start &&
           (collapsed[end - 1] == ' ' || collapsed[end - 1] == '\t' || collapsed[end - 1] == '\n')) {
        --end;
    }
    return collapsed.substr(start, end - start);
}

} // namespace detail

inline ParsedChatMessage parseChatImages(std::string_view body, size_t maxImages = 2) {
    ParsedChatMessage result;
    std::string stripped;
    stripped.reserve(body.size());

    constexpr std::string_view kPrefix = "[pimg:";
    size_t cursor = 0;
    while (cursor < body.size()) {
        auto open = body.find(kPrefix, cursor);
        if (open == std::string_view::npos) {
            stripped.append(body.substr(cursor));
            break;
        }
        stripped.append(body.substr(cursor, open - cursor));
        auto close = body.find(']', open + kPrefix.size());
        if (close == std::string_view::npos) {
            stripped.append(body.substr(open));
            break;
        }

        auto inner = body.substr(open + kPrefix.size(), close - (open + kPrefix.size()));
        ChatImageRef ref;
        if (result.images.size() < maxImages && detail::tryParseTokenBody(inner, ref)) {
            result.images.push_back(std::move(ref));
        } else {
            stripped.append(body.substr(open, close - open + 1));
        }
        cursor = close + 1;
    }

    result.displayText = detail::cleanDisplayText(stripped);
    return result;
}

inline std::string formatChatImageToken(ChatImageRef const& ref) {
    if (ref.id.size() != 24 || ref.ext.empty() || ref.width <= 0 || ref.height <= 0
        || ref.key.size() != 22 || ref.sig.size() != 16) {
        return "";
    }
    return "[pimg:" + ref.id + "." + ref.ext + ":" +
           std::to_string(ref.width) + "x" + std::to_string(ref.height) + ":" +
           ref.key + ":" + ref.sig + "]";
}

inline DisplaySize computeInlineDisplaySize(
    int srcW, int srcH, float maxWidth = 210.f, float maxHeight = 135.f
) {
    if (srcW <= 0 || srcH <= 0) {
        return {std::min(168.f, maxWidth), std::min(105.f, maxHeight)};
    }
    constexpr float kMaxUpscale = 1.5f;
    constexpr float kMinDim = 44.f;
    float const fw = static_cast<float>(srcW);
    float const fh = static_cast<float>(srcH);
    float const scale = std::min({kMaxUpscale, maxWidth / fw, maxHeight / fh});
    float const minW = std::min(kMinDim, maxWidth);
    float const minH = std::min(kMinDim, maxHeight);
    return {
        std::clamp(fw * scale, minW, maxWidth),
        std::clamp(fh * scale, minH, maxHeight),
    };
}

struct ComposeBarLayout {
    float composeWidth = 0.f;
    float composeBgX = 0.f;
    float messageInputX = 0.f;
    float attachX = 0.f;
    float attachY = 0.f;
};

inline float computeImageCornerRadius(float width, float height, float maxRadius = 8.f) {
    if (width <= 0.f || height <= 0.f || maxRadius <= 0.f) return 0.f;
    return std::min(maxRadius, std::max(0.f, std::min(width, height) * 0.25f));
}

inline ComposeBarLayout computeFixedComposeBarLayout(
    float origComposeWidth,
    float origBgX,
    float origInputX,
    float baseComposeY,
    float bgScaleX = 1.f,
    float bgAnchorX = 0.5f,
    float slotWidth = 26.f,
    float gapFromBox = 16.f
) {
    float const targetW = std::max(120.f, origComposeWidth - slotWidth);
    float const appliedShrink = std::max(0.f, origComposeWidth - targetW);
    float const halfShift = appliedShrink * 0.5f;
    float const bgX = origBgX + halfShift;
    float const inputX = origInputX + halfShift;
    float const bgW = (targetW + 12.f) * std::max(0.01f, bgScaleX);
    float const bgLeft = bgX - bgW * bgAnchorX;
    return {targetW, bgX, inputX, bgLeft - gapFromBox, baseComposeY};
}

} // namespace paimon::chatimg
