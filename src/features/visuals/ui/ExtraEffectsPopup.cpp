#include "ExtraEffectsPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/InfoButton.hpp"

using namespace geode::prelude;
using namespace cocos2d;

bool ExtraEffectsPopup::init() {
    if (!Popup::init(240.f, 220.f)) return false;

    this->setTitle("Extra Effects");

    auto content = m_mainLayer->getContentSize();

    m_styles = {
        "normal", "pixel", "blur", "paimonblur", "grayscale", "sepia",
        "vignette", "scanlines", "bloom", "chromatic",
        "radial-blur", "glitch", "posterize",
        "rain", "matrix", "neon-pulse", "wave-distortion", "crt"
    };

    // leer extra styles guardados
    std::string raw = Mod::get()->getSettingValue<std::string>("levelinfo-extra-styles");
    if (!raw.empty()) {
        std::stringstream ss(raw);
        std::string token;
        while (std::getline(ss, token, ',') && (int)m_indices.size() < MAX_EXTRA) {
            size_t a = token.find_first_not_of(" \t");
            size_t b = token.find_last_not_of(" \t");
            if (a != std::string::npos) {
                std::string name = token.substr(a, b - a + 1);
                for (int i = 0; i < (int)m_styles.size(); i++) {
                    if (m_styles[i] == name) { m_indices.push_back(i); break; }
                }
            }
        }
    }

    m_rowContainer = CCNode::create();
    m_rowContainer->setPosition({0, 0});
    m_mainLayer->addChild(m_rowContainer, 10);

    m_rowMenu = CCMenu::create();
    m_rowMenu->setPosition({0, 0});
    m_mainLayer->addChild(m_rowMenu, 11);

    rebuildRows();

    paimon::markDynamicPopup(this);
    return true;
}

