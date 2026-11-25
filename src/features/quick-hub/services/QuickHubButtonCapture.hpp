#pragma once

#include "../data/QuickHubCategories.hpp"

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace paimon::quickhub {

bool handleQuickButtonRightClick();
bool activateCustomQuickButton(std::string const& id);

// true when the saved button can be pressed right now.
bool isCustomQuickButtonReachable(std::string const& id);

// opens the "Anadir al Quick Hub" editor; gives the candidate an id when it has none.
void openQuickHubButtonPopup(CustomQuickButton candidate);

// identity of a game button (no id): the same fields the quick hub saves to find it again.
CustomQuickButton captureButtonIdentity(cocos2d::CCMenuItem* button, cocos2d::CCScene* scene);

// node's own class when it is a screen layer (not a menu/scroll container), else "".
std::string screenLayerClassOf(cocos2d::CCNode* node);
// def.ownerClass, or "" when an old capture stored a generic container.
std::string usableOwnerClassOf(CustomQuickButton const& def);

// named frames drawn by the button (caption for text buttons): look-alike buttons share it.
std::string buttonVisualSignature(cocos2d::CCMenuItem* item);

// saved button inside root, hidden or disabled included. the id path is always tried;
// allowSweep adds the scored pass over candidates (costly on screens with many buttons).
cocos2d::CCMenuItem* locateButtonIn(cocos2d::CCScene* root, CustomQuickButton const& def,
                                    std::vector<cocos2d::CCMenuItem*> const& candidates,
                                    bool allowSweep = true);

} // namespace paimon::quickhub
