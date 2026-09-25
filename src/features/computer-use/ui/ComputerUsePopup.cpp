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
    if (who == "sys") return "-- ";
    return "";
}

// Parte el texto en lineas que caben en el scroll.
std::vector<std::string> wrap(std::string const& text, std::size_t width) {
    std::vector<std::string> lines;
    std::string current;
    std::size_t start = 0;
    auto flush = [&] {
        if (!current.empty()) lines.push_back(current);
        current.clear();
    };
    while (start < text.size()) {
        auto end = text.find(' ', start);
        if (end == std::string::npos) end = text.size();
        auto word = text.substr(start, end - start);
        if (word.size() > width) {
            flush();
            for (std::size_t i = 0; i < word.size(); i += width) {
                lines.push_back(word.substr(i, width));
            }
        } else if (current.size() + 1 + word.size() > width) {
            flush();
            current = word;
        } else {
            if (!current.empty()) current += ' ';
            current += word;
        }
        start = end + 1;
    }
    flush();
    if (lines.empty()) lines.emplace_back("");
    return lines;
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
    m_status->setPosition({kPopupW / 2.f, 80.f});
    m_mainLayer->addChild(m_status);

    addLine("sys", "Modo: la IA propone y tu apruebas. El modo libre llega con el servidor.");
    setStatus("Escribe y pulsa Enviar.");
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

void ComputerUsePopup::onApprove(CCObject* sender) {
    auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!item || item->getTag() <= 0) return;
    auto id = static_cast<unsigned long>(item->getTag());
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

void ComputerUsePopup::onReject(CCObject* sender) {
    auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!item || item->getTag() <= 0) return;
    auto id = static_cast<unsigned long>(item->getTag());
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
            addLine("ia", "Propuesta: " + task.proposal, task.id);
            setStatus("La IA propone: aprueba o rechaza.");
        } else if (task.status == "approved") {
            addLine("sys", "Aprobada, ejecutando...");
        } else if (task.status == "done") {
            addLine("ia", task.result.empty() ? "Hecho." : task.result);
            setStatus("Escribe y pulsa Enviar.");
        } else if (task.status == "rejected") {
            addLine("sys", "Propuesta rechazada.");
            setStatus("Escribe y pulsa Enviar.");
        }
    }
}

void ComputerUsePopup::addLine(std::string who, std::string text, unsigned long task) {
    if (text.size() > 440) text = text.substr(0, 440) + "...";
    m_lines.push_back({std::move(who), std::move(text), task});
    if (m_lines.size() > 50) m_lines.erase(m_lines.begin());
    rebuildMessages();
}

void ComputerUsePopup::rebuildMessages() {
    m_list->removeAllChildren();
    struct Row {
        std::string who;
        std::string text;
        unsigned long task = 0;
        bool proposal = false;
    };
    std::vector<Row> rows;
    for (std::size_t i = 0; i < m_lines.size(); ++i) {
        auto const& line = m_lines[m_lines.size() - 1 - i];
        bool first = true;
        for (auto& chunk : wrap(line.text, 64)) {
            Row row;
            row.who = first ? line.who : "";
            row.text = chunk;
            row.task = first ? line.task : 0;
            row.proposal = first && line.task != 0 && m_known[line.task] == "proposed";
            rows.push_back(std::move(row));
            first = false;
        }
    }
    float totalH = 8.f;
    for (auto const& row : rows) totalH += row.proposal ? kLineH + 26.f : kLineH;
    m_scroll->m_contentLayer->setContentSize({kScrollW, std::max(totalH, kScrollH)});
    float y = totalH - 12.f;
    for (auto const& row : rows) {
        auto* label = CCLabelBMFont::create((tag(row.who) + row.text).c_str(), "chatFont.fnt");
        label->setScale(0.5f);
        label->setAnchorPoint({0.f, 0.5f});
        if (row.who == "ia") label->setColor(ccc3(140, 220, 255));
        else if (row.who == "sys") label->setColor(ccc3(255, 220, 130));
        label->setPosition({6.f, y});
        m_list->addChild(label);
        y -= kLineH;
        if (!row.proposal) continue;
        auto* menu = CCMenu::create();
        menu->setPosition({330.f, y + 4.f});
        auto* ok = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Aprobar"), this, menu_selector(ComputerUsePopup::onApprove));
        ok->setScale(0.6f);
        ok->setPosition({-62.f, 0.f});
        ok->setTag(static_cast<int>(row.task));
        auto* no = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Rechazar"), this, menu_selector(ComputerUsePopup::onReject));
        no->setScale(0.6f);
        no->setPosition({62.f, 0.f});
        no->setTag(static_cast<int>(row.task));
        menu->addChild(ok);
        menu->addChild(no);
        m_list->addChild(menu);
        y -= 26.f;
    }
    m_scroll->scrollToTop();
}

void ComputerUsePopup::setStatus(std::string text) {
    m_status->setString(text.c_str());
}

} // namespace paimon::computeruse
