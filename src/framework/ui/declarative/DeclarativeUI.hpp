#pragma once

// Declarative node trees built from Spec/JSON through per-type factories and
// attribute appliers.

#include <matjson.hpp>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cocos2d { class CCNode; }

namespace paimon::ui::dec {

struct Spec {
    std::string type;                                    // Node type.
    std::string id;                                      // Optional node ID.
    matjson::Value attributes = matjson::Value::object();
    std::vector<Spec> children;

    static Spec fromJson(matjson::Value const& json);
};

// Type string -> base node creator.
using Creator = std::function<cocos2d::CCNode*(matjson::Value const& attrs)>;

// Default types register on first use.
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

// Apply attributes; anchored positions use parent or the screen when null.
void applyAttributes(cocos2d::CCNode* node, matjson::Value const& attrs,
                     cocos2d::CCNode* parent = nullptr);

// Build a Spec tree, optionally attaching the root to parent.
cocos2d::CCNode* build(Spec const& spec, cocos2d::CCNode* parent = nullptr);

// Find a descendant by ID query: "a > b" is direct; "a b" is recursive.
cocos2d::CCNode* query(cocos2d::CCNode* root, std::string_view path);

}
