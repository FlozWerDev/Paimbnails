#pragma once
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/Mod.hpp>
#include <unordered_set>

class PaimonButtonHighlighter {
    static std::string const& buttonFlag() {
        static const std::string flag = geode::Mod::get()->getID() + "/paimon-button";
        return flag;
    }

public:
    static void registerButton(CCMenuItemSpriteExtra* btn) {
        if (!btn) return;

        btn->setUserFlag(buttonFlag(), true);
    }
    
    static bool isRegisteredButton(CCMenuItemSpriteExtra* btn) {
        if (!btn) return false;
        if (btn->getUserFlag(buttonFlag())) return true;

        // Compat with older versions that marked the button by mutating the ID.
        std::string id = btn->getID();
        return id.find("paimon-mod-btn") == 0;
    }
};
