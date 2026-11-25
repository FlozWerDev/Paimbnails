#pragma once

#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include <string>
namespace paimon::translate {

// small language picker used by the Add Comment layer.
class TranslationTargetPopup : public PaimonPopup {
public:
    static TranslationTargetPopup* create(std::string title, std::string selected,
        geode::CopyableFunction<void(std::string const&)> onPick);

protected:
    bool init(std::string title, std::string selected,
        geode::CopyableFunction<void(std::string const&)> onPick);

    geode::CopyableFunction<void(std::string const&)> m_onPick;
};

}
