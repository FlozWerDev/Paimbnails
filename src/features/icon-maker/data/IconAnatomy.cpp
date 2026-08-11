#include "IconAnatomy.hpp"

#include <fmt/format.h>

#include <cctype>

namespace paimon::icon_maker {

namespace {

// Zone wording is aimed at someone who has never opened an icon editor: what
// the zone *is* on screen first, what GD calls it second.
constexpr SlotDef kMain      {"main",      "_001",
    "Cuerpo",  "El relleno grande. Toma el Color 1 del jugador.", {96, 170, 255}};
constexpr SlotDef kSecondary {"secondary", "_2_001",
    "Detalle", "Las partes que llevan el Color 2 del jugador.",   {255, 146, 92}};
constexpr SlotDef kTertiary  {"tertiary",  "_3_001",
    "Cupula",  "Solo en UFO: el cristal de arriba.",              {168, 128, 255}};
constexpr SlotDef kGlow      {"glow",      "_glow_001",
    "Brillo",  "El contorno que se enciende alrededor del icono.", {118, 240, 168}};
constexpr SlotDef kExtra     {"extra",     "_extra_001",
    "Blanco",  "Detalles que siempre van blancos (ojos, brillos).", {228, 232, 240}, true};

std::vector<SlotDef> simpleSlots(bool ufo) {
    std::vector<SlotDef> slots{kMain, kSecondary};
    if (ufo) slots.push_back(kTertiary);
    slots.push_back(kGlow);
    slots.push_back(kExtra);
    return slots;
}

// Robot/spider parts: main/secondary/glow per part; "extra" exists only on
// part 1 (matches vanilla sheets and MoreIcons' required-frame list).
std::vector<SlotDef> partSlots() {
    return {kMain, kSecondary, kGlow, kExtra};
}

std::vector<AnatomyDef> buildDefs() {
    std::vector<AnatomyDef> defs;
    defs.push_back({IconType::Cube,    "player_",      "icon",    "Cubo",    1, simpleSlots(false)});
    defs.push_back({IconType::Ship,    "ship_",        "ship",    "Nave",    1, simpleSlots(false)});
    defs.push_back({IconType::Ball,    "player_ball_", "ball",    "Bola",    1, simpleSlots(false)});
    defs.push_back({IconType::Ufo,     "bird_",        "ufo",     "UFO",     1, simpleSlots(true)});
    defs.push_back({IconType::Wave,    "dart_",        "wave",    "Wave",    1, simpleSlots(false)});
    defs.push_back({IconType::Robot,   "robot_",       "robot",   "Robot",   4, partSlots()});
    defs.push_back({IconType::Spider,  "spider_",      "spider",  "Spider",  4, partSlots()});
    defs.push_back({IconType::Swing,   "swing_",       "swing",   "Swing",   1, simpleSlots(false)});
    defs.push_back({IconType::Jetpack, "jetpack_",     "jetpack", "Jetpack", 1, simpleSlots(false)});
    return defs;
}

std::vector<AnatomyDef> const& allDefs() {
    static std::vector<AnatomyDef> defs = buildDefs();
    return defs;
}

}  // anonymous namespace

AnatomyDef const* anatomyFor(IconType type) {
    for (auto const& def : allDefs()) {
        if (def.type == type) return &def;
    }
    return nullptr;
}

std::vector<IconType> const& supportedTypes() {
    static std::vector<IconType> types = [] {
        std::vector<IconType> out;
        for (auto const& def : allDefs()) out.push_back(def.type);
        return out;
    }();
    return types;
}

std::string_view partLabel(IconType type, int part) {
    auto const* def = anatomyFor(type);
    if (!def || def->partCount <= 1 || part < 1 || part > def->partCount) return {};
    static constexpr std::string_view kNames[] = {"Parte 1", "Parte 2", "Parte 3", "Parte 4"};
    return kNames[part - 1];
}

// part == 0 for single-part modes; 1..4 for robot/spider.
std::string slotStorageKey(int part, std::string_view slotKey) {
    if (part <= 0) return std::string(slotKey);
    return fmt::format("p{}.{}", part, slotKey);
}

std::string frameName(std::string_view exportName, IconType type, int part,
                      std::string_view slotKey) {
    auto const* def = anatomyFor(type);
    if (!def) return {};

    std::string_view suffix;
    for (auto const& slot : def->slots) {
        if (slot.key == slotKey) { suffix = slot.suffix; break; }
    }
    if (suffix.empty()) return {};

    if (def->partCount > 1) {
        // "_glow_001" -> "name_02_glow_001.png"; "_001" -> "name_02_001.png"
        return fmt::format("{}_{:02}{}.png", exportName, part, suffix);
    }
    return fmt::format("{}{}.png", exportName, suffix);
}

bool slotForSuffix(IconType type, std::string_view fullSuffix,
                   int& outPart, std::string& outSlotKey) {
    auto const* def = anatomyFor(type);
    if (!def) return false;

    std::string_view rest = fullSuffix;
    int part = 0;
    if (def->partCount > 1) {
        // "_02_glow_001" -> part 2, rest "_glow_001".
        if (rest.size() < 4 || rest[0] != '_' ||
            !std::isdigit(static_cast<unsigned char>(rest[1])) ||
            !std::isdigit(static_cast<unsigned char>(rest[2]))) {
            return false;
        }
        part = (rest[1] - '0') * 10 + (rest[2] - '0');
        if (part < 1 || part > def->partCount) return false;
        rest.remove_prefix(3);
    }

    for (auto const& slot : def->slots) {
        if (slot.suffix == rest) {
            outPart = part;
            outSlotKey = std::string(slot.key);
            return true;
        }
    }
    return false;
}

}  // namespace paimon::icon_maker
