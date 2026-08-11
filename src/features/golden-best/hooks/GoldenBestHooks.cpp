#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <cstring>

using namespace geode::prelude;

namespace {

constexpr char const* kModuleId = "paimbnails.goldenbest.gameplay";

bool moduleEnabled() {
    return paimon::modules::isEnabled(kModuleId);
}

bool showInPractice() {
    return Mod::get()->getSettingValue<bool>("golden-best-practice-mode");
}

bool showInTestMode() {
    return Mod::get()->getSettingValue<bool>("golden-best-test-mode");
}

bool showInPlatformer() {
    return Mod::get()->getSettingValue<bool>("golden-best-platformer-mode");
}

} // namespace

class $modify(PaimonGoldenBestPlayLayer, PlayLayer) {
    struct Fields {
        CCLabelBMFont* m_label = nullptr;
        float m_oldScale = 1.0F;
        float m_oldAnchorY = -1.0F;
        bool m_initialized = false;
        bool m_goldApplied = false;
        bool m_customColor = false;
        ccColor3B m_color;
    };

    void restoreLabel(CCLabelBMFont* label) {
        if (m_fields->m_customColor) {
            label->setColor(ccWHITE);
        } else {
            label->setFntFile("bigFont.fnt");
            label->setScale(m_fields->m_oldScale);
            if (m_fields->m_oldAnchorY != -1.0F) {
                label->setAnchorPoint({label->getAnchorPoint().x, m_fields->m_oldAnchorY});
            }
        }
    }

    void goldifyLabel(CCLabelBMFont* label) {
        if (m_fields->m_customColor) {
            label->setColor(m_fields->m_color);
        } else {
            label->setFntFile("goldFont.fnt");
            label->setScale(m_fields->m_oldScale + 0.15F);
            label->setAnchorPoint({label->getAnchorPoint().x, m_fields->m_oldAnchorY - 0.05F});
        }
    }

    bool applyGold(bool gold) {
        if (!m_fields->m_label) return false;
        if (m_fields->m_goldApplied == gold) return true;
        if (gold) {
            goldifyLabel(m_fields->m_label);
        } else {
            restoreLabel(m_fields->m_label);
        }
        m_fields->m_goldApplied = gold;
        return true;
    }

    void updateProgressbar() {
        PlayLayer::updateProgressbar();

        if (!m_fields->m_initialized) {
            m_fields->m_customColor = Mod::get()->getSettingValue<bool>("golden-best-enable-colors");
            m_fields->m_color = Mod::get()->getSettingValue<ccColor3B>("golden-best-custom-color");
        }
        if (!m_fields->m_initialized && m_percentageLabel) {
            m_fields->m_label = m_percentageLabel;
            m_fields->m_oldScale = m_percentageLabel->getScale();
            m_fields->m_initialized = true;
        } else if (!m_fields->m_initialized) {
            for (size_t i = 0; i < this->getChildrenCount(); i++) {
                auto obj = this->getChildren()->objectAtIndex(i);
                auto* label = typeinfo_cast<CCLabelBMFont*>(obj);
                auto* text = label ? label->getString() : nullptr;
                auto len = text ? std::strlen(text) : 0;
                if (len > 0 && std::strcmp(text + len - 1, "%") == 0) {
                    m_fields->m_label = label;
                    m_fields->m_oldScale = label->getScale();
                    m_fields->m_initialized = true;
                    break;
                }
            }
        }
        if (!m_fields->m_initialized) return;

        if (!moduleEnabled()) {
            applyGold(false);
            return;
        }

        if (m_fields->m_oldAnchorY == -1.0F) {
            m_fields->m_oldAnchorY = m_fields->m_label->getAnchorPoint().y;
        }
        if (!m_level->isPlatformer()) {
            if ((!showInPractice() && m_isPracticeMode) || (!showInTestMode() && m_isTestMode)) {
                applyGold(false);
                return;
            }
            applyGold(static_cast<float>(this->getCurrentPercentInt()) > m_level->m_normalPercent.value());
        }
    }

    void updateTimeLabel(int p0, int p1, bool p2) {
        PlayLayer::updateTimeLabel(p0, p1, p2);
        if (!m_fields->m_label || !moduleEnabled() || !m_level->isPlatformer() || !showInPlatformer()) {
            return;
        }
        if (m_fields->m_oldAnchorY == -1.0F) {
            m_fields->m_oldAnchorY = m_fields->m_label->getAnchorPoint().y;
        }
        if ((!showInPractice() && m_isPracticeMode) || (!showInTestMode() && m_isTestMode)) {
            applyGold(false);
            return;
        }
        float actualTime = static_cast<float>(p0) + static_cast<float>(p1) / 100.0F;
        applyGold(m_level->m_bestTime == 0 || actualTime < static_cast<float>(m_level->m_bestTime) / 1000.0F);
    }
};
