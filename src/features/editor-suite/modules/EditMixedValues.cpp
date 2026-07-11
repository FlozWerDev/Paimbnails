// Edit mixed Editor Layer / Layer2 / Z Order when multi-selecting.
// Shows min-max ranges and applies typed values to ALL selected objects
// (BetterEdit-style: mixed no longer blocks writing a concrete value).

#include "../EditorModule.hpp"
#include "../api/GroupViewAPI.hpp"

#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/SetGroupIDLayer.hpp>
#include <Geode/modify/SetGroupIDLayer.hpp>
#include <optional>
#include <string>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace paimon::editor;

namespace {

bool on() { return moduleEnabled("editor-mod-edit-mixed"); }

struct Range {
    int minV = 0, maxV = 0;
    bool any = false;
    void add(int v) {
        if (!any) {
            minV = maxV = v;
            any = true;
        } else {
            minV = std::min(minV, v);
            maxV = std::max(maxV, v);
        }
    }
    bool mixed() const { return any && minV != maxV; }
    std::string label(char const* name) const {
        if (!any) return fmt::format("{}: -", name);
        if (mixed()) return fmt::format("{}: {}-{}", name, minV, maxV);
        return fmt::format("{}: {}", name, minV);
    }
};

std::optional<int> parseIntLoose(std::string const& s) {
    if (s.empty() || s == "Mixed" || s == "mixed" || s == "-") return std::nullopt;
    if (auto v = numFromString<int>(s)) return v.unwrap();
    return std::nullopt;
}

} // namespace

class $modify(PaimonEditMixed, SetGroupIDLayer) {
    struct Fields {
        Ref<CCLabelBMFont> hint;
        bool applying = false;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "SetGroupIDLayer::init");
    }

    void refreshHint() {
        if (!m_fields->hint) return;
        Range layer, layer2, zOrder;
        auto consume = [&](GameObject* o) {
            if (!o) return;
            layer.add(o->m_editorLayer);
            layer2.add(o->m_editorLayer2);
            zOrder.add(o->m_zOrder);
        };
        consume(m_targetObject);
        if (m_targetObjects) {
            for (auto* o : CCArrayExt<GameObject*>(m_targetObjects)) consume(o);
        }
        auto text = fmt::format(
            "{}\n{}\n{}\n<cg>type to set all</c>",
            layer.label("L1"), layer2.label("L2"), zOrder.label("Z")
        );
        // CCLabelBMFont has no color tags — plain text.
        m_fields->hint->setString(
            fmt::format("{}\n{}\n{}  (type=set all)", layer.label("L1"), layer2.label("L2"), zOrder.label("Z")).c_str()
        );
    }

    void applyToAll(std::function<void(GameObject*)> const& fn) {
        if (m_targetObject) fn(m_targetObject);
        if (m_targetObjects) {
            for (auto* o : CCArrayExt<GameObject*>(m_targetObjects)) {
                if (o) fn(o);
            }
        }
        group_view::updateGroupView();
        refreshHint();
    }

    $override
    bool init(GameObject* obj, CCArray* objs) {
        if (!SetGroupIDLayer::init(obj, objs)) return false;
        if (!on() || !m_mainLayer) return true;

        Range layer, layer2, zOrder;
        auto consume = [&](GameObject* o) {
            if (!o) return;
            layer.add(o->m_editorLayer);
            layer2.add(o->m_editorLayer2);
            zOrder.add(o->m_zOrder);
        };
        consume(obj);
        if (objs) {
            for (auto* o : CCArrayExt<GameObject*>(objs)) consume(o);
        }

        auto text = fmt::format(
            "{}\n{}\n{}  (type=set all)",
            layer.label("L1"), layer2.label("L2"), zOrder.label("Z")
        );
        auto* hint = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
        hint->setScale(0.4f);
        hint->setAlignment(kCCTextAlignmentLeft);
        hint->setAnchorPoint({0.f, 1.f});
        hint->setID("paimbnails/mixed-values-hint");
        auto size = m_mainLayer->getContentSize();
        hint->setPosition({12.f, size.height - 12.f});
        m_mainLayer->addChild(hint, 50);
        m_fields->hint = hint;

        // If fields show Mixed, clear so the user can type a concrete value.
        auto clearMixed = [](CCTextInputNode* input, bool isMixed) {
            if (!input || !isMixed) return;
            input->setString("");
        };
        clearMixed(m_editorLayerInput, layer.mixed());
        clearMixed(m_editorLayer2Input, layer2.mixed());
        clearMixed(m_zOrderInput, zOrder.mixed());

        return true;
    }

    $override
    void textChanged(CCTextInputNode* node) {
        if (!on() || m_fields->applying || !node) {
            return SetGroupIDLayer::textChanged(node);
        }

        // Let vanilla update internal values first when possible.
        SetGroupIDLayer::textChanged(node);

        auto raw = node->getString();
        auto parsed = parseIntLoose(raw);
        if (!parsed) return;

        m_fields->applying = true;
        int v = *parsed;

        if (node == m_editorLayerInput) {
            m_editorLayerValue = v;
            m_editorLayerEdited = true;
            applyToAll([v](GameObject* o) { o->m_editorLayer = static_cast<short>(v); });
            this->updateEditorLayerID();
            this->updateEditorLabel();
        } else if (node == m_editorLayer2Input) {
            m_editorLayer2Value = v;
            m_editorLayerEdited = true;
            applyToAll([v](GameObject* o) { o->m_editorLayer2 = static_cast<short>(v); });
            this->updateEditorLayerID2();
            this->updateEditorLabel2();
        } else if (node == m_zOrderInput) {
            m_zOrderValue = v;
            applyToAll([v](GameObject* o) { o->m_zOrder = v; });
            this->updateZOrder();
            this->updateZOrderLabel();
        }

        m_fields->applying = false;
    }
};
