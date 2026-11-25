#include <Geode/Geode.hpp>
#include <Geode/binding/CommentCell.hpp>
#include <Geode/binding/GJComment.hpp>
#include <Geode/binding/TextArea.hpp>
#include <Geode/modify/CommentCell.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/cocos.hpp>
#include "../core/RuntimeLifecycle.hpp"
#include "../features/translator/TranslationService.hpp"
#include "../features/translator/TranslatorConfig.hpp"
#include "../features/translator/TranslatorText.hpp"
#include "../features/translator/ui/TextSwapAnimation.hpp"
#include "../features/translator/ui/TranslateButton.hpp"
#include "../features/translator/ui/TranslateToggle.hpp"
#include "../framework/HookConventions.hpp"
#include "../utils/Localization.hpp"
#include <algorithm>
#include <cmath>
#include <functional>

using namespace geode::prelude;
using namespace cocos2d;

namespace {

// native cells keep like, delete and options in the same top-right row. Leave
// a small gap to the left of that whole row so none of them gets covered.
constexpr float kControlGap = 6.f;
constexpr float kControlRowTolerance = 26.f;
// about the height of the native like and delete icons
constexpr float kTranslateButtonScale = .5f;

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

TextArea* findCommentTextArea(CCNode* root) {
    TextArea* best = nullptr;
    float bestArea = 0.f;
    std::function<void(CCNode*)> walk = [&](CCNode* node) {
        if (!node) return;
        if (auto* area = typeinfo_cast<TextArea*>(node)) {
            auto size = area->getScaledContentSize();
            float const areaSize = size.width * size.height;
            if (areaSize > bestArea) {
                bestArea = areaSize;
                best = area;
            }
        }
        if (auto* children = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) walk(child);
        }
    };
    walk(root);
    return best;
}

}

class $modify(PaimonCommentTranslatorCell, CommentCell) {
public:
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "CommentCell::loadFromComment");
    }

    struct Fields {
        Ref<CCMenu> menu = nullptr;
        Ref<CCMenuItemSpriteExtra> button = nullptr;
        std::string original;
        std::string translated;
        std::string translatedTarget;
        // text currently painted in the cell, which may be the translation
        std::string displayed;
        bool showing = false;
        bool busy = false;
        // set while we re-run loadFromComment ourselves to repaint the text
        bool rebuilding = false;
        int commentID = 0;
        unsigned int request = 0;
    };

    void loadFromComment(GJComment* comment) {
        this->unschedule(schedule_selector(PaimonCommentTranslatorCell::onAutoTranslateDelay));
        CommentCell::loadFromComment(comment);
        // our own rebuild keeps the button and its state
        if (m_fields->rebuilding) {
            positionTranslateButton();
            return;
        }
        resetButtonState();

        if (!comment || paimon::isRuntimeShuttingDown()) return;

        m_fields->commentID = comment->m_commentID;
        m_fields->original = comment->m_commentString;
        m_fields->displayed = comment->m_commentString;
        m_fields->showing = false;

        auto settings = paimon::translate::loadSettings();
        if (!settings.enabled) return;

        addTranslateButton();
        if (settings.autoTranslate) {
            this->scheduleOnce(
                schedule_selector(PaimonCommentTranslatorCell::onAutoTranslateDelay), .35f);
        }
    }

    // called by the settings popup so open comment lists react immediately.
    void syncTranslateButton() {
        auto settings = paimon::translate::loadSettings();
        if (!settings.enabled) {
            // the cell may be painting a translation while m_commentString was
            // already restored, so compare against what is actually shown
            if (m_fields->showing || m_fields->displayed != m_fields->original) {
                applyText(m_fields->original);
            }
            resetButtonState();
            return;
        }

        if (m_fields->original.empty()) {
            if (!m_comment || paimon::isRuntimeShuttingDown()) return;
            m_fields->commentID = m_comment->m_commentID;
            m_fields->original = m_comment->m_commentString;
        }

        if (!m_fields->translatedTarget.empty() && m_fields->translatedTarget != settings.target) {
            if (m_fields->showing || m_fields->displayed != m_fields->original) {
                applyText(m_fields->original);
            }
            m_fields->showing = false;
            m_fields->translated.clear();
            m_fields->translatedTarget.clear();
        }

        if (!m_fields->button) addTranslateButton();
        positionTranslateButton();
        updateButtonVisual();

        if (settings.autoTranslate && !m_fields->showing && !m_fields->busy && this->isRunning()) {
            requestTranslation(true);
        }
    }

