#pragma once

// node trees built from spec through per-type factories.

#include <matjson.hpp>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cocos2d { class CCNode; }

namespace paimon::ui::dec {

struct Spec {
    std::string type;
    std::string id;
    matjson::Value attributes = matjson::Value::object();
    std::vector<Spec> children;
};

using Creator = std::function<cocos2d::CCNode*(matjson::Value const& attrs)>;

// default types register on first use.
class Factory {
public:
    static Factory& get();
    void registerType(std::string_view type, Creator creator);
    cocos2d::CCNode* create(std::string_view type, matjson::Value const& attrs);

private:
    void ensureDefaults();
    bool m_ready = false;
    std::unordered_map<std::string, Creator> m_creators;
};

// anchored positions fall back to the screen when parent is null.
void applyAttributes(cocos2d::CCNode* node, matjson::Value const& attrs,
                     cocos2d::CCNode* parent = nullptr);

cocos2d::CCNode* build(Spec const& spec, cocos2d::CCNode* parent = nullptr);

}
