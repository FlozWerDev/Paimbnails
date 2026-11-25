#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include "../data/ShopPreset.hpp"

namespace paimon::shop {
class ShopPresetPopup : public PaimonPopup {
public:
    static ShopPresetPopup* create(std::string id, Preset preset, std::function<void()> onChanged);
protected:
    bool init(std::string id, Preset preset, std::function<void()> onChanged);
private:
    std::string m_id;
    Preset m_preset;
    std::function<void()> m_onChanged;
    void apply();
    void exportPreset();
    void deletePreset();
};
} // namespace paimon::shop
