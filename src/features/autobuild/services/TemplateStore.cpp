#include "TemplateStore.hpp"

#include "Capture.hpp"
#include "SaveString.hpp"

#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

using namespace geode::prelude;

namespace paimon::autobuild {

namespace {

constexpr char const* kHeader = "PAIMAB 1";
constexpr char const* kSelectedKey = "autobuild-selected";
constexpr char const* kExtension = ".pab";
constexpr size_t kMaxFileBytes = 64u * 1024u * 1024u;
constexpr int kMaxImportObjects = 120000;

void stripCr(std::string& line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
}

bool startsWith(std::string const& line, char const* prefix) {
    return line.rfind(prefix, 0) == 0;
}

std::string slug(std::string const& name) {
    std::string out;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(c));
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.empty()) out = "plantilla";
    if (out.size() > 40) out.resize(40);
    return out;
}

std::string joinIds(std::vector<int> const& ids) {
    std::string out;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i) out += ',';
        out += std::to_string(ids[i]);
    }
    return out;
}

std::vector<int> splitIds(std::string const& text) {
    std::vector<int> out;
    std::string token;
    std::istringstream in(text);
    while (std::getline(in, token, ',')) {
        if (token.empty()) continue;
        try {
            out.push_back(std::stoi(token));
        } catch (...) {
        }
    }
    return out;
}

