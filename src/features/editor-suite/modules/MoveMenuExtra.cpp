// Quarter-block move buttons injected into the vanilla EDIT button bar.
// Vanilla 2.2 already ships half-block arrows (EditCommand::Half*), so only
// the 1/4 step is added. The move sections get reordered by step size:
// 1/4 -> 1/2 -> 1 -> 5 blocks, then the unit steps (2u, 0.5u), then the rest.

#include "../EditorModule.hpp"
#include "../EditorHelpers.hpp"
#include "../api/EditCommands.hpp"

#include <Geode/Enums.hpp>
#include <Geode/binding/EditButtonBar.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <algorithm>
#include <array>
#include <vector>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

namespace {
bool on() { return moduleEnabled("editor-mod-move-menu"); }

float grid() {
    float g = Mod::get()->getSavedValue<float>("paim-editor-grid-size", 30.f);
    return g < 0.9f ? 30.f : g;
}

void nudge(EditorUI* ui, float dx, float dy) {
    if (!ui) return;
    for (auto* o : getSelectedObjects(ui)) {
        ui->moveObject(o, {dx, dy});
    }
    ui->updateButtons();
    ui->updateObjectInfoLabel();
}

// Section rank inside the edit bar, ordered by step size. Buttons that are
// not move arrows (flip/rotate/other mods) keep their relative order at the end.
int sectionRank(int tag) {
    switch (tag) {
        case 0x401: case 0x402: case 0x403: case 0x404:
            return 0; // 1/4 block (ours)
        case int(EditCommand::HalfLeft): case int(EditCommand::HalfRight):
        case int(EditCommand::HalfUp):   case int(EditCommand::HalfDown):
            return 1; // 1/2 block
        case int(EditCommand::Left): case int(EditCommand::Right):
        case int(EditCommand::Up):   case int(EditCommand::Down):
            return 2; // 1 block
        case int(EditCommand::BigLeft): case int(EditCommand::BigRight):
        case int(EditCommand::BigUp):   case int(EditCommand::BigDown):
            return 3; // 5 blocks
        case int(EditCommand::SmallLeft): case int(EditCommand::SmallRight):
        case int(EditCommand::SmallUp):   case int(EditCommand::SmallDown):
            return 4; // 2 units
        case int(EditCommand::TinyLeft): case int(EditCommand::TinyRight):
        case int(EditCommand::TinyUp):   case int(EditCommand::TinyDown):
            return 5; // 1/2 unit
        default:
            return 6;
    }
}

// Same green arrow plate as the vanilla move buttons, tagged with "1/4".
CCMenuItemSpriteExtra* quarterButton(char const* frame, int tag, std::function<void()> action) {
    auto* face = CCSprite::createWithSpriteFrameName(frame);
    if (!face) return nullptr;
    auto const size = face->getContentSize();

    auto* label = CCLabelBMFont::create("1/4", "bigFont.fnt");
    label->setScale(0.325f);
    label->setAnchorPoint({0.5f, 0.f});
    label->setPosition({size.width / 2.f, 1.5f});
    face->addChild(label, 5);

    auto* item = CCMenuItemExt::createSpriteExtra(
        face, [callback = std::move(action)](CCMenuItemSpriteExtra*) {
            if (callback) callback();
        }
    );
    if (item) item->setTag(tag);
    return item;
}

void reloadEditBar(EditorUI* ui) {
    if (!ui || !ui->m_editButtonBar) return;
    int cols = GameManager::get()->getIntGameVariable("0049");
    int rows = GameManager::get()->getIntGameVariable("0050");
    if (cols < 1) cols = 6;
    if (rows < 1) rows = 2;
    cols = std::clamp(cols, 1, 12);
    rows = std::clamp(rows, 1, 6);
    ui->m_editButtonBar->reloadItems(cols, rows);
}
} // namespace

class $modify(PaimonMoveMenuUI, EditorUI) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (!on()) return true;
        if (!m_editButtonBar || !m_editButtonBar->m_buttonArray) return true;

        struct Quarter {
            char const* frame;
            int tag;
            float dx;
            float dy;
        };
        constexpr std::array<Quarter, 4> quarters{{
            {"edit_upBtn_001.png",    int(EditCommandExt::QuarterUp),    0.f,  1.f},
            {"edit_downBtn_001.png",  int(EditCommandExt::QuarterDown),  0.f, -1.f},
            {"edit_leftBtn_001.png",  int(EditCommandExt::QuarterLeft), -1.f,  0.f},
            {"edit_rightBtn_001.png", int(EditCommandExt::QuarterRight), 1.f,  0.f},
        }};

        auto* arr = m_editButtonBar->m_buttonArray;
        for (auto const& q : quarters) {
            auto* item = quarterButton(q.frame, q.tag, [this, q] {
                float const amount = grid() * 0.25f;
                nudge(this, q.dx * amount, q.dy * amount);
            });
            if (!item) continue;
            item->setID(fmt::format("paimbnails/move-quarter-{}", q.tag - 0x400));
            arr->addObject(item);
        }

        // Group the bar into step-size sections (1/4, 1/2, 1, 5, ...) with a
        // stable sort so non-arrow buttons keep their original order.
        std::vector<Ref<CCObject>> items;
        items.reserve(arr->count());
        for (unsigned i = 0; i < arr->count(); ++i) {
            items.emplace_back(arr->objectAtIndex(i));
        }
        std::stable_sort(items.begin(), items.end(), [](Ref<CCObject> const& a, Ref<CCObject> const& b) {
            auto* na = typeinfo_cast<CCNode*>(a.data());
            auto* nb = typeinfo_cast<CCNode*>(b.data());
            int ra = na ? sectionRank(na->getTag()) : 6;
            int rb = nb ? sectionRank(nb->getTag()) : 6;
            return ra < rb;
        });
        arr->removeAllObjects();
        for (auto const& it : items) arr->addObject(it.data());

        reloadEditBar(this);
        return true;
    }
};
