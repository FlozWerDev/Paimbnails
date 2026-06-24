#include "PlistBuilder.hpp"

#include <Geode/utils/file.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

// Format helpers

// Format an int as decimal — wrapped because we want to be explicit and
// future-proof against the day someone passes a signed value we should clamp.
std::string formatInt(int v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", v);
    return std::string(buf);
}

// Match PackGen's {x,y} style: no trailing zeros.
//   Number.isInteger(rounded) ? rounded : trim(rounded.toFixed(2))
// GD itself emits 1-decimal forms like "{-58.5,58.5}" and 0-decimal forms
// like "{-79,79}". We replicate by:
//   1. Rounding to 2 decimal places.
//   2. Emitting integer form if the result is integer-valued.
//   3. Otherwise emitting %.2f and trimming trailing zeros (and the dot
//      if no decimals remain).
std::string formatNumber(float v) {
    // Round to 2 decimals first.
    float rounded = std::round(v * 100.0f) / 100.0f;
    if (rounded == std::floor(rounded)) {
        // Integer-valued: emit without decimals.
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(rounded));
        return std::string(buf);
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", rounded);
    std::string s(buf);
    // Trim trailing zeros after the decimal point (e.g. "1.50" → "1.5").
    if (auto dot = s.find('.'); dot != std::string::npos) {
        std::size_t lastNonZero = s.find_last_not_of('0');
        if (lastNonZero != std::string::npos) {
            // Cut the trailing zeros.
            s.erase(lastNonZero + 1);
            // If the dot itself is left at the end, remove it.
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

// XML-escape five entities. We only need the bare minimum because plist
// content is ASCII-safe in practice (sprite names, integers, numeric tuples).
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

// XML writer with indentation tracking

class PlistWriter {
public:
    std::string take() { return std::move(m_out); }

    void writeProlog() {
        // Standard plist 1.0 header.
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

    // Self-closing empty array. GD's plists use this form for the (very
    // common) empty `aliases` arrays; matching it avoids a diff against the
    // original sheets.
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
        for (int i = 0; i < m_depth; ++i) m_out += "    ";  // 4 spaces, matches PackGen
    }
    void line(std::string_view s) {
        indent();
        m_out += s;
        m_out += '\n';
    }
};

}  // anonymous namespace

// Public API

geode::Result<std::string> PlistBuilder::buildString(ParsedSpritesheet const& sheet) {
    if (sheet.metadata.sizeW <= 0 || sheet.metadata.sizeH <= 0) {
        return Err("PlistBuilder: invalid metadata.size {}x{}",
            sheet.metadata.sizeW, sheet.metadata.sizeH);
    }

    PlistWriter w;
    w.writeProlog();
    w.openDict();

    // <key>frames</key>
    w.key("frames");
    w.openDict();

    // Frames are emitted in insertion order — we rely on the upstream
    // packer/extractor to put them in a deterministic sequence (matching
    // PackGen's "tallest first" sort). Re-sorting here would invalidate
    // the rect data we computed.
    for (auto const& f : sheet.frames) {
        w.key(f.name);
        w.openDict();

        // aliases array (always present for round-trip — even if empty).
        // GD emits the empty case as <array/> (self-closing); we match
        // that for byte-level compat with the original sheets.
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

        // spriteOffset
        w.key("spriteOffset");
        w.valueString(formatBracedTuple(f.offsetX, f.offsetY));

        // spriteSize  (un-rotated logical size)
        w.key("spriteSize");
        w.valueString(formatBracedTuple(static_cast<float>(f.spriteW),
                                        static_cast<float>(f.spriteH)));

        // spriteSourceSize  (full size before trim)
        w.key("spriteSourceSize");
        w.valueString(formatBracedTuple(static_cast<float>(f.sourceW),
                                        static_cast<float>(f.sourceH)));

        // textureRect  (where it lives in the atlas; rotated frames record
        // the rotated footprint here)
        w.key("textureRect");
        w.valueString(formatRect(f.rectX, f.rectY, f.rectW, f.rectH));

        // textureRotated
        w.key("textureRotated");
        w.valueBool(f.rotated);

        w.closeDict();  // close per-frame dict
    }

    w.closeDict();  // close <frames> dict

    // <key>metadata</key>
    w.key("metadata");
    w.openDict();

    w.key("format");
    w.valueInt(3);  // we always write format 3

    w.key("pixelFormat");
    // GD always uses RGBA8888 as the runtime pixelFormat declaration.
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

    w.closeDict();  // close metadata

    w.closeDict();  // close root dict
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
