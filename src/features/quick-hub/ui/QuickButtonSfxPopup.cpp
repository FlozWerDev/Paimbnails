#include "QuickButtonSfxPopup.hpp"

#include "QuickButtonEditKit.hpp"
#include "../services/QuickButtonSfx.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <algorithm>
#include <filesystem>

using namespace geode::prelude;

namespace paimon::quickhub {

QuickButtonSfxPopup* QuickButtonSfxPopup::create(
    CustomQuickButton* target, std::function<void()> onChanged) {
    if (!target) return nullptr;
    auto* ret = new QuickButtonSfxPopup();
    ret->m_target = target;
    ret->m_onChanged = std::move(onChanged);
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void QuickButtonSfxPopup::changed() {
    if (m_onChanged) m_onChanged();
    refresh();
}

void QuickButtonSfxPopup::syncInputs() {
    if (m_gameInput && m_gameInput->isVisible()) {
        m_target->sfxPath = std::string(m_gameInput->getString());
    }
    if (m_onlineInput && m_onlineInput->isVisible()) {
        m_target->sfxId = std::max(0, utils::numFromString<int>(
            std::string(m_onlineInput->getString())).unwrapOr(0));
    }
}

void QuickButtonSfxPopup::onExit() {
    syncInputs();
    if (m_onChanged) m_onChanged();
    Popup::onExit();
}

bool QuickButtonSfxPopup::init() {
    if (!Popup::init(400.f, 330.f)) return false;
    paimon::markDynamicPopup(this);
    this->setTitle("Sonido del boton");

    auto size = m_mainLayer->getContentSize();
    float cx = size.width * 0.5f;

    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    m_menu->setContentSize(size);
    m_mainLayer->addChild(m_menu, 3);

    m_ctx = CCNode::create();
    m_ctx->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_ctx, 2);

    struct Src { char const* label; QuickButtonSfxKind kind; };
    static const Src sources[] = {
        {"No", QuickButtonSfxKind::None},
        {"Juego", QuickButtonSfxKind::Game},
        {"Online", QuickButtonSfxKind::Online},
        {"Archivo", QuickButtonSfxKind::File},
    };
    float sx = cx - 141.f;
    for (auto const& src : sources) {
        bool selected = static_cast<QuickButtonSfxKind>(m_target->sfxKind) == src.kind;
        auto kind = src.kind;
        auto* item = makeMiniButton(src.label, selected, [this, kind] {
            syncInputs();
            m_target->sfxKind = static_cast<int>(kind);
            changed();
        });
        item->setPosition({sx + 47.f, 282.f});
        m_menu->addChild(item);
        sx += 94.f;
    }

    auto addValue = [&](CCLabelBMFont*& slot, float x, float y) {
        slot = CCLabelBMFont::create("", "chatFont.fnt");
        slot->setScale(0.5f);
        slot->setPosition({x, y});
        m_mainLayer->addChild(slot, 2);
    };
    auto addCap = [&](char const* text, float x, float y) {
        auto* label = CCLabelBMFont::create(text, "goldFont.fnt");
        label->setScale(0.32f);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({x, y});
        m_mainLayer->addChild(label, 2);
    };
    auto addStepper = [&](float y, float cxb, std::function<void(int)> step) {
        auto* minus = makeMiniButton("-", false, [step] { step(-1); });
        minus->setPosition({cxb - 62.f, y});
        m_menu->addChild(minus);
        auto* plus = makeMiniButton("+", false, [step] { step(1); });
        plus->setPosition({cxb + 62.f, y});
        m_menu->addChild(plus);
    };

    addCap("Volumen", 24.f, 168.f);
    addValue(m_volValue, cx + 40.f, 168.f);
    addStepper(168.f, cx + 40.f, [this](int d) {
        m_target->sfxVolume = std::clamp(m_target->sfxVolume + d * 0.05f, 0.f, 1.f);
        changed();
    });

    addCap("Velocidad", 24.f, 146.f);
    addValue(m_speedValue, cx + 40.f, 146.f);
    addStepper(146.f, cx + 40.f, [this](int d) {
        m_target->sfxSpeed = std::clamp(m_target->sfxSpeed + d * 0.05f, 0.4f, 2.5f);
        changed();
    });

    auto addPreset = [&](char const* label, float x, float vol, float speed, int fi, int fo) {
        auto* item = makeMiniButton(label, false, [this, vol, speed, fi, fo] {
            m_target->sfxVolume = vol;
            m_target->sfxSpeed = speed;
            m_target->sfxFadeInMs = fi;
            m_target->sfxFadeOutMs = fo;
            changed();
        });
        item->setPosition({x, 124.f});
        m_menu->addChild(item);
    };
    addPreset("Suave", cx - 80.f, 0.6f, 0.9f, 150, 200);
    addPreset("Normal", cx, 1.f, 1.f, 0, 0);
    addPreset("Impacto", cx + 80.f, 1.f, 1.25f, 0, 0);

    addCap("Inicio", 24.f, 102.f);
    addValue(m_startValue, cx - 40.f, 102.f);
    addStepper(102.f, cx - 40.f, [this](int d) {
        m_target->sfxStartMs = std::max(0, m_target->sfxStartMs + d * 100);
        changed();
    });

    addCap("Fin (0=todo)", 200.f, 102.f);
    addValue(m_endValue, cx + 116.f, 102.f);
    addStepper(102.f, cx + 116.f, [this](int d) {
        m_target->sfxEndMs = std::max(0, m_target->sfxEndMs + d * 100);
        changed();
    });

    addCap("Fundido in/out", 24.f, 80.f);
    addValue(m_fadeValue, cx - 40.f, 80.f);
    {
        auto* minus = makeMiniButton("-", false, [this] {
            m_target->sfxFadeInMs = std::max(0, m_target->sfxFadeInMs - 50);
            m_target->sfxFadeOutMs = std::max(0, m_target->sfxFadeOutMs - 50);
            changed();
        });
        minus->setPosition({cx - 102.f, 80.f});
        m_menu->addChild(minus);
        auto* plus = makeMiniButton("+", false, [this] {
            m_target->sfxFadeInMs = std::min(2000, m_target->sfxFadeInMs + 50);
            m_target->sfxFadeOutMs = std::min(2000, m_target->sfxFadeOutMs + 50);
            changed();
        });
        plus->setPosition({cx + 22.f, 80.f});
        m_menu->addChild(plus);
    }

    m_durLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_durLabel->setScale(0.42f);
    m_durLabel->setPosition({cx, 60.f});
    m_mainLayer->addChild(m_durLabel, 2);

    auto* test = ButtonSprite::create("Probar", "bigFont.fnt", "GJ_button_04.png", .8f);
    test->setScale(0.55f);
    auto* testBtn = CCMenuItemExt::createSpriteExtra(test, [this](CCMenuItemSpriteExtra*) {
        syncInputs();
        if (!playQuickButtonSfx(*m_target)) {
            PaimonNotify::create("Sin sonido: elige o descarga primero.", NotificationIcon::Warning)->show();
        }
        refresh();
    });
    testBtn->setPosition({cx - 70.f, 32.f});
    m_menu->addChild(testBtn);

    auto* done = ButtonSprite::create("Listo", "goldFont.fnt", "GJ_button_01.png", .8f);
    done->setScale(0.6f);
    auto* doneBtn = CCMenuItemExt::createSpriteExtra(done, [this](CCMenuItemSpriteExtra*) {
        syncInputs();
        this->keyBackClicked();
    });
    doneBtn->setPosition({cx + 70.f, 32.f});
    m_menu->addChild(doneBtn);

    refresh();
    return true;
}

void QuickButtonSfxPopup::refresh() {
    if (!m_ctx) return;
    m_ctx->removeAllChildren();
    m_gameInput = nullptr;
    m_onlineInput = nullptr;
    m_fileLabel = nullptr;
    // Own menu for context buttons (Online/File): clearing m_ctx drops it with no count pruning.
    m_dynMenu = CCMenu::create();
    m_dynMenu->setPosition({0.f, 0.f});
    m_dynMenu->setContentSize(m_mainLayer->getContentSize());
    m_ctx->addChild(m_dynMenu, 3);

    auto size = m_mainLayer->getContentSize();
    float cx = size.width * 0.5f;
    auto kind = static_cast<QuickButtonSfxKind>(m_target->sfxKind);

    if (kind == QuickButtonSfxKind::None) {
        auto* hint = CCLabelBMFont::create("Suena el sonido original del juego.", "chatFont.fnt");
        hint->setScale(0.45f);
        hint->setPosition({cx, 236.f});
        m_ctx->addChild(hint);
    } else if (kind == QuickButtonSfxKind::Game) {
        m_gameInput = TextInput::create(240.f / 0.8f, "explode_11.ogg", "chatFont.fnt");
        m_gameInput->setCommonFilter(CommonFilter::Any);
        m_gameInput->setMaxCharCount(64);
        m_gameInput->setString(m_target->sfxPath);
        m_gameInput->setScale(0.8f);
        m_gameInput->setPosition({cx, 244.f});
        m_ctx->addChild(m_gameInput);
        auto* hint = CCLabelBMFont::create("Nombre del ogg del juego", "chatFont.fnt");
        hint->setScale(0.38f);
        hint->setColor({150, 160, 185});
        hint->setPosition({cx, 222.f});
        m_ctx->addChild(hint);
    } else if (kind == QuickButtonSfxKind::Online) {
        m_onlineInput = TextInput::create(140.f / 0.8f, "ID de SFX", "chatFont.fnt");
        m_onlineInput->setCommonFilter(CommonFilter::Uint);
        m_onlineInput->setMaxCharCount(10);
        m_onlineInput->setString(m_target->sfxId > 0 ? std::to_string(m_target->sfxId) : "");
        m_onlineInput->setScale(0.8f);
        m_onlineInput->setPosition({cx - 60.f, 244.f});
        m_ctx->addChild(m_onlineInput);
        auto* dl = makeMiniButton("Probar", false, [this] {
            syncInputs();
            if (!playQuickButtonSfx(*m_target)) {
                PaimonNotify::create("Descargando o sin ID...", NotificationIcon::Warning)->show();
            }
            refresh();
        });
        dl->setPosition({cx + 90.f, 244.f});
        m_dynMenu->addChild(dl);
        auto* hint = CCLabelBMFont::create("ID de la libreria de SFX", "chatFont.fnt");
        hint->setScale(0.38f);
        hint->setColor({150, 160, 185});
        hint->setPosition({cx, 222.f});
        m_ctx->addChild(hint);
    } else {
        std::string name = fileNameOf(m_target->sfxPath);
        auto* pick = makeMiniButton(
            name.empty() ? "Elegir mp3/ogg/wav" : "Cambiar archivo", false, [this] { onChooseAudio(); });
        pick->setPosition({cx - 70.f, 244.f});
        m_dynMenu->addChild(pick);
        m_fileLabel = CCLabelBMFont::create(
            name.empty() ? "Sin archivo" : name.c_str(), "chatFont.fnt");
        m_fileLabel->setScale(0.42f);
        m_fileLabel->limitLabelWidth(150.f, 0.42f, 0.2f);
        m_fileLabel->setPosition({cx + 80.f, 244.f});
        m_ctx->addChild(m_fileLabel);
        if (!name.empty()) {
            auto* rm = makeMiniButton("X", false, [this] {
                m_target->sfxPath.clear();
                changed();
            });
            rm->setPosition({cx + 168.f, 244.f});
            m_dynMenu->addChild(rm);
        }
        auto* hint = CCLabelBMFont::create("mp3, ogg, wav, flac", "chatFont.fnt");
        hint->setScale(0.38f);
        hint->setColor({150, 160, 185});
        hint->setPosition({cx, 222.f});
        m_ctx->addChild(hint);
    }

    if (m_volValue) {
        m_volValue->setString(
            fmt::format("{}%", static_cast<int>(std::round(m_target->sfxVolume * 100.f))).c_str());
    }
    if (m_speedValue) {
        m_speedValue->setString(fmt::format("{:.2f}x", m_target->sfxSpeed).c_str());
    }
    if (m_startValue) {
        m_startValue->setString(fmt::format("{}ms", m_target->sfxStartMs).c_str());
    }
    if (m_endValue) {
        m_endValue->setString(
            m_target->sfxEndMs <= 0 ? "todo" : fmt::format("{}ms", m_target->sfxEndMs).c_str());
    }
    if (m_fadeValue) {
        m_fadeValue->setString(
            fmt::format("{}/{}ms", m_target->sfxFadeInMs, m_target->sfxFadeOutMs).c_str());
    }
    updateDuration();
}

void QuickButtonSfxPopup::updateDuration() {
    if (!m_durLabel) return;
    CustomQuickButton probe = *m_target;
    if (m_gameInput && m_gameInput->isVisible()) {
        probe.sfxPath = std::string(m_gameInput->getString());
    }
    if (m_onlineInput && m_onlineInput->isVisible()) {
        probe.sfxId = std::max(0, utils::numFromString<int>(
            std::string(m_onlineInput->getString())).unwrapOr(0));
    }
    std::string path = resolveQuickButtonSfxPath(probe);
    if (path.empty()) {
        m_durLabel->setString("Sin archivo");
        return;
    }
    unsigned int ms = 0;
    if (!probeQuickButtonSfxDuration(path, &ms) || ms == 0) {
        m_durLabel->setString("No se pudo leer");
        return;
    }
    int start = std::max(0, probe.sfxStartMs);
    int end = probe.sfxEndMs > start ? probe.sfxEndMs : static_cast<int>(ms);
    if (end > static_cast<int>(ms)) end = static_cast<int>(ms);
    m_durLabel->setString(
        fmt::format("Dura {:.1f}s - suena {:.1f}s", ms / 1000.f, (end - start) / 1000.f).c_str());
}

void QuickButtonSfxPopup::onChooseAudio() {
    WeakRef<QuickButtonSfxPopup> self = this;
    pt::pickAudio([self](geode::Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        if (!popup) return;
        auto opt = std::move(result).unwrapOr(std::nullopt);
        if (!opt.has_value() || opt->empty()) return;
        auto* p = static_cast<QuickButtonSfxPopup*>(popup.data());
        p->importAudio(*opt);
    });
}

void QuickButtonSfxPopup::importAudio(std::filesystem::path const& src) {
    std::string stem = m_target->id.empty() ? m_target->name : m_target->id;
    std::string dest = importFileToConfigDir(
        src, quickHubSfxDir(), stem,
        [](std::filesystem::path const& srcPath, std::string const& destStr) {
            if (!isQuickHubAudioFile(srcPath)) {
                PaimonNotify::create("Usa mp3, ogg, wav o flac.", NotificationIcon::Warning)->show();
                return false;
            }
            unsigned int ms = 0;
            if (!probeQuickButtonSfxDuration(destStr, &ms) || ms == 0) {
                PaimonNotify::create("FMOD no pudo abrir ese audio.", NotificationIcon::Error)->show();
                return false;
            }
            return true;
        });
    if (dest.empty()) return;
    m_target->sfxPath = std::move(dest);
    changed();
}

} // namespace paimon::quickhub
