#include "PlistParser.hpp"

#include <Geode/utils/file.hpp>

#include <cctype>
#include <charconv>
#include <cmath>
#include <unordered_map>
#include <utility>
#include <variant>

using namespace geode::prelude;

namespace paimon::texture_studio {
namespace {

struct Value;
using Dict  = std::vector<std::pair<std::string, Value>>;
using Array = std::vector<Value>;

struct Value {
    std::variant<std::monostate, std::string, bool, int, Dict, Array> v;

    bool isString() const { return std::holds_alternative<std::string>(v); }
    bool isInt()    const { return std::holds_alternative<int>(v);         }
    bool isBool()   const { return std::holds_alternative<bool>(v);        }
    bool isDict()   const { return std::holds_alternative<Dict>(v);        }
    bool isArray()  const { return std::holds_alternative<Array>(v);       }

    std::string const& asString() const { return std::get<std::string>(v); }
    int                asInt()    const { return std::get<int>(v);         }
    bool               asBool()   const { return std::get<bool>(v);        }
    Dict const&        asDict()   const { return std::get<Dict>(v);        }
    Array const&       asArray()  const { return std::get<Array>(v);       }

    Value const* lookup(std::string_view key) const {
        if (!isDict()) return nullptr;
        for (auto const& [k, val] : asDict()) {
            if (k == key) return &val;
        }
        return nullptr;
    }
};

class Tokenizer {
public:
    explicit Tokenizer(std::string_view xml) : m_xml(xml) {}

    geode::Result<Value> parse() {
        skipPrologueAndDoctype();
        if (!skipToTag("plist")) {
            return Err("PlistParser: <plist> root not found");
        }
        skipWhitespace();
        return readValue();
    }

private:
    std::string_view m_xml;
    std::size_t m_pos = 0;
    std::size_t m_depth = 0;
    static constexpr std::size_t kMaxDepth = 200;

    bool eof() const { return m_pos >= m_xml.size(); }

    char peek(std::size_t off = 0) const {
        return (m_pos + off < m_xml.size()) ? m_xml[m_pos + off] : '\0';
    }

    void advance(std::size_t n = 1) { m_pos = std::min(m_pos + n, m_xml.size()); }

    void skipWhitespace() {
        while (!eof() && std::isspace(static_cast<unsigned char>(peek()))) advance();
    }

    void skipPrologueAndDoctype() {
        while (!eof()) {
            skipWhitespace();
            if (eof()) return;
            if (peek() != '<') return;
            if (m_pos + 1 < m_xml.size() && peek(1) == '?') {
                auto end = m_xml.find("?>", m_pos);
                if (end == std::string_view::npos) { m_pos = m_xml.size(); return; }
                m_pos = end + 2;
                continue;
            }
            if (m_pos + 1 < m_xml.size() && peek(1) == '!') {
                if (m_xml.compare(m_pos, 4, "<!--") == 0) {
                    auto end = m_xml.find("-->", m_pos);
                    if (end == std::string_view::npos) { m_pos = m_xml.size(); return; }
                    m_pos = end + 3;
                    continue;
                }
                int depth = 0;
                while (!eof()) {
                    if (peek() == '<') depth++;
                    else if (peek() == '>') {
                        depth--;
                        advance();
                        if (depth <= 0) break;
                        continue;
                    }
                    advance();
                }
                continue;
            }
            return;
        }
    }

    bool skipToTag(std::string_view tag) {
        std::string opener = "<" + std::string(tag) + ">";
        std::string self_close = "<" + std::string(tag) + "/>";
        std::string with_attr = "<" + std::string(tag) + " ";
        auto pos = m_xml.find(opener, m_pos);
        auto pos_attr = m_xml.find(with_attr, m_pos);
        auto pos_self = m_xml.find(self_close, m_pos);
        std::size_t chosen = std::string_view::npos;
        if (pos != std::string_view::npos) chosen = pos;
        if (pos_attr != std::string_view::npos && (chosen == std::string_view::npos || pos_attr < chosen)) chosen = pos_attr;
        if (pos_self != std::string_view::npos && (chosen == std::string_view::npos || pos_self < chosen)) chosen = pos_self;
        if (chosen == std::string_view::npos) return false;
        auto closeAngle = m_xml.find('>', chosen);
        if (closeAngle == std::string_view::npos) return false;
        m_pos = closeAngle + 1;
        return true;
    }

