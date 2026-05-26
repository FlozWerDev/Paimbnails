#pragma once
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/Mod.hpp>
#include <unordered_set>

// Helper to mark mod buttons
class PaimonButtonHighlighter {
    static auto& registry() {
        static std::unordered_set<CCMenuItemSpriteExtra*> s_registry;
        return s_registry;
    }

public:
    // Register a mod button
    static void registerButton(CCMenuItemSpriteExtra* btn) {
        if (!btn) return;

        registry().insert(btn);
    }
    
    // Check if a button is registered
    static bool isRegisteredButton(CCMenuItemSpriteExtra* btn) {
        if (!btn) return false;
        if (registry().find(btn) != registry().end()) return true;

        // Compat con versiones anteriores que marcaban el boton mutando el ID.
        std::string id = btn->getID();
        return id.find("paimon-mod-btn") == 0;
    }
};
