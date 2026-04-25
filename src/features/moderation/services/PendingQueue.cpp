#include "PendingQueue.hpp"
#include <Geode/loader/Mod.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/utils/file.hpp>
#include <chrono>
#include <sstream>

using namespace geode::prelude;

PendingQueue& PendingQueue::get() {
    static PendingQueue q; return q;
}

std::filesystem::path PendingQueue::jsonPath() const {
    return Mod::get()->getSaveDir() / "thumbnails" / "pending_queue.json";
}

char const* PendingQueue::catToStr(PendingCategory c) {
    switch (c) {
        case PendingCategory::Verify: return "verify";
        case PendingCategory::Update: return "update";
        case PendingCategory::Report: return "report";
        case PendingCategory::ProfileBackground: return "profilebackground";
        case PendingCategory::ProfileImg: return "profileimg";
    }
    return "verify";
}

PendingCategory PendingQueue::strToCat(std::string const& s) {
    if (s == "update") return PendingCategory::Update;
    if (s == "report") return PendingCategory::Report;
    if (s == "profilebackground") return PendingCategory::ProfileBackground;
    if (s == "banner" || s == "background" || s == "profile") return PendingCategory::ProfileBackground;
    if (s == "profileimg") return PendingCategory::ProfileImg;
    return PendingCategory::Verify;
}

char const* PendingQueue::statusToStr(PendingStatus s) {
    switch (s) {
        case PendingStatus::Open: return "open";
        case PendingStatus::Accepted: return "accepted";
        case PendingStatus::Rejected: return "rejected";
    }
    return "open";
}

PendingStatus PendingQueue::strToStatus(std::string const& s) {
    if (s == "accepted") return PendingStatus::Accepted;
    if (s == "rejected") return PendingStatus::Rejected;
    return PendingStatus::Open;
}