    struct Tag {
        std::string name;
        bool closing      = false;
        bool selfClosing  = false;
    };

    geode::Result<Tag> readTag() {
        skipWhitespace();
        if (eof() || peek() != '<') return Err("PlistParser: expected '<' at offset {}", m_pos);
        advance();
        Tag t;
        if (peek() == '/') { t.closing = true; advance(); }
        std::size_t start = m_pos;
        while (!eof() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) advance();
        t.name.assign(m_xml.data() + start, m_pos - start);
        while (!eof() && peek() != '>' && peek() != '/') advance();
        if (peek() == '/') { t.selfClosing = true; advance(); }
        if (eof() || peek() != '>') return Err("PlistParser: expected '>' for tag <{}>", t.name);
        advance();
        return Ok(std::move(t));
    }

    std::string readUntilTag() {
        std::string out;
        while (!eof() && peek() != '<') {
            char c = peek();
            advance();
            if (c == '&') {
                if (m_xml.compare(m_pos, 3, "lt;") == 0) { out += '<'; m_pos += 3; }
                else if (m_xml.compare(m_pos, 3, "gt;") == 0) { out += '>'; m_pos += 3; }
                else if (m_xml.compare(m_pos, 4, "amp;") == 0) { out += '&'; m_pos += 4; }
                else if (m_xml.compare(m_pos, 5, "apos;") == 0) { out += '\''; m_pos += 5; }
                else if (m_xml.compare(m_pos, 5, "quot;") == 0) { out += '"'; m_pos += 5; }
                else { out += '&'; }
            } else {
                out += c;
            }
        }
        return out;
    }

    geode::Result<Value> readValue() {
        // Bound recursion: readValue -> readDict/readArray -> readValue has no
        // natural depth limit, so a deeply nested (user-selected) plist could
        // overflow the native stack. Guard with an RAII depth counter.
        struct DepthGuard {
            std::size_t& d;
            explicit DepthGuard(std::size_t& d) : d(d) { ++d; }
            ~DepthGuard() { --d; }
        } depthGuard(m_depth);
        if (m_depth > kMaxDepth) {
            return Err("PlistParser: nesting too deep");
        }
        GEODE_UNWRAP_INTO(auto tag, readTag());
        if (tag.closing) {
            return Err("PlistParser: unexpected closing tag </{}>", tag.name);
        }
        if (tag.selfClosing) {
            if (tag.name == "true")   return Ok(Value{true});
            if (tag.name == "false")  return Ok(Value{false});
            if (tag.name == "string") return Ok(Value{std::string{}});
            if (tag.name == "dict")   return Ok(Value{Dict{}});
            if (tag.name == "array")  return Ok(Value{Array{}});
            return Err("PlistParser: unsupported self-closing tag <{}/>", tag.name);
        }

        if (tag.name == "dict")  return readDict();
        if (tag.name == "array") return readArray();

        std::string text = readUntilTag();
        GEODE_UNWRAP_INTO(auto closer, readTag());
        if (!closer.closing || closer.name != tag.name) {
            return Err("PlistParser: expected </{}>, got <{}{}>",
                tag.name, closer.closing ? "/" : "", closer.name);
        }

        if (tag.name == "string") return Ok(Value{std::move(text)});
        if (tag.name == "true")   return Ok(Value{true});
        if (tag.name == "false")  return Ok(Value{false});
        if (tag.name == "integer" || tag.name == "real") {
            auto begin = text.find_first_not_of(" \t\r\n");
            auto end   = text.find_last_not_of(" \t\r\n");
            if (begin == std::string::npos) return Ok(Value{0});
            std::string_view sv(text.data() + begin, end - begin + 1);
            int sign = 1;
            std::size_t i = 0;
            if (i < sv.size() && (sv[i] == '+' || sv[i] == '-')) {
                if (sv[i] == '-') sign = -1;
                ++i;
            }
            // int64 + saturación evita signed overflow (UB) si un plist trae un entero > INT_MAX.
            int64_t acc = 0;
            for (; i < sv.size() && std::isdigit(static_cast<unsigned char>(sv[i])); ++i) {
                acc = acc * 10 + (sv[i] - '0');
                if (acc > 2147483647LL) { acc = 2147483647LL; break; }
            }
            return Ok(Value{static_cast<int>(sign * acc)});
        }
        if (tag.name == "key") return Ok(Value{std::move(text)});

        return Ok(Value{std::move(text)});
    }

