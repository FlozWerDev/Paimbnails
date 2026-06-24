#pragma once
//
// SlotsGridView.hpp - Grid of slot cards displayed inside TextureStudioLayer.
// Each card represents one TextureProject and exposes Apply / Edit / Delete
// actions. The view subscribes to SlotStore changes via direct refresh()
// calls — the parent layer triggers refresh after slot creation/deletion.
//
// Layout: a flow of cards inside a ScrollLayer. Cards are 174×110 and lay
// out 2 per row at the popup's grid width, each exposing Apply / Edit /
// Delete. A trailing "New Pack" tile (fully clickable) creates a new slot.
//

#include <Geode/Geode.hpp>

#include <functional>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "../persist/TextureProject.hpp"

namespace paimon::texture_studio {

class ImageBuffer;

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
    void requestThumbnails(std::vector<std::pair<int, TextureProject>> jobs,
                           int generation);
    void applyThumbnail(int cardTag, int generation,
                        std::shared_ptr<ImageBuffer> image);

    SlotActionCallback     m_onApply;
    SlotActionCallback     m_onEdit;
    SlotActionCallback     m_onDelete;
    std::function<void()>  m_onNewPack;

    cocos2d::CCNode* m_contentLayer = nullptr;
    float m_widgetWidth  = 0.f;
    float m_widgetHeight = 0.f;
    std::shared_ptr<std::atomic<int>> m_thumbnailGeneration =
        std::make_shared<std::atomic<int>>(0);
};

}  // namespace paimon::texture_studio
