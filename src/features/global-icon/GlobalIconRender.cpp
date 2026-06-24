#include "GlobalIconRender.hpp"
#include "GlobalIconTypes.hpp"
#include "services/GlobalIconClient.hpp"
#include "services/GlobalIconStorage.hpp"
#include "ui/GlobalIconViewPopup.hpp"

#include <Geode/Geode.hpp>

#define MORE_ICONS_EVENTS
#include <hiimjustin000.more_icons/include/MoreIcons.hpp>

using namespace geode::prelude;

namespace paimon::globalicon {

namespace {
    SimplePlayer* findSimplePlayerRec(CCNode* node, int depth = 0) {
        if (!node || depth > 6) return nullptr;
        for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
            if (!child) continue;
            if (auto* sp = typeinfo_cast<SimplePlayer*>(child)) return sp;
            if (auto* found = findSimplePlayerRec(child, depth + 1)) return found;
        }
        return nullptr;
    }

    void makeIconClickable(SimplePlayer* sp, int accountID, std::string const& username,
                           GlobalIconSlot const& cubeSlot) {
        if (!sp) return;
        if (sp->getChildByID("paimon-globalicon-touch"_spr)) return;

        CCSize spSize = sp->getContentSize();
        if (spSize.width < 1.f || spSize.height < 1.f) spSize = CCSize{30.f, 30.f};

        auto hit = CCLayerColor::create(ccc4(0, 0, 0, 0), spSize.width, spSize.height);
        if (!hit) return;
        hit->ignoreAnchorPointForPosition(false);
        hit->setAnchorPoint({0.5f, 0.5f});

        auto item = CCMenuItemExt::createSpriteExtra(hit,
            [accountID, username, cubeSlot](CCMenuItemSpriteExtra*) {
                if (auto p = GlobalIconViewPopup::create(accountID, username, cubeSlot)) p->show();
            });
        if (!item) return;
        item->setContentSize(spSize);
        item->setPosition({0, 0});

        auto menu = CCMenu::create();
        menu->setID("paimon-globalicon-touch"_spr);
        menu->setPosition(sp->getContentSize() / 2.f);
        menu->addChild(item);
        sp->addChild(menu, 100);
    }
}

void renderProfileCube(cocos2d::CCNode* searchRoot, int accountID) {
    if (!searchRoot || accountID <= 0) return;
    if (!Mod::get()->getSettingValue<bool>("global-icons-enabled")) return;
    if (!GlobalIconStorage::available()) return;

    WeakRef<CCNode> rootWeak = searchRoot;

    GlobalIconClient::get().getMetadata(accountID,
        [rootWeak, accountID](bool success, bool found, GlobalIconMeta const& meta) mutable {
            if (!success || !found || !meta.enabled) return;
            auto it = meta.icons.find("cube");
            if (it == meta.icons.end()) return;
            GlobalIconSlot cubeSlot = it->second;
            std::string username = meta.username;

            GlobalIconStorage::get().ensureIcon(accountID, cubeSlot,
                [rootWeak, accountID, username, cubeSlot](bool ok, std::string const& iconName) mutable {
                    if (!ok || iconName.empty()) return;
                    auto root = rootWeak.lock();
                    if (!root) return;
                    auto* sp = findSimplePlayerRec(root.data());
                    if (!sp) return;
                    if (auto* info = more_icons::getIcon(iconName, IconType::Cube)) {
                        more_icons::updateSimplePlayer(sp, info);
                    }
                    makeIconClickable(sp, accountID, username, cubeSlot);
                });
        });
}

} // namespace paimon::globalicon
