#pragma once
#include "../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>

class PaimonInfoPopup : public PaimonPopup {
protected:
    std::string m_infoTitle;
    std::string m_infoDesc;

    bool init(std::string const& title, std::string const& desc);

public:
    static PaimonInfoPopup* create(std::string const& title, std::string const& desc);
};
