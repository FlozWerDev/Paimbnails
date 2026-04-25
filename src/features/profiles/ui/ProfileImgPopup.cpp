#include "ProfileImgPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Shaders.hpp"
#include "../../../blur/BlurSystem.hpp"
#include "../../../utils/PaimonDrawNode.hpp"

using namespace geode::prelude;
using namespace cocos2d;

ProfileImgPopup* ProfileImgPopup::create(int accountID, CCTexture2D* texture) {
    auto ret = new ProfileImgPopup();
    if (ret && ret->init(accountID, texture)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ProfileImgPopup::init(int accountID, CCTexture2D* texture) {
    // Mismo tamano que el popup de perfil de GD
    if (!Popup::init(440.f, 290.f)) return false;

    m_accountID = accountID;
    m_texture = texture;

    // Oculta el fondo del popup
    if (m_bgSprite) {
        m_bgSprite->setVisible(false);
    }

    // Oculta el boton de cerrar
    if (m_closeBtn) {
        m_closeBtn->setVisible(false);
    }

    // Deja pasar los toques al popup de abajo
    this->setTouchEnabled(false);

    auto winSize = m_mainLayer->getContentSize();

    // Recorte geometrico del fondo
    auto stencil = PaimonDrawNode::create();
    CCPoint rect[4] = { ccp(0,0), ccp(winSize.width,0), ccp(winSize.width,winSize.height), ccp(0,winSize.height) };
    ccColor4F white = {1,1,1,1};
    stencil->drawPolygon(rect, 4, white, 0, white);

    auto clip = CCClippingNode::create();
    clip->setStencil(stencil);
    clip->setContentSize(winSize);
    clip->setAnchorPoint(ccp(0.5f, 0.5f));
    clip->setPosition(ccp(winSize.width * 0.5f, winSize.height * 0.5f));

    // Crea el sprite de la imagen
    CCSprite* imgSprite = nullptr;

    // Lee el efecto guardado
    std::string effect = Mod::get()->getSavedValue<std::string>(
        "profileimg-effect-" + std::to_string(accountID), "none");
    float effectIntensity = static_cast<float>(Mod::get()->getSavedValue<double>(
        "profileimg-effect-intensity-" + std::to_string(accountID), 1.0));

    // Efecto blur
    if (effect == "blur") {
        imgSprite = BlurSystem::getInstance()->createBlurredSprite(
            texture, winSize, effectIntensity > 0.1f ? effectIntensity * 5.f : 3.f);
    }

    // Sprite sin efectos
    if (!imgSprite) {
        imgSprite = CCSprite::createWithTexture(texture);
    }

    if (!imgSprite) return true;

    // Escala la imagen para cubrir todo el popup
    float scaleX = winSize.width / imgSprite->getContentWidth();
    float scaleY = winSize.height / imgSprite->getContentHeight();
    float scale = std::max(scaleX, scaleY);
    imgSprite->setScale(scale);
    imgSprite->setAnchorPoint(ccp(0.5f, 0.5f));
    imgSprite->setPosition(ccp(winSize.width * 0.5f, winSize.height * 0.5f));

    // Aplica shader de efecto
    if (effect != "none" && effect != "blur") {
        CCGLProgram* shader = nullptr;
        if (effect == "grayscale") {
            shader = Shaders::getOrCreateShader("profileimg_grayscale"_spr,
                Shaders::vertexShaderCell, Shaders::fragmentShaderGrayscaleCell);
        } else if (effect == "sepia") {
            shader = Shaders::getOrCreateShader("profileimg_sepia"_spr,
                Shaders::vertexShaderCell, Shaders::fragmentShaderSepiaCell);
        } else if (effect == "vignette") {
            shader = Shaders::getOrCreateShader("profileimg_vignette"_spr,
                Shaders::vertexShaderCell, Shaders::fragmentShaderVignetteCell);
        } else if (effect == "pixelate") {
            shader = Shaders::getOrCreateShader("profileimg_pixelate"_spr,
                Shaders::vertexShaderCell, Shaders::fragmentShaderPixelateCell);
        } else if (effect == "posterize") {
            shader = Shaders::getOrCreateShader("profileimg_posterize"_spr,
                Shaders::vertexShaderCell, Shaders::fragmentShaderPosterizeCell);
        } else if (effect == "chromatic") {
            shader = Shaders::getOrCreateShader("profileimg_chromatic"_spr,
                Shaders::vertexShaderCell, Shaders::fragmentShaderChromaticCell);
        } else if (effect == "scanlines") {
            shader = Shaders::getOrCreateShader("profileimg_scanlines"_spr,
                Shaders::vertexShaderCell, Shaders::fragmentShaderScanlinesCell);
        } else if (effect == "invert") {
            shader = Shaders::getOrCreateShader("profileimg_invert"_spr,
                Shaders::vertexShaderCell, Shaders::fragmentShaderInvertCell);
        } else if (effect == "solarize") {
            shader = Shaders::getOrCreateShader("profileimg_solarize"_spr,
                Shaders::vertexShaderCell, Shaders::fragmentShaderSolarizeCell);
        }

        if (shader) {
            imgSprite->setShaderProgram(shader);
            shader->use();
            shader->setUniformsForBuiltins();

            GLint intensityLoc = shader->getUniformLocationForName("u_intensity");
            if (intensityLoc >= 0) shader->setUniformLocationWith1f(intensityLoc, effectIntensity);

            GLint texSizeLoc = shader->getUniformLocationForName("u_texSize");
            if (texSizeLoc >= 0) shader->setUniformLocationWith2f(texSizeLoc, winSize.width, winSize.height);
        }
    }

    clip->addChild(imgSprite);

    // Oscurece sutilmente para legibilidad
    auto darkOverlay = CCLayerColor::create(ccc4(0, 0, 0, 60));
    darkOverlay->setContentSize(winSize);
    darkOverlay->setPosition(ccp(0, 0));
    darkOverlay->setAnchorPoint(ccp(0, 0));
    clip->addChild(darkOverlay);

    m_imgClip = clip;

    // Agrega al fondo del popup
    m_mainLayer->addChild(clip, -1);

    paimon::markDynamicPopup(this);
    return true;
}

