#include "IconProject.hpp"
#include "IconAnatomy.hpp"

#include <algorithm>
#include <random>

using namespace geode::prelude;
using namespace paimon::icon_maker;
namespace ts = paimon::texture_studio;

namespace {

matjson::Value color4ToJson(cocos2d::ccColor4B c) {
    auto arr = matjson::Value::array();
    arr.push(static_cast<int>(c.r));
    arr.push(static_cast<int>(c.g));
    arr.push(static_cast<int>(c.b));
    arr.push(static_cast<int>(c.a));
    return arr;
}

cocos2d::ccColor4B color4FromJson(matjson::Value const& v, cocos2d::ccColor4B fallback) {
    auto arr = v.asArray();
    if (arr.isErr()) return fallback;
    auto items = arr.unwrap();
    if (items.size() < 3) return fallback;
    auto comp = [&](std::size_t i, std::uint8_t fb) {
        if (i >= items.size()) return fb;
        return static_cast<std::uint8_t>(
            std::clamp<int>(static_cast<int>(items[i].asInt().unwrapOr(fb)), 0, 255));
    };
    return {comp(0, fallback.r), comp(1, fallback.g), comp(2, fallback.b), comp(3, 255)};
}

float clampedF(matjson::Value const& v, float fallback, float lo, float hi) {
    return std::clamp(static_cast<float>(v.asDouble().unwrapOr(fallback)), lo, hi);
}

matjson::Value gradientToJson(GradientSpec const& g) {
    auto obj = matjson::Value::object();
    obj["kind"]    = static_cast<int>(g.kind);
    obj["angle"]   = g.angleDeg;
    obj["cx"]      = g.centerX;
    obj["cy"]      = g.centerY;
    obj["radius"]  = g.radius;
    auto stops = matjson::Value::array();
    for (auto const& s : g.stops) {
        auto sObj = matjson::Value::object();
        sObj["pos"]   = s.pos;
        sObj["color"] = color4ToJson(s.color);
        stops.push(sObj);
    }
    obj["stops"] = stops;
    return obj;
}

GradientSpec gradientFromJson(matjson::Value const& v) {
    GradientSpec g;
    int kind = static_cast<int>(v["kind"].asInt().unwrapOr(0));
    g.kind = kind == 1 ? GradientKind::Radial : GradientKind::Linear;
    g.angleDeg = clampedF(v["angle"], g.angleDeg, -360.f, 360.f);
    g.centerX  = clampedF(v["cx"], g.centerX, -1.f, 2.f);
    g.centerY  = clampedF(v["cy"], g.centerY, -1.f, 2.f);
    g.radius   = clampedF(v["radius"], g.radius, 0.01f, 4.f);
    if (auto arr = v["stops"].asArray(); arr.isOk() && !arr.unwrap().empty()) {
        g.stops.clear();
        for (auto const& sVal : arr.unwrap()) {
            GradientStop s;
            s.pos   = clampedF(sVal["pos"], 0.f, 0.f, 1.f);
            s.color = color4FromJson(sVal["color"], s.color);
            g.stops.push_back(s);
        }
        std::sort(g.stops.begin(), g.stops.end(),
            [](GradientStop const& a, GradientStop const& b) { return a.pos < b.pos; });
    }
    return g;
}

matjson::Value imageFillToJson(ImageFillSpec const& im) {
    auto obj = matjson::Value::object();
    obj["file"]    = im.file;
    obj["fit"]     = static_cast<int>(im.fit);
    obj["scale"]   = im.scale;
    obj["ox"]      = im.offsetX;
    obj["oy"]      = im.offsetY;
    obj["rot"]     = im.rotationDeg;
    obj["opacity"] = im.opacity;
    return obj;
}

ImageFillSpec imageFillFromJson(matjson::Value const& v) {
    ImageFillSpec im;
    im.file = v["file"].asString().unwrapOr("");
    int fit = static_cast<int>(v["fit"].asInt().unwrapOr(1));
    im.fit = fit >= 0 && fit <= 3 ? static_cast<FillFitMode>(fit) : FillFitMode::Fill;
    im.scale       = clampedF(v["scale"], 1.f, 0.05f, 20.f);
    im.offsetX     = clampedF(v["ox"], 0.f, -1.f, 1.f);
    im.offsetY     = clampedF(v["oy"], 0.f, -1.f, 1.f);
    im.rotationDeg = clampedF(v["rot"], 0.f, -360.f, 360.f);
    im.opacity     = std::clamp(static_cast<int>(v["opacity"].asInt().unwrapOr(255)), 0, 255);
    return im;
}

matjson::Value outlineToJson(OutlineSpec const& o) {
    auto obj = matjson::Value::object();
    obj["on"]    = o.enabled;
    obj["width"] = o.width;
    obj["color"] = color4ToJson(o.color);
    return obj;
}

OutlineSpec outlineFromJson(matjson::Value const& v) {
    OutlineSpec o;
    o.enabled = v["on"].asBool().unwrapOr(false);
    o.width   = clampedF(v["width"], o.width, 0.f, 40.f);
    o.color   = color4FromJson(v["color"], o.color);
    return o;
}

matjson::Value fillToJson(FillSpec const& f) {
    auto obj = matjson::Value::object();
    obj["type"]     = static_cast<int>(f.type);
    obj["keepLuma"] = f.keepLuminance;
    obj["chroma"]   = f.chroma;
    obj["flat"]     = color4ToJson(f.flat);
    obj["gradient"] = gradientToJson(f.gradient);
    obj["image"]    = imageFillToJson(f.image);
    obj["outline"]  = outlineToJson(f.outline);
    return obj;
}

FillSpec fillFromJson(matjson::Value const& v) {
    FillSpec f;
    int type = static_cast<int>(v["type"].asInt().unwrapOr(0));
    f.type = type >= 0 && type <= 2 ? static_cast<FillType>(type) : FillType::Flat;
    f.keepLuminance = v["keepLuma"].asBool().unwrapOr(true);
    f.chroma        = v["chroma"].asBool().unwrapOr(false);
    f.flat          = color4FromJson(v["flat"], f.flat);
    f.gradient      = gradientFromJson(v["gradient"]);
    f.image         = imageFillFromJson(v["image"]);
    f.outline       = outlineFromJson(v["outline"]);
    return f;
}

matjson::Value transformToJson(ts::ImageTransform const& t) {
    auto obj = matjson::Value::object();
    obj["fit"]     = static_cast<int>(t.fitMode);
    obj["scale"]   = t.scale;
    obj["ox"]      = t.offsetX;
    obj["oy"]      = t.offsetY;
    obj["rot"]     = t.rotationDeg;
    obj["opacity"] = t.opacity;
    obj["flipX"]   = t.flipX;
    obj["flipY"]   = t.flipY;
    return obj;
}

ts::ImageTransform transformFromJson(matjson::Value const& v) {
    ts::ImageTransform t;
    int fit = static_cast<int>(v["fit"].asInt().unwrapOr(0));
    t.fitMode = fit >= 0 && fit <= 2 ? static_cast<ts::ImageFitMode>(fit) : ts::ImageFitMode::Fit;
    t.scale       = clampedF(v["scale"], 1.f, 0.05f, 20.f);
    t.offsetX     = clampedF(v["ox"], 0.f, -1.f, 1.f);
    t.offsetY     = clampedF(v["oy"], 0.f, -1.f, 1.f);
    t.rotationDeg = clampedF(v["rot"], 0.f, -360.f, 360.f);
    t.opacity     = std::clamp(static_cast<int>(v["opacity"].asInt().unwrapOr(255)), 0, 255);
    t.flipX       = v["flipX"].asBool().unwrapOr(false);
    t.flipY       = v["flipY"].asBool().unwrapOr(false);
    return t;
}

matjson::Value shapeToJson(PieceShape const& s) {
    auto obj = matjson::Value::object();
    obj["kind"]     = static_cast<int>(s.kind);
    obj["file"]     = s.file;
    obj["tplIcon"]  = s.templateIconId;
    obj["tplFrame"] = s.templateFrameSuffix;
    return obj;
}

PieceShape shapeFromJson(matjson::Value const& v) {
    PieceShape s;
    int kind = static_cast<int>(v["kind"].asInt().unwrapOr(0));
    s.kind = kind == 1 ? PieceShape::Kind::Template : PieceShape::Kind::Import;
    s.file              = v["file"].asString().unwrapOr("");
    s.templateIconId    = std::clamp(
        static_cast<int>(v["tplIcon"].asInt().unwrapOr(1)), 1, 1000);
    s.templateFrameSuffix = v["tplFrame"].asString().unwrapOr("");
    return s;
}

matjson::Value pieceToJson(IconPiece const& p) {
    auto obj = matjson::Value::object();
    obj["id"]        = p.id;
    obj["name"]      = p.name;
    obj["visible"]   = p.visible;
    obj["transform"] = transformToJson(p.transform);
    obj["shape"]     = shapeToJson(p.shape);
    obj["fill"]      = fillToJson(p.fill);
    return obj;
}

IconPiece pieceFromJson(matjson::Value const& v) {
    IconPiece p;
    p.id        = v["id"].asString().unwrapOr("");
    p.name      = v["name"].asString().unwrapOr("Capa");
    p.visible   = v["visible"].asBool().unwrapOr(true);
    p.transform = transformFromJson(v["transform"]);
    p.shape     = shapeFromJson(v["shape"]);
    p.fill      = fillFromJson(v["fill"]);
    return p;
}

IconType iconTypeFromInt(int raw) {
    for (IconType t : supportedTypes()) {
        if (static_cast<int>(t) == raw) return t;
    }
    return IconType::Cube;
}

}  // anonymous namespace

