#include <Geode/Geode.hpp>
#include <Geode/modify/ShareCommentLayer.hpp>
#include <Geode/binding/CCTextInputNode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/TextInput.hpp>
#include <algorithm>
#include "../features/emotes/ui/EmoteButton.hpp"
#include "../features/emotes/ui/EmoteAutocomplete.hpp"
#include "../features/fonts/ui/FontButton.hpp"
#include "../features/fonts/FontTag.hpp"
#include "../utils/SpriteHelper.hpp"

using namespace geode::prelude;
using namespace cocos2d;

class $modify(PaimonShareComment, ShareCommentLayer) {
    struct Fields {
        CCMenu* m_toolMenu = nullptr;
        CCMenuItemSpriteExtra* m_copyBtn = nullptr;
        CCMenuItemSpriteExtra* m_pasteBtn = nullptr;
        int m_clipboardCheckCounter = 0;
        bool m_lastClipboardHasContent = false;
    };

    static void onModify(auto& self) {
        (void)self.setHookPriorityAfterPost("ShareCommentLayer::init", "geode.node-ids");
    }

    void onCopyInput(CCObject*) {
        if (!m_commentInput) return;
        std::string text = m_commentInput->getString();
        if (!text.empty()) {
            geode::utils::clipboard::write(text);
        }
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
            text, 40, true, "bigFont.fnt", "GJ_plainBtn_001.png", 25.f, 0.5f);
        btnSpr->setScale(0.6f);
        return CCMenuItemSpriteExtra::create(btnSpr, this, sel);
    }

    CCPoint getNodePointInLayer(CCLayer* layer, CCNode* node, CCPoint localPoint) {
        if (!layer || !node) return {0.f, 0.f};
        return layer->convertToNodeSpace(node->convertToWorldSpace(localPoint));
    }

    $override
    bool init(gd::string title, int charLimit, CommentType type, int ID, gd::string desc) {
        if (!ShareCommentLayer::init(title, charLimit, type, ID, desc)) return false;

        // Permite caracteres especiales para emotes
        if (m_commentInput) {
            m_commentInput->m_allowedChars = geode::getCommonFilterAllowedChars(geode::CommonFilter::Any);
        }

        auto* layer = m_mainLayer;
        if (!layer) return true;
        auto contentSize = layer->getContentSize();

        float quickButtonX = contentSize.width - 28.f;
        float quickButtonY = contentSize.height / 2.f - 4.f;

        if (m_commentInput) {
            auto inputSize = m_commentInput->getContentSize();
            if (inputSize.width > 0.f && inputSize.height > 0.f) {
                auto inputCenter = getNodePointInLayer(
                    layer,
                    m_commentInput,
                    {inputSize.width * 0.5f, inputSize.height * 0.5f}
                );
                auto inputRight = getNodePointInLayer(
                    layer,
                    m_commentInput,
                    {inputSize.width, inputSize.height * 0.5f}
                );

                quickButtonX = std::clamp(inputRight.x + 18.f, 18.f, contentSize.width - 18.f);
                quickButtonY = inputCenter.y;
            }
        }

        auto quickInsertMenu = CCMenu::create();
        quickInsertMenu->setID("comment-quick-insert-menu"_spr);
        quickInsertMenu->setPosition({0.f, 0.f});
        quickInsertMenu->setContentSize(contentSize);
        layer->addChild(quickInsertMenu, 15);

        // Boton de emote
        Ref<ShareCommentLayer> self = this;
        paimon::emotes::EmoteInputContext ctx;
        ctx.getText = [self]() -> std::string {
            if (!self->m_commentInput) return "";
            return std::string(self->m_commentInput->getString());
        };
        ctx.setText = [self](std::string const& text) {
            if (!self->m_commentInput) return;
            if (static_cast<int>(text.size()) <= self->m_charLimit) {
                self->m_commentInput->setString(text);
                self->updateCharCountLabel();
            }
        };
        ctx.charLimit = self->m_charLimit;

        auto emoteBtn = paimon::emotes::EmoteButton::create(std::move(ctx));
        if (emoteBtn) {
            emoteBtn->setScale(0.5f);
            emoteBtn->setPosition({quickButtonX, quickButtonY + 14.f});
            quickInsertMenu->addChild(emoteBtn);
        }

        // Boton de fuente al lado del emote
        auto fontBtn = paimon::fonts::FontButton::create(
            [self](std::string const& fontTag) {
                if (!self->m_commentInput) return;
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
            fontBtn->setScale(0.6f);
            fontBtn->setPosition({quickButtonX, quickButtonY - 14.f});
            quickInsertMenu->addChild(fontBtn);
        }

        // Barra de herramientas Copy/Paste
        {
            auto toolMenu = CCMenu::create();
            toolMenu->setPosition({0.f, 0.f});
            toolMenu->setContentSize(contentSize);
            m_fields->m_toolMenu = toolMenu;

            float toolY = contentSize.height / 2.f - 40.f;
            float gap = 4.f;
            float centerX = contentSize.width - 28.f;

            // Copy button
            auto copyBtn = createToolButton("Copy",
                menu_selector(PaimonShareComment::onCopyInput));
            copyBtn->setPosition({centerX - 22.f - gap / 2, toolY});
            copyBtn->setVisible(false);
            toolMenu->addChild(copyBtn);
            m_fields->m_copyBtn = copyBtn;

            // Paste button
            auto pasteBtn = createToolButton("Paste",
                menu_selector(PaimonShareComment::onPasteInput));
            pasteBtn->setPosition({centerX + 22.f + gap / 2, toolY});
            pasteBtn->setVisible(false);
            toolMenu->addChild(pasteBtn);
            m_fields->m_pasteBtn = pasteBtn;

            layer->addChild(toolMenu, 10);
        }

        // Autocomplete
        if (m_commentInput) {
            auto ac = paimon::emotes::EmoteAutocomplete::create(
                m_commentInput,
                [self](std::string const& newText) {
                    if (!self->m_commentInput) return;
                    self->m_commentInput->setString(newText);
                    self->updateCharCountLabel();
                }
            );
            ac->setPosition({contentSize.width / 2.f - 60.f, contentSize.height / 2.f - 30.f + 40.f});
            layer->addChild(ac, 100);
        }

        // Programa update para visibilidad dinamica
        this->scheduleUpdate();

        return true;
    }

    $override
    void update(float dt) {
        ShareCommentLayer::update(dt);

        if (!m_commentInput || !m_fields->m_toolMenu) return;

        // Muestra Copy si hay texto
        bool hasText = !std::string(m_commentInput->getString()).empty();
        if (m_fields->m_copyBtn) {
            m_fields->m_copyBtn->setVisible(hasText);
        }

        // Revisa clipboard cada ~30 frames
        m_fields->m_clipboardCheckCounter++;
        if (m_fields->m_clipboardCheckCounter >= 30) {
            m_fields->m_clipboardCheckCounter = 0;
            m_fields->m_lastClipboardHasContent = !geode::utils::clipboard::read().empty();
        }

        if (m_fields->m_pasteBtn) {
            m_fields->m_pasteBtn->setVisible(m_fields->m_lastClipboardHasContent);
        }
    }
};