// One "field|field|field" line, kept simple because save strings never contain
// a pipe (GD uses commas and semicolons).
std::vector<std::string> splitFields(std::string const& line, size_t expected) {
    std::vector<std::string> out;
    out.reserve(expected);
    size_t start = 0;
    while (out.size() + 1 < expected) {
        auto pos = line.find('|', start);
        if (pos == std::string::npos) break;
        out.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    out.push_back(line.substr(start));
    return out;
}

float toFloat(std::string const& text, float fallback = 0.f) {
    try {
        return std::stof(text);
    } catch (...) {
        return fallback;
    }
}

int toInt(std::string const& text, int fallback = 0) {
    try {
        return std::stoi(text);
    } catch (...) {
        return fallback;
    }
}

void centerPiece(Piece& piece) {
    if (piece.objects.empty()) return;
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (auto const& obj : piece.objects) {
        minX = std::min(minX, obj.dx);
        minY = std::min(minY, obj.dy);
        maxX = std::max(maxX, obj.dx);
        maxY = std::max(maxY, obj.dy);
    }
    float cx = (minX + maxX) / 2.f;
    float cy = (minY + maxY) / 2.f;
    for (auto& obj : piece.objects) {
        obj.dx -= cx;
        obj.dy -= cy;
    }
    piece.width = maxX - minX;
    piece.height = maxY - minY;
}

// Libraries written by the older autobuild mods: a header block, then pieces
// made of "<id> <x> <y> <flag>" + the raw level string of each object.
Result<Template> parseTblib(std::string const& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line)) return Err("El archivo esta vacio.");
    stripCr(line);
    if (!startsWith(line, "TBLIB")) return Err("No parece una libreria .tblib.");

    std::string colors;
    std::vector<std::vector<CapturedObject>> rawPieces;
    int totalObjects = 0;

    auto readPieces = [&](int count) {
        for (int p = 0; p < count; ++p) {
            std::string header;
            do {
                if (!std::getline(in, header)) return;
                stripCr(header);
            } while (header.empty() || header == "---");

            int unusedA = 0;
            int unusedB = 0;
            int pairs = 0;
            std::istringstream headerIn(header);
            headerIn >> unusedA >> unusedB >> pairs;
            for (int i = 0; i < pairs; ++i) {
                if (!std::getline(in, line)) return;
            }

            if (!std::getline(in, line)) return;
            stripCr(line);
            int objectCount = toInt(line, 0);
            if (objectCount < 0 || objectCount > kMaxImportObjects) return;

            std::vector<CapturedObject> objects;
            objects.reserve(objectCount);
            for (int o = 0; o < objectCount; ++o) {
                std::string entry;
                do {
                    if (!std::getline(in, entry)) return;
                    stripCr(entry);
                } while (entry.empty() || entry == "---");

                int objectId = 0;
                double x = 0.0;
                double y = 0.0;
                std::istringstream entryIn(entry);
                entryIn >> objectId >> x >> y;

                if (!std::getline(in, line)) return;
                stripCr(line);

                CapturedObject captured;
                captured.objectId = objectId;
                captured.dx = static_cast<float>(x);
                captured.dy = static_cast<float>(y);
                if (line != "---" && !line.empty()) {
                    captured.save = line;
                    float sx = 0.f;
                    float sy = 0.f;
                    if (positionOf(captured.save, sx, sy)) {
                        captured.dx = sx;
                        captured.dy = sy;
                    }
                    if (captured.objectId == 0) captured.objectId = objectIdOf(captured.save);
                }
                if (captured.save.empty()) continue;
                objects.push_back(std::move(captured));
            }
            totalObjects += static_cast<int>(objects.size());
            if (!objects.empty()) rawPieces.push_back(std::move(objects));
        }
    };

    while (std::getline(in, line)) {
        stripCr(line);
        if (line.empty() || line == "---") continue;
        if (startsWith(line, "LSET ")) {
            if (toInt(line.substr(5), 0) == 1 && std::getline(in, line)) {
                stripCr(line);
                auto key = line.find("kS38,");
                if (key == std::string::npos) {
                    colors = line;
                } else {
                    auto start = key + 5;
                    auto end = line.find_first_of(",;", start);
                    colors = end == std::string::npos ? line.substr(start)
                                                      : line.substr(start, end - start);
                }
            }
        } else if (startsWith(line, "PIECES ") || startsWith(line, "FILLERS ") ||
                   startsWith(line, "DECO3D ")) {
            auto space = line.find(' ');
            readPieces(toInt(line.substr(space + 1), 0));
        }
    }

    if (rawPieces.empty()) return Err("La libreria no trae piezas legibles.");
    if (totalObjects > kMaxImportObjects) {
        return Err(fmt::format("La libreria trae {} objetos, demasiados para importar.",
                               totalObjects));
    }

    // A single huge piece is a whole decorated section: cut it into cells so the
    // wave can reuse the parts instead of stamping the entire thing per marker.
    if (rawPieces.size() == 1 && rawPieces[0].size() >= 48) {
        auto tpl = waveFromObjects(std::move(rawPieces[0]), 30.f);
        tpl.colors = std::move(colors);
        return Ok(std::move(tpl));
    }

    Template tpl;
    tpl.mode = Mode::Stamp;
    tpl.cell = 30.f;
    tpl.colors = std::move(colors);
    for (auto& objects : rawPieces) {
        Piece piece;
        piece.objects = std::move(objects);
        centerPiece(piece);
        tpl.pieces.push_back(std::move(piece));
    }
    return Ok(std::move(tpl));
}

} // namespace

std::string serialize(Template const& tpl) {
    std::string out;
    out.reserve(static_cast<size_t>(tpl.objectCount()) * 64 + 256);
    out += kHeader;
    out += '\n';
    out += fmt::format("NAME {}\n", tpl.name);
    out += fmt::format("MODE {}\n", tpl.mode == Mode::Wave ? "wave" : "stamp");
    out += fmt::format("CELL {:.3f}\n", tpl.cell);
    out += fmt::format("SAMPLES {}\n", tpl.samples);
    out += fmt::format("COLORS {}\n", tpl.colors);
    out += fmt::format("PIECES {}\n", tpl.pieces.size());
    for (auto const& piece : tpl.pieces) {
        out += fmt::format("P {} {}\n", piece.weight, piece.objects.size());
        for (auto const& obj : piece.objects) {
            out += fmt::format("{:.3f}|{:.3f}|{}\n", obj.dx, obj.dy, obj.save);
        }
    }
    if (tpl.mode == Mode::Wave) {
        out += fmt::format("LINKS {}\n", tpl.links.size());
        for (auto const& link : tpl.links) {
            int mask = 0;
            for (int d = 0; d < 4; ++d) {
                if (link.open[d]) mask |= 1 << d;
            }
            out += fmt::format("L {}|{}|{}|{}|{}\n", mask, joinIds(link.side[0]),
                               joinIds(link.side[1]), joinIds(link.side[2]),
                               joinIds(link.side[3]));
        }
    }
    return out;
}

