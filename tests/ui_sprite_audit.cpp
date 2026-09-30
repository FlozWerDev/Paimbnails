// auditoria ui-only sobre los resources oficiales del juego.
// recorre todos los .plist de resources, clasifica cada marco con el
// uispritecatalog real y vuelca por hoja: cuantos marcos caerian en cada
// spritekind (es decir, que se pintaria con cada tintscope). tambien
// clasifica los pngs sueltos (no-hoja).
// compila sin geode:
//   g++ -std=c++17 -o2 -o /tmp/uiaudit tests/ui_sprite_audit.cpp
//   /tmp/uiaudit [dir-resources] [archivo-salida]
// solo necesita los nombres de marco, asi que el parser plist es minimo:
// toma las <key>*.png</key> del dict de frames.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../src/features/texture-studio/engine/UiSpriteCatalog.cpp"

using namespace paimon::texture_studio;
namespace fs = std::filesystem;

namespace {

std::string readFile(fs::path const& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// nombres de marco: claves que terminan en .png dentro del plist.
// (las claves de metadata no terminan en .png; los valores .png van en
// <string>, no en <key>.)
std::vector<std::string> frameNames(std::string const& xml) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while ((pos = xml.find("<key>", pos)) != std::string::npos) {
        auto end = xml.find("</key>", pos);
        if (end == std::string::npos) break;
        std::string key = xml.substr(pos + 5, end - pos - 5);
        pos = end + 6;
        if (key.size() > 4 &&
            key.compare(key.size() - 4, 4, ".png") == 0) {
            out.push_back(key);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::string sheetBase(std::string const& fileName) {
    std::string b = fileName;
    if (b.size() > 6 && b.compare(b.size() - 6, 6, ".plist") == 0)
        b.resize(b.size() - 6);
    for (auto suf : {"-uhd", "-hd"}) {
        std::string s = suf;
        if (b.size() > s.size() &&
            b.compare(b.size() - s.size(), s.size(), s) == 0) {
            b.resize(b.size() - s.size());
            break;
        }
    }
    return b;
}

char const* kindTag(SpriteKind k) {
    switch (k) {
        case SpriteKind::Button:   return "BTN";
        case SpriteKind::MenuUi:   return "UI ";
        case SpriteKind::Gameplay: return "PLAY";
        case SpriteKind::Other:    return "----";
    }
    return "????";
}

}  // namespace

int main(int argc, char** argv) {
    fs::path res = argc > 1 ? argv[1]
        : "/home/fernando/.steam/debian-installation/steamapps/common/Geometry Dash/Resources";
    std::ostream* out = &std::cout;
    std::ofstream fout;
    if (argc > 2) {
        fout.open(argv[2]);
        out = &fout;
    }

    // familia de hoja -> marcos unicos (todas las calidades).
    std::map<std::string, std::set<std::string>> families;
    std::size_t plistCount = 0;
    for (auto const& e : fs::directory_iterator(res)) {
        if (e.path().extension() != ".plist") continue;
        ++plistCount;
        std::string xml = readFile(e.path());
        for (auto const& n : frameNames(xml))
            families[sheetBase(e.path().filename().string())].insert(n);
    }

    // pngs sueltos: sin plist de ninguna calidad.
    std::set<std::string> sheetPngs;
    for (auto const& [base, names] : families) {
        for (auto q : {"-uhd.png", "-hd.png", ".png"})
            sheetPngs.insert(base + q);
    }
    std::vector<std::string> standalone;
    for (auto const& e : fs::directory_iterator(res)) {
        if (e.path().extension() != ".png") continue;
        if (!sheetPngs.count(e.path().filename().string()))
            standalone.push_back(e.path().filename().string());
    }
    std::sort(standalone.begin(), standalone.end());

    *out << "plists: " << plistCount
         << "  familias: " << families.size()
         << "  pngs-sueltos: " << standalone.size() << "\n\n";

    long tBtn = 0, tUi = 0, tPlay = 0, tOther = 0, tFrames = 0;
    for (auto const& [base, names] : families) {
        long nBtn = 0, nUi = 0, nPlay = 0, nOther = 0;
        std::vector<std::string> tinted;
        for (auto const& n : names) {
            switch (UiSpriteCatalog::classify(n, base)) {
                case SpriteKind::Button: ++nBtn; break;
                case SpriteKind::MenuUi: ++nUi; break;
                case SpriteKind::Gameplay: ++nPlay; break;
                case SpriteKind::Other: ++nOther; break;
            }
        }
        for (auto const& n : names) {
            auto k = UiSpriteCatalog::classify(n, base);
            if (k == SpriteKind::Button || k == SpriteKind::MenuUi)
                tinted.push_back(std::string(kindTag(k)) + " " + n);
        }
        tBtn += nBtn; tUi += nUi; tPlay += nPlay; tOther += nOther;
        tFrames += (long)names.size();
        *out << "### " << base
             << "  marcos=" << names.size()
             << "  BTN=" << nBtn << " UI=" << nUi
             << " PLAY=" << nPlay << " ----=" << nOther << "\n";
        for (auto const& t : tinted) *out << "    " << t << "\n";
    }

    *out << "\n=== TOTAL marcos=" << tFrames
         << " BTN=" << tBtn << " UI=" << tUi
         << " PLAY=" << tPlay << " ----=" << tOther << "\n";

    *out << "\n=== PNGs SUELTOS que se pintarian (ButtonsAndMenuUi) ===\n";
    long sBtn = 0, sUi = 0, sSkip = 0;
    for (auto const& n : standalone) {
        auto k = UiSpriteCatalog::classify(n, "");
        if (k == SpriteKind::Button) { ++sBtn; *out << "    BTN " << n << "\n"; }
        else if (k == SpriteKind::MenuUi) { ++sUi; *out << "    UI  " << n << "\n"; }
        else ++sSkip;
    }
    *out << "sueltos: BTN=" << sBtn << " UI=" << sUi
         << " omitidos=" << sSkip << "\n";
    return 0;
}
