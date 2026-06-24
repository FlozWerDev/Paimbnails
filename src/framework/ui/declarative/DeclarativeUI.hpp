#pragma once

// DeclarativeUI.hpp — Declarative UI engine.
//
// Describes node trees as data (Spec or JSON) and builds them with a per-type
// factory plus attribute appliers.
//
//   Spec spec{ "CCLabelBMFont", "my-label", attrs, {} };
//   auto* node = dec::build(spec, parent);
//
// or from JSON:
//
//   auto* node = dec::build(dec::Spec::fromJson(json), parent);

#include <matjson.hpp>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cocos2d { class CCNode; }

namespace paimon::ui::dec {

// Declarative node description.
struct Spec {
    std::string type;                                    // "CCNode", "CCLabelBMFont", ...
    std::string id;                                      // optional (node id)
    matjson::Value attributes = matjson::Value::object();
    std::vector<Spec> children;

    static Spec fromJson(matjson::Value const& json);
};

// type string -> base node creator.
using Creator = std::function<cocos2d::CCNode*(matjson::Value const& attrs)>;

// Type registry (factory pattern). Default types are registered on first use.
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

// Apply attributes to an existing node. 'parent' is used for anchored positions
// (anchor relative to the parent); if null, anchors to the screen.
void applyAttributes(cocos2d::CCNode* node, matjson::Value const& attrs,
                     cocos2d::CCNode* parent = nullptr);

// Build a Spec's tree. If 'parent' != null, adds the root node to it.
cocos2d::CCNode* build(Spec const& spec, cocos2d::CCNode* parent = nullptr);

// Find a descendant by a node-ID query:
//   "a > b"  -> direct child 'b' of child 'a'
//   "a b"    -> recursive descendant 'b' under 'a'
cocos2d::CCNode* query(cocos2d::CCNode* root, std::string_view path);

} // namespace paimon::ui::dec