namespace paimon::icon_maker {

std::string IconProject::makePieceId() const {
    static std::mt19937 rng{std::random_device{}()};
    static constexpr char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (int attempt = 0; attempt < 64; ++attempt) {
        std::string candidate(4, 'a');
        for (auto& c : candidate) c = kChars[rng() % (sizeof(kChars) - 1)];
        bool taken = false;
        for (auto const& [key, slot] : slots) {
            for (auto const& piece : slot.pieces) {
                if (piece.id == candidate) { taken = true; break; }
            }
            if (taken) break;
        }
        if (!taken) return candidate;
    }
    return std::to_string(nowUnixMs());
}

}  // namespace paimon::icon_maker

matjson::Value matjson::Serialize<IconProject>::toJson(IconProject const& project) {
    auto obj = matjson::Value::object();
    obj["schema"]       = project.schemaVersion;
    obj["id"]           = project.id;
    obj["name"]         = project.name;
    obj["type"]         = static_cast<int>(project.type);
    obj["createdAt"]    = project.createdAt;
    obj["modifiedAt"]   = project.modifiedAt;
    obj["hasBuiltOnce"] = project.hasBuiltOnce;
    obj["lastBuiltAt"]  = project.lastBuiltAt;
    obj["exactColors"]  = project.exactColors;

    auto slots = matjson::Value::object();
    for (auto const& [key, content] : project.slots) {
        auto pieces = matjson::Value::array();
        for (auto const& piece : content.pieces) pieces.push(pieceToJson(piece));
        auto slotObj = matjson::Value::object();
        slotObj["pieces"] = pieces;
        slots[key] = slotObj;
    }
    obj["slots"] = slots;
    return obj;
}

