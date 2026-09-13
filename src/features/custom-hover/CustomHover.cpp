#include "CustomHover.hpp"
#include "../../utils/MainThreadDelay.hpp"
using namespace geode::prelude;
namespace paimon::hover {
namespace {
matjson::Value encode(Config const& c) {
    auto j=matjson::Value::object();
    j["enabled"]=c.enabled;
#define FIELD(n) j[#n]=c.n
    FIELD(scale); FIELD(stretch); FIELD(lift); FIELD(slide); FIELD(rotation);
    FIELD(enter); FIELD(exit); FIELD(delay); FIELD(amplitude); FIELD(frequency); FIELD(easing); FIELD(loop);
#undef FIELD
    return j;
}
Config decode(matjson::Value const& j) {
    Config c; c.enabled=j["enabled"].asBool().unwrapOr(true);
#define FIELD(n) c.n=j[#n].asDouble().unwrapOr(c.n)
    FIELD(scale); FIELD(stretch); FIELD(lift); FIELD(slide); FIELD(rotation);
    FIELD(enter); FIELD(exit); FIELD(delay); FIELD(amplitude); FIELD(frequency);
#undef FIELD
    c.easing=j["easing"].asInt().unwrapOr(1); c.loop=j["loop"].asInt().unwrapOr(0);
    return sanitize(c);
}
}
Manager& Manager::get() { static Manager m; return m; }
void Manager::load() {
    auto j=Mod::get()->getSavedValue<matjson::Value>("custom-hover-v1",matjson::Value::object());
    global=decode(j["global"]); linked=j["linked"].asBool().unwrapOr(false);
    buttons.clear(); saved.clear(); picking=false;
    if (auto entries = j["buttons"].asArray()) {
        for (auto const& entry : entries.unwrap()) {
            auto key=entry["key"].asString().unwrapOr("");
            if (!key.empty() && buttons.size()<2048) buttons[key]=decode(entry["config"]);
        }
    }
    if (auto entries = j["presets"].asArray()) {
        for (auto const& entry : entries.unwrap()) {
            auto name=entry["key"].asString().unwrapOr("");
            if (!name.empty() && saved.size()<100) saved[name.substr(0,24)]=decode(entry["config"]);
        }
    }
}
void Manager::save() {
    auto j=matjson::Value::object(); j["global"]=encode(global); j["linked"]=linked;
    auto pack=[](auto const& map) {
        std::vector<matjson::Value> a;
        for(auto const& [key,c]:map) { auto e=matjson::Value::object(); e["key"]=key; e["config"]=encode(c); a.push_back(e); }
        return matjson::Value(a);
    };
    j["buttons"]=pack(buttons); j["presets"]=pack(saved);
    Mod::get()->setSavedValue("custom-hover-v1",j); paimon::requestDeferredModSave();
}
Config Manager::resolve(std::string const& key) const {
    auto it=buttons.find(key); return !linked && it!=buttons.end() ? it->second : global;
}
void Manager::set(std::string const& key, Config c) {
    c=sanitize(c);
    if (key.empty() || linked) global=c; else buttons[key]=c;
    save();
}
void Manager::group(Config c) { global=sanitize(c); linked=true; save(); }
void reset() { auto& m=Manager::get(); m.global=Config{}; m.linked=false; m.picking=false; m.buttons.clear(); m.saved.clear(); m.save(); }
std::string buttonKey(CCNode* node) {
    std::string key;
    // Full hierarchy scopes repeated IDs to their screen/menu. Anonymous nodes
    // use type + sibling index; stable across visits with the same layout.
    for(auto* n=node; n && !typeinfo_cast<CCScene*>(n); n=n->getParent()) {
        auto id=n->getID();
        if(id.empty()) {
            unsigned index=0;
            if(auto* p=n->getParent(); p && p->getChildren()) index=p->getChildren()->indexOfObject(n);
            id=fmt::format("{}[{}]",typeid(*n).name(),index);
        }
        key=fmt::format("{}/{}{}",id.size(),id,key);
    }
    return key;
}
}
