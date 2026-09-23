#include <Geode/Geode.hpp>

#include "LevelParse.hpp"

#include <string_view>

namespace paimon::autobuild {

namespace {

constexpr int kKeyId          = 1;
constexpr int kKeyX           = 2;
constexpr int kKeyY           = 3;
constexpr int kKeyFlipX       = 4;
constexpr int kKeyFlipY       = 5;
constexpr int kKeyRotation    = 6;
constexpr int kKeyEditorLayer = 20;
constexpr int kKeyMainColor   = 21;
constexpr int kKeyDetailColor = 22;
constexpr int kKeyZLayer      = 24;
constexpr int kKeyZOrder      = 25;
constexpr int kKeyScale       = 32;
constexpr int kKeyGroups      = 57;
constexpr int kKeyHighDetail  = 103;
constexpr int kKeyScaleX      = 128;
constexpr int kKeyScaleY      = 129;

int toInt(std::string_view token, int fallback = 0) {
    return geode::utils::numFromString<int>(std::string(token)).unwrapOr(fallback);
}

float toFloat(std::string_view token, float fallback = 0.f) {
    return geode::utils::numFromString<float>(std::string(token)).unwrapOr(fallback);
}

int countGroups(std::string_view token) {
    if (token.empty()) return 0;
    int count = 1;
    for (char c : token) {
        if (c == '.') ++count;
    }
    return count;
}

LevelObject parseObject(std::string chunk) {
    LevelObject object;
    bool sawScale = false;

    size_t start = 0;
    std::string_view key;
    bool expectValue = false;
    auto const view = std::string_view(chunk);
    for (size_t i = 0; i <= view.size(); ++i) {
        if (i != view.size() && view[i] != ',') continue;
        auto token = view.substr(start, i - start);
        start = i + 1;
        if (!expectValue) {
            key = token;
            expectValue = true;
            continue;
        }
        expectValue = false;

        switch (toInt(key, -1)) {
            case kKeyId:          object.id = toInt(token); break;
            case kKeyX:           object.x = toFloat(token); break;
            case kKeyY:           object.y = toFloat(token); break;
            case kKeyFlipX:       object.flipX = toInt(token) != 0; break;
            case kKeyFlipY:       object.flipY = toInt(token) != 0; break;
            case kKeyRotation:    object.rotation = toFloat(token); break;
            case kKeyEditorLayer: object.editorLayer = toInt(token); break;
            case kKeyMainColor:   object.mainColor = toInt(token); break;
            case kKeyDetailColor: object.detailColor = toInt(token); break;
            case kKeyZLayer:      object.zLayer = toInt(token, kZLayerDefault); break;
            case kKeyZOrder:      object.zOrder = toInt(token); break;
            case kKeyHighDetail:  object.highDetail = toInt(token) != 0; break;
            case kKeyGroups:      object.groupCount = countGroups(token); break;
            case kKeyScale:
                object.scaleX = toFloat(token, 1.f);
                object.scaleY = object.scaleX;
                sawScale = true;
                break;
            // 2.2 writes the axes separately and keeps key 32 for compatibility,
            // so the per-axis values win whichever order they arrive in.
            case kKeyScaleX:      object.scaleX = toFloat(token, 1.f); sawScale = true; break;
            case kKeyScaleY:      object.scaleY = toFloat(token, 1.f); sawScale = true; break;
            default: break;
        }
    }
    if (!sawScale) {
        object.scaleX = 1.f;
        object.scaleY = 1.f;
    }
    object.save = std::move(chunk);
    return object;
}

} // namespace

bool objectKey(std::string const& save, int key, std::string& out) {
    auto const view = std::string_view(save);
    size_t start = 0;
    std::string_view current;
    bool expectValue = false;
    for (size_t i = 0; i <= view.size(); ++i) {
        if (i != view.size() && view[i] != ',') continue;
        auto token = view.substr(start, i - start);
        start = i + 1;
        if (!expectValue) {
            current = token;
            expectValue = true;
            continue;
        }
        expectValue = false;
        if (toInt(current, -1) == key) {
            out.assign(token);
            return true;
        }
    }
    return false;
}

LevelData parseLevelString(std::string const& text, int maxObjects) {
    LevelData data;
    if (text.empty()) return data;

    size_t start = 0;
    bool first = true;
    while (start <= text.size()) {
        auto end = text.find(';', start);
        auto const length = end == std::string::npos ? text.size() - start : end - start;
        auto chunk = text.substr(start, length);
        start = end == std::string::npos ? text.size() + 1 : end + 1;
        if (chunk.empty()) continue;

        if (first) {
            first = false;
            // A level string always opens with the settings chunk, but a raw
            // copy-paste of objects does not, so only treat it as settings when
            // it looks like one.
            if (chunk.rfind("kA", 0) == 0 || chunk.rfind("kS", 0) == 0) {
                auto key = chunk.find("kS38,");
                if (key != std::string::npos) {
                    auto body = key + 5;
                    auto stop = chunk.find_first_of(',', body);
                    data.colors = stop == std::string::npos ? chunk.substr(body)
                                                            : chunk.substr(body, stop - body);
                }
                data.settings = std::move(chunk);
                continue;
            }
        }

        if (static_cast<int>(data.objects.size()) >= maxObjects) {
            data.truncated = true;
            break;
        }
        auto object = parseObject(std::move(chunk));
        if (object.id <= 0) continue;
        data.objects.push_back(std::move(object));
    }
    return data;
}

std::vector<ColorChannel> parseColorChannels(std::string const& colors) {
    std::vector<ColorChannel> out;
    size_t start = 0;
    while (start <= colors.size()) {
        auto end = colors.find('|', start);
        auto const length = end == std::string::npos ? colors.size() - start : end - start;
        auto entry = std::string_view(colors).substr(start, length);
        start = end == std::string::npos ? colors.size() + 1 : end + 1;
        if (entry.empty()) continue;

        ColorChannel channel;
        bool hasId = false;
        size_t field = 0;
        std::string_view key;
        bool expectValue = false;
        for (size_t i = 0; i <= entry.size(); ++i) {
            if (i != entry.size() && entry[i] != '_') continue;
            auto token = entry.substr(field, i - field);
            field = i + 1;
            if (!expectValue) {
                key = token;
                expectValue = true;
                continue;
            }
            expectValue = false;
            switch (toInt(key, -1)) {
                case 1: channel.r = static_cast<unsigned char>(toInt(token, 255) & 0xff); break;
                case 2: channel.g = static_cast<unsigned char>(toInt(token, 255) & 0xff); break;
                case 3: channel.b = static_cast<unsigned char>(toInt(token, 255) & 0xff); break;
                case 5: channel.blending = toInt(token) != 0; break;
                case 6: channel.id = toInt(token); hasId = true; break;
                case 7: channel.opacity = toFloat(token, 1.f); break;
                case 9: channel.copyId = toInt(token); break;
                default: break;
            }
        }
        if (hasId) out.push_back(channel);
    }
    return out;
}

} // namespace paimon::autobuild
