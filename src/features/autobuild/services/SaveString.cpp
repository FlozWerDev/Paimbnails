#include "SaveString.hpp"

#include <fmt/format.h>

#include <cstdlib>
#include <string_view>
#include <vector>

namespace paimon::autobuild {

namespace {

// Object keys we touch. Everything else is copied through untouched.
constexpr int kKeyId          = 1;
constexpr int kKeyX           = 2;
constexpr int kKeyY           = 3;
constexpr int kKeyEditorLayer = 20;
constexpr int kKeyMainColor   = 21;
constexpr int kKeyDetailColor = 22;
constexpr int kKeyZOrder      = 25;
constexpr int kKeyGroups      = 57;
constexpr int kKeyEditorLayer2 = 61;

std::vector<std::string_view> tokenize(std::string const& save) {
    std::vector<std::string_view> out;
    out.reserve(32);
    size_t start = 0;
    for (size_t i = 0; i <= save.size(); ++i) {
        if (i == save.size() || save[i] == ',' || save[i] == ';' ||
            save[i] == '\n' || save[i] == '\r') {
            if (i > start) out.emplace_back(save.data() + start, i - start);
            start = i + 1;
        }
    }
    return out;
}

int toInt(std::string_view token, int fallback = 0) {
    std::string buf(token);
    char* end = nullptr;
    long value = std::strtol(buf.c_str(), &end, 10);
    if (end == buf.c_str()) return fallback;
    return static_cast<int>(value);
}

float toFloat(std::string_view token, float fallback = 0.f) {
    std::string buf(token);
    char* end = nullptr;
    float value = std::strtof(buf.c_str(), &end);
    if (end == buf.c_str()) return fallback;
    return value;
}

// Only user channels move: 1000+ are the built-in ones (BG, ground, player...)
// and shifting them would repaint the level.
std::string shiftColor(std::string_view token, int delta) {
    int id = toInt(token, 0);
    if (delta == 0 || id < 1 || id > 999) return std::string(token);
    int shifted = id + delta;
    if (shifted < 1 || shifted > 999) return std::string(token);
    return std::to_string(shifted);
}

std::string shiftPlain(std::string_view token, int delta, int lo, int hi) {
    int id = toInt(token, 0);
    if (delta == 0) return std::string(token);
    int shifted = id + delta;
    if (shifted < lo || shifted > hi) return std::string(token);
    return std::to_string(shifted);
}

std::string shiftGroups(std::string_view token, int delta) {
    if (delta == 0) return std::string(token);
    std::string out;
    out.reserve(token.size() + 8);
    size_t start = 0;
    for (size_t i = 0; i <= token.size(); ++i) {
        if (i == token.size() || token[i] == '.') {
            if (i > start) {
                int id = toInt(token.substr(start, i - start), 0);
                int shifted = (id >= 1 && id <= 9999) ? id + delta : id;
                if (shifted < 1 || shifted > 9999) shifted = id;
                if (!out.empty()) out += '.';
                out += std::to_string(shifted);
            }
            start = i + 1;
        }
    }
    return out.empty() ? std::string(token) : out;
}

} // namespace

std::string retarget(std::string const& save, float x, float y, IdShift const& shift) {
    auto tokens = tokenize(save);
    std::string out;
    out.reserve(save.size() + 32);

    bool wroteX = false;
    bool wroteY = false;
    bool wroteGroups = false;
    for (size_t i = 0; i + 1 < tokens.size(); i += 2) {
        int key = toInt(tokens[i], -1);
        std::string value;
        switch (key) {
            case kKeyX:            value = fmt::format("{:.3f}", x); wroteX = true; break;
            case kKeyY:            value = fmt::format("{:.3f}", y); wroteY = true; break;
            case kKeyMainColor:
            case kKeyDetailColor:  value = shiftColor(tokens[i + 1], shift.colors); break;
            case kKeyGroups:
                value = shiftGroups(tokens[i + 1], shift.groups);
                if (shift.addGroup > 0) value += fmt::format(".{}", shift.addGroup);
                wroteGroups = true;
                break;
            case kKeyEditorLayer:
            case kKeyEditorLayer2: value = shiftPlain(tokens[i + 1], shift.layers, 0, 9999); break;
            case kKeyZOrder:       value = shiftPlain(tokens[i + 1], shift.zOrder, -999, 999); break;
            default:               value.assign(tokens[i + 1]); break;
        }
        if (!out.empty()) out += ',';
        out.append(tokens[i].data(), tokens[i].size());
        out += ',';
        out += value;
    }

    if (!wroteX) out += fmt::format(",{},{:.3f}", kKeyX, x);
    if (!wroteY) out += fmt::format(",{},{:.3f}", kKeyY, y);
    if (!wroteGroups && shift.addGroup > 0) {
        out += fmt::format(",{},{}", kKeyGroups, shift.addGroup);
    }
    out += ';';
    return out;
}

std::string shapeKey(std::string const& save) {
    auto tokens = tokenize(save);
    std::string out;
    out.reserve(save.size());
    for (size_t i = 0; i + 1 < tokens.size(); i += 2) {
        int key = toInt(tokens[i], -1);
        if (key == kKeyX || key == kKeyY) continue;
        if (!out.empty()) out += ',';
        out.append(tokens[i].data(), tokens[i].size());
        out += ',';
        out.append(tokens[i + 1].data(), tokens[i + 1].size());
    }
    return out;
}

int objectIdOf(std::string const& save) {
    auto tokens = tokenize(save);
    for (size_t i = 0; i + 1 < tokens.size(); i += 2) {
        if (toInt(tokens[i], -1) == kKeyId) return toInt(tokens[i + 1], 0);
    }
    return 0;
}

bool positionOf(std::string const& save, float& x, float& y) {
    auto tokens = tokenize(save);
    bool gotX = false;
    bool gotY = false;
    for (size_t i = 0; i + 1 < tokens.size(); i += 2) {
        int key = toInt(tokens[i], -1);
        if (key == kKeyX) { x = toFloat(tokens[i + 1], 0.f); gotX = true; }
        else if (key == kKeyY) { y = toFloat(tokens[i + 1], 0.f); gotY = true; }
    }
    return gotX && gotY;
}

void collectColorIds(std::string const& save, std::set<int>& out) {
    auto tokens = tokenize(save);
    for (size_t i = 0; i + 1 < tokens.size(); i += 2) {
        int key = toInt(tokens[i], -1);
        if (key != kKeyMainColor && key != kKeyDetailColor) continue;
        int id = toInt(tokens[i + 1], 0);
        if (id > 0) out.insert(id);
    }
}

} // namespace paimon::autobuild