    geode::Result<Value> readDict() {
        Dict dict;
        while (true) {
            skipWhitespace();
            if (eof()) return Err("PlistParser: unexpected EOF inside <dict>");
            if (peek() == '<' && m_xml.compare(m_pos, 7, "</dict>") == 0) {
                m_pos += 7;
                return Ok(Value{std::move(dict)});
            }
            GEODE_UNWRAP_INTO(auto keyTag, readTag());
            if (keyTag.closing || keyTag.name != "key") {
                return Err("PlistParser: <dict> expected <key>, got <{}>", keyTag.name);
            }
            std::string key = readUntilTag();
            GEODE_UNWRAP_INTO(auto keyClose, readTag());
            if (!keyClose.closing || keyClose.name != "key") {
                return Err("PlistParser: <key> not closed properly");
            }
            skipWhitespace();
            GEODE_UNWRAP_INTO(auto val, readValue());
            dict.emplace_back(std::move(key), std::move(val));
        }
    }

    geode::Result<Value> readArray() {
        Array arr;
        while (true) {
            skipWhitespace();
            if (eof()) return Err("PlistParser: unexpected EOF inside <array>");
            if (peek() == '<' && m_xml.compare(m_pos, 8, "</array>") == 0) {
                m_pos += 8;
                return Ok(Value{std::move(arr)});
            }
            GEODE_UNWRAP_INTO(auto val, readValue());
            arr.emplace_back(std::move(val));
        }
    }
};

bool parseBracedTuple(std::string_view s, float& outA, float& outB) {
    auto open = s.find('{');
    auto close = s.rfind('}');
    if (open == std::string_view::npos || close == std::string_view::npos || close <= open) return false;
    std::string_view inner = s.substr(open + 1, close - open - 1);
    auto comma = inner.find(',');
    if (comma == std::string_view::npos) return false;
    std::string_view aStr = inner.substr(0, comma);
    std::string_view bStr = inner.substr(comma + 1);

    auto trim = [](std::string_view& v) {
        while (!v.empty() && std::isspace(static_cast<unsigned char>(v.front()))) v.remove_prefix(1);
        while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back())))  v.remove_suffix(1);
    };
    trim(aStr);
    trim(bStr);

    auto parseFloat = [](std::string_view sv, float& out) {
        // strtof (not std::from_chars): float from_chars is unsupported on some target toolchains.
        std::string tmp(sv);
        char* end = nullptr;
        out = std::strtof(tmp.c_str(), &end);
        if (end == tmp.c_str()) return false;
        // Reject non-finite / absurd geometry: callers cast these to int, and
        // converting inf/NaN (or a value outside int range) to int is UB. Real
        // spritesheet coordinates are far within +/-1e6.
        if (!std::isfinite(out) || out < -1000000.0f || out > 1000000.0f) return false;
        return true;
    };
    return parseFloat(aStr, outA) && parseFloat(bStr, outB);
}

bool parseRect(std::string_view s, float& x, float& y, float& w, float& h) {
    auto firstOpen = s.find("{{");
    auto firstClose = s.find("}");
    auto secondOpen = s.find('{', firstClose == std::string_view::npos ? 0 : firstClose);
    auto secondClose = s.rfind("}}");
    if (firstOpen == std::string_view::npos || firstClose == std::string_view::npos
        || secondOpen == std::string_view::npos || secondClose == std::string_view::npos) {
        return false;
    }
    std::string_view a = s.substr(firstOpen + 1, firstClose - firstOpen);
    std::string_view b = s.substr(secondOpen, secondClose - secondOpen + 1);
    return parseBracedTuple(a, x, y) && parseBracedTuple(b, w, h);
}

