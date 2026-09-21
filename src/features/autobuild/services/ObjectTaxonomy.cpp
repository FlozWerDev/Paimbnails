#include "ObjectTaxonomy.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <unordered_map>

namespace paimon::autobuild {

namespace {

struct KindEntry {
    int id;
    ObjectKind kind;
};

// Only behaviour-critical ids live here; the rest stays Unknown on purpose:
// a wrong guess in this table hurts more than no guess.
constexpr KindEntry kCurated[] = {
    {8,    ObjectKind::Hazard},   {39,   ObjectKind::Hazard},
    {103,  ObjectKind::Hazard},   {392,  ObjectKind::Hazard},
    {88,   ObjectKind::Hazard},   {89,   ObjectKind::Hazard},
    {98,   ObjectKind::Hazard},   {397,  ObjectKind::Hazard},
    {398,  ObjectKind::Hazard},   {399,  ObjectKind::Hazard},
    {678,  ObjectKind::Hazard},   {679,  ObjectKind::Hazard},
    {680,  ObjectKind::Hazard},   {740,  ObjectKind::Hazard},
    {741,  ObjectKind::Hazard},   {742,  ObjectKind::Hazard},
    {918,  ObjectKind::Hazard},   {1715, ObjectKind::Hazard},
    {1716, ObjectKind::Hazard},   {1717, ObjectKind::Hazard},

    {1,    ObjectKind::Solid},    {2,    ObjectKind::Solid},
    {3,    ObjectKind::Solid},    {4,    ObjectKind::Solid},
    {5,    ObjectKind::Solid},    {6,    ObjectKind::Solid},
    {7,    ObjectKind::Solid},
    {1202, ObjectKind::Solid},    {1203, ObjectKind::Solid},
    {1204, ObjectKind::Solid},    {1205, ObjectKind::Solid},
    {1206, ObjectKind::Solid},    {1207, ObjectKind::Solid},
    {1208, ObjectKind::Solid},    {1209, ObjectKind::Solid},
    {1210, ObjectKind::Solid},

    {40,   ObjectKind::Slope},    {41,   ObjectKind::Slope},
    {42,   ObjectKind::Slope},    {43,   ObjectKind::Slope},
    {44,   ObjectKind::Slope},    {289,  ObjectKind::Slope},
    {290,  ObjectKind::Slope},    {291,  ObjectKind::Slope},
    {292,  ObjectKind::Slope},    {293,  ObjectKind::Slope},

    {10,   ObjectKind::Portal},   {11,   ObjectKind::Portal},
    {12,   ObjectKind::Portal},   {13,   ObjectKind::Portal},
    {45,   ObjectKind::Portal},   {46,   ObjectKind::Portal},
    {47,   ObjectKind::Portal},   {99,   ObjectKind::Portal},
    {101,  ObjectKind::Portal},   {111,  ObjectKind::Portal},
    {200,  ObjectKind::Portal},   {201,  ObjectKind::Portal},
    {202,  ObjectKind::Portal},   {203,  ObjectKind::Portal},
    {286,  ObjectKind::Portal},   {287,  ObjectKind::Portal},
    {660,  ObjectKind::Portal},   {745,  ObjectKind::Portal},
    {747,  ObjectKind::Portal},   {1331, ObjectKind::Portal},
    {1334, ObjectKind::Portal},   {1933, ObjectKind::Portal},

    {35,   ObjectKind::Pad},      {67,   ObjectKind::Pad},
    {140,  ObjectKind::Pad},      {1332, ObjectKind::Pad},
    {1932, ObjectKind::Pad},

    {36,   ObjectKind::Orb},      {84,   ObjectKind::Orb},
    {141,  ObjectKind::Orb},      {1022, ObjectKind::Orb},
    {1330, ObjectKind::Orb},      {1333, ObjectKind::Orb},
    {1594, ObjectKind::Orb},      {1704, ObjectKind::Orb},
    {1751, ObjectKind::Orb},

    {142,  ObjectKind::Collectible}, {1329, ObjectKind::Collectible},

    {29,   ObjectKind::Trigger},  {30,   ObjectKind::Trigger},
    {104,  ObjectKind::Trigger},  {105,  ObjectKind::Trigger},
    {221,  ObjectKind::Trigger},  {717,  ObjectKind::Trigger},
    {718,  ObjectKind::Trigger},  {743,  ObjectKind::Trigger},
    {744,  ObjectKind::Trigger},  {899,  ObjectKind::Trigger},
    {900,  ObjectKind::Trigger},  {901,  ObjectKind::Trigger},
    {1006, ObjectKind::Trigger},  {1007, ObjectKind::Trigger},
    {1049, ObjectKind::Trigger},  {1268, ObjectKind::Trigger},
    {1346, ObjectKind::Trigger},  {1347, ObjectKind::Trigger},
    {1520, ObjectKind::Trigger},  {1585, ObjectKind::Trigger},
    {1611, ObjectKind::Trigger},  {1612, ObjectKind::Trigger},
    {1613, ObjectKind::Trigger},  {1614, ObjectKind::Trigger},
    {1616, ObjectKind::Trigger},  {1811, ObjectKind::Trigger},
    {1812, ObjectKind::Trigger},  {1814, ObjectKind::Trigger},
    {1815, ObjectKind::Trigger},  {1816, ObjectKind::Trigger},
    {1817, ObjectKind::Trigger},  {2015, ObjectKind::Trigger},
    {2016, ObjectKind::Trigger},  {2062, ObjectKind::Trigger},
    {2066, ObjectKind::Trigger},  {2067, ObjectKind::Trigger},
    {2068, ObjectKind::Trigger},  {2899, ObjectKind::Trigger},
    {2900, ObjectKind::Trigger},  {2901, ObjectKind::Trigger},
    {2903, ObjectKind::Trigger},  {2904, ObjectKind::Trigger},
    {2905, ObjectKind::Trigger},  {2907, ObjectKind::Trigger},
    {2909, ObjectKind::Trigger},  {2911, ObjectKind::Trigger},
    {2912, ObjectKind::Trigger},  {2913, ObjectKind::Trigger},
    {2914, ObjectKind::Trigger},  {2915, ObjectKind::Trigger},
    {2916, ObjectKind::Trigger},  {2917, ObjectKind::Trigger},
    {2919, ObjectKind::Trigger},  {2920, ObjectKind::Trigger},
    {2921, ObjectKind::Trigger},  {2922, ObjectKind::Trigger},
    {2923, ObjectKind::Trigger},  {2924, ObjectKind::Trigger},
    {2925, ObjectKind::Trigger},

    {914,  ObjectKind::Text},
};

std::unordered_map<int, ObjectKind>& curatedTable() {
    static std::unordered_map<int, ObjectKind> table = [] {
        std::unordered_map<int, ObjectKind> out;
        out.reserve(std::size(kCurated) * 2);
        for (auto const& entry : kCurated) out.emplace(entry.id, entry.kind);
        return out;
    }();
    return table;
}

std::unordered_map<int, ObjectKind>& overrideTable() {
    static std::unordered_map<int, ObjectKind> table;
    return table;
}

ObjectKind kindFromName(std::string const& name) {
    if (name == "solid")       return ObjectKind::Solid;
    if (name == "slope")       return ObjectKind::Slope;
    if (name == "hazard")      return ObjectKind::Hazard;
    if (name == "portal")      return ObjectKind::Portal;
    if (name == "pad")         return ObjectKind::Pad;
    if (name == "orb")         return ObjectKind::Orb;
    if (name == "collectible") return ObjectKind::Collectible;
    if (name == "trigger")     return ObjectKind::Trigger;
    if (name == "deco")        return ObjectKind::Deco;
    if (name == "text")        return ObjectKind::Text;
    if (name == "particle")    return ObjectKind::Particle;
    return ObjectKind::Unknown;
}

// Keys only triggers ever carry. Any one of them is enough: a decoration never
// gets a target group, an activation flag or a target colour channel.
constexpr int kTriggerKeys[] = {23, 36, 51, 56, 62, 87};

} // namespace

char const* kindName(ObjectKind kind) {
    switch (kind) {
        case ObjectKind::Solid:       return "bloque";
        case ObjectKind::Slope:       return "rampa";
        case ObjectKind::Hazard:      return "pincho";
        case ObjectKind::Portal:      return "portal";
        case ObjectKind::Pad:         return "plataforma";
        case ObjectKind::Orb:         return "anillo";
        case ObjectKind::Collectible: return "moneda";
        case ObjectKind::Trigger:     return "trigger";
        case ObjectKind::Deco:        return "adorno";
        case ObjectKind::Text:        return "texto";
        case ObjectKind::Particle:    return "particula";
        case ObjectKind::Unknown:     return "sin clasificar";
    }
    return "sin clasificar";
}

ObjectKind kindOf(int objectId) {
    auto const& overrides = overrideTable();
    if (auto found = overrides.find(objectId); found != overrides.end()) return found->second;
    auto const& table = curatedTable();
    if (auto found = table.find(objectId); found != table.end()) return found->second;
    return ObjectKind::Unknown;
}

int loadTaxonomyOverrides(std::string const& text) {
    auto& table = overrideTable();
    std::istringstream in(text);
    std::string line;
    int loaded = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        std::istringstream fields(line);
        int id = 0;
        std::string name;
        if (!(fields >> id >> name)) continue;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        auto kind = kindFromName(name);
        if (id <= 0 || kind == ObjectKind::Unknown) continue;
        table[id] = kind;
        ++loaded;
    }
    return loaded;
}

void clearTaxonomyOverrides() {
    overrideTable().clear();
}

bool looksLikeTrigger(LevelObject const& object) {
    auto const kind = kindOf(object.id);
    if (kind == ObjectKind::Trigger) return true;
    // A trigger orb carries the same target and activation keys as a trigger,
    // so a known kind always wins over the key sniffing below.
    if (kind != ObjectKind::Unknown) return false;
    std::string value;
    for (int key : kTriggerKeys) {
        if (objectKey(object.save, key, value)) return true;
    }
    return false;
}

bool isMarkerObject(int objectId) {
    return objectId == 467 || objectId == 143 || objectId == 146;
}

} // namespace paimon::autobuild
