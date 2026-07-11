// Next Free group ID starts from a configurable offset.

#include "../EditorModule.hpp"

#include <Geode/binding/EffectGameObject.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/SetGroupIDLayer.hpp>
#include <Geode/modify/SetGroupIDLayer.hpp>
#include <Geode/ui/TextInput.hpp>
#include <unordered_set>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace paimon::editor;

namespace {

constexpr char const* kOffsetKey = "paim-next-free-group-offset";

int loadOffset() {
    return static_cast<int>(Mod::get()->getSavedValue<int64_t>(kOffsetKey, 1));
}
void saveOffset(int v) {
    Mod::get()->setSavedValue<int64_t>(kOffsetKey, std::max(1, v));
}

void collectUsed(GameObject* obj, std::unordered_set<int>& used) {
    if (!obj) return;
    if (obj->m_groups) {
        for (short i = 0; i < obj->m_groupCount; ++i) {
            used.insert(obj->m_groups->at(static_cast<size_t>(i)));
        }
    }
    if (auto* e = typeinfo_cast<EffectGameObject*>(obj)) {
        if (e->m_targetGroupID > 0) used.insert(e->m_targetGroupID);
        if (e->m_centerGroupID > 0) used.insert(e->m_centerGroupID);
    }
}

int nextFreeFrom(std::unordered_set<int> const& used, int offset) {
    int id = std::max(1, offset);
    while (id <= 9999 && used.contains(id)) ++id;
    return std::min(id, 9999);
}

} // namespace

class $modify(PaimonNextFreeOffset, SetGroupIDLayer) {
    struct Fields {
        TextInput* offsetInput = nullptr;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "SetGroupIDLayer::init");
    }

    $override
    bool init(GameObject* obj, CCArray* objs) {
        if (!SetGroupIDLayer::init(obj, objs)) return false;
        if (!moduleEnabled("editor-mod-next-free-offset") || !m_mainLayer) return true;

        auto* menu = m_mainLayer->getChildByID("next-free-menu");
        if (!menu) return true;

        auto* input = TextInput::create(40.f, "1");
        input->setScale(0.5f);
        input->setID("paimbnails/next-free-offset");
        input->setCommonFilter(CommonFilter::Int);
        input->setString(fmt::format("{}", loadOffset()));
        input->setCallback([](std::string const& s) {
            if (auto v = numFromString<int>(s)) saveOffset(v.unwrap());
        });
        menu->addChild(input);
        menu->updateLayout();
        m_fields->offsetInput = input;
        return true;
    }

    $override
    void onNextGroupID1(CCObject* sender) {
        if (!moduleEnabled("editor-mod-next-free-offset")) {
            return SetGroupIDLayer::onNextGroupID1(sender);
        }
        std::unordered_set<int> used;
        if (m_targetObject) collectUsed(m_targetObject, used);
        if (m_targetObjects) {
            for (auto* o : CCArrayExt<GameObject*>(m_targetObjects)) collectUsed(o, used);
        }
        // Also scan whole level if editor is open
        if (auto* lel = LevelEditorLayer::get()) {
            if (lel->m_objects) {
                for (auto* o : CCArrayExt<GameObject*>(lel->m_objects)) collectUsed(o, used);
            }
        }
        int offset = loadOffset();
        if (m_fields->offsetInput) {
            if (auto v = numFromString<int>(m_fields->offsetInput->getString())) {
                offset = v.unwrap();
                saveOffset(offset);
            }
        }
        m_groupIDValue = nextFreeFrom(used, offset);
        this->updateGroupIDLabel();
    }
};
