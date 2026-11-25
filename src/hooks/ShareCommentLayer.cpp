#include <Geode/Geode.hpp>
#include <Geode/modify/ShareCommentLayer.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <Geode/ui/NodeEvent.hpp>
#include <Geode/ui/Notification.hpp>
#include "../framework/HookConventions.hpp"
#include <algorithm>
#include <limits>
#include <vector>
#include "../features/emotes/ui/EmoteButton.hpp"
#include "../features/emotes/ui/EmoteAutocomplete.hpp"
#include "../features/fonts/ui/FontButton.hpp"
#include "../features/fonts/FontTag.hpp"
#include "../features/translator/TranslationService.hpp"
#include "../features/translator/TranslatorConfig.hpp"
#include "../features/translator/TranslatorText.hpp"
#include "../features/translator/ui/TranslateButton.hpp"
#include "../features/translator/ui/TranslationTargetPopup.hpp"
#include "../utils/Localization.hpp"
#include "../utils/SpriteHelper.hpp"

using namespace geode::prelude;
using namespace cocos2d;

namespace {

// our quick buttons sit inside the brown window: the round ones in the free
// space right of Submit, Copy/Paste on the title row left of the title.
constexpr float kQuickButtonHeight = 22.f;
constexpr float kQuickButtonGap = 5.f;
constexpr float kNativeRowGap = 6.f;
constexpr float kFramePadding = 12.f;
// the corner info button covers about this much of the top-left corner
constexpr float kTopCornerClearance = 24.f;
constexpr float kToolButtonHeight = 18.f;
constexpr float kToolButtonGap = 4.f;

CCRect boundsInLayer(CCNode* node, CCNode* layer) {
    auto const size = node->getContentSize();
    auto const a = layer->convertToNodeSpace(node->convertToWorldSpace({0.f, 0.f}));
    auto const b = layer->convertToNodeSpace(node->convertToWorldSpace({size.width, size.height}));
    float const minX = std::min(a.x, b.x);
    float const minY = std::min(a.y, b.y);
    return {minX, minY, std::max(a.x, b.x) - minX, std::max(a.y, b.y) - minY};
}

// the brown window is the largest scale9 on the main layer; the comment box is smaller.
CCNode* findWindowBackground(CCNode* layer) {
    CCNode* best = nullptr;
    float bestArea = 0.f;
    for (auto* child : layer->getChildrenExt()) {
        if (!child || !child->isVisible()) continue;
        if (!typeinfo_cast<extension::CCScale9Sprite*>(child) && !typeinfo_cast<NineSlice*>(child)) {
            continue;
        }
        auto const size = child->getScaledContentSize();
        float const area = size.width * size.height;
        if (area > bestArea) {
            bestArea = area;
            best = child;
        }
    }
    return best;
}

void setItemScale(CCMenuItemSpriteExtra* item, float scale) {
    item->setScale(scale);
    // the press animation scales back to m_baseScale
    item->m_baseScale = scale;
}

}

