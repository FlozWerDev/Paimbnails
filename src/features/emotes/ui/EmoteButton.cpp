#include "EmoteButton.hpp"
#include "EmotePickerPopup.hpp"
#include "../services/EmoteService.hpp"
#include "../services/EmoteCache.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include <Geode/ui/BasedButtonSprite.hpp>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::emotes;

EmoteButton* EmoteButton::create(EmoteInputContext context) {
    auto ret = new EmoteButton();
    if (ret && ret->init(std::move(context))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool EmoteButton::init(EmoteInputContext context) {
    // Fallback label shown until async emote loads
    auto fallback = CCLabelBMFont::create(":)", "chatFont.fnt");
    fallback->setScale(0.55f);

    // Geode circular button with the fallback as initial top node
    auto circle = CircleButtonSprite::create(
        fallback,
        CircleBaseColor::DarkPurple,
        CircleBaseSize::Medium
    );
    if (!circle) return false;

    if (!CCMenuItemSpriteExtra::init(circle, nullptr, this, menu_selector(EmoteButton::onToggle))) {
        return false;
    }

    m_context = std::move(context);
    this->setID("paimon-emote-btn"_spr);

    loadRandomEmote();

    return true;
}

void EmoteButton::loadRandomEmote() {
    if (!EmoteService::get().isLoaded()) return;

    auto randomEmote = EmoteService::get().getRandomEmote();
    if (!randomEmote) return;

    auto emoteCopy = *randomEmote;
    Ref<EmoteButton> self = this;
    EmoteCache::get().loadEmote(emoteCopy,
        [self](CCTexture2D* tex, bool isGif, std::vector<uint8_t> const& gifData) {
            Loader::get()->queueInMainThread([self, tex, isGif, gifData]() {
                if (!self || !self->getParent()) return;

                auto circle = typeinfo_cast<CircleButtonSprite*>(self->getNormalImage());
                if (!circle) return;

                CCNode* sprite = nullptr;
                if (isGif && !gifData.empty()) {
                    sprite = AnimatedGIFSprite::create(gifData.data(), gifData.size());
                } else if (tex) {
                    sprite = CCSprite::createWithTexture(tex);
                }

                if (sprite) {
                    // Remove the old fallback top node
                    if (auto oldTop = circle->getTopNode()) {
                        oldTop->removeFromParent();
                    }

                    // Scale emote to fit inside the circle
                    auto maxSize = circle->getMaxTopSize();
                    float scale = std::min(
                        maxSize.width / sprite->getContentSize().width,
                        maxSize.height / sprite->getContentSize().height
                    );
                    sprite->setScale(scale);
                    sprite->setPosition(circle->getContentSize() / 2.f);
                    circle->addChild(sprite, 10);
                }
            });
        });
}

void EmoteButton::onToggle(CCObject*) {
    // Close existing picker if it's still in the scene
    if (m_activePicker && m_activePicker->getParent()) {
        m_activePicker->removeFromParent();
        m_activePicker = nullptr;
        return;
    }
    // Clear stale ref (popup was closed externally)
    m_activePicker = nullptr;

    auto picker = EmotePickerPopup::create(
        m_context.getText,
        m_context.setText,
        m_context.charLimit
    );

    if (picker) {
        picker->show();
        picker->positionNearBottom(this, 0.f);
        m_activePicker = picker;
    }
}
