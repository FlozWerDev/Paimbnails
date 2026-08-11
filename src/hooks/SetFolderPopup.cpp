#include <Geode/modify/SetFolderPopup.hpp>
#include <Geode/modify/SetTextPopup.hpp>

using namespace geode::prelude;

namespace {
SetFolderPopup* s_openingFolderPopup = nullptr;
}

class $modify(PaimonSetTextPopupLifetime, SetTextPopup) {
    struct Fields {
        Ref<SetFolderPopup> m_folderPopup = nullptr;
    };

    $override
    void show() {
        if (s_openingFolderPopup) {
            m_fields->m_folderPopup = s_openingFolderPopup;
        }
        SetTextPopup::show();
    }
};

class $modify(PaimonSetFolderPopupLifetime, SetFolderPopup) {
    $override
    void onSetFolderName(CCObject* sender) {
        auto* previous = s_openingFolderPopup;
        s_openingFolderPopup = this;
        SetFolderPopup::onSetFolderName(sender);
        s_openingFolderPopup = previous;
    }
};