geode::Result<IconProject> matjson::Serialize<IconProject>::fromJson(matjson::Value const& v) {
    IconProject project;
    project.schemaVersion = static_cast<int>(v["schema"].asInt().unwrapOr(1));
    project.id            = v["id"].asString().unwrapOr("");
    project.name          = v["name"].asString().unwrapOr("");
    project.type          = iconTypeFromInt(static_cast<int>(v["type"].asInt().unwrapOr(0)));
    project.createdAt     = v["createdAt"].asInt().unwrapOr(0);
    project.modifiedAt    = v["modifiedAt"].asInt().unwrapOr(0);
    project.hasBuiltOnce  = v["hasBuiltOnce"].asBool().unwrapOr(false);
    project.lastBuiltAt   = v["lastBuiltAt"].asInt().unwrapOr(0);
    project.exactColors   = v["exactColors"].asBool().unwrapOr(true);

    if (project.id.empty()) {
        return geode::Err("project.json sin id");
    }

    if (v["slots"].isObject()) {
        for (auto const& slotVal : v["slots"]) {
            auto key = slotVal.getKey();
            if (!key || key->empty()) continue;
            IconSlotContent content;
            if (auto arr = slotVal["pieces"].asArray(); arr.isOk()) {
                for (auto const& pieceVal : arr.unwrap()) {
                    auto piece = pieceFromJson(pieceVal);
                    if (!piece.id.empty()) content.pieces.push_back(std::move(piece));
                }
            }
            project.slots[*key] = std::move(content);
        }
    }
    return geode::Ok(std::move(project));
}
