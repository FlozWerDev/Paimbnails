// Right-click a create-tab object to favorite it.
// "Favs" button next to Search opens a list of favorites.

#include "../EditorModule.hpp"
#include "../EditorAssets.hpp"
#include "../EditorUIKit.hpp"
#include "../services/ObjectFavoritesStore.hpp"
#include "../tabs/EditorTabsAPI.hpp"

#include <Geode/binding/CreateMenuItem.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/ObjectToolbox.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/cocos.hpp>

#include "../../../framework/HookConventions.hpp"
#include "../../../utils/EditorContext.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;
using namespace paimon::editor::assets;
using namespace paimon::editor::tabs;

namespace {

bool favsOn() {
    return moduleEnabled("editor-mod-object-favorites")
        || (Mod::get()->hasSetting("editor-mod-object-favorites")
            && Mod::get()->getSettingValue<bool>("editor-suite-enable")
            && Mod::get()->getSettingValue<bool>("editor-mod-object-favorites"));
}

bool settingOn(char const* key, bool def = true) {
    auto* mod = Mod::get();
    if (!mod || !mod->hasSetting(key)) return def;
    return mod->getSettingValue<bool>(key);
}

int createItemUnderMouse(EditorUI* ui, CCPoint mouse) {
    if (!ui || !ui->m_createButtonArray) return -1;
    for (auto* item : CCArrayExt<CreateMenuItem*>(ui->m_createButtonArray)) {
        if (!item || !item->isVisible()) continue;
        auto* parent = item->getParent();
        if (parent && !parent->isVisible()) continue;
        auto bb = item->boundingBox();
        if (parent) {
            auto bl = parent->convertToWorldSpace({bb.getMinX(), bb.getMinY()});
            auto tr = parent->convertToWorldSpace({bb.getMaxX(), bb.getMaxY()});
            CCRect world{bl.x, bl.y, tr.x - bl.x, tr.y - bl.y};
            if (world.containsPoint(mouse)) {
                int id = item->m_objectID;
                if (id <= 0) id = item->getTag();
                return id;
            }
        }
    }
    return -1;
}

class FavoritesLayer : public CCLayer {
public:
    EditorUI* m_ui = nullptr;

    static FavoritesLayer* create(EditorUI* ui);
    bool init(EditorUI* ui);
    void addToEditor(EditorUI* ui) { ui->addChild(this, 200); }
    void keyBackClicked() override { this->removeFromParentAndCleanup(true); }
    void onClose(CCObject*) { this->removeFromParentAndCleanup(true); }
    void onClear(CCObject*);
    void onPick(CCObject* sender);
};

void openFavorites(EditorUI* ui) {
    if (!ui) return;
    if (ui->getChildByID("paimbnails/object-favorites-layer")) return;
    if (auto* layer = FavoritesLayer::create(ui)) {
        layer->addToEditor(ui);
    }
}

FavoritesLayer* FavoritesLayer::create(EditorUI* ui) {
    auto* ret = new FavoritesLayer();
    if (ret && ret->init(ui)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void FavoritesLayer::onClear(CCObject*) {
    ObjectFavoritesStore::get().clear();
    auto* ui = m_ui;
    this->removeFromParentAndCleanup(true);
    if (ui) openFavorites(ui);
}

void FavoritesLayer::onPick(CCObject* sender) {
    auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn || !m_ui) return;
    int id = btn->getTag();
    // See ObjectSearch::onPick — updateCreateMenu(true) is unsafe with other
    // editor-tab mods hooked into the create bars.
    m_ui->onCreateObject(id);
    this->removeFromParentAndCleanup(true);
}

bool FavoritesLayer::init(EditorUI* ui) {
    if (!CCLayer::init()) return false;
    m_ui = ui;
    this->setTouchEnabled(true);
    this->setKeypadEnabled(true);
    this->setID("paimbnails/object-favorites-layer");

    auto win = CCDirector::get()->getWinSize();
    this->addChild(CCLayerColor::create({0, 0, 0, 180}, win.width, win.height), -1);

    auto* panel = CCScale9Sprite::create("GJ_square01.png");
    if (!panel) panel = CCScale9Sprite::create("square02_001.png");
    panel->setContentSize({340.f, 240.f});
    panel->setPosition(win / 2.f);
    this->addChild(panel);

    auto* title = CCLabelBMFont::create("Favorite Objects", "goldFont.fnt");
    title->setScale(0.6f);
    title->setPosition(win / 2.f + ccp(0.f, 100.f));
    this->addChild(title);

    auto* tip = CCLabelBMFont::create("Right-click objects in the create tab to add/remove", "chatFont.fnt");
    tip->setScale(0.42f);
    tip->setPosition(win / 2.f + ccp(0.f, 82.f));
    tip->setOpacity(170);
    this->addChild(tip);

    auto* listBg = CCScale9Sprite::create("square02b_001.png");
    if (listBg) {
        listBg->setContentSize({290.f, 150.f});
        listBg->setColor({0, 0, 0});
        listBg->setOpacity(90);
        listBg->setPosition(win / 2.f + ccp(0.f, -15.f));
        this->addChild(listBg);
    }

    auto* scroll = ScrollLayer::create({280.f, 140.f});
    scroll->setPosition(win / 2.f + ccp(-140.f, -85.f));
    this->addChild(scroll);

    auto& store = ObjectFavoritesStore::get();
    auto* menu = CCMenu::create();
    menu->setContentSize({260.f, std::max(20.f, static_cast<float>(store.ids().size()) * 30.f)});
    menu->setLayout(
        ColumnLayout::create()
            ->setGap(4.f)
            ->setAxisReverse(true)
            ->setAutoScale(false)
    );

    if (store.ids().empty()) {
        auto* empty = CCLabelBMFont::create("No favorites yet", "bigFont.fnt");
        empty->setScale(0.35f);
        empty->setPosition(win / 2.f);
        this->addChild(empty);
    } else {
        auto* box = ObjectToolbox::sharedState();
        for (int id : store.ids()) {
            std::string name = "?";
            if (box) {
                auto it = box->m_allKeys.find(id);
                if (it != box->m_allKeys.end()) name = it->second;
            }
            auto label = fmt::format("{} — {}", id, name);
            auto* spr = ButtonSprite::create(
                label.c_str(), 270, true, "chatFont.fnt", "GJ_button_05.png", 22.f, 0.6f
            );
            auto* btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(FavoritesLayer::onPick)
            );
            btn->setTag(id);
            menu->addChild(btn);
        }
    }
    menu->updateLayout();
    scroll->m_contentLayer->addChild(menu);
    scroll->m_contentLayer->setContentSize(menu->getContentSize());
    scroll->scrollToTop();

    // Close at the panel's top-left corner, vanilla style
    auto* closeMenu = CCMenu::create();
    closeMenu->setPosition(win / 2.f + ccp(-170.f, 120.f));
    auto* closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
    if (closeSpr) closeSpr->setScale(0.8f);
    auto* close = CCMenuItemSpriteExtra::create(
        closeSpr, this, menu_selector(FavoritesLayer::onClose)
    );
    closeMenu->addChild(close);
    this->addChild(closeMenu);

    auto* clearMenu = CCMenu::create();
    clearMenu->setPosition(win / 2.f + ccp(0.f, -105.f));
    auto* clearSpr = ButtonSprite::create("Clear All", "bigFont.fnt", "GJ_button_06.png", 0.4f);
    auto* clearBtn = CCMenuItemSpriteExtra::create(
        clearSpr, this, menu_selector(FavoritesLayer::onClear)
    );
    clearMenu->addChild(clearBtn);
    this->addChild(clearMenu);

    return true;
}

} // namespace

