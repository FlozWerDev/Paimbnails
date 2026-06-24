#include "MenuMusicLibraryPopup.hpp"
#include "MenuMusicAddPopup.hpp"

#include "../services/MenuMusicLibrary.hpp"
#include "../services/MenuMusicPlayer.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/DominantColors.hpp"
#include "../../../utils/stb_image.h"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

namespace paimon::menumusic {

// Layout constants
//
// Popup 15% mas chico que la version anterior (460x310 → 390x264).
// Cards con altura 56 y gap 8 se mantienen para que el grid siga
// legible, solo cambia la ventana contenedora.

static constexpr float kPopupWidth  = 390.f;
static constexpr float kPopupHeight = 264.f;

static constexpr float kCardHeight = 56.f;
static constexpr float kCardGap    = 8.f;
static constexpr float kCardRadius = 8.f;   // radio de los bordes de la card
static constexpr float kThumbRadius = 6.f;  // radio del recorte del thumbnail

// Color base de la card (el fondo oscuro semi-transparente).
static constexpr cocos2d::ccColor4B kCardBaseColor = {24, 26, 38, 210};

// Color fallback cuando la extraccion de dominante falla: un morado
// suave que combina bien con el tema oscuro. Se usa para que TODAS
// las cards tengan gradient, no solo las que tienen portada.
static constexpr cocos2d::ccColor3B kFallbackDominantColor = {96, 84, 164};

// Dominant color cache
//
// Extraer colores dominantes involucra decodificar la imagen y correr
// k-means. Es rapido pero no gratis, asi que cacheamos por path.
// Guardamos la cache en un map statico; se limpia naturalmente cuando
// se cierra el popup sin preocuparnos: la libreria Paimbnails no tiene
// miles de tracks.
//
// El negro puro (0,0,0) se usa como sentinela de "no hay cover".

namespace {
    struct CachedColor {
        bool ok = false;
        cocos2d::ccColor3B color{};
    };

    static std::unordered_map<std::string, CachedColor>& colorCache() {
        static std::unordered_map<std::string, CachedColor> cache;
        return cache;
    }

    // Lee un archivo entero en memoria. Devuelve un vector vacio si falla.
    static std::vector<uint8_t> readBinary(const std::string& path) {
        auto res = geode::utils::file::readBinary(path);
        if (!res) return {};
        return res.unwrap();
    }

    // Extrae el color dominante de una imagen (JPEG/PNG/WebP) en disco.
    // Hace una decodificacion reducida (solo 64x64) para acelerar el
    // k-means y no gastar CPU al abrir la library.
    static CachedColor extractDominant(const std::string& coverPath) {
        auto it = colorCache().find(coverPath);
        if (it != colorCache().end()) return it->second;

        CachedColor out;
        auto bytes = readBinary(coverPath);
        if (bytes.empty()) {
            colorCache().emplace(coverPath, out);
            return out;
        }

        int w = 0, h = 0, ch = 0;
        unsigned char* px = stbi_load_from_memory(
            bytes.data(), static_cast<int>(bytes.size()), &w, &h, &ch, 3);
        if (!px || w <= 0 || h <= 0) {
            if (px) stbi_image_free(px);
            colorCache().emplace(coverPath, out);
            return out;
        }

        // Submuestreo a ~64x64 manteniendo aspect ratio: el dominant
        // color solo necesita una impresion general del contenido, no
        // precision pixel-perfect. Downsample box-filter simple.
        constexpr int targetDim = 64;
        int dsW = w, dsH = h;
        if (w > targetDim || h > targetDim) {
            const float ratio = std::max(
                static_cast<float>(w) / targetDim,
                static_cast<float>(h) / targetDim);
            dsW = std::max(1, static_cast<int>(w / ratio));
            dsH = std::max(1, static_cast<int>(h / ratio));
        }

        std::vector<uint8_t> ds;
        const uint8_t* rgbSrc = px;
        if (dsW != w || dsH != h) {
            ds.resize(static_cast<size_t>(dsW) * dsH * 3);
            for (int y = 0; y < dsH; ++y) {
                const int srcY = std::min(h - 1, y * h / dsH);
                for (int x = 0; x < dsW; ++x) {
                    const int srcX = std::min(w - 1, x * w / dsW);
                    const uint8_t* s = px + (srcY * w + srcX) * 3;
                    uint8_t* d = ds.data() + (y * dsW + x) * 3;
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
                }
            }
            rgbSrc = ds.data();
        }

        auto pair = DominantColors::extract(rgbSrc, dsW, dsH);
        stbi_image_free(px);

        out.ok = true;
        out.color = {pair.first.r, pair.first.g, pair.first.b};
        colorCache().emplace(coverPath, out);
        return out;
    }

