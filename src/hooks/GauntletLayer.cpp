#include <Geode/Geode.hpp>
#include <Geode/modify/GauntletLayer.hpp>
#include <Geode/binding/GauntletLayer.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJMapPack.hpp>
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../utils/Debug.hpp"
#include "../utils/Shaders.hpp"
#include "../blur/BlurSystem.hpp"

using namespace geode::prelude;

static char const* g_vertexShader = R"(
    attribute vec4 a_position;
    attribute vec4 a_color;
    attribute vec2 a_texCoord;

    #ifdef GL_ES
    varying lowp vec4 v_fragmentColor;
    varying mediump vec2 v_texCoord;
    #else
    varying vec4 v_fragmentColor;
    varying vec2 v_texCoord;
    #endif

    void main()
    {
        gl_Position = CC_MVPMatrix * a_position;
        v_fragmentColor = a_color;
        v_texCoord = a_texCoord;
    }
)";

// Dual Kawase blur
static char const* g_fragmentShaderDualKawase = R"(
    #ifdef GL_ES
    precision highp float;
    #endif

    varying vec4 v_fragmentColor;
    varying vec2 v_texCoord;
    uniform sampler2D u_texture;
    uniform vec2 u_texSize;
    uniform float u_blurAmount;

    void main() {
        vec2 halfpixel = (u_blurAmount * 0.5) / u_texSize;
        vec2 offset = u_blurAmount / u_texSize;

        vec4 color = texture2D(u_texture, v_texCoord) * 4.0;

        color += texture2D(u_texture, v_texCoord - halfpixel);
        color += texture2D(u_texture, v_texCoord + halfpixel);
        color += texture2D(u_texture, v_texCoord + vec2(halfpixel.x, -halfpixel.y));
        color += texture2D(u_texture, v_texCoord - vec2(halfpixel.x, -halfpixel.y));

        color += texture2D(u_texture, v_texCoord + vec2(-offset.x, 0.0)) * 2.0;
        color += texture2D(u_texture, v_texCoord + vec2( offset.x, 0.0)) * 2.0;
        color += texture2D(u_texture, v_texCoord + vec2(0.0, -offset.y)) * 2.0;
        color += texture2D(u_texture, v_texCoord + vec2(0.0,  offset.y)) * 2.0;

        gl_FragColor = (color / 16.0) * v_fragmentColor;
    }
)";

class GauntletThumbnailNode : public CCNode {
    std::vector<int> m_levelIDs;
    std::vector<Ref<CCTexture2D>> m_loadedTextures;
    int m_currentIndex = 0;
    float m_timer = 0.f;
    
    // 2 sprites reusables
    CCSprite* m_sprites[2] = {nullptr, nullptr};
    int m_activeSpriteIndex = 0; // Sprite visible
    bool m_transitioning = false;
    float m_transitionTime = 0.f; // Timer de transicion
    bool m_loadingStarted = false;
    bool m_firstLoad = true;

public:
    using CCNode::init; // Silencia warning

    static GauntletThumbnailNode* create(std::vector<int> const& levelIDs) {
        auto node = new GauntletThumbnailNode();
        if (node && node->init(levelIDs)) {
            node->autorelease();
            return node;
        }
        CC_SAFE_DELETE(node);
        return nullptr;
    }

    bool init(std::vector<int> const& levelIDs) {
        if (!CCNode::init()) return false;
        
        m_levelIDs = levelIDs;
        m_currentIndex = 0;
        
        // Fondo negro
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto bg = CCLayerColor::create(ccc4(0, 0, 0, 200), winSize.width, winSize.height);
        this->addChild(bg, -1);
        
        this->setContentSize(winSize);
        this->setAnchorPoint({0.f, 0.f});
        this->setZOrder(-100); 

        // Crea 2 sprites
        m_sprites[0] = CCSprite::create();
        m_sprites[1] = CCSprite::create();
        
        // Los hace invisibles
        m_sprites[0]->setOpacity(0);
        m_sprites[1]->setOpacity(0);
        // IDs para mods
        m_sprites[0]->setID("paimon-bg-sprite-1"_spr);
        m_sprites[1]->setID("paimon-bg-sprite-2"_spr);

        this->addChild(m_sprites[0], 0);
        this->addChild(m_sprites[1], 0);

        this->schedule(schedule_selector(GauntletThumbnailNode::updateSlide), 1.0f / 60.f);
        
        // Carga thumbnails
        this->loadAllThumbnails();
        
        return true;
    }

    void loadAllThumbnails() {
        if (m_loadingStarted) return;
        m_loadingStarted = true;
        log::info("[GauntletLayer] loadAllThumbnails: {} levels", m_levelIDs.size());

        // Ref<> mantiene vivo el nodo
        Ref<GauntletThumbnailNode> self = this;
        for (int id : m_levelIDs) {
            ThumbnailLoader::get().requestLoad(id, "", [self](CCTexture2D* tex, bool success) {
                // precache
            }, 10, false);
        }
    }

    void updateSlide(float dt) {
        if (m_levelIDs.empty()) return;

        m_timer += dt;
        
        // Timer de transicion
        if (m_transitioning) {
            m_transitionTime += dt;
            if (m_transitionTime >= 0.6f) { // buffer fade
                onTransitionFinished();
            }
        }

        // Carga inicial
        if (m_firstLoad && m_timer > 0.1f) {
            showNextImage();
            m_timer = 0;
            m_firstLoad = false;
        }
        // Ciclo automatico
        else if (!m_firstLoad && m_timer > 3.0f && !m_transitioning) {
            m_currentIndex = (m_currentIndex + 1) % m_levelIDs.size();
            showNextImage();
            m_timer = 0;
        }
    }

