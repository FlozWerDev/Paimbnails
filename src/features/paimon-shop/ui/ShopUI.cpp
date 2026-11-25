#include "ShopUI.hpp"
#include "../../../utils/Base64.hpp"
#include "../../../utils/ImageLoadHelper.hpp"

using namespace geode::prelude;

namespace paimon::shop {
void showShopError(std::string const& error) {
    FLAlertLayer::create("Paimon Shop", error, "OK")->show();
}

CCNode* makePreview(std::string const& encoded, CCSize size) {
    auto* holder = CCNode::create();
    holder->setContentSize(size);
    holder->addChild(paimon::ui::makeInset(size, 140, {10, 17, 30}));
    std::vector<uint8_t> bytes;
    if (encoded.size() <= (kMaxPreviewBytes + 2) / 3 * 4 && paimon::base64Decode(encoded, bytes) && validatePreview(bytes)) {
        auto image = ImageLoadHelper::loadWithSTBFromMemory(bytes.data(), bytes.size(), false);
        if (image.success && image.texture) {
            auto* sprite = CCSprite::createWithTexture(image.texture);
            image.texture->release();
            if (sprite) {
                auto content = sprite->getContentSize();
                sprite->setScale(std::min((size.width - 4.f) / content.width, (size.height - 4.f) / content.height));
                sprite->setPosition(size / 2.f);
                holder->addChild(sprite);
                return holder;
            }
        }
    }
    auto* placeholder = paimon::ui::makeLabel(text("Vista previa PNG", "PNG preview"), size.width - 8.f, 0.3f, paimon::ui::palette::dim);
    placeholder->setPosition(size / 2.f);
    holder->addChild(placeholder);
    return holder;
}
} // namespace paimon::shop