    // Construye un gradient horizontal izquierda -> derecha dibujando
    // N cuadrilateros adyacentes con colores interpolados.
    //
    // Evitar artefactos:
    //   * Bordes coincidentes: usamos la MISMA x para el borde derecho
    //     de la strip i y el borde izquierdo de la i+1, asi no queda
    //     ni un fragmento vacio ni uno solapado. CCDrawNode rasteriza
    //     los poligonos con barycentrics que convergen al mismo pixel.
    //   * "Banding" rosado por sumar alphas: si solapamos polygons
    //     semi-transparentes, el overlap suma opacidad y se ve como
    //     una linea brillante. Por eso NO solapamos.
    //   * `TRIANGLE_FAN`-like artefacts: CCDrawNode triangula los
    //     poligonos por earcut; con cuadrilateros CW bien formados
    //     (4 puntos en sentido horario) no hay degenerados.
    //
    // Devuelve el node ya con `contentSize` seteado.
    static PaimonDrawNode* buildHorizontalGradient(
        float width, float height,
        cocos2d::ccColor4F from,
        cocos2d::ccColor4F to,
        float fadePivot = 0.5f
    ) {
        auto node = PaimonDrawNode::create();
        if (!node) return nullptr;

        // 48 tiras da un gradient fluido sin saturar el rasterizer.
        const int strips = 48;

        for (int i = 0; i < strips; ++i) {
            // x calculada a partir de `i / strips * width` — usando la
            // MISMA expresion en ambos lados (fin de strip i == inicio
            // de strip i+1) garantiza que no hay hueco ni solape.
            const float x0 = static_cast<float>(i)     / strips * width;
            const float x1 = static_cast<float>(i + 1) / strips * width;
            const float tm = (x0 + x1) * 0.5f / width;

            // Remapeamos contra el pivot: antes del pivot interpolamos,
            // despues nos quedamos en `to`. smoothstep para que la curva
            // no sea lineal.
            const float clamped = fadePivot > 0.f
                ? std::clamp(tm / fadePivot, 0.f, 1.f)
                : 1.f;
            const float s = clamped * clamped * (3.f - 2.f * clamped);

            cocos2d::ccColor4F c {
                from.r + (to.r - from.r) * s,
                from.g + (to.g - from.g) * s,
                from.b + (to.b - from.b) * s,
                from.a + (to.a - from.a) * s,
            };

            // 4 puntos en sentido antihorario (cocos convention).
            cocos2d::CCPoint rect[4] = {
                {x0, 0},
                {x1, 0},
                {x1, height},
                {x0, height},
            };
            node->drawPolygon(rect, 4, c, 0.f, ccc4f(0, 0, 0, 0));
        }

        node->setContentSize({width, height});
        return node;
    }

