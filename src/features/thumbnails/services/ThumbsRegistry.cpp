#include "ThumbsRegistry.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <filesystem>
#include <sstream>

using namespace geode::prelude;

namespace {
    char const* kindToStr(ThumbKind k) { return k == ThumbKind::Level ? "level" : "profile"; }
    ThumbKind strToKind(std::string const& s) { return s == "profile" ? ThumbKind::Profile : ThumbKind::Level; }
}

ThumbsRegistry& ThumbsRegistry::get() { static ThumbsRegistry r; return r; }

std::filesystem::path ThumbsRegistry::path() const {
    return Mod::get()->getSaveDir() / "thumbnails" / "registry.csv";
}

void ThumbsRegistry::load() const {
    if (m_loaded) return;
    m_loaded = true;
    m_items.clear();
    auto p = path();
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || ec) return;
    auto data = file::readString(p).unwrapOr("");
    std::stringstream ss(data);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        std::stringstream ls(line);
        std::string kind, idStr, verStr;
        if (!std::getline(ls, kind, ',')) continue;
        if (!std::getline(ls, idStr, ',')) continue;
        if (!std::getline(ls, verStr, ',')) verStr = "0";
        ThumbRecord r{};
        r.kind = strToKind(kind);
        r.id = geode::utils::numFromString<int>(idStr).unwrapOr(0);
        r.verified = (verStr == "1");
        if (r.id != 0) m_items.push_back(r);
    }
}

void ThumbsRegistry::save() const {
    std::stringstream ss;
    for (auto const& r : m_items) {
        ss << kindToStr(r.kind) << "," << r.id << "," << (r.verified ? "1" : "0") << "\n";
    }
    auto p = path();
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    auto res = file::writeStringSafe(p, ss.str());
    if (!res) {
        log::warn("[ThumbsRegistry] Failed to save registry: {}", res.unwrapErr());
    }
}

void ThumbsRegistry::mark(ThumbKind kind, int id, bool verified) {
    std::lock_guard<std::mutex> lock(m_mutex);
    load();
    for (auto& r : m_items) {
        if (r.kind == kind && r.id == id) { r.verified = verified; save(); return; }
    }
    m_items.push_back({kind, id, verified});
    save();
}

bool ThumbsRegistry::isVerified(ThumbKind kind, int id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    load();
    for (auto const& r : m_items) if (r.kind == kind && r.id == id) return r.verified;
    return false;
}

std::vector<ThumbRecord> ThumbsRegistry::list(ThumbKind kind, bool onlyUnverified) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    load();
    std::vector<ThumbRecord> out;
    for (auto const& r : m_items) {
        if (r.kind != kind) continue;
        if (onlyUnverified && r.verified) continue;
        out.push_back(r);
    }
    return out;
}
