#pragma once

#include <Geode/loader/Mod.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>

namespace PaimonUtils {

    // verifica si el usuario es moderador verificado
    // lee moderator_verification.dat y comprueba que no hayan pasado mas de 30 dias
    // No usa try/catch — Geode v5 recomienda evitar excepciones C++ por ABI
    inline bool isUserModerator() {
        auto isFreshVerification = [](std::filesystem::path const& path) {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec) || ec) return false;

            std::ifstream file(path, std::ios::binary);
            if (!file) return false;

            time_t timestamp{};
            if (!file.read(reinterpret_cast<char*>(&timestamp), sizeof(timestamp))) return false;

            auto now = std::chrono::system_clock::now();
            auto fileTime = std::chrono::system_clock::from_time_t(timestamp);
            auto daysDiff = std::chrono::duration_cast<std::chrono::hours>(now - fileTime).count() / 24;
            return daysDiff < 30;
        };

        auto saveDir = geode::Mod::get()->getSaveDir();
        if (isFreshVerification(saveDir / "moderator_verification.dat")) return true;
        if (isFreshVerification(saveDir / "admin_verification.dat")) return true;

        return geode::Mod::get()->getSavedValue<bool>("is-verified-moderator", false) ||
               geode::Mod::get()->getSavedValue<bool>("is-verified-admin", false);
    }

} // namespace PaimonUtils

