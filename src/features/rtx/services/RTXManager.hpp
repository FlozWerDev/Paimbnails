#pragma once

// Shared RTX state: loads/saves config and gates postfx per scene (renderer polls each frame).

#include "RTXConfig.hpp"

#include <filesystem>

namespace paimon::rtx {

class RTXManager {
public:
    static RTXManager& get();

    RTXConfig& config() { return m_config; }
    RTXConfig const& config() const { return m_config; }

    void init();
    void loadConfig();
    void saveConfig();
    void resetToDefaults();

    // Own switch, ignoring module and scene.
    bool isEnabled() const;
    void setEnabled(bool enabled);

    // What the renderer asks: module + switch + scene scope.
    bool shouldRender() const;

private:
    RTXManager() = default;
    RTXManager(RTXManager const&) = delete;
    RTXManager& operator=(RTXManager const&) = delete;

    std::filesystem::path configPath() const;
    void sanitize();

    RTXConfig m_config;
    bool m_loaded = false;
};

} // namespace paimon::rtx
