// Jump To Page: replaces the vanilla capped SetIDPopup with our own, and
// optionally returns to the page you left a search on.

#include "../InfoModule.hpp"
#include "../services/InfoStore.hpp"
#include "../services/SearchObjectBuilder.hpp"
#include "../ui/JumpToPagePopup.hpp"
#include "../../../framework/HookConventions.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <algorithm>
#include <string>

using namespace geode::prelude;

namespace {

bool jumpEnabled() {
    return paimon::info::moduleEnabled("info-mod-jump-page");
}

bool rememberEnabled() {
    return paimon::info::subEnabled("info-mod-jump-page", "info-jump-remember-page", true);
}

using paimon::info::searchKey;

} // namespace

class $modify(PaimonInfoSuiteBrowser, LevelBrowserLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LevelBrowserLayer::init");
    }

    struct Fields {
        std::string m_key;
    };

    bool init(GJSearchObject* object) {
        // Restore before the base init so the first request already asks for the
        // remembered page instead of loading page 1 and then jumping.
        if (object && rememberEnabled() && object->m_page == 0) {
            if (auto stored = paimon::info::InfoStore::get().lastPage(searchKey(object))) {
                object->m_page = std::max(0, *stored - 1);
            }
        }

        if (!LevelBrowserLayer::init(object)) return false;
        m_fields->m_key = searchKey(object);
        return true;
    }

    void loadPage(GJSearchObject* object) {
        LevelBrowserLayer::loadPage(object);
        if (!object || !rememberEnabled()) return;
        paimon::info::InfoStore::get().setLastPage(searchKey(object), object->m_page + 1);
    }

    void onBack(CCObject* sender) {
        // Flush here rather than on every page turn: leaving the browser is the
        // natural checkpoint and keeps the file off the hot path.
        if (rememberEnabled()) paimon::info::InfoStore::get().save();
        LevelBrowserLayer::onBack(sender);
    }

    void onGoToPage(CCObject* sender) {
        if (!jumpEnabled() || !m_searchObject) {
            LevelBrowserLayer::onGoToPage(sender);
            return;
        }

        // m_lastPage is the last index the server reported; 0 means "unknown",
        // which happens on searches that never return a total.
        int pageCount = std::max(0, m_lastPage + 1);
        int current = m_searchObject->m_page + 1;

        auto self = WeakRef<LevelBrowserLayer>(this);
        auto popup = paimon::info::JumpToPagePopup::create(current, pageCount,
            [self](int page) {
                auto ref = self.lock();
                if (!ref) return;
                auto* layer = static_cast<PaimonInfoSuiteBrowser*>(ref.data());
                if (!layer->m_searchObject) return;
                layer->m_searchObject->m_page = std::max(0, page - 1);
                layer->loadPage(layer->m_searchObject);
            });

        if (popup) popup->show();
        else LevelBrowserLayer::onGoToPage(sender);
    }
};