geode::Result<SpriteFrameInfo> decodeFrameFormat3(std::string const& name, Value const& v) {
    if (!v.isDict()) return Err("frame '{}': value is not a dict", name);
    SpriteFrameInfo f;
    f.name = name;

    auto* rect      = v.lookup("textureRect");
    auto* spriteSz  = v.lookup("spriteSize");
    auto* spriteOff = v.lookup("spriteOffset");
    auto* sourceSz  = v.lookup("spriteSourceSize");
    auto* rotated   = v.lookup("textureRotated");
    auto* aliases   = v.lookup("aliases");

    if (!rect || !rect->isString())
        return Err("frame '{}': missing textureRect", name);

    float rx, ry, rw, rh;
    if (!parseRect(rect->asString(), rx, ry, rw, rh)) {
        return Err("frame '{}': cannot parse textureRect '{}'", name, rect->asString());
    }
    f.rectX = static_cast<int>(rx);
    f.rectY = static_cast<int>(ry);
    f.rectW = static_cast<int>(rw);
    f.rectH = static_cast<int>(rh);

    f.rotated = (rotated && rotated->isBool() && rotated->asBool());

    if (spriteSz && spriteSz->isString()) {
        float a, b;
        if (parseBracedTuple(spriteSz->asString(), a, b)) {
            f.spriteW = static_cast<int>(a);
            f.spriteH = static_cast<int>(b);
        }
    }
    if (f.spriteW <= 0 || f.spriteH <= 0) {
        // In format 3 textureRect already carries the logical size (it mirrors
        // spriteSize); textureRotated only says how the slot is stored.
        f.spriteW = f.rectW;
        f.spriteH = f.rectH;
    }

    if (spriteOff && spriteOff->isString()) {
        float a, b;
        if (parseBracedTuple(spriteOff->asString(), a, b)) {
            f.offsetX = a;
            f.offsetY = b;
        }
    }

    if (sourceSz && sourceSz->isString()) {
        float a, b;
        if (parseBracedTuple(sourceSz->asString(), a, b)) {
            f.sourceW = static_cast<int>(a);
            f.sourceH = static_cast<int>(b);
        }
    }
    if (f.sourceW <= 0 || f.sourceH <= 0) {
        f.sourceW = f.spriteW;
        f.sourceH = f.spriteH;
    }

    if (aliases && aliases->isArray()) {
        for (auto const& a : aliases->asArray()) {
            if (a.isString()) f.aliases.push_back(a.asString());
        }
    }

    return Ok(std::move(f));
}

geode::Result<SpriteFrameInfo> decodeFrameLegacy(std::string const& name, Value const& v) {
    if (!v.isDict()) return Err("frame '{}': value is not a dict", name);
    SpriteFrameInfo f;
    f.name = name;

    auto* frameStr  = v.lookup("frame");
    auto* offsetStr = v.lookup("offset");
    auto* sizeStr   = v.lookup("sourceSize");
    auto* rotated   = v.lookup("rotated");

    if (frameStr && frameStr->isString()) {
        float rx, ry, rw, rh;
        if (!parseRect(frameStr->asString(), rx, ry, rw, rh)) {
            return Err("frame '{}': cannot parse frame '{}'", name, frameStr->asString());
        }
        f.rectX = static_cast<int>(rx);
        f.rectY = static_cast<int>(ry);
        f.rectW = static_cast<int>(rw);
        f.rectH = static_cast<int>(rh);
        f.rotated = (rotated && rotated->isBool() && rotated->asBool());
        // The legacy rect stores the packed footprint; normalise it to the
        // logical size so rectW/rectH mean the same in every format.
        if (f.rotated) std::swap(f.rectW, f.rectH);
        f.spriteW = f.rectW;
        f.spriteH = f.rectH;
        if (offsetStr && offsetStr->isString()) {
            float a, b;
            if (parseBracedTuple(offsetStr->asString(), a, b)) {
                f.offsetX = a;
                f.offsetY = b;
            }
        }
        if (sizeStr && sizeStr->isString()) {
            float a, b;
            if (parseBracedTuple(sizeStr->asString(), a, b)) {
                f.sourceW = static_cast<int>(a);
                f.sourceH = static_cast<int>(b);
            }
        }
        if (f.sourceW <= 0 || f.sourceH <= 0) {
            f.sourceW = f.spriteW;
            f.sourceH = f.spriteH;
        }
        return Ok(std::move(f));
    }

    auto* x  = v.lookup("x");
    auto* y  = v.lookup("y");
    auto* w  = v.lookup("width");
    auto* h  = v.lookup("height");
    auto* ow = v.lookup("originalWidth");
    auto* oh = v.lookup("originalHeight");
    auto* ox = v.lookup("offsetX");
    auto* oy = v.lookup("offsetY");
    if (!x || !y || !w || !h) {
        return Err("frame '{}': missing x/y/width/height (format 0/1)", name);
    }
    f.rectX = x->isInt() ? x->asInt() : 0;
    f.rectY = y->isInt() ? y->asInt() : 0;
    f.rectW = w->isInt() ? w->asInt() : 0;
    f.rectH = h->isInt() ? h->asInt() : 0;
    f.spriteW = f.rectW;
    f.spriteH = f.rectH;
    f.sourceW = (ow && ow->isInt()) ? ow->asInt() : f.spriteW;
    f.sourceH = (oh && oh->isInt()) ? oh->asInt() : f.spriteH;
    f.offsetX = (ox && ox->isInt()) ? static_cast<float>(ox->asInt()) : 0.0f;
    f.offsetY = (oy && oy->isInt()) ? static_cast<float>(oy->asInt()) : 0.0f;
    f.rotated = false;
    return Ok(std::move(f));
}

}  // anonymous namespace

