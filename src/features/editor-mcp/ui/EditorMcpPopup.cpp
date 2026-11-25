#include "EditorMcpPopup.hpp"

#include "../EditorMcpBridge.hpp"
#include "../EditorMcpRenderer.hpp"
#include "../EditorMcpServer.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/binding/LevelEditorLayer.hpp>
#include <fmt/format.h>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::editormcp {

namespace kit = paimon::editor::kit;

namespace {

constexpr char const* kModuleID = "paimbnails.mcp.editor";
constexpr float kPopupWidth = 490.f;
constexpr float kPopupHeight = 295.f;

bool spanish() {
    return Localization::get().getLanguage() == Localization::Language::SPANISH;
}

char const* tr(char const* en, char const* es) {
    return spanish() ? es : en;
}

} // namespace

EditorMcpPopup* EditorMcpPopup::create() {
    auto* popup = new EditorMcpPopup();
    if (popup && popup->init()) {
        popup->autorelease();
        return popup;
    }
    CC_SAFE_DELETE(popup);
    return nullptr;
}

bool EditorMcpPopup::init() {
    if (!PaimonPopup::init(kPopupWidth, kPopupHeight)) return false;
    if (!EditorMcpServer::isInitialized()) {
        EditorMcpServer::get().init();
    }
    setID("editor-mcp-popup"_spr);
    setTitle(tr("Editor MCP (AI Bridge)", "Editor MCP (Puente IA)"));
    addInfoButton(
        tr("Geometry Dash Editor MCP", "Geometry Dash Editor MCP"),
        tr(
            "Exposes the entire level editor over a local Model Context Protocol (MCP) server "
            "with real-time 1:1 OpenGL rendering, full trigger/object creation, colors, groups, "
            "and a complete GD 2.2091 trigger & editor reference.\n\n"
            "<cy>HTTP MCP:</c> http://127.0.0.1:<port>/mcp\n"
            "<cg>1:1 Preview:</c> http://127.0.0.1:<port>/render.png\n"
            "<cb>Stdio Bridge:</c> http://127.0.0.1:<port>/bridge.py",
            "Expone todo el editor de niveles mediante un servidor local Model Context Protocol (MCP) "
            "con render 1:1 en tiempo real, creacion completa de triggers/objetos, colores, grupos "
            "y referencia completa de GD 2.2091.\n\n"
            "<cy>HTTP MCP:</c> http://127.0.0.1:<port>/mcp\n"
            "<cg>Preview 1:1:</c> http://127.0.0.1:<port>/render.png\n"
            "<cb>Puente Stdio:</c> http://127.0.0.1:<port>/bridge.py"
        )
    );
    addCorners(SideArtStyle::PopupGold, 0.85f);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kPopupWidth, kPopupHeight});
    m_mainLayer->addChild(menu, 10);

    // Left panel: live 1:1 render preview
    float const prevX = 16.f;
    float const prevY = 68.f;
    float const prevW = 228.f;
    float const prevH = 168.f;

    if (auto* inset = kit::inset({prevX, prevY, prevW, prevH}, 150)) {
        m_mainLayer->addChild(inset, 2);
    }

    auto* prevHeader = kit::header("paim_gif_modeRender.png", "GJ_cameraIcon_001.png",
        tr("1:1 Live Render Preview", "Vista Previa Render 1:1"), prevW - 12.f);
    prevHeader->setPosition({prevX + 6.f, prevY + prevH - 18.f});
    m_mainLayer->addChild(prevHeader, 4);

    m_previewFrame = CCNode::create();
    m_previewFrame->setContentSize({prevW - 12.f, prevH - 28.f});
    m_previewFrame->setPosition({prevX + 6.f, prevY + 6.f});
    m_mainLayer->addChild(m_previewFrame, 4);

    m_previewHint = kit::label(
        tr("Press 'Test 1:1 Render'\nto capture live editor", "Pulsa 'Probar Render 1:1'\npara capturar el editor"),
        "chatFont.fnt", 0.55f, kit::tint::muted);
    m_previewHint->setAlignment(kCCTextAlignmentCenter);
    m_previewHint->setPosition(m_previewFrame->getContentSize() / 2.f);
    m_previewFrame->addChild(m_previewHint, 1);

    // Right panel: server status, URL, stats & actions
    float const rightX = 256.f;
    float const rightW = 218.f;
    if (auto* infoBg = kit::inset({rightX, 68.f, rightW, 168.f}, 125)) {
        m_mainLayer->addChild(infoBg, 2);
    }

    auto* srvHeader = kit::header("paim_gif_optimize.png", "GJ_optionsBtn_001.png",
        tr("MCP Server Status", "Estado del Servidor MCP"), rightW - 12.f);
    srvHeader->setPosition({rightX + 6.f, 218.f});
    m_mainLayer->addChild(srvHeader, 4);

    m_serverStatusLabel = kit::label("...", "bigFont.fnt", 0.42f, kit::tint::green);
    m_serverStatusLabel->setAnchorPoint({0.f, 0.5f});
    m_serverStatusLabel->setPosition({rightX + 10.f, 198.f});
    m_mainLayer->addChild(m_serverStatusLabel, 5);

    m_urlLabel = kit::label("http://127.0.0.1:21690/mcp", "chatFont.fnt", 0.52f, kit::tint::cyan);
    m_urlLabel->setAnchorPoint({0.f, 0.5f});
    m_urlLabel->setPosition({rightX + 10.f, 180.f});
    m_mainLayer->addChild(m_urlLabel, 5);

    m_statsLabel = kit::label("", "chatFont.fnt", 0.50f, kit::tint::silver);
    m_statsLabel->setAnchorPoint({0.f, 1.f});
    m_statsLabel->setPosition({rightX + 10.f, 164.f});
    m_mainLayer->addChild(m_statsLabel, 5);

    m_togglePill = kit::pill(nullptr, "GJ_playBtn2_001.png",
        tr("Toggle Server", "Activar / Pausar"), "GJ_button_01.png", {96.f, 24.f},
        [this]() { toggleServer(); }, "goldFont.fnt", kit::tint::white);
    m_togglePill.button->setPosition({rightX + 54.f, 86.f});
    menu->addChild(m_togglePill.button);

    m_renderPill = kit::pill("paim_gif_modeRender.png", "GJ_cameraIcon_001.png",
        tr("Test 1:1 Render", "Probar Render 1:1"), "GJ_button_02.png", {102.f, 24.f},
        [this]() { runTestRender(); }, "goldFont.fnt", kit::tint::white);
    m_renderPill.button->setPosition({rightX + 160.f, 86.f});
    menu->addChild(m_renderPill.button);

    // Bottom action bar: Copy configs
    m_httpPill = kit::pill(nullptr, "GJ_copyBtn_001.png",
        tr("Copy HTTP MCP Config", "Copiar Config HTTP MCP"), "GJ_button_04.png", {190.f, 24.f},
        [this]() { copyHttpConfig(); }, "goldFont.fnt", kit::tint::white);
    m_httpPill.button->setPosition({132.f, 44.f});
    menu->addChild(m_httpPill.button);

    m_bridgePill = kit::pill(nullptr, "GJ_copyBtn_001.png",
        tr("Copy Stdio Bridge Config", "Copiar Config Stdio Bridge"), "GJ_button_05.png", {200.f, 24.f},
        [this]() { copyBridgeConfig(); }, "goldFont.fnt", kit::tint::white);
    m_bridgePill.button->setPosition({348.f, 44.f});
    menu->addChild(m_bridgePill.button);

    // Status bar
    if (auto* stBg = kit::inset({18.f, 10.f, kPopupWidth - 36.f, 20.f}, 115)) {
        m_mainLayer->addChild(stBg, 2);
    }
    m_statusIcon = kit::glyph(nullptr, "GJ_infoIcon_001.png", 12.f, kit::tint::cyan);
    m_statusIcon->setPosition({30.f, 20.f});
    m_mainLayer->addChild(m_statusIcon, 5);

    m_statusLabel = kit::label(
        tr("Ready for AI connections (Claude, Cursor, Antigravity, VS Code)",
           "Listo para conectar cualquier IA (Claude, Cursor, Antigravity, VS Code)"),
        "chatFont.fnt", 0.5f, kit::tint::silver);
    m_statusLabel->setAnchorPoint({0.f, 0.5f});
    m_statusLabel->setPosition({42.f, 20.f});
    m_statusLabel->limitLabelWidth(kPopupWidth - 62.f, 0.5f, 0.25f);
    m_mainLayer->addChild(m_statusLabel, 5);

    refreshState();
    scheduleUpdate();
    return true;
}

