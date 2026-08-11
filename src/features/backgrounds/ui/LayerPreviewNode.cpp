#include "LayerPreviewNode.hpp"

#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonShaderSprite.hpp"
#include "../../../utils/Shaders.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/VideoThumbnailSprite.hpp"
#include "../../../managers/ThumbnailAPI.hpp"
#include "../../thumbnails/services/LocalThumbs.hpp"

#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>

#include <algorithm>
#include <filesystem>
#include <random>

using namespace geode::prelude;

namespace paimon::bgpreview {

namespace {

std::string tr(char const* key, char const* fallback) {
    auto value = Localization::get().getString(key);
    if (value == key && fallback && fallback[0] != '\0') return fallback;
    return value;
}


CCSprite* addSpr(CCNode* parent, char const* frameName, CCPoint pos,
                 float scale, int z = 0, GLubyte opacity = 255) {
    auto* spr = paimon::SpriteHelper::safeCreateWithFrameName(frameName);
    if (!spr) return nullptr;
    spr->setPosition(pos);
    spr->setScale(scale);
    if (opacity < 255) {
        spr->setOpacity(opacity);
    }
    parent->addChild(spr, z);
    return spr;
}

CCLabelBMFont* addLabel(CCNode* parent, char const* text, char const* font,
                        CCPoint pos, float scale, ccColor3B color,
                        int z = 1, GLubyte opacity = 255) {
    auto* lbl = CCLabelBMFont::create(text, font);
    if (!lbl) return nullptr;
    lbl->setPosition(pos);
    lbl->setScale(scale);
    lbl->setColor(color);
    if (opacity < 255) lbl->setOpacity(opacity);
    parent->addChild(lbl, z);
    return lbl;
}

// Ventana del juego (GJ_square01) centrada en `center`.
CCNode* addWindow(CCNode* parent, CCPoint center, CCSize size, int z = 0,
                  GLubyte opacity = 255) {
    CCNode* window = paimon::SpriteHelper::safeCreateNineSliceFromFile("GJ_square01.png");
    if (!window) {
        window = paimon::SpriteHelper::createColorPanel(
            size.width, size.height, {14, 24, 52}, opacity, 6.f);
    }
    if (!window) return nullptr;
    window->setContentSize(size);
    window->setAnchorPoint({0.5f, 0.5f});
    window->setPosition(center);
    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(window)) {
        rgba->setOpacity(opacity);
    }
    parent->addChild(window, z);
    return window;
}

// Filas de celda tipo lista dentro de una ventana.
void addListRows(CCNode* parent, CCPoint windowCenter, CCSize windowSize,
                 int rows, int z = 2) {
    float const rowW = windowSize.width - 22.f;
    float const gap = 4.f;
    float const rowH = (windowSize.height - 26.f - gap * (rows - 1)) / std::max(rows, 1);
    if (rowH <= 2.f) return;

    float top = windowCenter.y + windowSize.height / 2.f - 16.f;
    for (int i = 0; i < rows; ++i) {
        float cy = top - rowH / 2.f - i * (rowH + gap);
        auto* plate = paimon::SpriteHelper::createColorPanel(
            rowW, rowH, {0, 0, 0}, 90, 3.f);
        if (!plate) continue;
        plate->setAnchorPoint({0.5f, 0.5f});
        plate->setPosition({windowCenter.x, cy});
        parent->addChild(plate, z);

        auto* bar = paimon::SpriteHelper::createColorPanel(
            rowW * 0.42f, std::min(4.f, rowH * 0.22f), {255, 255, 255}, 90, 1.f);
        if (bar) {
            bar->setAnchorPoint({0.f, 0.5f});
            bar->setPosition({windowCenter.x - rowW / 2.f + 8.f, cy + rowH * 0.16f});
            parent->addChild(bar, z + 1);
        }
        auto* sub = paimon::SpriteHelper::createColorPanel(
            rowW * 0.24f, std::min(3.f, rowH * 0.16f), {255, 255, 255}, 55, 1.f);
        if (sub) {
            sub->setAnchorPoint({0.f, 0.5f});
            sub->setPosition({windowCenter.x - rowW / 2.f + 8.f, cy - rowH * 0.18f});
            parent->addChild(sub, z + 1);
        }
    }
}

// Barra inferior tipica del juego (flechas + boton central).
void addBottomBar(CCNode* parent, CCSize win) {
    addSpr(parent, "GJ_arrow_01_001.png", {28.f, 28.f}, 0.9f, 3);
    addSpr(parent, "GJ_infoIcon_001.png", {win.width - 26.f, 28.f}, 0.9f, 3);
}

// El icono del jugador tal cual lo tiene configurado el usuario.
CCNode* makePlayerIcon(float scale) {
    auto* gm = GameManager::sharedState();
    if (!gm) return nullptr;
    auto* player = SimplePlayer::create(gm->getPlayerFrame());
    if (!player) return nullptr;
    player->setColor(gm->colorForIdx(gm->getPlayerColor()));
    player->setSecondColor(gm->colorForIdx(gm->getPlayerColor2()));
    player->setScale(scale);
    return player;
}


void mockMenu(CCNode* root, CCSize win) {
    float const cx = win.width / 2.f;

    addSpr(root, "GJ_logo_001.png", {cx, win.height - 78.f}, 1.f, 2);

    float const btnY = win.height * 0.45f;
    addSpr(root, "GJ_playBtn_001.png", {cx, btnY}, 1.f, 2);
    addSpr(root, "GJ_garageBtn_001.png", {cx - 100.f, btnY - 6.f}, 0.9f, 2);
    addSpr(root, "GJ_creatorBtn_001.png", {cx + 100.f, btnY - 6.f}, 0.9f, 2);

    addSpr(root, "GJ_achBtn_001.png", {28.f, 28.f}, 0.75f, 2);
    addSpr(root, "GJ_optionsBtn_001.png", {74.f, 28.f}, 0.75f, 2);
    addSpr(root, "GJ_statsBtn_001.png", {120.f, 28.f}, 0.75f, 2);

    addSpr(root, "GJ_profileButton_001.png", {win.width - 28.f, 28.f}, 0.75f, 2);
    addSpr(root, "GJ_creatorsBtn_001.png", {win.width - 74.f, 28.f}, 0.75f, 2);
}

void mockLevelSelect(CCNode* root, CCSize win) {
    float const cx = win.width / 2.f;
    float const cy = win.height / 2.f;

    addWindow(root, {cx, cy + 6.f}, {300.f, 168.f}, 1, 235);
    addLabel(root, "Stereo Madness", "bigFont.fnt", {cx, cy + 66.f}, 0.6f, {255, 255, 255}, 3);
    addSpr(root, "GJ_playBtn_001.png", {cx, cy + 4.f}, 0.85f, 3);
    addLabel(root, "Normal", "goldFont.fnt", {cx, cy - 54.f}, 0.5f, {255, 255, 255}, 3);

    auto* left = addSpr(root, "navArrowBtn_001.png", {cx - 190.f, cy + 6.f}, 1.f, 3);
    if (left) left->setFlipX(true);
    addSpr(root, "navArrowBtn_001.png", {cx + 190.f, cy + 6.f}, 1.f, 3);

    addBottomBar(root, win);
}

void mockLevelInfo(CCNode* root, CCSize win) {
    float const cx = win.width / 2.f;

    addLabel(root, "Level Name", "bigFont.fnt", {cx, win.height - 34.f}, 0.85f, {255, 255, 255}, 3);
    addLabel(root, "By Player", "goldFont.fnt", {cx, win.height - 58.f}, 0.5f, {255, 255, 255}, 3);

    addWindow(root, {cx, win.height * 0.46f}, {330.f, 108.f}, 1, 200);
    addSpr(root, "GJ_playBtn_001.png", {cx, win.height * 0.46f}, 0.85f, 3);

    addSpr(root, "GJ_starsIcon_001.png", {cx - 120.f, 62.f}, 0.9f, 3);
    addSpr(root, "GJ_downloadsIcon_001.png", {cx - 40.f, 62.f}, 0.9f, 3);
    addSpr(root, "GJ_likesIcon_001.png", {cx + 40.f, 62.f}, 0.9f, 3);
    addSpr(root, "GJ_lengthIcon_001.png", {cx + 120.f, 62.f}, 0.9f, 3);

    addBottomBar(root, win);
}

void mockCreator(CCNode* root, CCSize win) {
    float const cx = win.width / 2.f;
    float const cy = win.height / 2.f;

    char const* top[] = {"GJ_createBtn_001.png", "GJ_savedBtn_001.png", "GJ_scoresBtn_001.png"};
    char const* bottom[] = {"GJ_featuredBtn_001.png", "GJ_mapPacksBtn_001.png", "GJ_dailyBtn_001.png"};

    for (int i = 0; i < 3; ++i) {
        addSpr(root, top[i], {cx + (i - 1) * 108.f, cy + 44.f}, 0.85f, 2);
        addSpr(root, bottom[i], {cx + (i - 1) * 108.f, cy - 46.f}, 0.85f, 2);
    }

    addBottomBar(root, win);
}

void mockListLayer(CCNode* root, CCSize win, char const* title) {
    float const cx = win.width / 2.f;
    float const cy = win.height / 2.f - 4.f;
    CCSize const windowSize{std::min(360.f, win.width - 90.f), 210.f};

    addLabel(root, title, "bigFont.fnt", {cx, win.height - 26.f}, 0.7f, {255, 255, 255}, 4);
    addWindow(root, {cx, cy}, windowSize, 1, 235);
    addListRows(root, {cx, cy}, windowSize, 4);

    auto* left = addSpr(root, "GJ_arrow_03_001.png", {cx - windowSize.width / 2.f - 26.f, cy}, 0.9f, 3);
    if (left) left->setFlipX(true);
    addSpr(root, "GJ_arrow_03_001.png", {cx + windowSize.width / 2.f + 26.f, cy}, 0.9f, 3);

    addBottomBar(root, win);
}

void mockSearch(CCNode* root, CCSize win) {
    float const cx = win.width / 2.f;
    float const cy = win.height / 2.f;

    addLabel(root, "Search", "bigFont.fnt", {cx, win.height - 26.f}, 0.7f, {255, 255, 255}, 4);

    auto* bar = paimon::SpriteHelper::createColorPanel(300.f, 34.f, {0, 0, 0}, 140, 4.f);
    if (bar) {
        bar->setAnchorPoint({0.5f, 0.5f});
        bar->setPosition({cx - 16.f, cy + 62.f});
        root->addChild(bar, 2);
    }
    if (auto* hint = addLabel(root, "Enter a level name", "chatFont.fnt",
                              {cx - 156.f, cy + 62.f}, 0.6f, {170, 180, 200}, 3)) {
        hint->setAnchorPoint({0.f, 0.5f});
    }
    addSpr(root, "GJ_searchBtn_001.png", {cx + 158.f, cy + 62.f}, 0.85f, 3);

    char const* quick[] = {"GJ_longBtn01_001.png", "GJ_longBtn02_001.png", "GJ_longBtn03_001.png"};
    for (int i = 0; i < 3; ++i) {
        addSpr(root, quick[i], {cx - 96.f + i * 96.f, cy - 16.f}, 0.85f, 2);
    }

    addBottomBar(root, win);
}

void mockProfile(CCNode* root, CCSize win) {
    float const cx = win.width / 2.f;
    float const cy = win.height / 2.f;
    CCSize const windowSize{std::min(380.f, win.width - 70.f), 230.f};

    addWindow(root, {cx, cy}, windowSize, 1, 235);
    addLabel(root, "Player", "bigFont.fnt", {cx, cy + windowSize.height / 2.f - 22.f}, 0.75f,
             {255, 255, 255}, 4);

    if (auto* icon = makePlayerIcon(1.1f)) {
        icon->setPosition({cx - windowSize.width / 2.f + 46.f, cy + 24.f});
        root->addChild(icon, 4);
    }

    char const* stats[] = {"GJ_starsIcon_001.png", "GJ_coinsIcon_001.png",
                           "GJ_diamondsIcon_001.png", "GJ_secretCoinIcon_001.png"};
    for (int i = 0; i < 4; ++i) {
        addSpr(root, stats[i], {cx - 40.f + i * 62.f, cy + 26.f}, 0.85f, 4);
    }

    addListRows(root, {cx, cy - 44.f}, {windowSize.width, 92.f}, 2, 3);
    addBottomBar(root, win);
}

void mockGarage(CCNode* root, CCSize win) {
    float const cx = win.width / 2.f;

    if (auto* icon = makePlayerIcon(2.2f)) {
        icon->setPosition({cx, win.height - 72.f});
        root->addChild(icon, 4);
    }
    addLabel(root, "Player", "bigFont.fnt", {cx, win.height - 22.f}, 0.7f, {255, 255, 255}, 4);

    CCSize const windowSize{std::min(400.f, win.width - 60.f), 140.f};
    CCPoint const windowCenter{cx, win.height * 0.29f};
    addWindow(root, windowCenter, windowSize, 1, 235);

    int const cols = 8;
    int const rows = 3;
    float const cell = std::min((windowSize.width - 20.f) / cols, (windowSize.height - 20.f) / rows);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            auto* slot = paimon::SpriteHelper::createColorPanel(
                cell - 3.f, cell - 3.f, {0, 0, 0}, 90, 2.f);
            if (!slot) continue;
            slot->setAnchorPoint({0.5f, 0.5f});
            slot->setPosition({
                windowCenter.x + (c - (cols - 1) / 2.f) * cell,
                windowCenter.y + ((rows - 1) / 2.f - r) * cell,
            });
            root->addChild(slot, 3);
        }
    }

    addBottomBar(root, win);
}

} // namespace


