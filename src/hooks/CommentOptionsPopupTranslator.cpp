#include <Geode/Geode.hpp>
#include <Geode/binding/GJComment.hpp>
#include <Geode/modify/CommentOptionsPopup.hpp>
#include <Geode/ui/NodeEvent.hpp>
#include "../framework/HookConventions.hpp"
#include "../features/translator/ui/TranslatorSettingsPopup.hpp"
#include "../features/translator/ui/TranslateButton.hpp"
#include "../ui/PaimonUI.hpp"
#include "../utils/Localization.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;
using namespace cocos2d;

// the "..." popup of a comment gets an extra button that opens the translator.
class $modify(PaimonCommentOptionsTranslator, CommentOptionsPopup) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "CommentOptionsPopup::init");
    }

    bool init(GJComment* comment, bool isLevelComment) {
        if (!CommentOptionsPopup::init(comment, isLevelComment)) return false;
        ensureTranslatorButton();
        this->addOnEnterTransitionDidFinishCallback([this] {
            ensureTranslatorButton();
            return true;
        });
        return true;
    }

private:
    void ensureTranslatorButton() {
        if (!m_mainLayer) return;
        if (auto* existing = typeinfo_cast<CCMenuItemSpriteExtra*>(
                m_mainLayer->getChildByIDRecursive("paimon-translator-btn"_spr))) {
            positionTranslatorButton(existing);
            return;
        }

        auto* button = paimon::translate::ui::makeTranslateButton([this] {
            this->onClose(nullptr);
            if (auto* popup = paimon::translate::TranslatorSettingsPopup::create()) {
                popup->show();
            }
        }, .8f);
        if (!button) return;

        button->setID("paimon-translator-btn"_spr);
        attachTranslatorButton(button);
        log::debug("[Translator] CommentOptionsPopup button added");
    }
    CCRect boundsInNode(CCNode* node, CCNode* space) {
        auto size = node->getContentSize();
        auto bottomLeft = space->convertToNodeSpace(node->convertToWorldSpace({0.f, 0.f}));
        auto topRight = space->convertToNodeSpace(
            node->convertToWorldSpace({size.width, size.height}));
        float const minX = std::min(bottomLeft.x, topRight.x);
        float const maxX = std::max(bottomLeft.x, topRight.x);
        float const minY = std::min(bottomLeft.y, topRight.y);
        float const maxY = std::max(bottomLeft.y, topRight.y);
        return {minX, minY, maxX - minX, maxY - minY};
    }

    // the closest large sprite inside the layer is GD's popup background; the
    // close button and corner art are much smaller.
    CCSprite* findBackground() {
        CCSprite* best = nullptr;
        float bestArea = 0.f;
        if (!m_mainLayer) return nullptr;
        if (auto* children = m_mainLayer->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                auto* sprite = typeinfo_cast<CCSprite*>(child);
                if (!sprite) continue;
                auto const size = sprite->getScaledContentSize();
                float const areaSize = size.width * size.height;
                if (areaSize > bestArea) {
                    bestArea = areaSize;
                    best = sprite;
                }
            }
        }
        return best;
    }

    // m_buttonMenu is centred on the alert, so raw menu coordinates put the
    // button outside the frame. Derive the spot from nodes already in place.
    void attachTranslatorButton(CCMenuItemSpriteExtra* button) {
        auto* menu = m_buttonMenu;
        if (!menu) {
            auto* ownMenu = CCMenu::create();
            ownMenu->setPosition({0.f, 0.f});
            m_mainLayer->addChild(ownMenu, 10);
            menu = ownMenu;
        }
        menu->addChild(button);
        positionTranslatorButton(button);
    }

    void positionTranslatorButton(CCMenuItemSpriteExtra* button) {
        auto* menu = button->getParent();
        if (!menu) return;

        // find the widest sibling (REPORT in every known layout) and keep our
        // button left of every control sharing its row. COPY and DELETE may sit
        // left of REPORT, so anchoring only to the widest one covered them.
        CCMenuItemSpriteExtra* widest = nullptr;
        float widestWidth = 0.f;
        std::vector<CCMenuItemSpriteExtra*> items;
        if (auto* children = menu->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(child);
                if (!item || item->getID() == "paimon-translator-btn"_spr) continue;
                if (!typeinfo_cast<CCSprite*>(item->getNormalImage())) continue;
                auto const bounds = boundsInNode(item, menu);
                if (bounds.size.width > widestWidth) {
                    widestWidth = bounds.size.width;
                    widest = item;
                }
                items.push_back(item);
            }
        }

        auto* background = findBackground();
        auto const half = button->getScaledContentSize() * .5f;
        auto const menuSize = menu->getContentSize();

        if (!widest) {
            // no siblings to lean on: use the inside of the frame corner.
            float const x = background ? boundsInNode(background, menu).origin.x + 24.f + half.width
                                       : -menuSize.width * .5f + 24.f + half.width;
            float const y = background
                ? boundsInNode(background, menu).origin.y
                    + boundsInNode(background, menu).size.height - 24.f - half.height
                : menuSize.height * .5f - 24.f - half.height;
            button->setPosition({x, y});
            return;
        }

        auto const reference = boundsInNode(widest, menu);
        float const rowY = reference.origin.y + reference.size.height * .5f;
        float leftMost = reference.origin.x;
        for (auto* item : items) {
            auto const bounds = boundsInNode(item, menu);
            float const centerY = bounds.origin.y + bounds.size.height * .5f;
            if (std::abs(centerY - rowY) <= 24.f) {
                leftMost = std::min(leftMost, bounds.origin.x);
            }
        }

        float x = leftMost - 6.f - half.width;
        float y = rowY;

        if (background) {
            auto const bg = boundsInNode(background, menu);
            float const minX = bg.origin.x + 16.f + half.width;
            float const maxX = bg.origin.x + bg.size.width - 16.f - half.width;
            if (x < minX) {
                // the row fills the frame: stack above it instead of clamping
                // onto a neighbour.
                x = std::clamp(leftMost + half.width, minX, maxX);
                float const above = rowY + reference.size.height * .5f + half.height + 12.f;
                float const top = bg.origin.y + bg.size.height - 16.f - half.height;
                y = above <= top
                    ? above
                    : rowY - reference.size.height * .5f - half.height - 12.f;
            } else {
                x = std::min(x, maxX);
            }
        }

        button->setPosition({x, y});
    }
};

