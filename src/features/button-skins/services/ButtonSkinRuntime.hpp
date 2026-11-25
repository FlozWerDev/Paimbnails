#pragma once

#include "../data/ButtonSkin.hpp"
#include "ButtonSkinRender.hpp"
#include "../../../utils/SafeNodeHandle.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::buttonskin {

// what a skinned button does with its click sound.
struct SkinSound {
    quickhub::CustomQuickButton sfx;  // sfx* fields
    bool custom = false;              // play sfx after the click
    bool mute = false;                // swallow the game's own sound
};

// screen a "same screen" skin is bound to: its owner layer, else the scene's main layer.
std::string sameScreenClass(quickhub::CustomQuickButton const& target);

// finds the saved skins' buttons in the running scene and keeps them dressed.
class ButtonSkinRuntime {
public:
    static ButtonSkinRuntime& get();
    static bool isInitialized();
    static bool hasVisibleSkin(cocos2d::CCMenuItem* item);

    // scan on the next frame; cheap to call repeatedly.
    void requestScan();
    // dress the scene about to show (before its first frame).
    void onSceneSwitch(cocos2d::CCScene* scene);
    // dress a scene that is being entered, synchronously, from the entering layer's onEnter.
    void dressEntered(cocos2d::CCScene* scene);

    // takes the skin off item until the next scan; returns the id it carried ("" if none).
    std::string release(cocos2d::CCMenuItem* item);
    // dresses item with a saved skin right away and keeps it on this button for the scene.
    void adopt(cocos2d::CCMenuItem* item, std::string const& skinId);
    std::string appliedSkinId(cocos2d::CCMenuItem* item);
    std::optional<SkinSound> soundFor(cocos2d::CCMenuItem* item);

    // the item a skin lives on: right-clicks land on a toggle's inner state, menus touch the toggle.
    static cocos2d::CCMenuItem* skinnableItemFor(cocos2d::CCMenuItem* item);

    // the scan skips nodes marked here (our own popups).
    static void markSkinUi(cocos2d::CCNode* node);

    void onGLContextReload();
    void shutdown();

    // driven by the scheduler.
    void tick(float dt);

private:
    ButtonSkinRuntime() = default;

    struct ShapeEntry {
        AssetShape shape;
        geode::Ref<cocos2d::CCTexture2D> source;  // pins the key's texture address
    };

    void ensureTicker();
    int scanScene(cocos2d::CCScene* scene, bool thorough);
    int tendDressed();
    bool dressOrKeep(cocos2d::CCMenuItem* item, ButtonSkin const& skin, size_t hash);
    void applyFresh(cocos2d::CCMenuItem* item, ButtonSkin const& skin, size_t hash);
    AssetShape shapeFor(cocos2d::CCNode* normal);

    std::unordered_map<std::string, ShapeEntry> m_shapes;
    std::vector<paimon::SafeNodeHandle<cocos2d::CCMenuItem>> m_dressed;
    bool m_tickerStarted = false;
    bool m_scanRequested = true;
    bool m_hasApplied = false;
    float m_elapsed = 0.f;
    int m_lightScans = 0;
    cocos2d::CCScene* m_lastScene = nullptr;    // compared only, never dereferenced
    cocos2d::CCScene* m_walkedScene = nullptr;  // compared only
    size_t m_walkedItems = 0;
    unsigned int m_lastChildCount = 0;
    uint64_t m_lastRevision = 0;
    uint64_t m_generation = 1;
};

} // namespace paimon::buttonskin
