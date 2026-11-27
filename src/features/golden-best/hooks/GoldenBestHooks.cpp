#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <cstring>

using namespace geode::prelude;

namespace {

constexpr char const* kModuleId = "paimbnails.goldenbest.gameplay";

// frames between label searches while none has been found yet.
constexpr int kLabelSearchInterval = 30;

bool moduleEnabled() {
    return paimon::modules::isEnabled(kModuleId);
}

} // namespace

class $modify(PaimonGoldenBestPlayLayer, PlayLayer) {
    struct Fields {
        WeakRef<CCLabelBMFont> m_label;
        float m_oldScale = 1.0F;
        float m_oldAnchorY = -1.0F;
        std::string m_oldFont;
        int   m_labelSearchCooldown = 0;
        bool m_initialized = false;
        bool m_goldApplied = false;
        bool m_customColor = false;
        bool m_showInPractice = false;
        bool m_showInTestMode = false;
        bool m_showInPlatformer = false;
        ccColor3B m_color;
    };

    void restoreLabel(CCLabelBMFont* label) {
        if (!label) return;
        if (m_fields->m_customColor) {
            label->setColor(ccWHITE);
        } else {
            label->setFntFile(m_fields->m_oldFont.empty() ? "bigFont.fnt" : m_fields->m_oldFont.c_str());
            label->setScale(m_fields->m_oldScale);
            if (m_fields->m_oldAnchorY != -1.0F) {
                label->setAnchorPoint({label->getAnchorPoint().x, m_fields->m_oldAnchorY});
            }
        }
    }

    void goldifyLabel(CCLabelBMFont* label) {
        if (!label) return;
        if (m_fields->m_customColor) {
            label->setColor(m_fields->m_color);
        } else {
            label->setFntFile("goldFont.fnt");
            label->setScale(m_fields->m_oldScale + 0.15F);
            label->setAnchorPoint({label->getAnchorPoint().x, m_fields->m_oldAnchorY - 0.05F});
        }
    }

    bool applyGold(bool gold) {
        auto labelRef = m_fields->m_label.lock();
        if (!labelRef) {
            m_fields->m_initialized = false;
            m_fields->m_goldApplied = false;
            return false;
        }
        if (m_fields->m_goldApplied == gold) return true;
        if (gold) {
            goldifyLabel(labelRef.data());
        } else {
            restoreLabel(labelRef.data());
        }
        m_fields->m_goldApplied = gold;
        return true;
    }

    // levels with a hidden progress bar never produce a percentage label, so the
    // child scan would otherwise run on every frame for the whole attempt.
    bool ensureInitialized() {
        if (m_fields->m_initialized) {
            if (m_fields->m_label.lock()) return true;
            m_fields->m_initialized = false;
            m_fields->m_goldApplied = false;
        }
        if (--m_fields->m_labelSearchCooldown > 0) return false;
        m_fields->m_labelSearchCooldown = kLabelSearchInterval;

        CCLabelBMFont* found = nullptr;
        if (m_percentageLabel) {
            found = m_percentageLabel;
            m_fields->m_oldScale = m_percentageLabel->getScale();
        } else if (auto* children = this->getChildren()) {
            for (unsigned int i = 0; i < children->count(); i++) {
                auto obj = children->objectAtIndex(i);
                auto* label = typeinfo_cast<CCLabelBMFont*>(obj);
                auto* text = label ? label->getString() : nullptr;
                auto len = text ? std::strlen(text) : 0;
                if (len > 0 && std::strcmp(text + len - 1, "%") == 0) {
                    found = label;
                    m_fields->m_oldScale = label->getScale();
                    break;
                }
            }
        }
        if (!found) return false;
        m_fields->m_label = found;
        if (auto* fnt = found->getFntFile()) m_fields->m_oldFont = fnt;

        auto* mod = Mod::get();
        m_fields->m_customColor = mod->getSettingValue<bool>("golden-best-enable-colors");
        m_fields->m_color = mod->getSettingValue<ccColor3B>("golden-best-custom-color");
        m_fields->m_showInPractice = mod->getSettingValue<bool>("golden-best-practice-mode");
        m_fields->m_showInTestMode = mod->getSettingValue<bool>("golden-best-test-mode");
        m_fields->m_showInPlatformer = mod->getSettingValue<bool>("golden-best-platformer-mode");
        m_fields->m_initialized = true;
        return true;
    }

    void updateProgressbar() {
        PlayLayer::updateProgressbar();

        if (!moduleEnabled()) {
            applyGold(false);
            return;
        }
        if (!m_level || !ensureInitialized()) return;
        auto labelRef = m_fields->m_label.lock();
        if (!labelRef) return;

        if (m_fields->m_oldAnchorY == -1.0F) {
            m_fields->m_oldAnchorY = labelRef->getAnchorPoint().y;
        }
        if (!m_level->isPlatformer()) {
            if ((!m_fields->m_showInPractice && m_isPracticeMode) ||
                (!m_fields->m_showInTestMode && m_isTestMode)) {
                applyGold(false);
                return;
            }
            applyGold(static_cast<float>(this->getCurrentPercentInt()) > m_level->m_normalPercent.value());
        }
    }

    void updateTimeLabel(int p0, int p1, bool p2) {
        PlayLayer::updateTimeLabel(p0, p1, p2);
        if (!m_fields->m_initialized || !moduleEnabled() || !m_level ||
            !m_level->isPlatformer() || !m_fields->m_showInPlatformer) {
            return;
        }
        auto labelRef = m_fields->m_label.lock();
        if (!labelRef) return;
        if (m_fields->m_oldAnchorY == -1.0F) {
            m_fields->m_oldAnchorY = labelRef->getAnchorPoint().y;
        }
        if ((!m_fields->m_showInPractice && m_isPracticeMode) ||
            (!m_fields->m_showInTestMode && m_isTestMode)) {
            applyGold(false);
            return;
        }
        float actualTime = static_cast<float>(p0) + static_cast<float>(p1) / 100.0F;
        applyGold(m_level->m_bestTime == 0 || actualTime < static_cast<float>(m_level->m_bestTime) / 1000.0F);
    }
};
