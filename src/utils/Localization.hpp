#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <Geode/Geode.hpp>

class Localization {
public:
    enum class Language {
        SPANISH,
        ENGLISH
    };

    static Localization& get() {
        static Localization instance;
        return instance;
    }

    static std::vector<Language> getAvailableLanguages() {
        return {
            Language::SPANISH,
            Language::ENGLISH,
        };
    }

    static std::string languageToId(Language lang) {
        switch (lang) {
            case Language::SPANISH: return "spanish";
            case Language::ENGLISH: return "english";
        }
        return "english";
    }

    static Language languageFromId(std::string const& id) {
        if (id == "spanish") return Language::SPANISH;
        return Language::ENGLISH;
    }

    static std::string languageDisplayName(Language lang) {
        switch (lang) {
            case Language::SPANISH: return "Espanol";
            case Language::ENGLISH: return "English";
        }
        return "English";
    }

    void setLanguage(Language lang, bool persist = true) {
        m_currentLanguage = lang;
        if (!persist) return;

        auto id = languageToId(lang);
        geode::Mod::get()->setSavedValue("language", id);
        geode::Mod::get()->setSettingValue<std::string>("language", id);
    }

    Language getLanguage() const {
        return m_currentLanguage;
    }

    std::string getCurrentLanguageId() const {
        return languageToId(m_currentLanguage);
    }

    std::string getCurrentLanguageDisplayName() const {
        return languageDisplayName(m_currentLanguage);
    }

    std::string getString(std::string const& key) const {
        auto const& translations = translationsFor(m_currentLanguage);
        auto it = translations.find(key);
        if (it != translations.end()) {
            return it->second;
        }
        auto enIt = m_english.find(key);
        if (enIt != m_english.end()) {
            return enIt->second;
        }
        return key; // Fallback to key if not found
    }

    void loadFromSettings() {
        std::string langStr = geode::Mod::get()->getSavedValue<std::string>("language", "");
        if (langStr.empty()) {
            langStr = geode::Mod::get()->getSettingValue<std::string>("language");
        }
        if (langStr.empty()) {
            langStr = "english";
        }

        m_currentLanguage = languageFromId(langStr);
    }

private:
    Localization() {
        loadFromSettings();
        initTranslations();
    }

    std::unordered_map<std::string, std::string> const& translationsFor(Language lang) const {
        switch (lang) {
            case Language::SPANISH: return m_spanish;
            case Language::ENGLISH: return m_english;
        }
        return m_english;
    }

    void initTranslations();

    Language m_currentLanguage = Language::SPANISH;
    std::unordered_map<std::string, std::string> m_spanish;
    std::unordered_map<std::string, std::string> m_english;
};

