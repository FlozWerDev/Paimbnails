#pragma once

// Level Requests como escena completa, con la piel del juego: ventanas
// GJ_square01, placas de nivel con su cara de dificultad, botones redondos y
// goldFont. Todo lo que hace falta para usarlo (plataforma, canal y comando)
// se configura aqui mismo.

#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace geode { class ScrollLayer; }

namespace paimon::twitch {

struct LevelRequest;

class TwitchRequestsLayer : public cocos2d::CCLayer {
public:
    static TwitchRequestsLayer* create();
    static cocos2d::CCScene* scene();
    static void open();

protected:
    ~TwitchRequestsLayer() override;

    bool init() override;
    void onEnterTransitionDidFinish() override;
    void keyBackClicked() override;
    void update(float dt) override;
    void scrollWheel(float x, float y) override;

private:
    void buildBackground();
    void buildHeader();
    void buildSidePanel();
    void buildQueuePanel();
    void buildFooter();

    // Entrada de un nodo desde un desplazamiento relativo. Antes de que la
    // transicion de escena acabe se guarda y arranca en runIntro(), asi no se
    // gasta la animacion detras del fundido.
    void enterBy(cocos2d::CCNode* node, cocos2d::CCPoint offset, float delay, bool bounce = false);
    void runIntro();

    void tick(float dt);
    void refreshStatus();
    void refreshPercents();
    void rebuildRows();
    void scheduleRebuild();
    cocos2d::CCNode* buildRow(
        size_t index, size_t position, LevelRequest const& request, float width);

    bool inputsDiffer() const;
    void applyInputs();
    void onPrimary();
    void onPlatform();
    void applyPlatformSkin();
    void onToggleQueue();
    void onToggleOrder();
    // Modo pagina: encender/apagar tu URL y compartirla.
    void onToggleWebPage();
    void onCopyWebUrl();
    void onOpenWebUrl();
    void onFilters();
    void onNotify();
    void onStreamOverlay();
    void onPlayNext();
    void playRequest(size_t index);
    void openMessage(size_t index);
    void openVideo(std::string url);
    void onClearQueue();
    void onSettings();
    void onBack();

    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCNode* m_statusDot = nullptr;
    cocos2d::CCLabelBMFont* m_queueLabel = nullptr;
    cocos2d::CCLabelBMFont* m_hintLabel = nullptr;
    cocos2d::CCLabelBMFont* m_channelCaption = nullptr;
    cocos2d::CCLabelBMFont* m_commandCaption = nullptr;
    // Solo en modo pagina: la direccion y sus dos botones ocupan el hueco del
    // campo de canal y el de comandos.
    cocos2d::CCLabelBMFont* m_webUrlLabel = nullptr;
    cocos2d::CCMenu* m_webMenu = nullptr;
    cocos2d::CCSprite* m_background = nullptr;
    cocos2d::CCSprite* m_titleIcon = nullptr;
    geode::TextInput* m_channelInput = nullptr;
    geode::TextInput* m_commandInput = nullptr;
    ButtonSprite* m_primarySprite = nullptr;
    ButtonSprite* m_platformSprite = nullptr;
    ButtonSprite* m_queueSprite = nullptr;
    ButtonSprite* m_orderSprite = nullptr;
    cocos2d::CCNode* m_rowsHost = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;

    float m_listWidth = 0.f;
    float m_listHeight = 0.f;
    std::string m_lastStatus;
    std::string m_lastQueueText;
    uint64_t m_lastQueueRevision = UINT64_MAX;
    uint64_t m_lastBriefRevision = UINT64_MAX;

    struct IntroStep {
        geode::Ref<cocos2d::CCNode> node;
        cocos2d::CCPoint target;
        float delay;
        bool bounce;
    };
    std::vector<IntroStep> m_intro;
    bool m_introDone = false;
    float m_introWait = 0.f;
    std::optional<bool> m_lastAccepting;
    std::optional<bool> m_lastRandomOrder;
    bool m_dotPulsing = false;

    float m_wheelTargetY = 0.f;
    bool m_wheelTargetSet = false;
    bool m_popupOnTop = false;
};

} // namespace paimon::twitch