std::string displayNameForLayer(std::string const& layerKey) {
    for (auto const& [key, name] : LayerBackgroundManager::LAYER_OPTIONS) {
        if (key == layerKey) return name;
    }
    return layerKey;
}

ccColor3B accentForType(std::string const& type) {
    if (type == "custom") return {120, 255, 150};
    if (type == "video")  return {120, 200, 255};
    if (type == "shader") return {200, 150, 255};
    if (type == "random") return {255, 210, 110};
    if (type == "id")     return {255, 160, 100};
    if (type == "default") return {120, 130, 150};
    return {150, 200, 255}; // referencia a otra pantalla ("Mismo que...")
}

std::string describeConfig(LayerBgConfig const& cfg) {
    std::string status;
    if (cfg.type == "default")      status = tr("pai.config.status.default", "Default");
    else if (cfg.type == "custom")  status = tr("pai.config.status.custom_image", "Custom Image");
    else if (cfg.type == "video")   status = tr("pai.config.status.video", "Video");
    else if (cfg.type == "shader")  status = tr("pai.config.status.shader_bg", "Shader Background");
    else if (cfg.type == "random")  status = tr("pai.config.status.random", "Random");
    else if (cfg.type == "id")      status = tr("pai.config.status.level_id", "Level ID: ") + std::to_string(cfg.levelId);
    else status = tr("pai.config.status.same_as", "Same as ") + displayNameForLayer(cfg.type);
    return status;
}

