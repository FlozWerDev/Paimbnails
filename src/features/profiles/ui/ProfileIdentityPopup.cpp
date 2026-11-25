#include "ProfileIdentityPopup.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../emotes/services/EmoteCache.hpp"
#include "../../emotes/services/EmoteService.hpp"

using namespace geode::prelude;
using namespace paimon::profiles;

namespace {

constexpr float kPopupW = 300.f;
constexpr float kHeaderH = 50.f;
constexpr float kFooterH = 46.f;
constexpr float kRowW = 268.f;
constexpr float kRowH = 46.f;
constexpr float kRowGap = 6.f;
constexpr float kRowPad = 12.f;
constexpr float kChipHeight = 24.f;
constexpr float kRankBtnSize = 34.f;
constexpr float kBadgeSize = 30.f;

void placeInSlot(WeakRef<CCNode> const& slotRef, CCNode* node, float size) {
    auto slot = slotRef.lock();
    if (!slot || !slot->getParent() || !node) return;
    float const maxDim = std::max(node->getContentWidth(), node->getContentHeight());
    if (maxDim > 0.f) node->setScale(size / maxDim);
    node->setPosition({size / 2.f, size / 2.f});
    slot->addChild(node);
}

// emotes arrive async (static texture or gif bytes), so the slot is filled later.
void loadEmoteInto(WeakRef<CCNode> slotRef, std::string const& emoteName, float size) {
    auto info = paimon::emotes::EmoteService::get().getEmoteByName(emoteName);
    if (!info) return;

    paimon::emotes::EmoteCache::get().loadEmote(*info,
        [slotRef, size, emoteName](CCTexture2D* tex, bool isGif, std::vector<uint8_t> const& gifData) {
            if (!tex && !(isGif && !gifData.empty())) return;

            if (isGif && !gifData.empty()) {
                auto dataCopy = gifData;
                Loader::get()->queueInMainThread(
                    [slotRef, size, emoteName, dataCopy = std::move(dataCopy)]() mutable {
                        if (paimon::isRuntimeShuttingDown()) return;
                        AnimatedGIFSprite::createAsync(dataCopy, emoteName,
                            [slotRef, size](AnimatedGIFSprite* gifSpr) {
                                if (paimon::isRuntimeShuttingDown()) return;
                                placeInSlot(slotRef, gifSpr, size);
                            });
                    });
            } else {
                Ref<CCTexture2D> texRef = tex;
                Loader::get()->queueInMainThread([slotRef, size, texRef]() {
                    if (paimon::isRuntimeShuttingDown()) return;
                    placeInSlot(slotRef, CCSprite::createWithTexture(texRef.data()), size);
                });
            }
        });
}

CCMenuItemSpriteExtra* makeRankingButton(std::function<void()> onPress) {
    CCNode* sprite = nullptr;
    if (auto* icon = paimon::SpriteHelper::safeCreate("paim_ui_ranking.png"_spr)) {
        sprite = CircleButtonSprite::create(icon, CircleBaseColor::Green, CircleBaseSize::Medium);
    }
    if (!sprite) {
        sprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plainBtn_001.png");
    }
    if (!sprite) return nullptr;

    float const maxDim = std::max(sprite->getContentWidth(), sprite->getContentHeight());
    if (maxDim > 0.f) sprite->setScale(kRankBtnSize / maxDim);

    return CCMenuItemExt::createSpriteExtra(sprite,
        [cb = std::move(onPress)](CCMenuItemSpriteExtra*) {
            if (cb) cb();
        });
}

} // namespace

ProfileIdentityPopup* ProfileIdentityPopup::create(std::string const& username,
        std::vector<IdentityEntry> entries) {
    auto* ret = new ProfileIdentityPopup();
    if (ret && ret->init(username, std::move(entries))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ProfileIdentityPopup::init(std::string const& username, std::vector<IdentityEntry> entries) {
    float const rowsH = entries.empty()
        ? 0.f
        : static_cast<float>(entries.size()) * (kRowH + kRowGap) - kRowGap;
    float const height = kHeaderH + rowsH + kFooterH;
    if (!PaimonPopup::init(kPopupW, height)) return false;

    auto& loc = Localization::get();
    auto const content = m_mainLayer->getContentSize();
    float const cx = content.width / 2.f;

    this->setTitle(username);

    auto* menu = paimon::ui::makeZeroMenu();
    m_mainLayer->addChild(menu, 3);

    float rowTop = content.height - kHeaderH;
    for (auto const& entry : entries) {
        float const rowY = rowTop - kRowH / 2.f;

        if (auto* inset = paimon::ui::makeInset({kRowW, kRowH}, 95)) {
            inset->setPosition({cx - kRowW / 2.f, rowTop - kRowH});
            m_mainLayer->addChild(inset, 1);
        }

        float const left = cx - kRowW / 2.f + kRowPad;

        if (entry.buildChip) {
            if (auto* chip = entry.buildChip(kChipHeight)) {
                chip->setPosition({left + chip->getContentWidth() / 2.f, rowY});
                m_mainLayer->addChild(chip, 2);
            }
        } else if (!entry.emoteName.empty()) {
            auto* slot = CCNode::create();
            slot->setContentSize({kBadgeSize, kBadgeSize});
            slot->setAnchorPoint({0.f, 0.5f});
            slot->ignoreAnchorPointForPosition(false);
            slot->setPosition({left, rowY});
            m_mainLayer->addChild(slot, 2);
            loadEmoteInto(slot, entry.emoteName, kBadgeSize);

            auto* label = paimon::ui::makeLabel(loc.getString("profile.identity.badge").c_str(),
                kRowW - kRowPad * 2.f - kBadgeSize - 8.f, 0.45f, paimon::ui::palette::muted);
            if (label) {
                label->setAnchorPoint({0.f, 0.5f});
                label->setPosition({left + kBadgeSize + 8.f, rowY});
                m_mainLayer->addChild(label, 2);
            }
        }

        if (entry.onOpenRanking) {
            if (auto* btn = makeRankingButton(entry.onOpenRanking)) {
                btn->setID("paimon-identity-ranking-btn"_spr);
                btn->setPosition({cx + kRowW / 2.f - kRowPad - kRankBtnSize / 2.f, rowY});
                menu->addChild(btn);
            }
        }

        rowTop -= kRowH + kRowGap;
    }

    auto* copyBtn = paimon::ui::makeButton(loc.getString("profile.identity.copy").c_str(),
        [username]() {
            geode::utils::clipboard::write(username);
            Notification::create(Localization::get().getString("profile.identity.copied"),
                NotificationIcon::Info, 1.0f)->show();
        },
        paimon::ui::Btn::Cyan, 0.f, 0.6f, "bigFont.fnt");
    if (copyBtn) {
        copyBtn->setPosition({cx, kFooterH / 2.f + 2.f});
        copyBtn->setID("paimon-identity-copy-btn"_spr);
        menu->addChild(copyBtn);
    }

    this->setID("profile-identity-popup"_spr);
    paimon::markDynamicPopup(this);
    return true;
}
