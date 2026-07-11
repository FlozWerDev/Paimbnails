// Tooltips with object ID when hovering build-tab object buttons (desktop).

#include "../EditorModule.hpp"

#include <Geode/binding/CreateMenuItem.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/utils/cocos.hpp>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

class $modify(PaimonObjectTooltipsUI, EditorUI) {
    struct Fields {
        CCLabelBMFont* tip = nullptr;
        CCLayerColor* bg = nullptr;
        int lastId = -1;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void hideTip() {
        if (m_fields->tip) m_fields->tip->setVisible(false);
        if (m_fields->bg) m_fields->bg->setVisible(false);
        m_fields->lastId = -1;
    }

    void showTip(int objectId, CCPoint worldPos) {
        if (!m_fields->tip || !m_fields->bg) return;
        bool showId = moduleSetting<bool>("editor-mod-tooltips-show-id", true);
        std::string text = showId
            ? fmt::format("Object {}", objectId)
            : fmt::format("ID {}", objectId);
        m_fields->tip->setString(text.c_str());
        m_fields->tip->setScale(
            0.3f * static_cast<float>(moduleSetting<double>("editor-mod-tooltips-scale", 1.0))
        );

        auto size = m_fields->tip->getContentSize() * m_fields->tip->getScale();
        m_fields->bg->setContentSize(size + CCSize{10.f, 6.f});
        m_fields->bg->setPosition(worldPos + ccp(12.f, 12.f));
        m_fields->tip->setPosition(
            m_fields->bg->getPosition() + ccp(5.f, 3.f)
        );
        m_fields->tip->setAnchorPoint({0.f, 0.f});
        m_fields->bg->setAnchorPoint({0.f, 0.f});
        m_fields->bg->setVisible(true);
        m_fields->tip->setVisible(true);
        m_fields->lastId = objectId;
    }

    void tooltipTick(float) {
        if (!moduleEnabled("editor-mod-object-tooltips")) {
            hideTip();
            return;
        }
#ifdef GEODE_IS_DESKTOP
        auto mouse = getMousePos();
        // Walk create buttons under the cursor
        int foundId = -1;
        if (m_createButtonArray) {
            for (auto* item : CCArrayExt<CreateMenuItem*>(m_createButtonArray)) {
                if (!item || !item->isVisible()) continue;
                auto world = item->convertToWorldSpace({0, 0});
                auto size = item->getScaledContentSize();
                auto anchor = item->getAnchorPoint();
                CCRect rect{
                    world.x - size.width * anchor.x,
                    world.y - size.height * anchor.y,
                    size.width,
                    size.height
                };
                // Prefer parent transform
                auto bb = item->boundingBox();
                auto parent = item->getParent();
                if (parent) {
                    auto bl = parent->convertToWorldSpace({bb.getMinX(), bb.getMinY()});
                    auto tr = parent->convertToWorldSpace({bb.getMaxX(), bb.getMaxY()});
                    rect = CCRect{bl.x, bl.y, tr.x - bl.x, tr.y - bl.y};
                }
                if (rect.containsPoint(mouse)) {
                    // CreateMenuItem stores object ID in m_objectID (common) or tag
                    foundId = item->m_objectID;
                    if (foundId <= 0) foundId = item->getTag();
                    break;
                }
            }
        }
        if (foundId > 0) {
            if (foundId != m_fields->lastId) showTip(foundId, mouse);
            else {
                // follow mouse
                if (m_fields->bg) {
                    m_fields->bg->setPosition(mouse + ccp(12.f, 12.f));
                    m_fields->tip->setPosition(m_fields->bg->getPosition() + ccp(5.f, 3.f));
                }
            }
        } else {
            hideTip();
        }
#else
        hideTip();
#endif
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (!moduleEnabled("editor-mod-object-tooltips")) return true;

        auto* bg = CCLayerColor::create({20, 20, 30, 210}, 40.f, 16.f);
        bg->setVisible(false);
        bg->setID("paimbnails/object-tooltip-bg");
        this->addChild(bg, 120);

        auto* tip = CCLabelBMFont::create("", "chatFont.fnt");
        tip->setVisible(false);
        tip->setID("paimbnails/object-tooltip");
        this->addChild(tip, 121);

        m_fields->bg = bg;
        m_fields->tip = tip;
        this->schedule(schedule_selector(PaimonObjectTooltipsUI::tooltipTick), 0.05f);
        return true;
    }
};