    void showNextImage() {
        if (m_levelIDs.empty()) return;
        
        int id = m_levelIDs[m_currentIndex];
        
        // Busca otra si no esta cargada
        if (!ThumbnailLoader::get().isLoaded(id)) {
            bool found = false;
            for (size_t i = 0; i < m_levelIDs.size(); i++) {
                int checkIdx = (m_currentIndex + i) % m_levelIDs.size();
                if (ThumbnailLoader::get().isLoaded(m_levelIDs[checkIdx])) {
                    m_currentIndex = checkIdx;
                    id = m_levelIDs[m_currentIndex];
                    found = true;
                    break;
                }
            }
            if (!found) return;
        }

        // Ref<> evita use-after-free
        Ref<GauntletThumbnailNode> self = this;
        ThumbnailLoader::get().requestLoad(id, "", [self](CCTexture2D* tex, bool success) {
            if (success && tex && self->getParent()) {
                self->transitionTo(tex);
            }
        }, 11, false);
    }

    void transitionTo(CCTexture2D* tex) {
        if (m_transitioning) return;
        m_transitioning = true;

        // Siguiente sprite
        int nextIdx = 1 - m_activeSpriteIndex;
        CCSprite* nextSprite = m_sprites[nextIdx];
        CCSprite* currentSprite = m_sprites[m_activeSpriteIndex];

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        // Actualiza textura y shader
        nextSprite->setTexture(tex);
        nextSprite->setTextureRect(CCRect(0, 0, tex->getContentSize().width, tex->getContentSize().height));

        auto size = tex->getContentSizeInPixels();

        // Shader Dual Kawase
        if (auto* shader = BlurSystem::getInstance()->getRealtimeBlurShader()) {
            shader->use();
            shader->setUniformsForBuiltins();

            auto locTexSize = shader->getUniformLocationForName("u_texSize");
            shader->setUniformLocationWith2f(locTexSize, size.width, size.height);

            auto locIntensity = shader->getUniformLocationForName("u_intensity");
            shader->setUniformLocationWith1f(locIntensity, 0.5f);

            nextSprite->setShaderProgram(shader);
        } else if (auto* shader = Shaders::getOrCreateShader(
            "paimon-gauntlet-dual-kawase"_spr,
            g_vertexShader,
            g_fragmentShaderDualKawase
        )) {
            // Fallback: shader inline
            shader->use();
            shader->setUniformsForBuiltins();

            auto locTexSize = shader->getUniformLocationForName("u_texSize");
            shader->setUniformLocationWith2f(locTexSize, size.width, size.height);

            auto locBlurAmount = shader->getUniformLocationForName("u_blurAmount");
            shader->setUniformLocationWith1f(locBlurAmount, 3.5f);

            nextSprite->setShaderProgram(shader);
        }

        // Ajusta a pantalla
        float scaleX = winSize.width / nextSprite->getContentSize().width;
        float scaleY = winSize.height / nextSprite->getContentSize().height;
        float scale = std::max(scaleX, scaleY);
        
        nextSprite->setScale(scale);
        nextSprite->setPosition(winSize / 2);
        nextSprite->setColor({150, 150, 150});
        nextSprite->setOpacity(0);
        nextSprite->setZOrder(1); // Al frente

        if (currentSprite) {
            currentSprite->setZOrder(0); // Al fondo
        }

        // Acciones de animacion
        nextSprite->stopAllActions();
        nextSprite->runAction(CCFadeIn::create(0.5f));
        nextSprite->runAction(CCEaseSineOut::create(CCScaleTo::create(3.5f, scale * 1.05f)));

        if (currentSprite) {
            currentSprite->stopAllActions();
            currentSprite->runAction(CCFadeOut::create(0.5f));
        } else {
            // Primera vez listo
            onTransitionFinished();
        }
        
        // Reinicia timer
        m_transitionTime = 0.f;
        
        // Actualiza indice activo
        m_activeSpriteIndex = nextIdx;
    }

    void onTransitionFinished() {
        m_transitioning = false;
    }

    void onExit() override {
        this->unschedule(schedule_selector(GauntletThumbnailNode::updateSlide));
        CCNode::onExit();
    }
};

class $modify(PaimonGauntletLayer, GauntletLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityAfterPost("GauntletLayer::init", "geode.node-ids");
    }

    $override
    bool init(GauntletType type) {
        if (!GauntletLayer::init(type)) return false;
        log::info("[GauntletLayer] init: type={}", static_cast<int>(type));
        
        // Oculta fondo default
        if (auto bg = this->getChildByID("background")) {
            bg->setVisible(false);
        } else {
            // Fallback: primer hijo = fondo
            if (auto firstChild = this->getChildByType<CCNode>(0)) {
                 firstChild->setVisible(false);
            }
        }

        auto levelManager = GameLevelManager::sharedState();
        auto mapPack = levelManager->getSavedGauntlet(static_cast<int>(type));
        
        std::vector<int> ids;
        if (mapPack && mapPack->m_levels) {
            // Extrae IDs de m_levels
            for (auto* str : CCArrayExt<CCString*>(mapPack->m_levels)) {
                if (str) {
                    ids.push_back(str->intValue());
                }
            }
        }

        if (!ids.empty()) {
            auto bgNode = GauntletThumbnailNode::create(ids);
            if (bgNode) {
                bgNode->setID("paimon-gauntlet-background"_spr);
                this->addChild(bgNode, -100);
            }
        }
        
        return true;
    }
};
