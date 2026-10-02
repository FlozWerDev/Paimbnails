#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>

class SameAsPickerPopup : public PaimonPopup {
protected:
    std::string m_selectedLayerKey;
    geode::CopyableFunction<void(std::string const&)> m_onPick;

    bool init(std::string const& currentKey, geode::CopyableFunction<void(std::string const&)> onPick);

public:
    static SameAsPickerPopup* create(std::string const& currentKey, geode::CopyableFunction<void(std::string const&)> onPick);
};