geode::Result<ParsedSpritesheet> PlistParser::parseString(std::string_view xml) {
    Tokenizer tok(xml);
    GEODE_UNWRAP_INTO(auto root, tok.parse());
    if (!root.isDict()) return Err("PlistParser: root <plist> child is not <dict>");

    auto* metaV   = root.lookup("metadata");
    auto* framesV = root.lookup("frames");

    if (!framesV || !framesV->isDict()) {
        return Err("PlistParser: missing or invalid <key>frames</key>");
    }

    int format = 3;
    PlistMetadata meta;
    if (metaV && metaV->isDict()) {
        if (auto* f = metaV->lookup("format"); f && f->isInt()) {
            format = f->asInt();
        }
        meta.format = format;
        if (auto* size = metaV->lookup("size"); size && size->isString()) {
            float a, b;
            if (parseBracedTuple(size->asString(), a, b)) {
                meta.sizeW = static_cast<int>(a);
                meta.sizeH = static_cast<int>(b);
            }
        }
        if (auto* tn = metaV->lookup("textureFileName"); tn && tn->isString()) {
            meta.textureFileName = tn->asString();
        }
        if (auto* rtn = metaV->lookup("realTextureFileName"); rtn && rtn->isString()) {
            meta.realTextureFileName = rtn->asString();
        }
        if (auto* pma = metaV->lookup("premultiplyAlpha"); pma && pma->isBool()) {
            meta.premultiplyAlpha = pma->asBool();
        }
        if (auto* su = metaV->lookup("smartupdate"); su && su->isString()) {
            meta.smartUpdate = su->asString();
        }
    }

    ParsedSpritesheet out;
    out.metadata = meta;
    out.frames.reserve(framesV->asDict().size());

    for (auto const& [name, v] : framesV->asDict()) {
        auto frameRes = (format >= 3)
            ? decodeFrameFormat3(name, v)
            : decodeFrameLegacy(name, v);
        if (!frameRes) {
            return Err("PlistParser: frame '{}' decode failed: {}", name, frameRes.unwrapErr());
        }
        out.frames.push_back(std::move(frameRes).unwrap());
    }

    return Ok(std::move(out));
}

geode::Result<ParsedSpritesheet> PlistParser::parseFile(std::filesystem::path const& path) {
    auto content = file::readString(path);
    if (!content) {
        return Err("PlistParser::parseFile: cannot read {}: {}",
            geode::utils::string::pathToString(path), content.unwrapErr());
    }
    return parseString(content.unwrap());
}

geode::Result<int> PlistParser::sniffFormat(std::string_view xml) {
    auto fmtKey = xml.find("<key>format</key>");
    if (fmtKey == std::string_view::npos) {
        return Ok(0);
    }
    auto intOpen = xml.find("<integer>", fmtKey);
    if (intOpen == std::string_view::npos) return Err("sniffFormat: malformed format tag");
    intOpen += std::string_view("<integer>").size();
    auto intClose = xml.find("</integer>", intOpen);
    if (intClose == std::string_view::npos) return Err("sniffFormat: malformed format tag");
    std::string_view num = xml.substr(intOpen, intClose - intOpen);
    int v = 0;
    for (char c : num) {
        if (std::isdigit(static_cast<unsigned char>(c))) v = v * 10 + (c - '0');
    }
    return Ok(v);
}

}  // namespace paimon::texture_studio