private:
    void resetButtonState() {
        if (m_fields->menu) {
            m_fields->menu->removeFromParent();
            m_fields->menu = nullptr;
        }
        m_fields->button = nullptr;
        m_fields->translated.clear();
        m_fields->translatedTarget.clear();
        m_fields->showing = false;
        m_fields->busy = false;
        ++m_fields->request;
    }

    void positionTranslateButton() {
        auto button = m_fields->button;
        if (!button) return;

        auto const cellSize = this->getContentSize();
        auto const half = button->getScaledContentSize() * .5f;
        bool const haveLikeLabel =
            m_likeLabel && m_likeLabel->getParent() && m_likeLabel->isVisible();

        float rowY = cellSize.height - 20.f;
        float rowRight = cellSize.width - 24.f;
        if (haveLikeLabel) {
            auto const labelBounds = boundsInNode(m_likeLabel, this);
            rowY = labelBounds.origin.y + labelBounds.size.height * .5f;
            rowRight = labelBounds.origin.x;
        }

        // the like counter sits just right of the native like/delete icons. Only
        // controls inside that short window belong to the row cluster: the
        // username button and the level id pill sit much further left and must
        // not drag the button across the cell.
        float const windowLeft = rowRight - 96.f;
        float leftMost = rowRight;
        bool foundControl = false;
        std::function<void(CCNode*)> walk = [&](CCNode* node) {
            if (!node || node == m_fields->menu.data()) return;
            auto* item = typeinfo_cast<CCMenuItem*>(node);
            if (item && item->isVisible()) {
                // the username is a label inside a menu item; leave it alone
                auto* spriteItem = typeinfo_cast<CCMenuItemSprite*>(item);
                bool const isLabel = spriteItem
                    && typeinfo_cast<CCLabelBMFont*>(spriteItem->getNormalImage()) != nullptr;
                if (!isLabel) {
                    auto const bounds = boundsInNode(item, this);
                    float const centerY = bounds.origin.y + bounds.size.height * .5f;
                    float const centerX = bounds.origin.x + bounds.size.width * .5f;
                    if (centerX >= windowLeft && centerX <= rowRight
                        && std::abs(centerY - rowY) <= kControlRowTolerance) {
                        leftMost = std::min(leftMost, bounds.origin.x);
                        foundControl = true;
                    }
                }
            }
            if (auto* children = node->getChildren()) {
                for (auto* child : CCArrayExt<CCNode*>(children)) walk(child);
            }
        };
        walk(this);

        float x = foundControl
            ? leftMost - kControlGap - half.width
            : rowRight - 40.f - half.width;
        if (cellSize.width > 0.f) {
            float const minX = half.width + 4.f;
            float const maxX = cellSize.width - half.width - 4.f;
            if (minX <= maxX) x = std::clamp(x, minX, maxX);
        }
        button->setPosition({x, rowY});
    }

    void addTranslateButton() {
        if (m_fields->button) return;

        auto* menu = CCMenu::create();
        menu->setID("paimon-comment-translator-menu"_spr);
        menu->setPosition({0.f, 0.f});
        menu->setContentSize(this->getContentSize());

        auto* button = paimon::translate::ui::makeTranslateButton([this] {
            onTranslatePressed();
        }, kTranslateButtonScale);
        if (!button) return;

        button->setID("paimon-comment-translator"_spr);
        // native comment controls use a generous touch area around small icons
        button->setSizeMult(1.5f);
        button->setPosition({0.f, 0.f});
        menu->addChild(button);
        this->addChild(menu, 10);

        m_fields->menu = menu;
        m_fields->button = button;
        positionTranslateButton();
        updateButtonVisual();
    }

    void onTranslatePressed() {
        if (m_fields->busy) return;
        auto settings = paimon::translate::loadSettings();
        if (!settings.enabled) return;

        if (m_fields->showing) {
            applyText(m_fields->original);
            m_fields->showing = false;
            updateButtonVisual();
            return;
        }

        if (!m_fields->translated.empty() && m_fields->translatedTarget == settings.target) {
            applyText(m_fields->translated);
            m_fields->showing = true;
            updateButtonVisual();
            return;
        }

        requestTranslation(false);
    }

    void requestTranslation(bool silent) {
        auto settings = paimon::translate::loadSettings();
        auto text = paimon::translate::sanitizeForTranslation(m_fields->original);
        if (!paimon::translate::isTranslatable(text)) {
            if (!silent) {
                Notification::create(
                    paimon::loc::tr("translator.empty", "There is no text to translate"),
                    NotificationIcon::Info, 1.8f)->show();
            }
            return;
        }

        m_fields->busy = true;
        m_fields->showing = false;
        auto const requestId = ++m_fields->request;
        auto const commentID = m_fields->commentID;
        updateButtonVisual();

        WeakRef<PaimonCommentTranslatorCell> safeThis = this;
        paimon::translate::TranslationService::get().translate(text, settings.target,
            [safeThis, requestId, commentID, target = settings.target](bool ok, std::string result) {
                if (paimon::isRuntimeShuttingDown()) return;
                auto ref = safeThis.lock();
                auto* self = static_cast<PaimonCommentTranslatorCell*>(ref.data());
                if (!self) return;
                if (self->m_fields->request != requestId || self->m_fields->commentID != commentID) {
                    return;
                }

                self->m_fields->busy = false;
                if (!ok || result.empty()) {
                    self->updateButtonVisual();
                    Notification::create(
                        paimon::loc::tr("translator.failed", "Translation failed"),
                        NotificationIcon::Error, 2.2f)->show();
                    return;
                }

                self->m_fields->translated = std::move(result);
                self->m_fields->translatedTarget = target;
                self->m_fields->showing = true;
                self->applyText(self->m_fields->translated);
                self->updateButtonVisual();
            });
    }

    void applyText(std::string const& text) {
        // CommentCell::loadFromComment is what actually repaints a comment: it
        // rebuilds the TextArea from GJComment::m_commentString. Calling
        // TextArea::setString alone leaves the old glyphs on screen in 2.2091.
        if (m_fields->displayed == text) return;

        auto* comment = m_comment;
        if (!comment) {
            if (auto* area = findCommentTextArea(this)) {
                area->setString(text);
                paimon::translate::ui::animateTextSwap(area);
            }
            m_fields->displayed = text;
            return;
        }

        // m_commentString holds the original: applyText always puts it back
        std::string const stored = comment->m_commentString;

        // the same menu goes back after the rebuild so it keeps the touch
        // priority its host gave it (the redesigned profile raises it).
        Ref<CCMenu> menu = m_fields->menu;
        if (menu) menu->removeFromParent();

        m_fields->rebuilding = true;
        this->setUserObject(paimon::translate::ui::textSwapMarker(), CCBool::create(true));
        comment->m_commentString = text;
        CommentCell::loadFromComment(comment);
        comment->m_commentString = stored;
        this->setUserObject(paimon::translate::ui::textSwapMarker(), nullptr);
        m_fields->rebuilding = false;
        m_fields->displayed = text;

        if (paimon::translate::loadSettings().enabled) {
            if (menu && m_fields->button) {
                this->addChild(menu, 10);
                positionTranslateButton();
            } else {
                m_fields->menu = nullptr;
                m_fields->button = nullptr;
                addTranslateButton();
            }
        } else {
            m_fields->menu = nullptr;
            m_fields->button = nullptr;
        }
        updateButtonVisual();
        animateCommentText();
    }

    // the visible text node after a repaint: the emote overlay when the badge
    // hook drew one, else the TextArea, else the compact-mode label.
    void animateCommentText() {
        CCNode* layer = m_mainLayer ? static_cast<CCNode*>(m_mainLayer) : this;
        CCNode* text = layer->getChildByID("paimon-emote-overlay"_spr);
        if (!text || !text->isVisible()) {
            text = findCommentTextArea(this);
            if (text && !text->isVisible()) text = nullptr;
        }
        if (!text) text = layer->getChildByID("comment-text-label");
        if (text && text->isVisible()) paimon::translate::ui::animateTextSwap(text);
    }

    void updateButtonVisual() {
        using paimon::translate::ui::TranslateButtonState;
        auto button = m_fields->button;
        if (!button) return;
        paimon::translate::ui::setTranslateButtonState(button,
            m_fields->busy      ? TranslateButtonState::Busy
            : m_fields->showing ? TranslateButtonState::Showing
                                : TranslateButtonState::Idle);
    }

    void onAutoTranslateDelay(float) {
        if (!paimon::translate::loadSettings().enabled) return;
        if (m_fields->showing || m_fields->busy) return;
        requestTranslation(true);
    }
};

namespace paimon::translate {

void refreshCommentCells() {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene || paimon::isRuntimeShuttingDown()) return;

    bool const enabled = loadSettings().enabled;

    std::function<void(CCNode*)> walk = [&](CCNode* node) {
        if (!node) return;
        if (node->getID() == "paimon-desc-translator"_spr) {
            // hiding the toggle is not enough: an applied translation must go back
            if (!enabled) {
                if (auto* toggle = typeinfo_cast<paimon::translate::TranslateToggle*>(node)) {
                    toggle->restore();
                }
            }
            node->setVisible(enabled);
        }
        if (auto* cell = typeinfo_cast<CommentCell*>(node)) {
            static_cast<PaimonCommentTranslatorCell*>(cell)->syncTranslateButton();
        }
        if (auto* children = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) walk(child);
        }
    };
    walk(scene);
}

}
