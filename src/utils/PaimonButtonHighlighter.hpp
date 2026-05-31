#pragma once
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/Mod.hpp>
#include <unordered_set>

// Helper to mark mod buttons
class PaimonButtonHighlighter {
    static std::string const& buttonFlag() {
        static const std::string flag = geode::Mod::get()->getID() + "/paimon-button";
        return flag;
    }

public:
    // Register a mod button
    static void registerButton(CCMenuItemSpriteExtra* btn) {
        if (!btn) return;

        btn->setUserFlag(buttonFlag(), true);
    }
    
    // Check if a button is registered
    static bool isRegisteredButton(CCMenuItemSpriteExtra* btn) {
        if (!btn) return false;
        if (btn->getUserFlag(buttonFlag())) return true;

        // Compat con versiones anteriores que marcaban el boton mutando el ID.
        std::string id = btn->getID();
        return id.find("paimon-mod-btn") == 0;
    }
};