class $modify(PaimonShareComment, ShareCommentLayer) {
    struct Fields {
        // weakref avoids dangling toolbar nodes during teardown
        WeakRef<CCMenu> m_toolMenu;
        WeakRef<CCMenuItemSpriteExtra> m_copyBtn;
        WeakRef<CCMenuItemSpriteExtra> m_pasteBtn;
        WeakRef<CCMenuItemSpriteExtra> m_translateBtn;
        int m_clipboardCheckCounter = 0;
        bool m_lastClipboardHasContent = false;
        // title row slot for Copy/Paste, in m_mainLayer space
        float m_toolLeft = 0.f;
        float m_toolRight = 0.f;
        float m_toolY = 0.f;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "ShareCommentLayer::init");
    }

    void onCopyInput(CCObject*) {
        if (!m_commentInput) return;
        std::string text = m_commentInput->getString();
        if (!text.empty()) geode::utils::clipboard::write(text);
    }

    void onPasteInput(CCObject*) {
        if (!m_commentInput) return;
        std::string clipboard = geode::utils::clipboard::read();
        if (clipboard.empty()) return;
        std::string current = m_commentInput->getString();
        std::string newText = current + clipboard;
        if (static_cast<int>(newText.size()) <= m_charLimit) {
            m_commentInput->setString(newText);
            this->updateCharCountLabel();
        }
    }

    CCMenuItemSpriteExtra* createToolButton(const char* text, SEL_MenuHandler sel) {
        auto btnSpr = ButtonSprite::create(
            text, 40, true, "bigFont.fnt", "GJ_plainBtn_001.png", 18.f, 0.40f);
        return CCMenuItemSpriteExtra::create(btnSpr, this, sel);
    }

    void setTranslateBusy(bool busy) {
        if (auto button = m_fields->m_translateBtn.lock()) {
            paimon::translate::ui::setTranslateButtonState(button,
                busy ? paimon::translate::ui::TranslateButtonState::Busy
                     : paimon::translate::ui::TranslateButtonState::Idle);
        }
    }

    // translates what the player typed and writes the result back into the input.
    void onTranslateInput(CCObject*) {
        WeakRef<PaimonShareComment> weakSelf = this;
        auto settings = paimon::translate::loadSettings();
        auto* popup = paimon::translate::TranslationTargetPopup::create(
            paimon::loc::tr("translator.pick", "Translate to"), settings.composeTarget,
            [weakSelf](std::string const& target) {
                // remembered so the next picker marks this language, not the reading one
                paimon::translate::saveComposeTarget(target);
                auto selfRef = weakSelf.lock();
                auto* self = static_cast<PaimonShareComment*>(selfRef.data());
                if (!self || !self->m_commentInput) return;

                std::string text = paimon::translate::sanitizeForTranslation(
                    self->m_commentInput->getString());
                if (!paimon::translate::isTranslatable(text)) {
                    Notification::create(
                        paimon::loc::tr("translator.empty", "There is no text to translate"),
                        NotificationIcon::Info, 1.8f)->show();
                    return;
                }

                int const limit = self->m_charLimit;
                self->setTranslateBusy(true);
                paimon::translate::TranslationService::get().translate(text, target,
                    [weakSelf, limit](bool ok, std::string result) {
                        auto ref = weakSelf.lock();
                        auto* layer = static_cast<PaimonShareComment*>(ref.data());
                        if (!layer) return;
                        layer->setTranslateBusy(false);

                        if (!ok || result.empty()) {
                            Notification::create(
                                paimon::loc::tr("translator.failed", "Translation failed"),
                                NotificationIcon::Error, 2.2f)->show();
                            return;
                        }
                        if (static_cast<int>(result.size()) > limit) {
                            Notification::create(
                                paimon::loc::tr("translator.too_long", "The translation does not fit"),
                                NotificationIcon::Warning, 2.4f)->show();
                            return;
                        }
                        if (!layer->m_commentInput) return;
                        layer->m_commentInput->setString(result);
                        layer->updateCharCountLabel();
                    });
            });
        if (popup) popup->show();
    }

    // window rect in m_mainLayer space. m_mainLayer itself is screen sized.
    CCRect findWindowFrame(CCLayer* layer) {
        if (auto* bg = findWindowBackground(layer)) return boundsInLayer(bg, layer);

        auto const size = layer->getContentSize();
        CCRect frame{size.width * .5f - 200.f, size.height * .5f + 10.f, 400.f, 140.f};
        if (m_commentInput) {
            auto const inputSize = m_commentInput->getContentSize();
            if (inputSize.width > 0.f && inputSize.height > 0.f) {
                auto const input = boundsInLayer(m_commentInput, layer);
                frame = {input.getMinX() - 20.f, input.getMinY() - 50.f,
                    input.size.width + 40.f, input.size.height + 90.f};
            }
        }
        return frame;
    }

    // lays the round buttons out on the Cancel/Submit row, between Submit and the
    // window's right edge. They shrink together if the gap is narrow.
    void layoutQuickButtons(CCLayer* layer, CCRect const& frame,
        std::vector<CCMenuItemSpriteExtra*> const& buttons) {
        if (buttons.empty()) return;

        float rowY = frame.getMinY() + 24.f;
        float nativeRight = frame.getMidX() + frame.size.width * .28f;

        // native Cancel/Submit: bottom part of the window, away from the % toggle
        float sumY = 0.f;
        int count = 0;
        float right = -std::numeric_limits<float>::max();
        for (auto* child : layer->getChildrenExt()) {
            auto* menu = typeinfo_cast<CCMenu*>(child);
            if (!menu || !menu->isVisible()) continue;
            for (auto* node : menu->getChildrenExt()) {
                auto* item = typeinfo_cast<CCMenuItem*>(node);
                if (!item || !item->isVisible()) continue;
                auto const bounds = boundsInLayer(item, layer);
                float const cx = bounds.getMidX();
                float const cy = bounds.getMidY();
                bool const lowerHalf = cy > frame.getMinY()
                    && cy < frame.getMinY() + frame.size.height * .45f;
                bool const middle = cx > frame.getMinX() + frame.size.width * .2f
                    && cx < frame.getMinX() + frame.size.width * .85f;
                if (!lowerHalf || !middle) continue;
                sumY += cy;
                ++count;
                right = std::max(right, bounds.getMaxX());
            }
        }
        if (count > 0) {
            rowY = sumY / static_cast<float>(count);
            nativeRight = right;
        }

        float const left = nativeRight + kNativeRowGap;
        float const available = frame.getMaxX() - kFramePadding - left;

        std::vector<float> scales;
        float total = kQuickButtonGap * static_cast<float>(buttons.size() - 1);
        for (auto* button : buttons) {
            float const height = std::max(1.f, button->getContentSize().height);
            float const scale = kQuickButtonHeight / height;
            scales.push_back(scale);
            total += button->getContentSize().width * scale;
        }

        float fit = 1.f;
        if (available > 0.f && total > available) fit = std::max(.6f, available / total);
        float const width = total * fit;
        float x = left + std::max(0.f, (available - width) * .5f);
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            auto* button = buttons[i];
            float const scale = scales[i] * fit;
            setItemScale(button, scale);
            float const w = button->getContentSize().width * scale;
            button->setPosition({x + w * .5f, rowY});
            x += w + kQuickButtonGap * fit;
        }
    }

    // copy/Paste go on the title row, left of the title and clear of the corner button.
    void findToolSlot(CCLayer* layer, CCRect const& frame, std::string const& title) {
        float y = frame.getMaxY() - 18.f;
        float limit = frame.getMidX() - 70.f;
        for (auto* child : layer->getChildrenExt()) {
            auto* label = typeinfo_cast<CCLabelBMFont*>(child);
            if (!label || !label->isVisible() || title.empty()) continue;
            if (title != label->getString()) continue;
            auto const bounds = boundsInLayer(label, layer);
            y = bounds.getMidY();
            limit = bounds.getMinX() - kToolButtonGap * 2.f;
            break;
        }
        m_fields->m_toolLeft = frame.getMinX() + kTopCornerClearance;
        m_fields->m_toolRight = limit;
        m_fields->m_toolY = y;
    }

    // packs the visible tool buttons from the left so a lone Paste has no gap before it.
    void layoutToolButtons() {
        std::vector<CCMenuItemSpriteExtra*> visible;
        auto copyBtn = m_fields->m_copyBtn.lock();
        auto pasteBtn = m_fields->m_pasteBtn.lock();
        for (auto* button : {copyBtn.data(), pasteBtn.data()}) {
            if (button && button->isVisible()) visible.push_back(button);
        }
        if (visible.empty()) return;

        std::vector<float> scales;
        float total = kToolButtonGap * static_cast<float>(visible.size() - 1);
        for (auto* button : visible) {
            float const height = std::max(1.f, button->getContentSize().height);
            float const scale = std::min(1.f, kToolButtonHeight / height);
            scales.push_back(scale);
            total += button->getContentSize().width * scale;
        }
        float const available = m_fields->m_toolRight - m_fields->m_toolLeft;
        float fit = 1.f;
        if (available > 0.f && total > available) fit = std::max(.6f, available / total);

        float x = m_fields->m_toolLeft;
        for (std::size_t i = 0; i < visible.size(); ++i) {
            auto* button = visible[i];
            float const scale = scales[i] * fit;
            setItemScale(button, scale);
            float const w = button->getContentSize().width * scale;
            button->setPosition({x + w * .5f, m_fields->m_toolY});
            x += w + kToolButtonGap * fit;
        }
    }

    $override
    bool init(std::string title, int charLimit, CommentType type, int ID, std::string desc) {
        // extend the local limit; the server still validates length
        int extendedLimit = charLimit * 2;
        if (!ShareCommentLayer::init(title, extendedLimit, type, ID, desc)) return false;

        m_charLimit = extendedLimit;
        if (m_commentInput) {
            m_commentInput->setMaxLabelLength(extendedLimit);
        }
        this->updateCharCountLabel();

        if (m_commentInput) {
            m_commentInput->m_allowedChars = geode::getCommonFilterAllowedChars(geode::CommonFilter::TextEmojis);
        }

        auto* layer = m_mainLayer;
        if (!layer) return true;
        auto contentSize = layer->getContentSize();

        // measured before our menus exist so they never count as native controls
        auto const frame = findWindowFrame(layer);
        findToolSlot(layer, frame, title);

        auto quickInsertMenu = CCMenu::create();
        quickInsertMenu->setID("comment-quick-insert-menu"_spr);
        quickInsertMenu->setPosition({0.f, 0.f});
        quickInsertMenu->setContentSize(contentSize);

        std::vector<CCMenuItemSpriteExtra*> quickButtons;

        WeakRef<ShareCommentLayer> weakSelf = this;
        paimon::emotes::EmoteInputContext ctx;
        ctx.getText = [weakSelf]() -> std::string {
            auto self = weakSelf.lock();
            if (!self || !self->m_commentInput) return "";
            return std::string(self->m_commentInput->getString());
        };
        ctx.setText = [weakSelf](std::string const& text) {
            auto self = weakSelf.lock();
            if (!self || !self->m_commentInput) return;
            if (static_cast<int>(text.size()) <= self->m_charLimit) {
                self->m_commentInput->setString(text);
                self->updateCharCountLabel();
            }
        };
        ctx.charLimit = this->m_charLimit;
        ctx.pickerSize = paimon::emotes::EmotePickerPopup::LayoutSize::Large;
        ctx.centerPicker = true;

        auto emoteBtn = paimon::emotes::EmoteButton::create(std::move(ctx));
        if (emoteBtn) {
            quickInsertMenu->addChild(emoteBtn);
            // hidden when the emotes module is off; it should not take a slot
            if (emoteBtn->isVisible()) quickButtons.push_back(emoteBtn);
        }

        auto fontBtn = paimon::fonts::FontButton::create(
            [weakSelf](std::string const& fontTag) {
                auto self = weakSelf.lock();
                if (!self || !self->m_commentInput) return;
                std::string current = self->m_commentInput->getString();

                auto existing = paimon::fonts::extractFontId(current);
                if (!existing.empty()) {
                    auto closePos = current.find('>');
                    if (closePos != std::string::npos) {
                        current = current.substr(closePos + 1);
                        if (!current.empty() && current.front() == ' ') {
                            current.erase(current.begin());
                        }
                    }
                }

                std::string newText = fontTag.empty() ? current : fontTag + current;
                if (static_cast<int>(newText.size()) <= self->m_charLimit) {
                    self->m_commentInput->setString(newText);
                    self->updateCharCountLabel();
                }
            }
        );
        if (fontBtn) {
            quickInsertMenu->addChild(fontBtn);
            quickButtons.push_back(fontBtn);
        }

        auto translateBtn = paimon::translate::ui::makeTranslateButton(
            [this] { onTranslateInput(nullptr); }, 1.f);
        if (translateBtn) {
            translateBtn->setID("comment-translate-btn"_spr);
            quickInsertMenu->addChild(translateBtn);
            quickButtons.push_back(translateBtn);
            m_fields->m_translateBtn = translateBtn;
        }

        layoutQuickButtons(layer, frame, quickButtons);
        layer->addChild(quickInsertMenu, 15);

        {
            auto toolMenu = CCMenu::create();
            toolMenu->setID("comment-tool-menu"_spr);
            toolMenu->setPosition({0.f, 0.f});
            toolMenu->setContentSize(contentSize);
            m_fields->m_toolMenu = toolMenu;

            auto copyBtn = createToolButton("Copy",
                menu_selector(PaimonShareComment::onCopyInput));
            copyBtn->setVisible(false);
            toolMenu->addChild(copyBtn);
            m_fields->m_copyBtn = copyBtn;

            auto pasteBtn = createToolButton("Paste",
                menu_selector(PaimonShareComment::onPasteInput));
            pasteBtn->setVisible(false);
            toolMenu->addChild(pasteBtn);
            m_fields->m_pasteBtn = pasteBtn;

            layer->addChild(toolMenu, 10);
        }

        if (m_commentInput) {
            auto ac = paimon::emotes::EmoteAutocomplete::create(m_commentInput,
                [weakSelf](std::string const& newText) {
                    auto self = weakSelf.lock();
                    if (!self || !self->m_commentInput) return;
                    self->m_commentInput->setString(newText);
                    self->updateCharCountLabel();
                }
            );
            ac->setPosition({contentSize.width / 2.f - 60.f, contentSize.height / 2.f + 10.f});
            layer->addChild(ac, 100);
        }

        this->schedule(schedule_selector(PaimonShareComment::tickToolButtons), 0.1f);
        this->addOnExitCallback([this] {
            this->unschedule(schedule_selector(PaimonShareComment::tickToolButtons));
            return true;
        });

        return true;
    }

    void tickToolButtons(float) {
        auto toolMenu = m_fields->m_toolMenu.lock();
        if (!m_commentInput || !toolMenu) return;

        m_fields->m_clipboardCheckCounter++;
        if (m_fields->m_clipboardCheckCounter % 2 != 0) return;

        bool changed = false;
        bool hasText = !std::string(m_commentInput->getString()).empty();
        if (auto copyBtn = m_fields->m_copyBtn.lock(); copyBtn && copyBtn->isVisible() != hasText) {
            copyBtn->setVisible(hasText);
            changed = true;
        }

        if (m_fields->m_clipboardCheckCounter >= 20) {
            m_fields->m_clipboardCheckCounter = 0;
            m_fields->m_lastClipboardHasContent = !geode::utils::clipboard::read().empty();
        }

        bool const hasClipboard = m_fields->m_lastClipboardHasContent;
        if (auto pasteBtn = m_fields->m_pasteBtn.lock(); pasteBtn && pasteBtn->isVisible() != hasClipboard) {
            pasteBtn->setVisible(hasClipboard);
            changed = true;
        }

        if (changed) layoutToolButtons();
    }
};
