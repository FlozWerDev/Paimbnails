#include <Geode/Geode.hpp>
#include <Geode/modify/DailyLevelNode.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../utils/Shaders.hpp"
#include "../blur/BlurSystem.hpp"
#include "../utils/SpriteHelper.hpp"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace geode::prelude;

class PaimonBlurSprite : public CCSprite {
public:
    float m_intensity = 1.0f;
    CCSize m_texSize;
    float m_timer = 0.0f;
    int m_state = 0; // 0: max blur, 1: baja a 0, 2: hold 0, 3: sube a max

    static PaimonBlurSprite* createWithTexture(CCTexture2D* texture) {
        auto sprite = new PaimonBlurSprite();
        if (sprite && sprite->initWithTexture(texture)) {
            sprite->autorelease();
            return sprite;
        }
        CC_SAFE_DELETE(sprite);
        return nullptr;
    }

    static float smootherstep(float t) {
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    void startLoop() {
        m_intensity = 0.4f;
        m_timer = 0.0f;
        m_state = 0;
        this->scheduleUpdate();
    }

    void update(float dt) override {
        m_timer += dt;
        constexpr float maxBlur = 0.4f;
        constexpr float rampDur = 1.5f;

        switch (m_state) {
            case 0: // hold blur max
                m_intensity = maxBlur;
                if (m_timer > 0.5f) { m_state = 1; m_timer = 0.0f; }
                break;
            case 1: { // ramp down
                float p = std::min(m_timer / rampDur, 1.0f);
                m_intensity = maxBlur * (1.0f - smootherstep(p));
                if (p >= 1.0f) { m_intensity = 0.0f; m_state = 2; m_timer = 0.0f; }
            } break;
            case 2: // hold no blur
                m_intensity = 0.0f;
                if (m_timer > 2.0f) { m_state = 3; m_timer = 0.0f; }
                break;
            case 3: { // ramp up
                float p = std::min(m_timer / rampDur, 1.0f);
                m_intensity = maxBlur * smootherstep(p);
                if (p >= 1.0f) { m_intensity = maxBlur; m_state = 0; m_timer = 0.0f; }
            } break;
            default: break;
        }
        CCSprite::update(dt);
    }

    void onExit() override {
        this->unscheduleUpdate();
        CCSprite::onExit();
    }

    void draw() override {
        if (auto* prog = getShaderProgram()) {
            prog->use();
            prog->setUniformsForBuiltins();
            prog->setUniformLocationWith1f(prog->getUniformLocationForName("u_intensity"), m_intensity);
            // Try u_texSize first (.glsl shader from BlurSystem), then u_screenSize (inline fallback)
            GLint sizeLoc = prog->getUniformLocationForName("u_texSize");
            if (sizeLoc == -1) sizeLoc = prog->getUniformLocationForName("u_screenSize");
            if (sizeLoc != -1) {
                prog->setUniformLocationWith2f(sizeLoc, m_texSize.width, m_texSize.height);
            }
        }
        CCSprite::draw();
    }
};

class $modify(PaimonDailyLevelNode, DailyLevelNode) {
    static void onModify(auto& self) {
        // Prioridad para que geode.node-ids este listo
        (void)self.setHookPriorityAfterPost("DailyLevelNode::init", "geode.node-ids");
    }

    struct Fields {
        Ref<CCSprite> m_paimonThumb = nullptr;
        Ref<CCClippingNode> m_paimonClipper = nullptr;
        Ref<geode::LoadingSpinner> m_loadingSpinner = nullptr;
        int m_levelID = 0;
    };

