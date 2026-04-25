#pragma once
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/Mod.hpp>

// Helper to mark mod buttons
class PaimonButtonHighlighter {
public:
    // Register a mod button
    static void registerButton(CCMenuItemSpriteExtra* btn) {
        if (!btn) return;
        
        // Keep original ID (prefix it)
        std::string currentID = btn->getID();
        // Avoid double-prefixing
        if (currentID.find("paimon-mod-btn"_spr) != 0) {
            std::string newID = currentID.empty() ? "paimon-mod-btn"_spr : ("paimon-mod-btn-"_spr + currentID);
            btn->setID(newID);
        }
    }
    
    // Check if a button is registered
    static bool isRegisteredButton(CCMenuItemSpriteExtra* btn) {
        if (!btn) return false;
        // ID-based so it survives remove/re-add.
        std::string id = btn->getID();
        return id.find("paimon-mod-btn") == 0;
    }
};
