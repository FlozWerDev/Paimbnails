// Scrollable overview of all group IDs used by the selection.
// External modules can request a rebuild via group_view::updateGroupView().

#include "../EditorModule.hpp"
#include "../api/Events.hpp"

#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/SetGroupIDLayer.hpp>
#include <Geode/modify/SetGroupIDLayer.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <algorithm>
#include <set>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

namespace {

void collectGroups(GameObject* obj, std::set<int>& out) {
    if (!obj || !obj->m_groups) return;
    for (short i = 0; i < obj->m_groupCount; ++i) {
        int g = obj->m_groups->at(static_cast<size_t>(i));
        if (g > 0) out.insert(g);
    }
}

void fillScroll(ScrollLayer* scroll, std::set<int> const& groups) {
    if (!scroll) return;
    scroll->m_contentLayer->removeAllChildren();
    for (int g : groups) {
        auto* lab = CCLabelBMFont::create(fmt::format("G{}", g).c_str(), "chatFont.fnt");
        lab->setScale(0.5f);
        scroll->m_contentLayer->addChild(lab);
    }
    scroll->m_contentLayer->updateLayout();
    scroll->scrollToTop();
}

// Weak registry of open group-view UIs for updateGroupView().
struct OpenView {
    WeakRef<SetGroupIDLayer> layer;
    WeakRef<ScrollLayer> scroll;
    WeakRef<CCLabelBMFont> title;
};
std::vector<OpenView>& openViews() {
    static std::vector<OpenView> v;
    return v;
}

void pruneOpenViews() {
    auto& v = openViews();
    v.erase(
        std::remove_if(v.begin(), v.end(), [](OpenView const& o) {
            return !o.layer.lock() || !o.scroll.lock();
        }),
        v.end()
    );
}

void rebuildAllOpenViews() {
    pruneOpenViews();
    for (auto& o : openViews()) {
        auto layer = o.layer.lock();
        auto scroll = o.scroll.lock();
        auto title = o.title.lock();
        if (!layer || !scroll) continue;
        auto* self = static_cast<SetGroupIDLayer*>(layer.data());
        std::set<int> groups;
        if (self->m_targetObject) collectGroups(self->m_targetObject, groups);
        if (self->m_targetObjects) {
            for (auto* obj : CCArrayExt<GameObject*>(self->m_targetObjects)) {
                collectGroups(obj, groups);
            }
        }
        fillScroll(scroll.data(), groups);
        if (title) title->setString(fmt::format("{} groups", groups.size()).c_str());
    }
}

} // namespace

$execute {
    GroupViewUpdateEvent().listen(+[]() -> bool {
        rebuildAllOpenViews();
        return false; // propagate
    }).leak();
}

class $modify(PaimonGroupViewLayer, SetGroupIDLayer) {
    struct Fields {
        Ref<ScrollLayer> scroll;
        Ref<CCLabelBMFont> title;
        ~Fields() { pruneOpenViews(); }
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "SetGroupIDLayer::init");
    }

    $override
    bool init(GameObject* obj, CCArray* objs) {
        if (!SetGroupIDLayer::init(obj, objs)) return false;
        if (!moduleEnabled("editor-mod-group-view") || !m_mainLayer) return true;

        std::set<int> groups;
        if (obj) collectGroups(obj, groups);
        if (objs) {
            for (auto* o : CCArrayExt<GameObject*>(objs)) collectGroups(o, groups);
        }
        if (groups.size() <= 8) return true;

        auto* scroll = ScrollLayer::create({200.f, 90.f});
        scroll->setID("paimbnails/group-view-scroll");
        scroll->m_contentLayer->setLayout(
            ColumnLayout::create()
                ->setAxisReverse(true)
                ->setAutoScale(false)
                ->setGap(2.f)
                ->setAxisAlignment(AxisAlignment::End)
        );

        auto size = m_mainLayer->getContentSize();
        scroll->setPosition({size.width - 120.f, size.height * 0.35f});
        m_mainLayer->addChild(scroll, 20);

        auto* title = CCLabelBMFont::create(
            fmt::format("{} groups", groups.size()).c_str(), "bigFont.fnt"
        );
        title->setScale(0.25f);
        title->setID("paimbnails/group-view-title");
        title->setPosition({size.width - 120.f, size.height * 0.35f + 55.f});
        m_mainLayer->addChild(title, 20);

        m_fields->scroll = scroll;
        m_fields->title = title;
        fillScroll(scroll, groups);

        openViews().push_back(OpenView{
            WeakRef(static_cast<SetGroupIDLayer*>(this)),
            WeakRef(scroll),
            WeakRef(title),
        });
        return true;
    }
};
