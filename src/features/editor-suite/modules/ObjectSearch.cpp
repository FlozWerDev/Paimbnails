// Search objects by ID / name and select them in the create tab.

#include "../EditorModule.hpp"
#include "../EditorAssets.hpp"
#include "../EditorHelpers.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/ObjectToolbox.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <cctype>
#include <string>
#include <vector>

#include "../../../framework/HookConventions.hpp"
#include "../../../utils/EditorContext.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;
using namespace paimon::editor::assets;

namespace {

struct Hit {
    int id = 0;
    std::string name;
};

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool matchQuery(int id, std::string const& name, std::string const& qRaw) {
    auto q = toLower(qRaw);
    if (q.empty()) return true;
    if (q.rfind("id:", 0) == 0) {
        auto num = q.substr(3);
        return std::to_string(id).find(num) != std::string::npos;
    }
    if (q.rfind("exact:", 0) == 0) {
        return toLower(name) == q.substr(6);
    }
    if (std::to_string(id).find(q) != std::string::npos) return true;
    return toLower(name).find(q) != std::string::npos;
}

std::vector<Hit> searchObjects(std::string const& query, int limit = 40) {
    std::vector<Hit> out;
    auto* box = ObjectToolbox::sharedState();
    if (!box) return out;
    for (auto const& [id, name] : box->m_allKeys) {
        if (!matchQuery(id, name, query)) continue;
        out.push_back({id, name});
        if (static_cast<int>(out.size()) >= limit) break;
    }
    return out;
}

// Lightweight overlay layer for search results
class ObjectSearchLayer : public CCLayer {
public:
    EditorUI* m_ui = nullptr;
    TextInput* m_input = nullptr;
    ScrollLayer* m_scroll = nullptr;
    CCNode* m_list = nullptr;

