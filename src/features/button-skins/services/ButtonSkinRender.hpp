#pragma once

#include "../data/ButtonSkin.hpp"

#include <Geode/Geode.hpp>

namespace paimon::buttonskin {

// flat picture of an asset: render-texture pixels, premultiplied and stored upside down.
struct AssetShape {
    geode::Ref<cocos2d::CCTexture2D> texture;
    cocos2d::CCSize assetSize;    // content size of the captured node
    cocos2d::CCSize textureSize;  // texture size in points (long side capped)

    explicit operator bool() const { return texture.data() != nullptr; }
};

// renders node inside its own content box, untransformed; empty when it has no area.
AssetShape snapshotAsset(cocos2d::CCNode* node);

// the skin's paint, cut to the asset's alpha when maskToShape; same size as shape.texture.
// cached by paint settings + shape, so editor redraws and twin buttons share it.
cocos2d::CCTexture2D* bakeFill(ButtonSkin const& skin, AssetShape const& shape);

// premultiplied blend for the baked paint drawn over the asset.
cocos2d::ccBlendFunc overlayBlend(SkinBlend blend);

// sprite for a render texture: flipped upright, premultiplied blending.
cocos2d::CCSprite* makeShapeSprite(cocos2d::CCTexture2D* texture);

// asset with the skin, centered on (0,0) and fitted inside box. pointScale converts the
// skin's offsets (button parent points) to asset units; withTransform adds scale/turn/move.
cocos2d::CCNode* buildSkinPreview(ButtonSkin const& skin, AssetShape const& shape, float box,
                                  float pointScale, bool withTransform);

void clearRenderCaches();

} // namespace paimon::buttonskin
