#include "PlistBuilder.hpp"

#include <Geode/utils/file.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

std::string formatInt(int v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", v);
    return std::string(buf);
}

// Emit numbers in PackGen/GD style (no trailing zeros) for byte-compatible output.
std::string formatNumber(float v) {
    float rounded = std::round(v * 100.0f) / 100.0f;
    if (rounded == std::floor(rounded)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(rounded));
        return std::string(buf);
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", rounded);
    std::string s(buf);
    if (auto dot = s.find('.'); dot != std::string::npos) {
        std::size_t lastNonZero = s.find_last_not_of('0');
        if (lastNonZero != std::string::npos) {
            s.erase(lastNonZero + 1);
            if (!s.empty() && s.back() == '.') s.pop_back();
        }
    }
    return s;
}

std::string formatBracedTuple(float a, float b) {
    return "{" + formatNumber(a) + "," + formatNumber(b) + "}";
}

std::string formatRect(int x, int y, int w, int h) {
    return "{{" + formatInt(x) + "," + formatInt(y) + "},{"
                + formatInt(w) + "," + formatInt(h) + "}}";
}

std::string xmlEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '&':  out += "&amp;";  break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;        break;
        }
    }
    return out;
}

class PlistWriter {
public:
    std::string take() { return std::move(m_out); }

    void writeProlog() {
        m_out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        m_out += "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\""
                 " \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
        m_out += "<plist version=\"1.0\">\n";
    }

    void writeEpilog() {
        m_out += "</plist>\n";
    }

    void openDict() { line("<dict>"); ++m_depth; }
    void closeDict() { --m_depth; line("</dict>"); }
    void openArray() { line("<array>"); ++m_depth; }
    void closeArray() { --m_depth; line("</array>"); }

    // GD emits empty arrays self-closing; match for byte-compat.
    void emptyArray() { line("<array/>"); }

    void key(std::string_view k) {
        line("<key>" + xmlEscape(k) + "</key>");
    }

    void valueString(std::string_view v) {
        line("<string>" + xmlEscape(v) + "</string>");
    }
    void valueInt(int v)  { line("<integer>" + formatInt(v) + "</integer>"); }
    void valueBool(bool b) { line(b ? "<true/>" : "<false/>"); }

private:
    std::string m_out;
    int m_depth = 0;

    void indent() {
        for (int i = 0; i < m_depth; ++i) m_out += "    ";
    }
    void line(std::string_view s) {
        indent();
        m_out += s;
        m_out += '\n';
    }
};

}  // anonymous namespace

geode::Result<std::string> PlistBuilder::buildString(ParsedSpritesheet const& sheet) {
    if (sheet.metadata.sizeW <= 0 || sheet.metadata.sizeH <= 0) {
        return Err("PlistBuilder: invalid metadata.size {}x{}",
            sheet.metadata.sizeW, sheet.metadata.sizeH);
    }

    PlistWriter w;
    w.writeProlog();
    w.openDict();

    w.key("frames");
    w.openDict();

    // Emit in insertion order; re-sorting would invalidate the computed rects.
    for (auto const& f : sheet.frames) {
        w.key(f.name);
        w.openDict();

        w.key("aliases");
        if (f.aliases.empty()) {
            w.emptyArray();
        } else {
            w.openArray();
            for (auto const& a : f.aliases) {
                w.valueString(a);
            }
            w.closeArray();
        }

        w.key("spriteOffset");
        w.valueString(formatBracedTuple(f.offsetX, f.offsetY));

        w.key("spriteSize");
        w.valueString(formatBracedTuple(static_cast<float>(f.spriteW),
                                        static_cast<float>(f.spriteH)));

        w.key("spriteSourceSize");
        w.valueString(formatBracedTuple(static_cast<float>(f.sourceW),
                                        static_cast<float>(f.sourceH)));

        w.key("textureRect");
        w.valueString(formatRect(f.rectX, f.rectY, f.rectW, f.rectH));

        w.key("textureRotated");
        w.valueBool(f.rotated);

        w.closeDict();
    }

    w.closeDict();

    w.key("metadata");
    w.openDict();

    w.key("format");
    w.valueInt(3);

    w.key("pixelFormat");
    w.valueString("RGBA8888");

    w.key("premultiplyAlpha");
    w.valueBool(sheet.metadata.premultiplyAlpha);

    w.key("realTextureFileName");
    w.valueString(sheet.metadata.realTextureFileName);

    w.key("size");
    w.valueString(formatBracedTuple(static_cast<float>(sheet.metadata.sizeW),
                                    static_cast<float>(sheet.metadata.sizeH)));

    if (!sheet.metadata.smartUpdate.empty()) {
        w.key("smartupdate");
        w.valueString(sheet.metadata.smartUpdate);
    }

    w.key("textureFileName");
    w.valueString(sheet.metadata.textureFileName);

    w.closeDict();

    w.closeDict();
    w.writeEpilog();

    return Ok(w.take());
}

geode::Result<> PlistBuilder::buildFile(ParsedSpritesheet const& sheet,
                                       std::filesystem::path const& path) {
    auto content = buildString(sheet);
    if (!content) return Err(content.unwrapErr());

    auto res = file::writeString(path, content.unwrap());
    if (!res) {
        return Err("PlistBuilder::buildFile: write failed: {}", res.unwrapErr());
    }
    return Ok();
}

}  // namespace paimon::texture_studio
