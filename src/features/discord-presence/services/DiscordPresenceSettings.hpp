#pragma once

#include "../model/PresenceCustomization.hpp"
#include <Geode/Geode.hpp>

namespace paimon::discord {
inline PresenceConfiguration readPresenceConfiguration() {
    PresenceConfiguration configuration;
    auto* mod = geode::Mod::get();
    for (std::size_t i = 0; i < kBooleanOptions.size(); ++i) {
        configuration.flags[i] = mod->getSavedValue<bool>(kBooleanOptions[i].key, kBooleanOptions[i].value);
        if (std::string_view(kBooleanOptions[i].key) == "discord-rpc-show-best" && !mod->hasSavedValue(kBooleanOptions[i].key)) {
            configuration.flags[i] = mod->getSavedValue<bool>("discord-rpc-show-progress", true);
        }
    }
    for (std::size_t i = 0; i < kTextOptions.size(); ++i) {
        configuration.texts[i] = mod->getSavedValue<std::string>(kTextOptions[i].key, kTextOptions[i].value);
        if (!validChoice(kTextOptions[i].key, configuration.texts[i])) configuration.texts[i] = kTextOptions[i].value;
    }
    return configuration;
}
inline void resetPresenceConfiguration() {
    auto* mod = geode::Mod::get();
    for (auto const& option : kBooleanOptions) mod->setSavedValue(option.key, option.value);
    for (auto const& option : kTextOptions) mod->setSavedValue(option.key, std::string(option.value));
}
}