    static ObjectSearchLayer* create(EditorUI* ui) {
        auto* ret = new ObjectSearchLayer();
        if (ret && ret->init(ui)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool init(EditorUI* ui) {
        if (!CCLayer::init()) return false;
        m_ui = ui;
        this->setTouchEnabled(true);
        this->setKeypadEnabled(true);
        this->setID("paimbnails/object-search-layer");

        auto win = CCDirector::get()->getWinSize();
        auto* dim = CCLayerColor::create({0, 0, 0, 180}, win.width, win.height);
        this->addChild(dim, -1);

        auto* panel = CCScale9Sprite::create("GJ_square01.png");
        if (!panel) panel = CCScale9Sprite::create("square02_001.png");
        panel->setContentSize({360.f, 250.f});
        panel->setPosition(win / 2.f);
        this->addChild(panel);

        auto* title = CCLabelBMFont::create("Object Search", "goldFont.fnt");
        title->setScale(0.65f);
        title->setPosition(win / 2.f + ccp(0.f, 105.f));
        this->addChild(title);

        m_input = TextInput::create(280.f, "id:901 or name...");
        m_input->setPosition(win / 2.f + ccp(0.f, 75.f));
        m_input->setCallback([this](std::string const&) { rebuild(); });
        this->addChild(m_input);
        m_input->focus();

        // Inset list area so rows read as one column
        auto* listBg = CCScale9Sprite::create("square02b_001.png");
        if (listBg) {
            listBg->setContentSize({310.f, 150.f});
            listBg->setColor({0, 0, 0});
            listBg->setOpacity(90);
            listBg->setPosition(win / 2.f + ccp(0.f, -25.f));
            this->addChild(listBg);
        }

        m_scroll = ScrollLayer::create({300.f, 140.f});
        m_scroll->setPosition(win / 2.f + ccp(-150.f, -95.f));
        this->addChild(m_scroll);

        auto* hint = CCLabelBMFont::create("Prefix with id: or exact: to narrow down", "chatFont.fnt");
        hint->setScale(0.42f);
        hint->setOpacity(160);
        hint->setPosition(win / 2.f + ccp(0.f, -112.f));
        this->addChild(hint);

        // Close at the panel's top-left corner, vanilla style
        auto* closeMenu = CCMenu::create();
        closeMenu->setPosition(win / 2.f + ccp(-180.f, 125.f));
        auto* closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
        if (closeSpr) closeSpr->setScale(0.8f);
        auto* close = CCMenuItemSpriteExtra::create(
            closeSpr, this, menu_selector(ObjectSearchLayer::onClose)
        );
        closeMenu->addChild(close);
        this->addChild(closeMenu);

        rebuild();
        return true;
    }

    void rebuild() {
        if (!m_scroll) return;
        m_scroll->m_contentLayer->removeAllChildren();
        m_scroll->m_contentLayer->setLayout(
            ColumnLayout::create()
                ->setAxisReverse(true)
                ->setAutoScale(false)
                ->setGap(4.f)
                ->setAxisAlignment(AxisAlignment::End)
        );

        auto q = m_input ? m_input->getString() : "";
        auto hits = searchObjects(q, 50);
        auto* menu = CCMenu::create();
        menu->setContentSize({280.f, static_cast<float>(hits.size()) * 28.f});
        menu->setLayout(ColumnLayout::create()->setGap(4.f)->setAxisReverse(true)->setAutoScale(false));

        for (auto const& h : hits) {
            auto label = fmt::format("{} — {}", h.id, h.name.empty() ? "?" : h.name);
            auto* spr = ButtonSprite::create(
                label.c_str(), 290, true, "chatFont.fnt", "GJ_button_05.png", 22.f, 0.6f
            );
            auto* btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(ObjectSearchLayer::onPick)
            );
            btn->setTag(h.id);
            menu->addChild(btn);
        }
        menu->updateLayout();
        m_scroll->m_contentLayer->addChild(menu);
        m_scroll->m_contentLayer->setContentSize(menu->getContentSize());
        m_scroll->scrollToTop();
    }

    void onPick(CCObject* sender) {
        auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
        if (!btn || !m_ui) return;
        int id = btn->getTag();
        // onCreateObject already selects the object for placement. Do NOT call
        // updateCreateMenu(true): it rebuilds every create bar and crashes
        // when other tab mods (e.g. Alphalaneous' EditorTab API) have hooks
        // with scheduled relayouts on those bars.
        m_ui->onCreateObject(id);
        this->removeFromParentAndCleanup(true);
    }

    void onClose(CCObject*) {
        this->removeFromParentAndCleanup(true);
    }

    void keyBackClicked() override {
        this->removeFromParentAndCleanup(true);
    }
};

// Shared entry used by the Search tab and keybind.
void openObjectSearchFromUI(EditorUI* ui) {
    if (!ui || !moduleEnabled("editor-mod-object-search")) return;
    if (ui->getChildByID("paimbnails/object-search-layer")) return;
    if (auto* layer = ObjectSearchLayer::create(ui)) {
        ui->addChild(layer, 200);
    }
}

} // namespace

namespace paimon::editor {
void openObjectSearchOverlay() {
    openObjectSearchFromUI(EditorUI::get());
}
} // namespace paimon::editor

class $modify(PaimonObjectSearchUI, EditorUI) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void onOpenSearch(CCObject*) {
        openObjectSearchFromUI(this);
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (!moduleEnabled("editor-mod-object-search")) return true;

        // With native tabs, Search is the tab; keep a small toolbar button only when
        // native tabs are off (Ctrl+Space always works either way).
        bool nativeTabs = Mod::get()->getSettingValue<bool>("editor-mod-native-tabs");
        if (nativeTabs) return true;

        auto* menu = typeinfo_cast<CCMenu*>(this->getChildByID("toolbar-categories-menu"));
        if (!menu) menu = typeinfo_cast<CCMenu*>(this->getChildByID("build-tabs-menu"));
        if (!menu) return true;

        // Custom: paim_object-search.png  |  Fallback: find / zoom
        auto* spr = loadIcon(
            files::objectSearch,
            { "gj_findBtn_001.png", "GJ_zoomInBtn_001.png" },
            0.7f
        );
        auto* btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(PaimonObjectSearchUI::onOpenSearch)
        );
        btn->setID("paimbnails/object-search-btn");
        menu->addChild(btn);
        menu->updateLayout();
        return true;
    }

    $override
    void keyDown(enumKeyCodes key, double timestamp) {
        // Ctrl+Space opens search
        if (moduleEnabled("editor-mod-object-search")
            && paimon::isEditorScene()
            && key == KEY_Space) {
            auto* kd = CCKeyboardDispatcher::get();
            if (kd && kd->getControlKeyPressed() && !focusedTextInput()) {
                this->onOpenSearch(nullptr);
                return;
            }
        }
        EditorUI::keyDown(key, timestamp);
    }
};