    $override
    bool init(GJGameLevel* level, DailyLevelPage* page, bool isTime) {
        if (!DailyLevelNode::init(level, page, isTime)) return false;

        if (!level) return true;
        m_fields->m_levelID = level->m_levelID;
        log::info("[DailyLevelNode] init: levelID={}", level->m_levelID.value());

        // Obtiene el tamaño del nodo
        CCSize nodeSize = this->getContentSize();

        CCNode* bg = this->getChildByID("background");
        if (!bg) {
             // Fallback a scale9sprite
             if (auto scale9 = this->getChildByType<CCScale9Sprite>(0)) {
                 bg = scale9;
             }
        }

        // Calcula tamaño y posicion del clipping
        CCSize clipSize;
        CCPoint clipPos;
        CCPoint clipAnchor = ccp(0.5f, 0.5f);
        float padding = 3.f;

        if (bg) {
            clipSize = bg->getScaledContentSize();
            clipPos  = bg->getPosition();
            clipAnchor = bg->getAnchorPoint();
        } else if (nodeSize.width >= 10.f) {
            clipSize = nodeSize;
            clipPos  = ccp(0.f, 0.f);
        } else {
            clipSize = CCSize(340.f, 230.f);
            clipPos  = ccp(0.f, 0.f);
        }

        // Resta padding para evitar bordes
        CCSize imgArea = CCSize(clipSize.width - padding * 2.f,
                                clipSize.height - padding * 2.f);

        // Crea clipping node con tamaño dinamico
        m_fields->m_paimonClipper = CCClippingNode::create();
        if (!m_fields->m_paimonClipper) return false;
        m_fields->m_paimonClipper->setContentSize(imgArea);
        m_fields->m_paimonClipper->setAnchorPoint(clipAnchor);
        m_fields->m_paimonClipper->setPosition(clipPos);
        m_fields->m_paimonClipper->setID("paimon-thumbnail-clipper"_spr);

        // Stencil redondeado para evitar conflictos con mods
        auto stencil = paimon::SpriteHelper::createRoundedRectStencil(imgArea.width, imgArea.height);
        m_fields->m_paimonClipper->setStencil(stencil);

        // Agrega el clipping con z=1
        this->addChild(m_fields->m_paimonClipper, 1);

        // Spinner de carga
        auto spinner = geode::LoadingSpinner::create(25.f);
        spinner->setPosition(imgArea / 2);
        m_fields->m_paimonClipper->addChild(spinner, 10);
        m_fields->m_loadingSpinner = spinner;

        // Solicita la miniatura
        int levelID = level->m_levelID;
        std::string fileName = fmt::format("{}.png", levelID);
        
        // Ref<> mantiene vivo el nodo
        log::info("[DailyLevelNode] requesting thumbnail: levelID={}", levelID);
        Ref<DailyLevelNode> self = this;
        ThumbnailLoader::get().requestLoad(levelID, fileName, [self, levelID](CCTexture2D* tex, bool success) {
            auto* fields = static_cast<PaimonDailyLevelNode*>(self.data())->m_fields.self();
            // Verifica si el nodo sigue activo
            if (!self->getParent() || !fields->m_paimonClipper) {
                return;
            }

            // Quita el spinner
            if (fields->m_loadingSpinner) {
                fields->m_loadingSpinner->removeFromParent();
                fields->m_loadingSpinner = nullptr;
            }

            if (success && tex && fields->m_paimonClipper) {
                log::info("[DailyLevelNode] thumbnail loaded OK: levelID={}", levelID);
                if (fields->m_paimonThumb) {
                    fields->m_paimonThumb->removeFromParent();
                }
                
                auto sprite = PaimonBlurSprite::createWithTexture(tex);
                sprite->m_texSize = tex->getContentSizeInPixels();
                fields->m_paimonThumb = sprite;

                // Usa shader cacheado
                if (auto* shader = BlurSystem::getInstance()->getRealtimeBlurShader()) {
                    sprite->setShaderProgram(shader);
                } else if (auto* shader = Shaders::getPaimonBlurShader()) {
                    sprite->setShaderProgram(shader);
                }

                // Ajusta al contenedor
                CCSize containerSize = fields->m_paimonClipper->getContentSize();
                float sx = containerSize.width / sprite->getContentWidth();
                float sy = containerSize.height / sprite->getContentHeight();
                float scale = std::max(sx, sy); // aspect fill: cubro todo el area

                sprite->setScale(scale);
                sprite->setPosition(containerSize / 2);
                
                // Animacion de entrada
                sprite->setOpacity(0);
                sprite->runAction(CCFadeIn::create(0.5f));
                
                // Inicia el loop de blur
                sprite->startLoop();

                fields->m_paimonClipper->addChild(sprite);
            }
        }, 0, false);

        return true;
    }
};
