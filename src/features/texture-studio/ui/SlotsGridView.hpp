#pragma once
//
// SlotsGridView.hpp - Grid of slot cards displayed inside TextureStudioLayer.
// Each card represents one TextureProject and exposes Apply / Edit / Delete
// actions. The view subscribes to SlotStore changes via direct refresh()
// calls — the parent layer triggers refresh after slot creation/deletion.
//
// Layout: a horizontal flow inside a ScrollLayer. Cards are 160×120, with
// 3 per row at the popup's typical 520-pixel width.
//

#include <Geode/Geode.hpp>

#include <functional>
#include <string>

namespace paimon::texture_studio {

class SlotsGridView : public cocos2d::CCNode {
public:
    using SlotActionCallback = std::function<void(std::string const& slotId)>;

    // Create the grid. The three callbacks are invoked when the user
    // clicks a slot's Apply / Edit / Delete button respectively.
    static SlotsGridView* create(float width, float height,
                                 SlotActionCallback onApply,
                                 SlotActionCallback onEdit,
                                 SlotActionCallback onDelete,
                                 std::function<void()> onNewPack);

    // Refresh the grid from SlotStore. Call after adding/removing slots.
    void refresh();

protected:
    bool init(float width, float height,
              SlotActionCallback onApply,
              SlotActionCallback onEdit,
              SlotActionCallback onDelete,
              std::function<void()> onNewPack);

private:
    cocos2d::CCNode* makeSlotCard(std::string const& id,
                                  std::string const& name,
                                  std::int64_t modifiedAt,
                                  bool hasBuiltOnce);
    cocos2d::CCNode* makeNewPackCard();

    SlotActionCallback     m_onApply;
    SlotActionCallback     m_onEdit;
    SlotActionCallback     m_onDelete;
    std::function<void()>  m_onNewPack;

    cocos2d::CCNode* m_contentLayer = nullptr;
    float m_widgetWidth  = 0.f;
    float m_widgetHeight = 0.f;
};

}  // namespace paimon::texture_studio