Result<Template> deserialize(std::string const& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line)) return Err("El archivo esta vacio.");
    stripCr(line);
    if (!startsWith(line, "PAIMAB")) return parseTblib(text);

    Template tpl;
    while (std::getline(in, line)) {
        stripCr(line);
        if (line.empty()) continue;

        if (startsWith(line, "NAME ")) {
            tpl.name = line.substr(5);
        } else if (startsWith(line, "MODE ")) {
            tpl.mode = line.substr(5) == "stamp" ? Mode::Stamp : Mode::Wave;
        } else if (startsWith(line, "CELL ")) {
            tpl.cell = toFloat(line.substr(5), 30.f);
        } else if (startsWith(line, "SAMPLES ")) {
            tpl.samples = std::max(1, toInt(line.substr(8), 1));
        } else if (startsWith(line, "COLORS")) {
            tpl.colors = line.size() > 7 ? line.substr(7) : std::string{};
        } else if (startsWith(line, "PIECES ")) {
            int count = toInt(line.substr(7), 0);
            tpl.pieces.reserve(std::max(0, count));
            for (int p = 0; p < count; ++p) {
                if (!std::getline(in, line)) break;
                stripCr(line);
                if (!startsWith(line, "P ")) break;
                std::istringstream headerIn(line.substr(2));
                Piece piece;
                int objectCount = 0;
                headerIn >> piece.weight >> objectCount;
                piece.weight = std::max(1, piece.weight);
                piece.objects.reserve(std::max(0, objectCount));
                for (int o = 0; o < objectCount; ++o) {
                    if (!std::getline(in, line)) break;
                    stripCr(line);
                    auto fields = splitFields(line, 3);
                    if (fields.size() < 3 || fields[2].empty()) continue;
                    CapturedObject object;
                    object.dx = toFloat(fields[0], 0.f);
                    object.dy = toFloat(fields[1], 0.f);
                    object.save = fields[2];
                    object.objectId = objectIdOf(object.save);
                    piece.objects.push_back(std::move(object));
                }
                if (!piece.objects.empty()) tpl.pieces.push_back(std::move(piece));
            }
        } else if (startsWith(line, "LINKS ")) {
            int count = toInt(line.substr(6), 0);
            tpl.links.reserve(std::max(0, count));
            for (int i = 0; i < count; ++i) {
                if (!std::getline(in, line)) break;
                stripCr(line);
                if (!startsWith(line, "L ")) break;
                auto fields = splitFields(line.substr(2), 5);
                if (fields.size() < 5) continue;
                Links link;
                int mask = toInt(fields[0], 0);
                for (int d = 0; d < 4; ++d) {
                    link.open[d] = (mask >> d) & 1;
                    link.side[d] = splitIds(fields[d + 1]);
                }
                tpl.links.push_back(std::move(link));
            }
        }
    }

    if (!tpl.valid()) return Err("La plantilla no trae piezas.");
    if (tpl.mode == Mode::Wave && tpl.links.size() != tpl.pieces.size()) {
        tpl.links.resize(tpl.pieces.size());
    }
    return Ok(std::move(tpl));
}

TemplateStore& TemplateStore::get() {
    static TemplateStore instance;
    return instance;
}