class $modify(PaimonFavoritesUI, EditorUI) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void onOpenFavorites(CCObject*) {
        if (!favsOn()) return;
        openFavorites(this);
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (!favsOn()) return true;

        // With native tabs, Favs is the side tab. Otherwise put a toolbar button.
        if (Mod::get()->getSettingValue<bool>("editor-mod-native-tabs")) {
            return true;
        }

        auto* menu = typeinfo_cast<CCMenu*>(this->getChildByID("toolbar-categories-menu"));
        if (!menu) menu = typeinfo_cast<CCMenu*>(this->getChildByID("build-tabs-menu"));
        if (!menu) return true;

        // Icon-only opener when native tabs are off (no text "Favs" plate).
        auto* btn = editBarToolButton(
            files::favorites, { "GJ_starsIcon_001.png" },
            [this] { this->onOpenFavorites(nullptr); }
        );
        if (!btn) return true;
        btn->setID("paimbnails/object-favorites-btn");
        menu->addChild(btn);
        menu->updateLayout();
        return true;
    }
};

// Right-click create objects → toggle favorite (not Alt — Alt is canvas rotate)
$execute {
    MouseInputEvent().listen([](MouseInputData& data) -> bool {
        if (data.button != MouseInputData::Button::Right) return ListenerResult::Propagate;
        if (data.action != MouseInputData::Action::Press) return ListenerResult::Propagate;
        if (!favsOn() || !paimon::isEditorScene()) return ListenerResult::Propagate;
        // Leave Alt+RMB for canvas rotate
        if (data.modifiers & KeyboardModifier::Alt) return ListenerResult::Propagate;

        auto* ui = EditorUI::get();
        if (!ui) return ListenerResult::Propagate;

        int id = createItemUnderMouse(ui, getMousePos());
        if (id <= 0) return ListenerResult::Propagate;

        bool now = ObjectFavoritesStore::get().toggle(id);
        std::string name;
        if (auto* box = ObjectToolbox::sharedState()) {
            auto it = box->m_allKeys.find(id);
            if (it != box->m_allKeys.end()) name = it->second;
        }
        auto msg = now
            ? fmt::format("★ Favorited {}", name.empty() ? std::to_string(id) : name)
            : fmt::format("☆ Removed {}", name.empty() ? std::to_string(id) : name);
        Notification::create(msg, now ? NotificationIcon::Success : NotificationIcon::Info)->show();
        return ListenerResult::Stop;
    }, 50).leak(); // before capture/default, after nothing critical for plain RMB
}

// Register as native tab next to Search — ACTION ONLY (opens overlay, no gray panel).
$on_mod(Loaded) {
    if (!settingOn("editor-suite-enable", true) || !settingOn("editor-mod-native-tabs", true)) {
        return;
    }
    if (!settingOn("editor-mod-object-favorites", true)) return;

    TabDesc d;
    d.id = "flozwer.paimbnails2/favorites-tab";
    d.mode = Mode::Build;
    d.displayName = "Favs";
    d.displayOrder = 30;
    d.actionOnly = true;
    d.onActivate = [] { openFavorites(EditorUI::get()); };
    d.createIcon = tabIcon(files::favorites, "GJ_starsIcon_001.png");
    d.isEnabled = [] {
        return settingOn("editor-suite-enable")
            && settingOn("editor-mod-native-tabs")
            && settingOn("editor-mod-object-favorites");
    };
    registerTab(std::move(d));
}

// Public entry used by EditorTabsBootstrap (and other modules).
namespace paimon::editor {
void openObjectFavoritesOverlay() {
    openFavorites(EditorUI::get());
}
}
