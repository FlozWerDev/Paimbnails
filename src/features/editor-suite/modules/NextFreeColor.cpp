// Next Free for color channels with configurable offset (BetterEdit idea).
// Hooks ColorSelectPopup custom color next-free path when available.

#include "../EditorModule.hpp"

#include <Geode/binding/ColorAction.hpp>
#include <Geode/binding/ColorSelectPopup.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/ColorSelectPopup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <unordered_set>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace paimon::editor;

namespace {

bool on() { return moduleEnabled("editor-mod-next-free-color"); }
constexpr char const* kKey = "paim-next-free-color-offset";

int loadOff() {
    return static_cast<int>(Mod::get()->getSavedValue<int64_t>(kKey, 1));
}
void saveOff(int v) {
    Mod::get()->setSavedValue<int64_t>(kKey, std::max(1, v));
}

bool channelUsed(GJEffectManager* mgr, int id) {
    if (!mgr || id <= 0) return false;
    auto* ca = mgr->getColorAction(id);
    if (!ca) return false;
    // Consider "used" if not default white fully opaque with no copy — heuristic.
    // Safer: mark used if any object references it is expensive; BE scans objects.
    // Light heuristic: non-default color or blending or copy.
    if (ca->m_copyID != 0) return true;
    if (ca->m_blending) return true;
    auto c = ca->m_color;
    if (c.r != 255 || c.g != 255 || c.b != 255) return true;
    return false;
}

int nextFreeColor(int offset) {
    auto* lel = LevelEditorLayer::get();
    auto* mgr = lel ? lel->m_effectManager : nullptr;
    int id = std::max(1, offset);
    for (; id <= 999; ++id) {
        if (!channelUsed(mgr, id)) return id;
    }
    return 999;
}

} // namespace

class $modify(PaimonNextFreeColor, ColorSelectPopup) {
    struct Fields {
        Ref<TextInput> offsetInput;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "ColorSelectPopup::init");
    }

    $override
    bool init(EffectGameObject* object, CCArray* objects, ColorAction* action) {
        if (!ColorSelectPopup::init(object, objects, action)) return false;
        if (!on() || !m_mainLayer) return true;

        auto* input = TextInput::create(36.f, "1");
        input->setScale(0.45f);
        input->setID("paimbnails/next-free-color-offset");
        input->setCommonFilter(CommonFilter::Int);
        input->setString(fmt::format("{}", loadOff()));
        input->setCallback([](std::string const& s) {
            if (auto v = numFromString<int>(s)) saveOff(v.unwrap());
        });
        auto size = m_mainLayer->getContentSize();
        input->setPosition({size.width - 50.f, size.height - 28.f});
        m_mainLayer->addChild(input, 50);
        m_fields->offsetInput = input;

        auto* lab = CCLabelBMFont::create("NF#", "chatFont.fnt");
        lab->setScale(0.35f);
        lab->setPosition({size.width - 50.f, size.height - 12.f});
        m_mainLayer->addChild(lab, 50);
        return true;
    }

    $override
    void onUpdateCustomColor(CCObject* sender) {
        if (!on()) {
            return ColorSelectPopup::onUpdateCustomColor(sender);
        }
        int off = loadOff();
        if (m_fields->offsetInput) {
            if (auto v = numFromString<int>(m_fields->offsetInput->getString())) {
                off = v.unwrap();
                saveOff(off);
            }
        }
        m_colorID = nextFreeColor(off);
        this->updateCustomColorIdx();
        this->updateColorLabels();
        this->updateOpacity();
    }
};