std::filesystem::path TemplateStore::directory() {
    auto dir = Mod::get()->getConfigDir() / "autobuild";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void TemplateStore::load() {
    if (m_loaded) return;
    m_loaded = true;

    auto entries = utils::file::readDirectory(directory());
    if (entries.isErr()) return;

    for (auto const& path : entries.unwrap()) {
        if (path.extension() != kExtension) continue;
        auto text = utils::file::readString(path);
        if (text.isErr()) continue;
        auto tpl = deserialize(text.unwrap());
        if (tpl.isErr()) {
            log::warn("[Autobuild] no se pudo leer {}: {}", path.filename().string(),
                      tpl.unwrapErr());
            continue;
        }
        auto value = tpl.unwrap();
        value.file = path.filename().string();
        m_items.push_back(std::move(value));
    }
    std::sort(m_items.begin(), m_items.end(),
              [](Template const& a, Template const& b) { return a.name < b.name; });

    auto last = Mod::get()->getSavedValue<std::string>(kSelectedKey, "");
    m_selected = m_items.empty() ? -1 : 0;
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].file == last) {
            m_selected = static_cast<int>(i);
            break;
        }
    }
    log::info("[Autobuild] {} plantillas cargadas", m_items.size());
}

void TemplateStore::reload() {
    m_items.clear();
    m_selected = -1;
    m_loaded = false;
    load();
}

void TemplateStore::select(int index) {
    m_selected = (index >= 0 && index < static_cast<int>(m_items.size())) ? index : -1;
    Mod::get()->setSavedValue(kSelectedKey,
                              m_selected < 0 ? std::string{} : m_items[m_selected].file);
}

Template const* TemplateStore::selected() const {
    if (m_selected < 0 || m_selected >= static_cast<int>(m_items.size())) return nullptr;
    return &m_items[m_selected];
}

int TemplateStore::add(Template tpl) {
    if (tpl.file.empty()) {
        auto base = slug(tpl.name);
        auto name = base + kExtension;
        int suffix = 2;
        auto exists = [&](std::string const& candidate) {
            return std::any_of(m_items.begin(), m_items.end(),
                               [&](Template const& t) { return t.file == candidate; });
        };
        while (exists(name)) name = fmt::format("{}-{}{}", base, suffix++, kExtension);
        tpl.file = name;
    }
    m_items.push_back(std::move(tpl));
    int index = static_cast<int>(m_items.size()) - 1;
    persist(index);
    select(index);
    return index;
}

void TemplateStore::replace(int index, Template tpl) {
    if (index < 0 || index >= static_cast<int>(m_items.size())) return;
    tpl.file = m_items[index].file;
    m_items[index] = std::move(tpl);
    persist(index);
}

void TemplateStore::rename(int index, std::string name) {
    if (index < 0 || index >= static_cast<int>(m_items.size())) return;
    if (name.empty()) return;
    m_items[index].name = std::move(name);
    persist(index);
}

void TemplateStore::remove(int index) {
    if (index < 0 || index >= static_cast<int>(m_items.size())) return;
    if (!m_items[index].file.empty()) {
        std::error_code ec;
        std::filesystem::remove(directory() / m_items[index].file, ec);
    }
    m_items.erase(m_items.begin() + index);
    select(m_items.empty() ? -1 : std::clamp(m_selected, 0, static_cast<int>(m_items.size()) - 1));
}

void TemplateStore::persist(int index) {
    if (index < 0 || index >= static_cast<int>(m_items.size())) return;
    auto& tpl = m_items[index];
    if (tpl.file.empty()) tpl.file = slug(tpl.name) + kExtension;
    auto result = utils::file::writeString(directory() / tpl.file, serialize(tpl));
    if (result.isErr()) {
        log::warn("[Autobuild] no se pudo guardar {}: {}", tpl.file, result.unwrapErr());
    }
}

Result<int> TemplateStore::importFile(std::filesystem::path const& path) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (!ec && size > kMaxFileBytes) return Err("El archivo es demasiado grande.");

    auto text = utils::file::readString(path);
    if (text.isErr()) return Err(text.unwrapErr());

    auto parsed = deserialize(text.unwrap());
    if (parsed.isErr()) return Err(parsed.unwrapErr());

    auto tpl = parsed.unwrap();
    if (!tpl.valid()) return Err("La plantilla importada esta vacia.");
    tpl.file.clear();
    if (tpl.name.empty() || tpl.name == "Sin nombre") tpl.name = path.stem().string();
    return Ok(add(std::move(tpl)));
}

} // namespace paimon::autobuild
