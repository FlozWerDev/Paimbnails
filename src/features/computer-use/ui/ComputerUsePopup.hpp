#pragma once

// Chat de mando del agente: el usuario escribe que hacer, la IA externa
// (conectada por MCP) propone, el panel aprueba y la IA ejecuta.

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "../McpBridge.hpp"

namespace paimon::computeruse {

struct ChatLine {
    std::string who; // "tu", "ia", "sys"
    std::string text;
};

class ComputerUsePopup : public geode::Popup {
public:
    static ComputerUsePopup* create();

private:
    bool init() override;
    void onExit() override;
    void update(float dt) override;
    void onSend(cocos2d::CCObject*);
    void onApprove(cocos2d::CCObject*);
    void onReject(cocos2d::CCObject*);

    void addLine(std::string who, std::string text);
    void rebuildMessages();
    void rebuildActions();
    void setStatus(std::string text);
    void poll();
    void applyTasks(std::vector<InboxTask> const& tasks);
    std::string editorContext();

    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCNode* m_list = nullptr;
    geode::TextInput* m_input = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCMenu* m_actions = nullptr;

    std::vector<ChatLine> m_lines;
    std::map<unsigned long, std::string> m_known;
    std::set<unsigned long> m_echoed;
    unsigned long m_pending = 0;
    float m_pollTimer = 0.f;
    bool m_fetching = false;
    bool m_warnedOffline = false;
};

} // namespace paimon::computeruse