void ExtraEffectsPopup::rebuildRows() {
    m_rowContainer->removeAllChildren();
    m_rowMenu->removeAllChildren();
    m_labels.clear();

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;
    float topY = content.height - 52.f;
    float rowH = 28.f;

    // info label
    auto info = CCLabelBMFont::create("Combine up to 4 extra effects", "chatFont.fnt");
    info->setScale(0.55f);
    info->setColor({200, 200, 200});
    info->setPosition({cx, topY});
    m_rowContainer->addChild(info);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Extra Effects",
            "Stack up to <cy>4 additional</c> visual effects on top of\n"
            "the main background style.\n\n"
            "Each slot applies its effect in order.\n"
            "Use the arrows to cycle effects, and <cr>X</c> to remove.\n"
            "Effects combine for unique visual results!", this, 0.3f);
        if (iBtn) {
            iBtn->setPosition({cx + 95.f, topY});
            m_rowMenu->addChild(iBtn);
        }
    }

    float baseY = topY - 28.f;

    for (int i = 0; i < (int)m_indices.size(); i++) {
        float y = baseY - i * rowH;

        // slot number
        auto numLabel = CCLabelBMFont::create(fmt::format("{}.", i + 1).c_str(), "bigFont.fnt");
        numLabel->setScale(0.3f);
        numLabel->setColor({180, 180, 180});
        numLabel->setPosition({cx - 95.f, y});
        m_rowContainer->addChild(numLabel);

        // left arrow
        auto lSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        lSpr->setScale(0.35f);
        auto lBtn = CCMenuItemExt::createSpriteExtra(lSpr, [this, i](CCMenuItemSpriteExtra*) {
            if (i < 0 || i >= (int)m_indices.size()) return;
            m_indices[i]--;
            if (m_indices[i] < 0) m_indices[i] = (int)m_styles.size() - 1;
            if (i < (int)m_labels.size() && m_labels[i])
                m_labels[i]->setString(displayName(m_styles[m_indices[i]]).c_str());
            save();
        });
        lBtn->setPosition({cx - 65.f, y});
        m_rowMenu->addChild(lBtn);

        // right arrow
        auto rSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        rSpr->setFlipX(true);
        rSpr->setScale(0.35f);
        auto rBtn = CCMenuItemExt::createSpriteExtra(rSpr, [this, i](CCMenuItemSpriteExtra*) {
            if (i < 0 || i >= (int)m_indices.size()) return;
            m_indices[i]++;
            if (m_indices[i] >= (int)m_styles.size()) m_indices[i] = 0;
            if (i < (int)m_labels.size() && m_labels[i])
                m_labels[i]->setString(displayName(m_styles[m_indices[i]]).c_str());
            save();
        });
        rBtn->setPosition({cx + 65.f, y});
        m_rowMenu->addChild(rBtn);

        // name label
        auto label = CCLabelBMFont::create(
            displayName(m_styles[m_indices[i]]).c_str(), "bigFont.fnt");
        label->setScale(0.35f);
        label->setPosition({cx, y});
        m_rowContainer->addChild(label);
        m_labels.push_back(label);

        // X remove button
        auto xSpr = CCLabelBMFont::create("X", "bigFont.fnt");
        xSpr->setScale(0.55f);
        xSpr->setColor({255, 80, 80});
        auto xBtn = CCMenuItemExt::createSpriteExtra(xSpr, [this, i](CCMenuItemSpriteExtra*) {
            if (i >= 0 && i < (int)m_indices.size()) {
                m_indices.erase(m_indices.begin() + i);
                rebuildRows();
                save();
            }
        });
        xBtn->setPosition({cx + 100.f, y});
        m_rowMenu->addChild(xBtn);
    }

    // add button
    if ((int)m_indices.size() < MAX_EXTRA) {
        float addY = baseY - (int)m_indices.size() * rowH;
        auto addSpr = ButtonSprite::create("+ Add Effect", "bigFont.fnt", "GJ_button_01.png", 0.7f);
        addSpr->setScale(0.6f);
        auto addBtn = CCMenuItemExt::createSpriteExtra(addSpr, [this](CCMenuItemSpriteExtra*) {
            if ((int)m_indices.size() >= MAX_EXTRA) return;
            std::string primary = Mod::get()->getSettingValue<std::string>("levelinfo-background-style");
            int idx = 0;
            for (int j = 0; j < (int)m_styles.size(); j++) {
                if (m_styles[j] != primary && m_styles[j] != "normal") { idx = j; break; }
            }
            m_indices.push_back(idx);
            rebuildRows();
            save();
        });
        addBtn->setID("add-effect-btn"_spr);
        addBtn->setPosition({cx, addY});
        m_rowMenu->addChild(addBtn);
    }

    // empty state hint
    if (m_indices.empty()) {
        auto hint = CCLabelBMFont::create("No extra effects added", "chatFont.fnt");
        hint->setScale(0.5f);
        hint->setColor({150, 150, 150});
        hint->setPosition({cx, baseY});
        m_rowContainer->addChild(hint);
    }
}

void ExtraEffectsPopup::save() {
    std::string str;
    for (int i = 0; i < (int)m_indices.size(); i++) {
        if (i > 0) str += ",";
        str += m_styles[m_indices[i]];
    }
    Mod::get()->setSettingValue<std::string>("levelinfo-extra-styles", str);
    if (m_onChanged) m_onChanged();
}

std::string ExtraEffectsPopup::displayName(std::string const& s) {
    if (s == "normal") return "Normal";
    if (s == "pixel") return "Pixel";
    if (s == "blur") return "Blur";
    if (s == "grayscale") return "Grayscale";
    if (s == "sepia") return "Sepia";
    if (s == "vignette") return "Vignette";
    if (s == "scanlines") return "Scanlines";
    if (s == "bloom") return "Bloom";
    if (s == "chromatic") return "Chromatic";
    if (s == "radial-blur") return "Radial Blur";
    if (s == "glitch") return "Glitch";
    if (s == "posterize") return "Posterize";
    if (s == "rain") return "Rain";
    if (s == "matrix") return "Matrix";
    if (s == "neon-pulse") return "Neon Pulse";
    if (s == "wave-distortion") return "Wave";
    if (s == "crt") return "CRT";
    return s;
}

ExtraEffectsPopup* ExtraEffectsPopup::create() {
    auto ret = new ExtraEffectsPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}
