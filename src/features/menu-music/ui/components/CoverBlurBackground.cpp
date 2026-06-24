#include "CoverBlurBackground.hpp"
#include "../../../../blur/BlurSystem.hpp"
#include "../../../../utils/SpriteHelper.hpp"

#include <Geode/loader/Loader.hpp>
#include <Geode/utils/cocos.hpp>
#include <fmt/format.h>

using namespace geode::prelude;

namespace paimon::menumusic {

CoverBlurBackground* CoverBlurBackground::create(CCSize const& size) {
    auto ret = new CoverBlurBackground();
    if (ret && ret->init(size)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool CoverBlurBackground::init(CCSize const& size) {
    if (!CCNode::init()) return false;
    m_size = size;
    this->setContentSize(size);
    this->setAnchorPoint({0.5f, 0.5f});

    // Sin overlay oscuro: el fondo es solo la imagen (borrosa). El
    // recorte redondeado lo hace el content clipper padre del popup.
    return true;
}

void CoverBlurBackground::setCoverFromPath(const std::string& absolutePath) {
    // Incrementar generacion para invalidar blur in-flight.
    m_generation++;
    auto gen = m_generation;
    m_lastPath = absolutePath;

    // Si no hay portada, limpiar sprite actual y listo.
    if (absolutePath.empty()) {
        if (m_currentBlur) {
            m_currentBlur->removeFromParent();
            m_currentBlur = nullptr;
        }
        return;
    }

    // Cargar textura source.
    auto* source = CCTextureCache::sharedTextureCache()->addImage(absolutePath.c_str(), false);
    if (!source) return;

    // Clave de cache estable usando el path del archivo.
    std::string key = fmt::format("menumusic_cover::{}", absolutePath);

    // Despachar blur async. La intensidad la sacamos de mod settings para
    // que el usuario pueda subirla o bajarla. Default = 6 (medio-alto) para
    // que el fondo del popup se sienta elegante sin emborronar demasiado.
    float intensity = static_cast<float>(
        Mod::get()->getSavedValue<double>("menuMusicBlurIntensity", 5.0));
    if (intensity <= 0.f) intensity = 6.f;

    auto callback = [this, gen](CCSprite* blurred) {
        if (!blurred) return;
        // Si mientras tanto se llamo a setCoverFromPath con otro path,
        // el gen ya no cuadra: descartar silenciosamente.
        if (gen != m_generation) return;
        applyBlurFromTexture(blurred->getTexture(), gen);
    };

    // Usamos priority para que el primer blur aparezca rapido al abrir
    // el popup, sin esperar a la cola de celdas del level list.
    BlurSystem::getInstance()->buildPaimonBlurPriority(
        source,
        m_size,
        intensity,
        std::move(key),
        callback
    );
}

void CoverBlurBackground::applyBlurFromTexture(CCTexture2D* tex, std::uint64_t generation) {
    if (generation != m_generation || !tex) return;

    auto newSprite = CCSprite::createWithTexture(tex);
    if (!newSprite) return;

    // Ajustar escala para que cubra todo el popup.
    const CCSize ts = newSprite->getContentSize();
    float sx = m_size.width / ts.width;
    float sy = m_size.height / ts.height;
    float scale = std::max(sx, sy);
    newSprite->setScale(scale);
    newSprite->setAnchorPoint({0.5f, 0.5f});
    newSprite->setPosition(m_size / 2);
    newSprite->setOpacity(0); // empezara invisible y hara fade-in

    // Anadimos el sprite como hijo directo. El recorte redondeado lo
    // hace el content clipper del popup (el padre), asi que aqui no
    // necesitamos nuestro propio CCClippingNode.
    this->addChild(newSprite, 0);

    // Crossfade: el nuevo entra, el viejo sale y se remueve al final.
    auto fadeIn = CCFadeTo::create(0.25f, 255);
    newSprite->runAction(fadeIn);

    if (m_currentBlur) {
        auto oldSprite = m_currentBlur;
        auto fadeOut = CCFadeTo::create(0.25f, 0);
        auto remove = CCCallFunc::create(oldSprite, callfunc_selector(CCNode::removeFromParent));
        oldSprite->runAction(CCSequence::create(fadeOut, remove, nullptr));
    }
    m_currentBlur = newSprite;
}

} // namespace paimon::menumusic
