#include <Geode/modify/SetFolderPopup.hpp>
#include <Geode/binding/SetTextPopup.hpp>

using namespace geode::prelude;

class $modify(PaimonSetFolderPopupLifetime, SetFolderPopup) {
    $override
    void onSetFolderName(CCObject* sender) {
        Ref<SetFolderPopup> keepAlive(this);
        SetFolderPopup::onSetFolderName(sender);

        auto* scene = CCDirector::get()->getRunningScene();
        if (!scene) return;
        auto* text = findFirstChildRecursive<SetTextPopup>(scene, [this](SetTextPopup* popup) {
            return popup->m_delegate == this;
        });
        if (!text || text->getChildByID("folder-popup-lifetime"_spr)) return;

        // keep the delegate alive until its name dialog is destroyed.
        auto* lifetime = CCNode::create();
        lifetime->setID("folder-popup-lifetime"_spr);
        lifetime->setUserObject(this);
        text->addChild(lifetime);
    }
};
