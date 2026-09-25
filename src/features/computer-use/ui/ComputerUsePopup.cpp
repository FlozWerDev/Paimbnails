#include "ComputerUsePopup.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/ui/TextInput.hpp>

using namespace geode::prelude;

namespace paimon::computeruse {

namespace {

constexpr float kPopupW = 480.f;
constexpr float kPopupH = 360.f;
constexpr float kScrollX = 20.f;
constexpr float kScrollY = 104.f;
constexpr float kScrollW = 440.f;
constexpr float kScrollH = 212.f;
constexpr float kLineH = 20.f;

std::string tag(std::string const& who) {
    if (who == "tu") return "Tu: ";
    if (who == "ia") return "IA: ";
    return "-- ";
}

} // namespace

ComputerUsePopup* ComputerUsePopup::create() {
    auto* ret = new ComputerUsePopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ComputerUsePopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;

    setTitle("AI Chat");
    setID("computeruse-popup"_spr);

    m_scroll = geode::ScrollLayer::create({kScrollW, kScrollH});
    m_scroll->setPosition({kScrollX, kScrollY});
    m_scroll->setID("chat-scroll"_spr);
    m_mainLayer->addChild(m_scroll);
    m_list = CCNode::create();
    m_scroll->m_contentLayer->addChild(m_list);

    m_input = TextInput::create(320.f, "Dile a la IA que hacer...");
    m_input->setMaxCharCount(500);
    m_input->setPosition({180.f, 28.f});
    m_input->setID("chat-input"_spr);
    m_mainLayer->addChild(m_input);

    auto* sendMenu = CCMenu::create();
    sendMenu->setPosition({408.f, 28.f});
    m_mainLayer->addChild(sendMenu);
    sendMenu->addChild(CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Enviar"), this, menu_selector(ComputerUsePopup::onSend)));

    m_status = CCLabelBMFont::create("", "chatFont.fnt");
    m_status->setScale(0.4f);
    m_status->setAlignment(kCCTextAlignmentCenter);
    m_status->setPosition({kPopupW / 2.f, 62.f});
    m_mainLayer->addChild(m_status);

    m_actions = CCMenu::create();
    m_actions->setPosition({kPopupW / 2.f, 84.f});
    m_mainLayer->addChild(m_actions);

    addLine("sys", "Modo: la IA propone y tu apruebas. El modo libre llega con el servidor.");
    setStatus("Escribe y pulsa Enviar.");
    rebuildActions();
    scheduleUpdate();
    poll();
    return true;
}

void ComputerUsePopup::onExit() {
    unscheduleUpdate();
    Popup::onExit();
}

void ComputerUsePopup::update(float dt) {
    m_pollTimer += dt;
    if (m_pollTimer < 2.5f || m_fetching) return;
    m_pollTimer = 0.f;
    poll();
}

std::string ComputerUsePopup::editorContext() {
    std::string level = "?";
    int selected = 0;
    if (auto* editor = LevelEditorLayer::get()) {
        if (editor->m_level) level = std::string(editor->m_level->m_levelName.c_str());
    }
    if (auto* ui = EditorUI::get()) {
        if (auto* objects = ui->getSelectedObjects()) selected = objects->count();
    }
    return "level=" + level + " selected=" + std::to_string(selected);
}

void ComputerUsePopup::onSend(CCObject*) {
    std::string text(m_input->getString().c_str());
    if (text.empty()) return;
    m_input->setString("");
    setStatus("Enviando...");
    WeakRef<ComputerUsePopup> weak = this;
    McpBridge::get().pushTask(text, editorContext(),
        [weak, text](bool ok, unsigned long id, std::string error) {
            auto self = weak.lock();
            if (!self) return;
            if (!ok) {
                self->addLine("sys", error + " Activa el mod GD MCP y allow-editor-writes.");
                self->setStatus("Sin conexion.");
                return;
            }
            self->m_echoed.insert(id);
            self->m_known[id] = "open";
            self->addLine("tu", text);
            self->setStatus("Enviado. La IA lo esta mirando.");
            self->m_pollTimer = 2.f;
        }
    );
}

void ComputerUsePopup::onApprove(CCObject*) {
    if (!m_pending) return;
    auto id = m_pending;
    m_pending = 0;
    rebuildActions();
    setStatus("Aprobada, ejecutando...");
    WeakRef<ComputerUsePopup> weak = this;
    McpBridge::get().decideTask(id, true,
        [weak](bool ok, std::string error) {
            auto self = weak.lock();
            if (!self) return;
            if (!ok) self->addLine("sys", error);
        }
    );
}

void ComputerUsePopup::onReject(CCObject*) {
    if (!m_pending) return;
    auto id = m_pending;
    m_pending = 0;
    rebuildActions();
    WeakRef<ComputerUsePopup> weak = this;
    McpBridge::get().decideTask(id, false,
        [weak](bool ok, std::string error) {
            auto self = weak.lock();
            if (!self) return;
            if (!ok) self->addLine("sys", error);
            else self->addLine("sys", "Propuesta rechazada.");
        }
    );
}

void ComputerUsePopup::poll() {
    if (m_fetching) return;
    m_fetching = true;
    WeakRef<ComputerUsePopup> weak = this;
    McpBridge::get().fetchTasks(
        [weak](bool ok, std::vector<InboxTask> tasks, std::string error) {
            auto self = weak.lock();
            if (!self) return;
            self->m_fetching = false;
            if (!ok) {
                if (!self->m_warnedOffline) {
                    self->m_warnedOffline = true;
                    self->addLine("sys", error + " Activa el mod GD MCP y allow-editor-writes.");
                }
                self->setStatus("Sin conexion.");
                return;
            }
            self->m_warnedOffline = false;
            self->applyTasks(tasks);
        }
    );
}

void ComputerUsePopup::applyTasks(std::vector<InboxTask> const& tasks) {
    for (auto const& task : tasks) {
        auto known = m_known.find(task.id);
        std::string prev = known == m_known.end() ? "" : known->second;
        if (prev == task.status) continue;
        m_known[task.id] = task.status;
        if (prev.empty() && task.status == "open" && !m_echoed.count(task.id)) {
            addLine("tu", task.text);
        } else if (task.status == "proposed") {
            addLine("ia", "Propuesta: " + task.proposal);
            m_pending = task.id;
            setStatus("La IA propone: aprueba o rechaza.");
        } else if (task.status == "approved" && prev == "proposed") {
            addLine("sys", "Aprobada, ejecutando...");
        } else if (task.status == "done") {
            if (m_pending == task.id) m_pending = 0;
            addLine("ia", task.result.empty() ? "Hecho." : task.result);
            setStatus("Escribe y pulsa Enviar.");
        } else if (task.status == "rejected") {
            if (m_pending == task.id) m_pending = 0;
            addLine("sys", "Propuesta rechazada.");
            setStatus("Escribe y pulsa Enviar.");
        }
    }
    rebuildActions();
}

void ComputerUsePopup::addLine(std::string who, std::string text) {
    if (text.size() > 220) text = text.substr(0, 220) + "...";
    m_lines.push_back({std::move(who), std::move(text)});
    if (m_lines.size() > 50) m_lines.erase(m_lines.begin());
    rebuildMessages();
}

void ComputerUsePopup::rebuildMessages() {
    m_list->removeAllChildren();
    float totalH = static_cast<float>(m_lines.size()) * kLineH + 8.f;
    float viewH = kScrollH;
    m_scroll->m_contentLayer->setContentSize({kScrollW, std::max(totalH, viewH)});
    // Lo nuevo arriba: el ScrollLayer abre en top sin pelear el scroll.
    for (std::size_t i = 0; i < m_lines.size(); ++i) {
        auto const& line = m_lines[m_lines.size() - 1 - i];
        auto* label = CCLabelBMFont::create((tag(line.who) + line.text).c_str(), "chatFont.fnt");
        label->setScale(0.5f);
        label->setAnchorPoint({0.f, 0.5f});
        if (line.who == "ia") label->setColor(ccc3(140, 220, 255));
        else if (line.who == "sys") label->setColor(ccc3(255, 220, 130));
        label->setPosition({6.f, totalH - 12.f - static_cast<float>(i) * kLineH});
        m_list->addChild(label);
    }
    m_scroll->scrollToTop();
}

void ComputerUsePopup::rebuildActions() {
    m_actions->removeAllChildren();
    if (!m_pending) return;
    auto* ok = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Aprobar"), this, menu_selector(ComputerUsePopup::onApprove));
    ok->setPosition({-70.f, 0.f});
    auto* no = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Rechazar"), this, menu_selector(ComputerUsePopup::onReject));
    no->setPosition({70.f, 0.f});
    m_actions->addChild(ok);
    m_actions->addChild(no);
}

void ComputerUsePopup::setStatus(std::string text) {
    m_status->setString(text.c_str());
}

} // namespace paimon::computeruse
