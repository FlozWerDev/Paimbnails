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

    // node-ids gives the profile icon a stable id; the recursive walk is only a
    // fallback for when that mod isn't installed.
    SimplePlayer* findProfileIcon(CCNode* root) {
        if (!root) return nullptr;
        if (auto* byID = typeinfo_cast<SimplePlayer*>(root->getChildByIDRecursive("player-icon"))) {
            return byID;
        }
        return findSimplePlayerRec(root);
    }

    void makeIconClickable(SimplePlayer* sp, int accountID, std::string const& username,
                           GlobalIconMeta const& meta) {
        if (!sp) return;
        if (sp->getChildByID("paimon-globalicon-touch"_spr)) return;

        CCSize spSize = sp->getContentSize();
        if (spSize.width < 1.f || spSize.height < 1.f) spSize = CCSize{30.f, 30.f};

        auto hit = CCLayerColor::create(ccc4(0, 0, 0, 0), spSize.width, spSize.height);
        if (!hit) return;
        hit->ignoreAnchorPointForPosition(false);
        hit->setAnchorPoint({0.5f, 0.5f});

        auto item = CCMenuItemExt::createSpriteExtra(hit,
            [accountID, username, meta](CCMenuItemSpriteExtra*) {
                if (auto p = GlobalIconViewPopup::create(accountID, username, meta)) p->show();
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

    // Paints the profile icon with the player's shared cube and wires the tap
    // target. Assumes the cube is already registered in More Icons.
    void applyToProfile(CCNode* root, int accountID, GlobalIconMeta const& meta) {
        auto it = meta.icons.find("cube");
        if (it == meta.icons.end()) return;

        auto* sp = findProfileIcon(root);
        if (!sp) return;

        auto regName = GlobalIconStorage::registeredName(accountID, it->second);
        if (auto* info = more_icons::getIcon(regName, IconType::Cube)) {
            more_icons::updateSimplePlayer(sp, info);
        }
        makeIconClickable(sp, accountID, meta.username, meta);
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

            auto root = rootWeak.lock();
            if (!root) return;

            // Already downloaded and registered: skip straight to drawing, which
            // is the common path once the cache is warm.
            if (GlobalIconStorage::get().isReady(accountID, it->second)) {
                applyToProfile(root.data(), accountID, meta);
                return;
            }

            GlobalIconStorage::get().ensureIcon(accountID, it->second,
                [rootWeak, accountID, meta](bool ok, std::string const& iconName) mutable {
                    if (!ok || iconName.empty()) return;
                    auto root = rootWeak.lock();
                    if (!root) return;
                    applyToProfile(root.data(), accountID, meta);
                });
        });
}

} // namespace paimon::globalicon
