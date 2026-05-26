#include "PaimonGuideChatPopup.hpp"

#include "../services/PaimonGuideService.hpp"
#include "../../../utils/Localization.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

using namespace geode::prelude;

namespace paimon::guide {

namespace {

std::string tr(char const* key, char const* fallback = "") {
    auto v = Localization::get().getString(key);
    if (v == key && fallback && fallback[0] != '\0') return fallback;
    return v;
}

// Word-wrap manual para CCLabelBMFont: divide `text` en lineas que caben
// en `maxChars` caracteres aproximadamente, respetando palabras.
std::string wrapText(std::string const& text, std::size_t maxChars) {
    std::string out;
    std::size_t lineLen = 0;
    std::string word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        if (lineLen + word.size() + (lineLen > 0 ? 1 : 0) > maxChars && lineLen > 0) {
            out.push_back('\n');
            lineLen = 0;
        }
        if (lineLen > 0) {
            out.push_back(' ');
            ++lineLen;
        }
        out += word;
        lineLen += word.size();
        word.clear();
    };
    for (char c : text) {
        if (c == '\n') {
            flushWord();
            out.push_back('\n');
            lineLen = 0;
        } else if (c == ' ' || c == '\t') {
            flushWord();
        } else {
            word.push_back(c);
        }
    }
    flushWord();
    return out;
}

// Strip de tags GD (<cy>, <cg>, <c_>, </c>, etc) del texto. CCLabelBMFont
// no soporta esos tags — solo FLAlertLayer/MDTextArea — asi que aparecen
// literalmente. Como las respuestas vienen formateadas para alertLayer,
// removemos tags antes del typewriter para que el texto se vea limpio.
//
// Reglas:
//   - <cX>  donde X = letra (color tag) -> remove
//   - <c_>  (rojo brillante) -> remove
//   - </c>  (cierre)         -> remove
//   - Cualquier otro `<...>` se conserva (por si hay caracteres < literales).
std::string stripGDColorTags(std::string const& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ) {
        if (in[i] == '<' && i + 2 < in.size()) {
            // Caso </c>
            if (in[i + 1] == '/' && in[i + 2] == 'c' && i + 3 < in.size() && in[i + 3] == '>') {
                i += 4;
                continue;
            }
            // Caso <cX> con X = letra o '_'
            if (in[i + 1] == 'c' && i + 3 < in.size() && in[i + 3] == '>') {
                char x = in[i + 2];
                bool isColor = (x >= 'a' && x <= 'z') || (x >= 'A' && x <= 'Z') || x == '_';
                if (isColor) {
                    i += 4;
                    continue;
                }
            }
        }
        out.push_back(in[i]);
        ++i;
    }
    return out;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construccion
// ─────────────────────────────────────────────────────────────────────────────

PaimonGuideChatPopup* PaimonGuideChatPopup::create() {
    auto ret = new PaimonGuideChatPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool PaimonGuideChatPopup::init() {
    if (!Popup::init(380.f, 240.f)) return false;

    auto title = tr("pai.guide.title", "Paimon Guide");
    this->setTitle(title.c_str());

    auto layerSize = m_mainLayer->getContentSize();

    // ─────────────────────────────────────────────────────────────────────
    // Layout (380x240). Coordenadas en m_mainLayer:
    //
    //   Title (auto)                                          ~y=210
    //   ┌─ responseBg (250x90) ─────────────────────────┐    y=110-200
    //   │  responseLabel (chatFont, scale 0.5)           │
    //   └────────────────────────────────────────────────┘
    //                                       takeMeBtn      y=88
    //   [input 195px            ] [send]                   y=52
    //   [chip] [chip] [chip] [chip] [chip] [chip]          y=22
    //
    //   Paimon en (50, 130) escala 0.55 (a la izquierda del responseBg)
    // ─────────────────────────────────────────────────────────────────────

    // ─── Paimon a la izquierda ────────────────────────────────────────────
    m_paimon = AnimatedPaimon::create(0.55f);
    if (m_paimon) {
        m_paimon->setLively(true);
        m_paimon->setAnchorPoint({0.5f, 0.5f});
        m_paimon->setPosition({50.f, 130.f});
        m_mainLayer->addChild(m_paimon, 5);
        m_paimon->play(AnimatedPaimon::Animation::Wave);
    }

    // ─── Boton "agent:on/off" debajo de Paimon ────────────────────────────
    // [REMOVIDO] Modo Agente eliminado: Paimon solo responde preguntas, no
    // ejecuta acciones. El boton, sus animaciones de toggle y todo el stack
    // AgentDSL/AgentExecutor/AgentPilot/ClickInvoker/etc. fueron borrados.

    // ─── Panel del mensaje (parte superior derecha) ───────────────────────
    constexpr float kBgW = 250.f;
    constexpr float kBgH = 90.f;
    m_responseBg = CCScale9Sprite::create("GJ_square01.png");
    m_responseBg->setColor({30, 35, 50});
    m_responseBg->setOpacity(220);
    m_responseBg->setContentSize({kBgW, kBgH});
    m_responseBg->setAnchorPoint({0.f, 0.5f});
    m_responseBg->setPosition({110.f, 155.f});
    m_mainLayer->addChild(m_responseBg, 4);

    m_responseLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_responseLabel->setScale(0.50f);
    m_responseLabel->setAnchorPoint({0.f, 1.f});
    m_responseLabel->setAlignment(kCCTextAlignmentLeft);
    m_responseLabel->setPosition({10.f, kBgH - 8.f});
    m_responseBg->addChild(m_responseLabel);

    // ─── Fila inferior: input + boton Preguntar ───────────────────────────
    // Para evitar solapes: input 195px en x=88..283, sendBtn ~340.
    constexpr float kInputW = 195.f;
    constexpr float kInputY = 52.f;

    m_input = AnimatedTextInput::create(kInputW,
        tr("pai.guide.placeholder", "Ask me anything..."));
    if (m_input) {
        m_input->setAnchorPoint({0.f, 0.5f});
        m_input->setPosition({88.f, kInputY});
        m_mainLayer->addChild(m_input, 5);
    }

    auto sendSpr = ButtonSprite::create(
        tr("pai.guide.send", "Ask").c_str(),
        "goldFont.fnt", "GJ_button_01.png", 0.8f
    );
    sendSpr->setScale(0.55f);
    auto sendBtn = CCMenuItemSpriteExtra::create(
        sendSpr, this, menu_selector(PaimonGuideChatPopup::onSubmitButton)
    );
    sendBtn->setID("guide-send-btn"_spr);

    auto sendMenu = CCMenu::create();
    sendMenu->setContentSize({60.f, 40.f});
    // sendMenu se posiciona a la derecha del input con margen para que
    // no choque con el. input termina en x=88+195=283; ponemos sendBtn en x=325.
    sendMenu->setPosition({325.f, kInputY});
    sendMenu->addChild(sendBtn);
    sendBtn->setPosition({0.f, 0.f});
    m_mainLayer->addChild(sendMenu, 5);

    // ─── Boton "Llevame ahi" (oculto por defecto) ─────────────────────────
    auto takeMeSpr = ButtonSprite::create(
        tr("pai.guide.take.me.there", "Take me there").c_str(),
        "bigFont.fnt", "GJ_button_05.png", 0.8f
    );
    takeMeSpr->setScale(0.45f);
    m_takeMeBtn = CCMenuItemSpriteExtra::create(
        takeMeSpr, this, menu_selector(PaimonGuideChatPopup::onTakeMeThere)
    );
    m_takeMeBtn->setID("guide-take-me-btn"_spr);
    m_takeMeBtn->setVisible(false);

    m_takeMeMenu = CCMenu::create();
    m_takeMeMenu->setContentSize({150.f, 22.f});
    // Posicion: centrado debajo del responseBg, encima del input. y = 88
    // (responseBg termina abajo en 155-45=110, asi que 88 deja un gap).
    m_takeMeMenu->setPosition({235.f, 95.f});
    m_takeMeMenu->addChild(m_takeMeBtn);
    m_takeMeBtn->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_takeMeMenu, 5);

    // ─── Sugerencias (chips clickables) ───────────────────────────────────
    m_suggestionsMenu = CCMenu::create();
    m_suggestionsMenu->setID("guide-suggestions"_spr);
    m_suggestionsMenu->setContentSize({layerSize.width - 30.f, 22.f});
    m_suggestionsMenu->setAnchorPoint({0.5f, 0.5f});
    m_suggestionsMenu->ignoreAnchorPointForPosition(false);
    m_suggestionsMenu->setPosition({layerSize.width * 0.5f, 22.f});
    m_suggestionsMenu->setLayout(
        RowLayout::create()
            ->setGap(5.f)
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(false)
    );

    auto suggestions = PaimonGuideService::get().getSuggestions();
    for (auto const& [chipText, query] : suggestions) {
        auto* chipSpr = ButtonSprite::create(
            chipText.c_str(), "bigFont.fnt", "GJ_button_05.png", 0.6f
        );
        chipSpr->setScale(0.42f);
        auto* chipBtn = CCMenuItemSpriteExtra::create(
            chipSpr, this, menu_selector(PaimonGuideChatPopup::onSuggestionChip)
        );
        chipBtn->setUserObject(CCString::create(query.c_str()));
        chipBtn->setID(("flozwer.paimbnails2/guide-chip-" + chipText));
        m_suggestionsMenu->addChild(chipBtn);
    }
    m_suggestionsMenu->updateLayout();
    m_mainLayer->addChild(m_suggestionsMenu, 5);

    // ─── Mensaje inicial de bienvenida (con typewriter) ───────────────────
    // Si la memoria conversacional tiene historial reciente, saludamos
    // diferente — "como ibamos diciendo..." en lugar del welcome formal.
    auto& mem = PaimonGuideService::get().memory();
    std::string welcome;
    bool isReturning = false;
    if (mem.size() > 0) {
        if (auto last = mem.lastFunctionalTurn();
            last && (std::time(nullptr) - last->timestamp) < 120)
        {
            isReturning = true;
            auto langId = Localization::get().getCurrentLanguageId();
            welcome = (langId == "spanish")
                ? "Hola otra vez! En que mas te ayudo?"
                : "Hello again! What else can I help with?";
        }
    }
    if (welcome.empty()) {
        welcome = tr("pai.guide.welcome",
            "Hi! I'm Paimon, your guide. Ask me where to configure things!");
    }
    displayMessage(welcome);
    (void)isReturning;

    // ─── Animacion de entrada del Paimon ──────────────────────────────────
    // Paimon entra desde la izquierda fuera del popup con un bounce, mientras
    // el responseBg hace fade-in para acompanar.
    if (m_paimon) {
        auto finalPos = m_paimon->getPosition();
        m_paimon->setPosition({finalPos.x - 80.f, finalPos.y});
        m_paimon->runAction(
            CCEaseBackOut::create(
                CCMoveTo::create(0.45f, finalPos)
            )
        );
    }
    if (m_responseBg) {
        m_responseBg->setOpacity(0);
        m_responseBg->runAction(CCFadeTo::create(0.35f, 220));
    }

    // IDs estables
    this->setID("paimon-guide-chat-popup"_spr);
    if (m_responseBg)    m_responseBg->setID("guide-response-bg"_spr);
    if (m_responseLabel) m_responseLabel->setID("guide-response-label"_spr);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Typewriter
// ─────────────────────────────────────────────────────────────────────────────

void PaimonGuideChatPopup::displayMessage(std::string const& message) {
    if (!m_responseLabel) return;

    this->unschedule(schedule_selector(PaimonGuideChatPopup::onTypewriterTick));

    // Strip de tags GD (<cy>...</c>) porque CCLabelBMFont no los procesa
    // y aparecen literales. Luego wrap a ~36 chars/linea para el ancho 250.
    auto cleaned = stripGDColorTags(message);
    m_pendingMessage = wrapText(cleaned, 36);
    m_typewriterIndex = 0;
    m_responseLabel->setString("");

    this->schedule(schedule_selector(PaimonGuideChatPopup::onTypewriterTick), 0.04f);

    if (m_paimon) m_paimon->play(AnimatedPaimon::Animation::Talk);
}

void PaimonGuideChatPopup::onTypewriterTick(float /*dt*/) {
    if (!m_responseLabel) return;

    if (m_typewriterIndex >= m_pendingMessage.size()) {
        this->unschedule(schedule_selector(PaimonGuideChatPopup::onTypewriterTick));
        return;
    }

    std::size_t advance = 2;
    std::size_t newIdx = std::min(m_typewriterIndex + advance, m_pendingMessage.size());

    auto partial = m_pendingMessage.substr(0, newIdx);
    m_responseLabel->setString(partial.c_str());
    m_typewriterIndex = newIdx;
}

// ─────────────────────────────────────────────────────────────────────────────
// API publica
// ─────────────────────────────────────────────────────────────────────────────

void PaimonGuideChatPopup::submitQuery(std::string const& query) {
    if (m_input) m_input->setString(query);
    onSubmitButton(nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Callbacks
// ─────────────────────────────────────────────────────────────────────────────

void PaimonGuideChatPopup::onSubmitButton(cocos2d::CCObject* /*sender*/) {
    if (!m_input) return;
    auto query = m_input->getString();
    if (query.empty()) return;

    m_input->playSendSweep();
    m_input->clear();

    // ── MODO GUIDE NORMAL ────────────────────────────────────────────────
    // (Modo Agente eliminado: Paimon solo responde, no ejecuta acciones.)
    auto answer = PaimonGuideService::get().ask(query);
    displayMessage(answer.message);

    if (m_paimon) {
        switch (answer.animation) {
            case GuideAnimation::Talk:     m_paimon->play(AnimatedPaimon::Animation::Talk); break;
            case GuideAnimation::Surprise: m_paimon->play(AnimatedPaimon::Animation::Surprise); break;
            case GuideAnimation::Wave:     m_paimon->play(AnimatedPaimon::Animation::Wave); break;
            case GuideAnimation::Sleep:    m_paimon->play(AnimatedPaimon::Animation::Sleep); break;
            case GuideAnimation::Point:    m_paimon->play(AnimatedPaimon::Animation::Point); break;
        }
    }

    m_pendingAction = answer.action;
    if (m_takeMeBtn) {
        bool hasAction = static_cast<bool>(m_pendingAction);
        m_takeMeBtn->setVisible(hasAction);

        if (hasAction) {
            m_takeMeBtn->stopAllActions();
            m_takeMeBtn->setScale(0.f);
            m_takeMeBtn->runAction(
                CCEaseElasticOut::create(CCScaleTo::create(0.45f, 0.45f), 0.5f)
            );
            if (m_paimon && m_takeMeBtn) {
                m_paimon->pointAt(m_takeMeBtn, 0.5f);
            }
        }
    }
}

void PaimonGuideChatPopup::onTakeMeThere(cocos2d::CCObject* /*sender*/) {
    if (!m_pendingAction) return;

    // Capturar la accion y el self ANTES de cerrar el popup. Las acciones
    // que abren otro layer normalmente lo hacen sobre la escena actual,
    // no sobre el popup destruido, asi que no necesitamos pasar self vivo.
    auto action = m_pendingAction;
    m_pendingAction = nullptr;

    // Encolar la accion despues de cerrar el popup. Como `this` quedara
    // destruido tras onClose, NO capturamos self en la lambda — la accion
    // recibe nullptr para indicar "popup ya cerrado".
    this->onClose(nullptr);
    Loader::get()->queueInMainThread([action]() {
        if (action) action(nullptr);
    });
}

void PaimonGuideChatPopup::onSuggestionChip(cocos2d::CCObject* sender) {
    auto* btn = typeinfo_cast<CCNode*>(sender);
    if (!btn) return;

    // La query asociada se guardo como CCString en setUserObject del chip.
    auto* obj = btn->getUserObject();
    if (auto* str = typeinfo_cast<CCString*>(obj)) {
        std::string query = str->getCString();
        submitQuery(query);
    }
}

} // namespace paimon::guide