void EditorMcpPopup::onClose(CCObject* sender) {
    unscheduleUpdate();
    PaimonPopup::onClose(sender);
}

void EditorMcpPopup::update(float dt) {
    m_pollClock += dt;
    if (m_pollClock >= 0.5f) {
        m_pollClock = 0.f;
        refreshState();
    }
}

void EditorMcpPopup::refreshState() {
    auto& srv = EditorMcpServer::get();
    auto const st = srv.stats();

    if (m_serverStatusLabel) {
        m_serverStatusLabel->setString(st.status.c_str());
        m_serverStatusLabel->setColor(st.running ? kit::tint::green : kit::tint::orange);
        m_serverStatusLabel->limitLabelWidth(196.f, 0.42f, 0.22f);
    }
    if (m_urlLabel) {
        m_urlLabel->setString(srv.mcpUrl().c_str());
        m_urlLabel->limitLabelWidth(196.f, 0.52f, 0.25f);
    }
    if (m_statsLabel) {
        std::string info = fmt::format(
            "{}: {}  |  {}: {}\n{}: {}  |  {}: {}\n{}: {}",
            tr("Requests", "Peticiones"), st.totalRequests,
            tr("Tools", "Tools"), st.toolCalls,
            tr("1:1 Renders", "Renders 1:1"), st.rendersServed,
            tr("Built", "Creados"), st.objectsCreated,
            tr("Last", "Ultima"), st.lastTool.empty() ? "-" : st.lastTool
        );
        m_statsLabel->setString(info.c_str());
        m_statsLabel->limitLabelWidth(198.f, 0.50f, 0.25f);
    }

    kit::setPillText(m_togglePill, st.running ? tr("Stop", "Detener") : tr("Start", "Iniciar"));
    kit::setPillSkin(m_togglePill, st.running ? "GJ_button_06.png" : "GJ_button_01.png");
}

