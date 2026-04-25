#pragma once

class PaimonMultiSettingsPanel;

class SettingsPanelManager {
public:
    static SettingsPanelManager& get() {
        static SettingsPanelManager instance;
        return instance;
    }

    void toggle();
    void close();
    bool isOpen() const { return m_panel != nullptr; }

    // llamado por el panel cuando termina la animacion de cierre
    void notifyPanelRemoved() { m_panel = nullptr; }

private:
    SettingsPanelManager() = default;
    PaimonMultiSettingsPanel* m_panel = nullptr;
};