CCNode* createSceneMock(std::string const& layerKey) {
    auto win = CCDirector::get()->getWinSize();
    auto* root = CCNode::create();
    root->setContentSize(win);
    root->setAnchorPoint({0.f, 0.f});

    if (layerKey == "menu")              mockMenu(root, win);
    else if (layerKey == "levelselect")  mockLevelSelect(root, win);
    else if (layerKey == "levelinfo")    mockLevelInfo(root, win);
    else if (layerKey == "creator")      mockCreator(root, win);
    else if (layerKey == "browser")      mockListLayer(root, win, "Online Levels");
    else if (layerKey == "search")       mockSearch(root, win);
    else if (layerKey == "leaderboards") mockListLayer(root, win, "Leaderboards");
    else if (layerKey == "profile")      mockProfile(root, win);
    else if (layerKey == "garage")       mockGarage(root, win);
    else                                 mockListLayer(root, win, displayNameForLayer(layerKey).c_str());

    return root;
}


LayerPreviewNode* LayerPreviewNode::create(CCSize box, std::string const& layerKey) {
    auto* ret = new LayerPreviewNode();
    if (ret && ret->initWithBox(box, layerKey)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool LayerPreviewNode::initWithBox(CCSize box, std::string const& layerKey) {
    if (!CCNode::init()) return false;

    m_key = layerKey.empty() ? "menu" : layerKey;

    // Encaja la caja en el aspecto real de la pantalla del juego para que la
    // maqueta quede a escala exacta.
    auto win = CCDirector::get()->getWinSize();
    float const aspect = win.height > 0.f ? win.width / win.height : 16.f / 9.f;
    float w = box.width;
    float h = w / aspect;
    if (h > box.height) {
        h = box.height;
        w = h * aspect;
    }
    m_frame = CCSize{std::floor(w), std::floor(h)};

    this->setContentSize(m_frame);
    this->setAnchorPoint({0.5f, 0.5f});
    this->setID("paimon-layer-preview"_spr);

    auto* stencil = paimon::SpriteHelper::createRectStencil(m_frame.width, m_frame.height);
    auto* clipper = CCClippingNode::create();
    clipper->setStencil(stencil);
    clipper->setAlphaThreshold(0.5f);
    clipper->setContentSize(m_frame);
    clipper->setPosition({0.f, 0.f});
    this->addChild(clipper, 0);

    m_bgHolder = CCNode::create();
    m_bgHolder->setContentSize(m_frame);
    m_bgHolder->setAnchorPoint({0.f, 0.f});
    clipper->addChild(m_bgHolder, 0);

    m_mockHolder = CCNode::create();
    m_mockHolder->setContentSize(m_frame);
    m_mockHolder->setAnchorPoint({0.f, 0.f});
    clipper->addChild(m_mockHolder, 10);

    refresh();
    return true;
}

void LayerPreviewNode::setLayerKey(std::string const& key) {
    if (key == m_key) return;
    m_key = key;
    refresh();
}

void LayerPreviewNode::setShowMock(bool show) {
    if (m_showMock == show) return;
    m_showMock = show;
    rebuildMock();
}

LayerBgConfig LayerPreviewNode::resolvedConfig() const {
    return LayerBackgroundManager::get().resolveConfig(m_key);
}

void LayerPreviewNode::refresh() {
    ++m_gen;
    rebuildBackground();
    rebuildMock();
}

void LayerPreviewNode::rebuildMock() {
    if (!m_mockHolder) return;
    m_mockHolder->removeAllChildren();
    if (!m_showMock) return;

    auto win = CCDirector::get()->getWinSize();
    if (win.height <= 0.f) return;

    auto* mock = createSceneMock(m_key);
    if (!mock) return;
    mock->setScale(m_frame.height / win.height);
    mock->setAnchorPoint({0.f, 0.f});
    mock->setPosition({(m_frame.width - win.width * mock->getScale()) / 2.f, 0.f});
    m_mockHolder->addChild(mock);
}

void LayerPreviewNode::addFlatFill(ccColor4B color) {
    if (!m_bgHolder) return;
    auto* fill = CCLayerColor::create(color);
    fill->setContentSize(m_frame);
    m_bgHolder->addChild(fill, 0);
}

void LayerPreviewNode::addPlaceholder(std::string const& text, ccColor3B color) {
    if (!m_bgHolder) return;
    addFlatFill({28, 32, 52, 255});
    auto* lbl = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    if (!lbl) return;
    lbl->setAlignment(kCCTextAlignmentCenter);
    lbl->limitLabelWidth(m_frame.width - 20.f, 0.34f, 0.12f);
    lbl->setColor(color);
    lbl->setPosition({m_frame.width / 2.f, m_frame.height / 2.f});
    m_bgHolder->addChild(lbl, 2);
}

void LayerPreviewNode::addCover(CCSprite* sprite) {
    if (!sprite || !m_bgHolder) return;
    float const cw = sprite->getContentWidth();
    float const ch = sprite->getContentHeight();
    if (cw <= 0.f || ch <= 0.f) return;

    sprite->setAnchorPoint({0.5f, 0.5f});
    sprite->setScale(std::max(m_frame.width / cw, m_frame.height / ch));
    sprite->setPosition({m_frame.width / 2.f, m_frame.height / 2.f});
    m_bgHolder->addChild(sprite, 1);
}

void LayerPreviewNode::applyPostShader(CCSprite* sprite, std::string const& shader) {
    if (!sprite || shader.empty() || shader == "none") return;
    auto* program = Shaders::getBgShaderProgram(shader);
    if (!program) return;
    sprite->setShaderProgram(program);
}

void LayerPreviewNode::addDarkOverlay(LayerBgConfig const& cfg) {
    if (!cfg.darkMode || !m_bgHolder) return;
    auto* dark = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(cfg.darkIntensity * 200.f)});
    dark->setContentSize(m_frame);
    m_bgHolder->addChild(dark, 5);
}

