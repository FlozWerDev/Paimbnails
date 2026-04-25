#pragma once
#include <Geode/Geode.hpp>

class PaimonInfoPopup : public geode::Popup {
protected:
    std::string m_infoTitle;
    std::string m_infoDesc;

    bool init(std::string const& title, std::string const& desc);
    void loadRandomThumbnailBg();

public:
    static PaimonInfoPopup* create(std::string const& title, std::string const& desc);
};