    // Normaliza un color dominante para que quede bien como gradient:
    //   * Si esta MUY cerca de negro, lo desplazamos hacia el fallback
    //     (morado) porque sobre el fondo oscuro de la card no se veria.
    //   * Si esta muy cerca de blanco, lo bajamos 20% para que no
    //     brille en exceso y quede un sutil off-white.
    //   * Tambien garantizamos un minimo de "color" (no gris) forzando
    //     al menos 40 unidades de diferencia entre max y min channel.
    static cocos2d::ccColor3B normalizeDominant(cocos2d::ccColor3B in) {
        const int r = in.r, g = in.g, b = in.b;
        const int maxC = std::max({r, g, b});
        const int minC = std::min({r, g, b});
        const int luma = (299 * r + 587 * g + 114 * b) / 1000;

        // Casi negro o casi gris sin saturacion: usamos el fallback.
        if (luma < 24 || (maxC - minC) < 12) {
            return kFallbackDominantColor;
        }
        // Casi blanco: lo oscurecemos un poco.
        if (luma > 225) {
            return cocos2d::ccColor3B{
                static_cast<uint8_t>(r * 4 / 5),
                static_cast<uint8_t>(g * 4 / 5),
                static_cast<uint8_t>(b * 4 / 5)
            };
        }
        return in;
    }
}

// Popup lifecycle

MenuMusicLibraryPopup* MenuMusicLibraryPopup::create() {
    auto ret = new MenuMusicLibraryPopup();
    if (ret && ret->init(kPopupWidth, kPopupHeight)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool MenuMusicLibraryPopup::init(float width, float height) {
    if (!Popup::init(width, height)) return false;
    paimon::markDynamicPopup(this);
    this->setTitle("Library");

    MenuMusicLibrary::get().load();

    buildHeader();
    buildList();
    rebuildList();

    m_libListenerToken = MenuMusicLibrary::get().addListener([this]() {
        this->rebuildList();
    });

    return true;
}

void MenuMusicLibraryPopup::onExit() {
    if (m_libListenerToken) {
        MenuMusicLibrary::get().removeListener(m_libListenerToken);
        m_libListenerToken = 0;
    }
    if (m_searchBar) {
        m_searchBar->setCallback(nullptr);
    }
    Popup::onExit();
}

// Build: header (search + Add Music)

void MenuMusicLibraryPopup::buildHeader() {
    auto size = m_mainLayer->getContentSize();

    // El titulo "Library" que setea Popup::setTitle queda arriba del
    // todo (~92% del alto). Bajamos el input 2 filas mas abajo para
    // que no se monte sobre el titulo y quede una banda de header
    // limpia: [titulo] / [search | Add].
    const float headerRowY = size.height - 42.f;

    m_searchBar = TextInput::create(size.width * 0.58f, "Search tracks...");
    if (m_searchBar) {
        m_searchBar->setPosition({size.width * 0.36f, headerRowY});
        m_searchBar->setCallback([this](const std::string& s) {
            this->onSearchChanged(s);
        });
        m_searchBar->setID("search-input"_spr);
        m_mainLayer->addChild(m_searchBar, 2);
    }

    auto addSpr = ButtonSprite::create("Add", 50, true, "bigFont.fnt", "GJ_button_05.png", 20.f, 0.55f);
    if (addSpr) {
        auto btn = CCMenuItemSpriteExtra::create(addSpr, this,
            menu_selector(MenuMusicLibraryPopup::onAddMusic));
        auto menu = CCMenu::create();
        menu->setPosition({size.width * 0.85f, headerRowY});
        menu->addChild(btn);
        menu->setID("add-menu"_spr);
        m_mainLayer->addChild(menu, 2);
    }
}

// Build: scroll list container

void MenuMusicLibraryPopup::buildList() {
    auto size = m_mainLayer->getContentSize();
    // Alto del scroll: todo entre la fila del header y el borde
    // inferior con algo de respiro. El header ocupa ~55px arriba.
    const float scrollH = size.height - 70.f;
    m_scroll = ScrollLayer::create({size.width - 26.f, scrollH});
    if (!m_scroll) return;
    m_scroll->setPosition({13.f, 12.f});
    m_scroll->m_contentLayer->setID("library-scroll-content"_spr);
    m_mainLayer->addChild(m_scroll, 2);
}

// Filtrado

static bool matchesQuery(const MusicTrack& t, const std::string& qLower) {
    if (qLower.empty()) return true;
    auto nameLower = geode::utils::string::toLower(t.displayName);
    if (nameLower.find(qLower) != std::string::npos) return true;
    auto artistLower = geode::utils::string::toLower(t.artist);
    if (artistLower.find(qLower) != std::string::npos) return true;
    return false;
}

void MenuMusicLibraryPopup::rebuildList() {
    if (!m_scroll) return;
    m_scroll->m_contentLayer->removeAllChildren();

    auto& lib = MenuMusicLibrary::get();
    auto& tracks = lib.tracks();

    // Ordenar alfabeticamente por nombre para estabilidad.
    std::vector<std::string> orderedIds;
    orderedIds.reserve(tracks.size());
    for (const auto& t : tracks) orderedIds.push_back(t.id);
    std::sort(orderedIds.begin(), orderedIds.end(), [&](const auto& a, const auto& b) {
        auto* ta = lib.findTrack(a);
        auto* tb = lib.findTrack(b);
        if (!ta || !tb) return false;
        return geode::utils::string::toLower(ta->displayName) <
               geode::utils::string::toLower(tb->displayName);
    });

    auto qLower = geode::utils::string::toLower(m_query);

    // Construir de arriba hacia abajo: la card mas reciente arriba.
    // El ScrollLayer asume que los hijos crecen en Y positivo, asi que
    // acumulamos primero la altura total y colocamos con (totalH - y).
    std::vector<std::string> visibleIds;
    for (const auto& tid : orderedIds) {
        auto* track = lib.findTrack(tid);
        if (!track) continue;
        if (!matchesQuery(*track, qLower)) continue;
        visibleIds.push_back(tid);
    }

    const float cardW = m_scroll->getContentSize().width - 4.f;
    float totalH = static_cast<float>(visibleIds.size()) * (kCardHeight + kCardGap);
    if (totalH > 0.f) totalH -= kCardGap;  // ultimo sin gap extra
    totalH = std::max(totalH, m_scroll->getContentSize().height);

    if (visibleIds.empty()) {
        auto label = CCLabelBMFont::create(
            tracks.empty() ? "Library empty — tap 'Add' to import or download a song."
                           : "No tracks match your search.",
            "chatFont.fnt");
        if (label) {
            label->setScale(0.5f);
            label->setPosition({m_scroll->getContentSize().width / 2.f,
                                m_scroll->getContentSize().height / 2.f});
            label->setColor({200, 200, 220});
            m_scroll->m_contentLayer->addChild(label);
        }
    } else {
        float y = totalH - kCardHeight;
        for (const auto& tid : visibleIds) {
            auto card = buildTrackCard(tid, cardW);
            if (!card) continue;
            card->setPosition({2.f, y});
            m_scroll->m_contentLayer->addChild(card);
            y -= (kCardHeight + kCardGap);
        }
    }

    m_scroll->m_contentLayer->setContentHeight(totalH);
    m_scroll->scrollToTop();
}

// Construccion de un tile

cocos2d::CCNode* MenuMusicLibraryPopup::buildTrackCard(const std::string& trackId, float widthOverride) {
    auto& lib = MenuMusicLibrary::get();
    auto* track = lib.findTrack(trackId);
    if (!track) return nullptr;

    auto node = CCNode::create();
    node->setContentSize({widthOverride, kCardHeight});
    node->setAnchorPoint({0, 0});
    node->setID(fmt::format("track-card-{}", trackId).c_str());

    // 1. Rounded card background
    //
    // Usamos PaimonDrawNode via SpriteHelper::createRoundedRect para
    // dibujar la base de la card con esquinas redondeadas. El color
    // base es oscuro semi-transparente; el gradient se dibuja encima.
    if (auto bg = paimon::SpriteHelper::createRoundedRect(
            widthOverride, kCardHeight, kCardRadius,
            ccc4FFromccc4B(kCardBaseColor))) {
        bg->setAnchorPoint({0.f, 0.f});
        bg->setPosition({0.f, 0.f});
        bg->setID("card-bg"_spr);
        node->addChild(bg, 0);
    }

    // 2. Gradient dominante (left -> transparent)
    //
    // TODAS las cards tienen gradient, no solo las que tienen portada.
    // Si no pudimos extraer el dominante, usamos el color de fallback.
    // El gradient se recorta dentro de un CCClippingNode con stencil
    // redondeado asi nunca desborda las esquinas de la card.
    cocos2d::ccColor3B dominant = kFallbackDominantColor;
    if (!track->coverPath.empty()) {
        auto cc = extractDominant(track->coverPath);
        if (cc.ok) dominant = cc.color;
    }
    dominant = normalizeDominant(dominant);

    {
        auto stencil = paimon::SpriteHelper::createRoundedRectStencil(
            widthOverride, kCardHeight, kCardRadius);
        auto clip = CCClippingNode::create();
        if (stencil && clip) {
            clip->setStencil(stencil);
            clip->setAlphaThreshold(0.05f);
            clip->setContentSize({widthOverride, kCardHeight});
            clip->setAnchorPoint({0.f, 0.f});
            clip->setPosition({0.f, 0.f});
            clip->setID("card-gradient-clip"_spr);

            // Color dominante al 100% a la izquierda (alpha 150 para
            // no competir con la portada ni saturar la card), fade a 0
            // alpha al 50%. Despues del 50% la card queda SOLO con el
            // color base del bg.
            cocos2d::ccColor4F from = {
                dominant.r / 255.f,
                dominant.g / 255.f,
                dominant.b / 255.f,
                150.f / 255.f
            };
            cocos2d::ccColor4F to = {
                dominant.r / 255.f,
                dominant.g / 255.f,
                dominant.b / 255.f,
                0.f
            };

            if (auto grad = buildHorizontalGradient(
                    widthOverride, kCardHeight, from, to, 0.5f)) {
                grad->setAnchorPoint({0.f, 0.f});
                grad->setPosition({0.f, 0.f});
                grad->setID("card-gradient"_spr);
                clip->addChild(grad, 0);
            }

            node->addChild(clip, 1);
        }
    }

    // 3. Thumbnail redondeado
    //
    // El thumbnail usa su propio CCClippingNode para quedar con
    // esquinas redondeadas sin depender de texturas pre-rendeadas.
    const float thumbSize = kCardHeight - 10.f;
    const float thumbX = 8.f;
    const float thumbY = (kCardHeight - thumbSize) / 2.f;

    auto thumbStencil = paimon::SpriteHelper::createRoundedRectStencil(
        thumbSize, thumbSize, kThumbRadius);
    auto thumbClip = CCClippingNode::create();
    if (thumbStencil && thumbClip) {
        thumbClip->setStencil(thumbStencil);
        thumbClip->setAlphaThreshold(0.05f);
        thumbClip->setContentSize({thumbSize, thumbSize});
        thumbClip->setAnchorPoint({0.f, 0.f});
        thumbClip->setPosition({thumbX, thumbY});
        thumbClip->setID("card-thumb-clip"_spr);
        node->addChild(thumbClip, 3);

        bool thumbFilled = false;
        if (!track->coverPath.empty()) {
            if (auto* tex = CCTextureCache::sharedTextureCache()->addImage(
                    track->coverPath.c_str(), false)) {
                if (auto spr = CCSprite::createWithTexture(tex)) {
                    const CCSize sz = spr->getContentSize();
                    // cover-fit: el lado mas corto llena el cuadrado
                    // para evitar barras negras.
                    const float s = thumbSize / std::min(sz.width, sz.height);
                    spr->setScale(s);
                    spr->setAnchorPoint({0.5f, 0.5f});
                    spr->setPosition({thumbSize / 2, thumbSize / 2});
                    thumbClip->addChild(spr);
                    thumbFilled = true;
                }
            }
        }

        if (!thumbFilled) {
            // Placeholder: relleno plano + icono central.
            if (auto solid = paimon::SpriteHelper::createRoundedRect(
                    thumbSize, thumbSize, 0.f,
                    ccc4FFromccc4B({36, 38, 55, 255}))) {
                solid->setAnchorPoint({0.f, 0.f});
                solid->setPosition({0.f, 0.f});
                thumbClip->addChild(solid, 0);
            }
            if (auto defSpr = CCSprite::createWithSpriteFrameName("GJ_musicOnBtn_001.png")) {
                const float s = thumbSize * 0.55f /
                    std::max(defSpr->getContentSize().width, 1.f);
                defSpr->setScale(s);
                defSpr->setAnchorPoint({0.5f, 0.5f});
                defSpr->setPosition({thumbSize / 2, thumbSize / 2});
                defSpr->setColor({160, 160, 180});
                thumbClip->addChild(defSpr, 1);
            }
        }
    }

    // 4. Texto (title + subtitle)
    //
    // Mismo column a la derecha del thumbnail. El titulo usa un
    // limitLabelWidth con buen padding para dejar espacio a los
    // botones de accion de la derecha.
    const float textX = thumbX + thumbSize + 12.f;
    const float textAreaW = widthOverride - textX - 110.f;

    auto nameLbl = CCLabelBMFont::create(track->displayName.c_str(), "bigFont.fnt");
    if (nameLbl) {
        nameLbl->setAnchorPoint({0.f, 0.5f});
        nameLbl->setColor({255, 255, 255});
        nameLbl->limitLabelWidth(textAreaW, 0.52f, 0.28f);
        nameLbl->setPosition({textX, kCardHeight * 0.66f});
        nameLbl->setID("card-title"_spr);
        node->addChild(nameLbl, 4);
    }

    // Subtitulo: source chip + duracion + artista opcional.
    std::string sourceStr = (track->source == TrackSource::Downloaded) ? "downloaded" : "local";
    std::string durStr = track->durationMs > 0
        ? fmt::format("{}:{:02}", track->durationMs / 60000, (track->durationMs / 1000) % 60)
        : "—";
    std::string subStr = fmt::format("{} • {}", sourceStr, durStr);
    if (!track->artist.empty()) {
        subStr = fmt::format("{} • {}", track->artist, subStr);
    }

    auto subLbl = CCLabelBMFont::create(subStr.c_str(), "chatFont.fnt");
    if (subLbl) {
        subLbl->setAnchorPoint({0.f, 0.5f});
        subLbl->setColor({200, 205, 225});
        subLbl->limitLabelWidth(textAreaW, 0.42f, 0.25f);
        subLbl->setPosition({textX, kCardHeight * 0.30f});
        subLbl->setID("card-subtitle"_spr);
        node->addChild(subLbl, 4);
    }

    // 5. Botones de accion
    auto menu = CCMenu::create();
    menu->setContentSize({100.f, kCardHeight});
    menu->setAnchorPoint({1.f, 0.5f});
    menu->setPosition({widthOverride - 6.f, kCardHeight / 2.f});
    menu->ignoreAnchorPointForPosition(false);

    auto makeIconBtn = [&](const char* frame, SEL_MenuHandler handler,
                           float scale) -> CCMenuItemSpriteExtra* {
        auto s = CCSprite::createWithSpriteFrameName(frame);
        if (!s) return nullptr;
        s->setScale(scale);
        auto b = CCMenuItemSpriteExtra::create(s, this, handler);
        if (b) b->setUserObject(CCString::create(trackId.c_str()));
        return b;
    };

    if (auto b = makeIconBtn("GJ_playMusicBtn_001.png",
            menu_selector(MenuMusicLibraryPopup::onPlayTrack), 0.7f)) {
        menu->addChild(b);
    }
    if (auto b = makeIconBtn("GJ_plusBtn_001.png",
            menu_selector(MenuMusicLibraryPopup::onAddToPlaylist), 0.55f)) {
        menu->addChild(b);
    }
    if (auto b = makeIconBtn("GJ_deleteBtn_001.png",
            menu_selector(MenuMusicLibraryPopup::onRemoveTrack), 0.55f)) {
        menu->addChild(b);
    }

    menu->setLayout(RowLayout::create()
        ->setGap(4.f)
        ->setAxisAlignment(AxisAlignment::End)
        ->setCrossAxisAlignment(AxisAlignment::Center));
    menu->setID("card-actions"_spr);
    menu->updateLayout();
    node->addChild(menu, 5);

    return node;
}

// Callbacks

void MenuMusicLibraryPopup::onSearchChanged(const std::string& query) {
    m_query = query;
    rebuildList();
}

void MenuMusicLibraryPopup::onPlayTrack(CCObject* sender) {
    auto* btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto* idStr = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!idStr) return;
    MenuMusicPlayer::get().playSpecific(idStr->getCString());
}

void MenuMusicLibraryPopup::onRemoveTrack(CCObject* sender) {
    auto* btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto* idStr = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!idStr) return;
    std::string id = idStr->getCString();

    geode::createQuickPopup(
        "Remove Track",
        "Remove this track from your library?\n<cy>Downloaded tracks will also have their file deleted.</c>",
        "Cancel", "Remove",
        [id](FLAlertLayer*, bool confirm) {
            if (!confirm) return;
            MenuMusicLibrary::get().removeTrack(id, /*deleteFiles=*/true);
        }
    );
}

void MenuMusicLibraryPopup::onAddToPlaylist(CCObject* sender) {
    auto* btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto* idStr = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!idStr) return;

    auto& lib = MenuMusicLibrary::get();
    if (lib.playlists().empty()) {
        Notification::create("Create a playlist first.", NotificationIcon::Info)->show();
        return;
    }
    // Anadir a la playlist activa (si no hay, a la primera).
    std::string plid = lib.activePlaylistId().empty()
        ? lib.playlists().front().id
        : lib.activePlaylistId();
    lib.addTrackToPlaylist(plid, idStr->getCString());
    Notification::create("Added to playlist.", NotificationIcon::Success)->show();
}

void MenuMusicLibraryPopup::onAddMusic(CCObject*) {
    if (auto p = MenuMusicAddPopup::create()) p->show();
}

} // namespace paimon::menumusic