void LayerPreviewNode::rebuildBackground() {
    if (!m_bgHolder) return;
    m_bgHolder->removeAllChildren();

    auto const cfg = LayerBackgroundManager::get().resolveConfig(m_key);

    // Fondo vanilla: el degradado azul del juego, igual que lo ve el jugador.
    if (cfg.type == "default") {
        if (auto* grad = paimon::SpriteHelper::safeCreate("GJ_gradientBG.png")) {
            auto const size = grad->getContentSize();
            grad->setAnchorPoint({0.f, 0.f});
            grad->setPosition({0.f, 0.f});
            grad->setScaleX(m_frame.width / std::max(size.width, 1.f));
            grad->setScaleY(m_frame.height / std::max(size.height, 1.f));
            grad->setColor({0, 102, 255}); // el azul vanilla del menu
            m_bgHolder->addChild(grad, 0);
        } else {
            addFlatFill({0, 72, 180, 255});
        }
        addDarkOverlay(cfg);
        return;
    }

    // Shader procedural: se dibuja de verdad y animado.
    if (cfg.type == "shader") {
        auto* program = Shaders::getProceduralBgShaderProgram(cfg.shader);
        auto* sprite = PaimonShaderGradient::create({255, 255, 255, 255}, {255, 255, 255, 255});
        if (program && sprite) {
            sprite->setShaderProgram(program);
            sprite->m_intensity = 1.f;
            sprite->m_time = 0.f;
            sprite->m_texSize = m_frame;
            sprite->setAnchorPoint({0.f, 0.f});
            sprite->setPosition({0.f, 0.f});
            sprite->setContentSize(m_frame);
            sprite->schedule(schedule_selector(PaimonShaderGradient::updateShaderTime));
            m_bgHolder->addChild(sprite, 1);
        } else {
            addPlaceholder(tr("pai.config.status.shader_bg", "Shader Background"), {150, 200, 255});
        }
        addDarkOverlay(cfg);
        return;
    }

    if (cfg.type == "video") {
        if (cfg.customPath.empty() || !paimon::assets::exists(cfg.customPath)) {
            addPlaceholder(tr("pai.config.preview.file_not_found", "File not found"), {255, 110, 110});
            return;
        }
        addFlatFill({14, 16, 26, 255});
        if (auto* video = VideoThumbnailSprite::create(cfg.customPath)) {
            video->setLoop(true);
            video->setVolume(0.f);
            addCover(video);
        } else {
            addPlaceholder("VIDEO", {120, 200, 255});
        }
        addDarkOverlay(cfg);
        return;
    }

    if (cfg.type == "custom") {
        if (cfg.customPath.empty() || !paimon::assets::exists(cfg.customPath)) {
            addPlaceholder(tr("pai.config.preview.file_not_found", "File not found"), {255, 110, 110});
            return;
        }

        auto const ext = geode::utils::string::toLower(
            geode::utils::string::pathToString(paimon::assets::pathFromUtf8(cfg.customPath).extension()));

        if (ext == ".gif") {
            addFlatFill({20, 22, 34, 255});
            int const gen = m_gen;
            Ref<LayerPreviewNode> self = this;
            auto const shader = cfg.shader;
            AnimatedGIFSprite::pinGIF(cfg.customPath);
            AnimatedGIFSprite::createAsync(cfg.customPath,
                [self, gen, cfg, shader](AnimatedGIFSprite* anim) {
                    if (!self || self->m_gen != gen || !self->m_bgHolder) return;
                    if (!anim) {
                        self->addPlaceholder(tr("pai.config.preview.gif_error", "GIF Error"), {255, 110, 110});
                        return;
                    }
                    if (!shader.empty() && shader != "none") {
                        if (auto* program = Shaders::getBgShaderProgram(shader)) {
                            anim->setShaderProgram(program);
                            anim->m_intensity = 0.5f;
                            anim->m_texSize = self->m_frame;
                        }
                    }
                    self->addCover(anim);
                    self->addDarkOverlay(cfg);
                });
            return;
        }

        auto loaded = ImageLoadHelper::loadStaticImage(cfg.customPath, 24);
        if (!loaded.success || !loaded.texture) {
            addPlaceholder(tr("pai.config.preview.load_error", "Load error"), {255, 110, 110});
            return;
        }

        CCSprite* sprite = nullptr;
        if (!cfg.shader.empty() && cfg.shader != "none") {
            if (auto* shaderSpr = Shaders::ShaderBgSprite::createWithTexture(loaded.texture)) {
                if (auto* program = Shaders::getBgShaderProgram(cfg.shader)) {
                    shaderSpr->setShaderProgram(program);
                    shaderSpr->m_shaderIntensity =
                        Mod::get()->getSavedValue<float>("layerbg-shader-intensity", 0.5f);
                    shaderSpr->m_screenW = m_frame.width;
                    shaderSpr->m_screenH = m_frame.height;
                    shaderSpr->schedule(schedule_selector(Shaders::ShaderBgSprite::updateShaderTime));
                }
                sprite = shaderSpr;
            }
        }
        if (!sprite) sprite = CCSprite::createWithTexture(loaded.texture);
        loaded.texture->release();

        addCover(sprite);
        addDarkOverlay(cfg);
        return;
    }

    if (cfg.type == "random") {
        auto ids = LocalThumbs::get().getAllLevelIDs();
        if (!ids.empty()) {
            static std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<size_t> dist(0, ids.size() - 1);
            if (auto* tex = LocalThumbs::get().loadTexture(ids[dist(rng)])) {
                auto* sprite = CCSprite::createWithTexture(tex);
                applyPostShader(sprite, cfg.shader);
                addCover(sprite);
                addDarkOverlay(cfg);
                return;
            }
        }
        addPlaceholder(tr("pai.config.preview.random_no_cache", "Random\n(no cache)"), {255, 210, 110});
        return;
    }

    if (cfg.type == "id" && cfg.levelId > 0) {
        if (auto* tex = LocalThumbs::get().loadTexture(cfg.levelId)) {
            auto* sprite = CCSprite::createWithTexture(tex);
            applyPostShader(sprite, cfg.shader);
            addCover(sprite);
            addDarkOverlay(cfg);
            return;
        }

        addFlatFill({20, 22, 34, 255});
        addPlaceholder(tr("leaderboard.loading", "Loading..."), {200, 210, 230});

        int const gen = m_gen;
        Ref<LayerPreviewNode> self = this;
        ThumbnailAPI::get().getThumbnail(cfg.levelId, [self, gen, cfg](bool success, CCTexture2D* tex) {
            if (!self || self->m_gen != gen || !self->m_bgHolder) return;
            self->m_bgHolder->removeAllChildren();
            if (!success || !tex) {
                self->addPlaceholder(
                    tr("pai.config.preview.not_found_server", "Not found\non server"), {255, 140, 100});
                return;
            }
            auto* sprite = CCSprite::createWithTexture(tex);
            self->applyPostShader(sprite, cfg.shader);
            self->addCover(sprite);
            self->addDarkOverlay(cfg);
        });
        return;
    }

    addPlaceholder(tr("pai.config.preview.unknown_type", "Unknown type"), {200, 150, 150});
}

} // namespace paimon::bgpreview