std::string PendingQueue::escape(std::string const& s) {
    std::string out; out.reserve(s.size()+8);
    for (char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

bool PendingQueue::isLevelCreator(GJGameLevel* level, std::string const& username) {
    if (!level || username.empty()) return false;

    // comparar con m_creatorName de GJGameLevel
    std::string creatorName = level->m_creatorName;

    return geode::utils::string::toLower(creatorName) == geode::utils::string::toLower(username);
}

void PendingQueue::load() {
    std::call_once(m_loadFlag, [this]() {
    m_items.clear();
    auto p = jsonPath();
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || ec) return;
    auto data = file::readString(p).unwrapOr("");
    if (data.empty()) return;

    auto jsonRes = matjson::parse(data);
    if (!jsonRes.isOk()) return;
    auto root = jsonRes.unwrap();

    auto itemsArr = root["items"].asArray();
    if (!itemsArr.isOk()) return;

    for (auto const& obj : itemsArr.unwrap()) {
        PendingItem it{};
        it.levelID = geode::utils::numFromString<int>(
            obj["levelID"].asString().unwrapOr("0")).unwrapOr(0);
        if (it.levelID == 0 && obj["levelID"].isNumber())
            it.levelID = obj["levelID"].asInt().unwrapOr(0);
        it.category = strToCat(obj["category"].asString().unwrapOr("verify"));
        it.timestamp = geode::utils::numFromString<int64_t>(
            obj["timestamp"].asString().unwrapOr("0")).unwrapOr(0);
        if (it.timestamp == 0 && obj["timestamp"].isNumber())
            it.timestamp = (int64_t)obj["timestamp"].asDouble().unwrapOr(0.0);
        it.submittedBy = obj["submittedBy"].asString().unwrapOr("");
        it.note = obj["note"].asString().unwrapOr("");
        it.status = strToStatus(obj["status"].asString().unwrapOr("open"));
        it.isCreator = obj["isCreator"].asBool().unwrapOr(false);
        if (it.levelID != 0) m_items.push_back(it);
    }
    }); // end call_once
}

void PendingQueue::save() {
    // mantener historial; escribir estado completo
    std::string json = toJson();
    auto p = jsonPath();
    std::error_code ec; std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << json; out.close();
}

std::string PendingQueue::toJson() const {
    // construir: {"items":[...]} con todos items
    std::stringstream ss;
    ss << "{\"items\":[";
    bool first = true;
    for (auto const& it : m_items) {
        if (!first) ss << ","; first = false;
        ss << "{"
           << "\"levelID\":" << it.levelID << ","
           << "\"category\":\"" << catToStr(it.category) << "\"," 
           << "\"timestamp\":" << it.timestamp << ","
           << "\"submittedBy\":\"" << escape(it.submittedBy) << "\"," 
           << "\"note\":\"" << escape(it.note) << "\"," 
           << "\"status\":\"" << statusToStr(it.status) << "\","
           << "\"isCreator\":" << (it.isCreator ? "true" : "false")
           << "}";
    }
    ss << "]}";
    return ss.str();
}

void PendingQueue::addOrBump(int levelID, PendingCategory cat, std::string submittedBy, std::string note, bool isCreator) {
    load();
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (auto& it : m_items) {
        if (it.levelID == levelID && it.category == cat && it.status == PendingStatus::Open) {
            it.timestamp = now; 
            if (!submittedBy.empty()) it.submittedBy = std::move(submittedBy); 
            if (!note.empty()) it.note = std::move(note);
            it.isCreator = isCreator;
            save(); syncNow();
            log::info("[PendingQueue] Updated item {} cat {} isCreator={}", levelID, catToStr(cat), isCreator);
            return;
        }
    }
    PendingItem it{}; 
    it.levelID = levelID; 
    it.category = cat; 
    it.timestamp = now; 
    it.submittedBy = std::move(submittedBy); 
    it.note = std::move(note); 
    it.status = PendingStatus::Open;
    it.isCreator = isCreator;
    m_items.push_back(std::move(it));
    save(); syncNow();
    log::info("[PendingQueue] Added item {} cat {} isCreator={}", levelID, catToStr(cat), isCreator);
}

void PendingQueue::removeForLevel(int levelID) {
    load();
    bool changed = false;
    for (auto& it : m_items) {
        if (it.levelID == levelID && it.status == PendingStatus::Open) {
            it.status = PendingStatus::Accepted; changed = true;
        }
    }
    if (changed) { save(); syncNow(); }
    log::info("[PendingQueue] Marked items as accepted for level {}", levelID);
}

void PendingQueue::reject(int levelID, PendingCategory cat, std::string reason) {
    load();
    bool changed = false;
    for (auto& it : m_items) {
        if (it.levelID == levelID && it.category == cat && it.status == PendingStatus::Open) {
            it.status = PendingStatus::Rejected; if (!reason.empty()) it.note = std::move(reason); changed = true;
        }
    }
    if (changed) { save(); syncNow(); }
    log::info("[PendingQueue] Rejected item {} cat {}", levelID, catToStr(cat));
}

void PendingQueue::accept(int levelID, PendingCategory cat) {
    load();
    bool changed = false;
    for (auto& it : m_items) {
        if (it.levelID == levelID && it.category == cat && it.status == PendingStatus::Open) {
            it.status = PendingStatus::Accepted; changed = true;
        }
    }
    if (changed) { save(); syncNow(); }
    log::info("[PendingQueue] Accepted item {} cat {}", levelID, catToStr(cat));
}

std::vector<PendingItem> PendingQueue::list(PendingCategory cat) const {
    const_cast<PendingQueue*>(this)->load(); // safe: load() uses std::call_once internally
    std::vector<PendingItem> out;
    for (auto const& it : m_items) if (it.category == cat && it.status == PendingStatus::Open) out.push_back(it);
    // orden: sugerencias creador primero, luego timestamp desc
    std::sort(out.begin(), out.end(), [](auto const& a, auto const& b){ 
        if (a.isCreator != b.isCreator) return a.isCreator > b.isCreator; // creadores primero
        return a.timestamp > b.timestamp; // luego mas nuevos primero
    });
    return out;
}

void PendingQueue::syncNow() {
    // sync servidor desactivada - cola ahora es solo local
    log::info("[PendingQueue] Server sync disabled - changes are saved locally only");
}