void EditorMcpPopup::toggleServer() {
    bool const next = !paimon::modules::isSelfEnabled(kModuleID);
    paimon::modules::setEnabled(kModuleID, next);
    EditorMcpServer::get().restart();
    refreshState();
    setStatus(
        next ? tr("MCP server started.", "Servidor MCP iniciado.")
             : tr("MCP server stopped.", "Servidor MCP detenido."),
        next ? kit::tint::green : kit::tint::orange
    );
}

void EditorMcpPopup::copyHttpConfig() {
    auto const url = EditorMcpServer::get().mcpUrl();
    std::string cfg = fmt::format(
        "{{\n"
        "  \"mcpServers\": {{\n"
        "    \"geometry-dash\": {{\n"
        "      \"type\": \"http\",\n"
        "      \"url\": \"{}\"\n"
        "    }}\n"
        "  }}\n"
        "}}",
        url
    );
    geode::utils::clipboard::write(cfg);
    PaimonNotify::show(tr("HTTP MCP config copied!", "Config HTTP MCP copiada!"), NotificationIcon::Success);
    setStatus(tr("Copied Streamable HTTP config to clipboard.", "Config HTTP copiada al portapapeles."), kit::tint::cyan);
}

void EditorMcpPopup::copyBridgeConfig() {
    auto const url = EditorMcpServer::get().mcpUrl();
    std::string cfg = fmt::format(
        "{{\n"
        "  \"mcpServers\": {{\n"
        "    \"geometry-dash\": {{\n"
        "      \"command\": \"python3\",\n"
        "      \"args\": [\"tools/gd_mcp_bridge.py\"],\n"
        "      \"env\": {{\n"
        "        \"GD_MCP_URL\": \"{}\"\n"
        "      }}\n"
        "    }}\n"
        "  }}\n"
        "}}",
        url
    );
    geode::utils::clipboard::write(cfg);
    PaimonNotify::show(tr("Stdio bridge config copied!", "Config Stdio Bridge copiada!"), NotificationIcon::Success);
    setStatus(tr("Copied Stdio Bridge config (also at GET /bridge.py).", "Config Stdio copiada (script en GET /bridge.py)."), kit::tint::cyan);
}

void EditorMcpPopup::runTestRender() {
    if (!LevelEditorLayer::get()) {
        setStatus(tr("Open a level in the editor first.", "Abre un nivel en el editor primero."), kit::tint::red);
        return;
    }

    RenderRequest req;
    req.mode = "1to1";
    req.width = 640;
    req.height = 360;
    req.previewColors = true;
    req.showGrid = false;

    auto res = renderEditor1To1(req);
    if (!res.ok || res.pngBytes.empty()) {
        setStatus(res.error.empty() ? "Render failed" : res.error, kit::tint::red);
        return;
    }
    EditorMcpServer::get().recordRenderServed();

    auto* img = new CCImage();
    if (!img->initWithImageData(const_cast<uint8_t*>(res.pngBytes.data()),
            static_cast<int>(res.pngBytes.size()), CCImage::kFmtPng)) {
        img->release();
        setStatus(tr("Failed to decode preview PNG.", "Error al decodificar PNG."), kit::tint::red);
        return;
    }
    auto* tex = new CCTexture2D();
    if (!tex->initWithImage(img)) {
        img->release();
        tex->release();
        setStatus(tr("Failed to upload preview texture.", "Error al subir textura."), kit::tint::red);
        return;
    }
    img->release();

    if (m_previewSprite) {
        m_previewSprite->removeFromParent();
        m_previewSprite = nullptr;
    }
    if (m_previewHint) m_previewHint->setVisible(false);

    m_previewSprite = CCSprite::createWithTexture(tex);
    tex->release();
    if (m_previewSprite && m_previewFrame) {
        auto const box = m_previewFrame->getContentSize();
        limitNodeSize(m_previewSprite, {box.width - 4.f, box.height - 4.f}, 4.f, 0.05f);
        m_previewSprite->setPosition(box / 2.f);
        m_previewFrame->addChild(m_previewSprite, 2);
    }

    refreshState();
    setStatus(
        fmt::format(
            "{} {}x{} ({} objs)",
            tr("1:1 render captured:", "Render 1:1 capturado:"),
            res.width, res.height, res.visibleObjectCount
        ),
        kit::tint::green
    );
}

void EditorMcpPopup::setStatus(std::string const& text, ccColor3B color) {
    if (!m_statusLabel) return;
    m_statusLabel->setString(text.c_str());
    m_statusLabel->setColor(color);
    m_statusLabel->limitLabelWidth(kPopupWidth - 62.f, 0.5f, 0.25f);
}

} // namespace paimon::editormcp
