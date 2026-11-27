#include "PetEditorsPopup.hpp"

#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/ui/PopupManager.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

using namespace geode::prelude;
using namespace cocos2d;

namespace {

struct GdSfxPreset {
    char const* entry;
    char const* label;
};

constexpr std::array<GdSfxPreset, 12> kGdSfxPresets = {{
    {"gd:chestClick.ogg",     "Click suave (chestClick)"},
    {"gd:gold01.ogg",         "Moneda 1 (gold01)"},
    {"gd:gold02.ogg",         "Moneda 2 (gold02)"},
    {"gd:crystal01.ogg",      "Cristal (crystal01)"},
    {"gd:achievement_01.ogg", "Logro (achievement_01)"},
    {"gd:chestOpen01.ogg",    "Cofre (chestOpen01)"},
    {"gd:playSound_01.ogg",   "Inicio (playSound_01)"},
    {"gd:secretKey.ogg",      "Llave secreta (secretKey)"},
    {"gd:buyItem01.ogg",      "Compra (buyItem01)"},
    {"gd:door01.ogg",         "Puerta (door01)"},
    {"gd:quitSound_01.ogg",   "Salida (quitSound_01)"},
    {"gd:explode_11.ogg",     "Explosion (explode_11)"},
}};

std::string formatAudioDisplayName(std::string const& entry) {
    if (entry.empty()) return "(ninguno)";
    if (entry.rfind("gd:", 0) == 0) {
        return "GD: " + entry.substr(3);
    }
    return entry;
}

class PetGdSfxPickerPopup final : public PaimonPopup {
protected:
    std::function<void(std::string const&)> m_onPick;

    bool init(std::function<void(std::string const&)> onPick) {
        if (!PaimonPopup::init(310.f, 245.f)) return false;
        m_onPick = std::move(onPick);
        setTitle("Elegir SFX de GD");

        auto content = m_mainLayer->getContentSize();
        auto* scroll = ScrollLayer::create({content.width - 24.f, content.height - 62.f});
        scroll->setAnchorPoint({0.f, 0.f});
        scroll->setPosition({12.f, 14.f});
        m_mainLayer->addChild(scroll, 5);

        float const rowH = 26.f;
        float const totalH = std::max(scroll->getContentSize().height,
                                      static_cast<float>(kGdSfxPresets.size()) * rowH + 8.f);
        auto* sc = scroll->m_contentLayer;
        sc->setContentSize({scroll->getContentSize().width, totalH});

        float const w = scroll->getContentSize().width;
        float y = totalH - 16.f;
        for (auto const& preset : kGdSfxPresets) {
            auto* rowBg = paimon::ui::makeInset({w - 4.f, rowH - 3.f}, 55);
            rowBg->setPosition({2.f, y - (rowH - 3.f) * 0.5f});
            sc->addChild(rowBg);

            auto* lbl = CCLabelBMFont::create(preset.label, "chatFont.fnt");
            lbl->setAnchorPoint({0.f, 0.5f});
            lbl->limitLabelWidth(w - 118.f, 0.56f, 0.25f);
            lbl->setPosition({10.f, y});
            sc->addChild(lbl, 2);

            auto* menu = CCMenu::create();
            menu->setPosition({0.f, 0.f});
            sc->addChild(menu, 5);

            std::string entryStr(preset.entry);
            auto* testSpr = paimon::ui::makeButtonSprite("Oir", paimon::ui::Btn::Cyan, 38.f, 0.5f, "bigFont.fnt");
            auto* testBtn = CCMenuItemExt::createSpriteExtra(
                testSpr, [entryStr](CCMenuItemSpriteExtra*) {
                    PetManager::get().playAudioEntry(entryStr, false);
                });
            testBtn->setPosition({w - 76.f, y});
            menu->addChild(testBtn);

            auto* addSpr = paimon::ui::makeButtonSprite("Usar", paimon::ui::Btn::Green, 44.f, 0.5f, "bigFont.fnt");
            auto* addBtn = CCMenuItemExt::createSpriteExtra(
                addSpr, [this, entryStr](CCMenuItemSpriteExtra*) {
                    if (m_onPick) m_onPick(entryStr);
                    this->onClose(nullptr);
                });
            addBtn->setPosition({w - 28.f, y});
            menu->addChild(addBtn);

            y -= rowH;
        }

        scroll->scrollToTop();
        return true;
    }

public:
    static PetGdSfxPickerPopup* create(std::function<void(std::string const&)> onPick) {
        auto* ret = new PetGdSfxPickerPopup();
        if (ret && ret->init(std::move(onPick))) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

class PetDialogueEditPopup final : public PaimonPopup {
protected:
    PetDialogueEntry m_entry;
    std::function<void(PetDialogueEntry const&)> m_onSave;
    TextInput* m_textInput = nullptr;
    TextInput* m_weightInput = nullptr;
    CCLabelBMFont* m_audioLabel = nullptr;

    bool init(PetDialogueEntry initial, bool isNew, std::function<void(PetDialogueEntry const&)> onSave) {
        if (!PaimonPopup::init(330.f, 215.f)) return false;
        m_entry = std::move(initial);
        m_onSave = std::move(onSave);
        setTitle(isNew ? "Anadir dialogo" : "Editar dialogo");

        auto content = m_mainLayer->getContentSize();
        float const cx = content.width * 0.5f;

        auto* textLbl = CCLabelBMFont::create("Texto del dialogo:", "bigFont.fnt");
        textLbl->setAnchorPoint({0.f, 0.5f});
        textLbl->setScale(0.36f);
        textLbl->setPosition({20.f, 165.f});
        m_mainLayer->addChild(textLbl);

        m_textInput = TextInput::create(content.width - 40.f, "Escribe la frase...", "chatFont.fnt");
        m_textInput->setMaxCharCount(90);
        m_textInput->setString(m_entry.text);
        m_textInput->setPosition({cx, 140.f});
        m_mainLayer->addChild(m_textInput, 5);

        auto* weightLbl = CCLabelBMFont::create("Peso / Probabilidad (1-100):", "bigFont.fnt");
        weightLbl->setAnchorPoint({0.f, 0.5f});
        weightLbl->setScale(0.35f);
        weightLbl->setPosition({20.f, 108.f});
        m_mainLayer->addChild(weightLbl);

        m_weightInput = TextInput::create(74.f, "10", "bigFont.fnt");
        m_weightInput->setCommonFilter(CommonFilter::Uint);
        m_weightInput->setMaxCharCount(3);
        m_weightInput->setString(std::to_string(std::max(1, static_cast<int>(std::lround(m_entry.weight)))));
        m_weightInput->setPosition({content.width - 57.f, 108.f});
        m_mainLayer->addChild(m_weightInput, 5);

        auto* audioTitle = CCLabelBMFont::create("Audio opcional:", "bigFont.fnt");
        audioTitle->setAnchorPoint({0.f, 0.5f});
        audioTitle->setScale(0.35f);
        audioTitle->setPosition({20.f, 76.f});
        m_mainLayer->addChild(audioTitle);

        m_audioLabel = CCLabelBMFont::create(formatAudioDisplayName(m_entry.audioFile).c_str(), "chatFont.fnt");
        m_audioLabel->setAnchorPoint({0.f, 0.5f});
        m_audioLabel->setColor({255, 224, 150});
        m_audioLabel->limitLabelWidth(170.f, 0.52f, 0.2f);
        m_audioLabel->setPosition({20.f, 58.f});
        m_mainLayer->addChild(m_audioLabel);

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        m_mainLayer->addChild(menu, 10);

        auto* fileSpr = paimon::ui::makeButtonSprite("Archivo", paimon::ui::Btn::Cyan, 56.f, 0.5f, "bigFont.fnt");
        auto* fileBtn = CCMenuItemExt::createSpriteExtra(
            fileSpr, [this](CCMenuItemSpriteExtra*) {
                WeakRef<PetDialogueEditPopup> self = this;
                pt::pickAudio([self](geode::Result<std::optional<std::filesystem::path>> res) {
                    auto popup = self.lock();
                    if (!popup) return;
                    auto pathOpt = std::move(res).unwrapOr(std::nullopt);
                    if (!pathOpt || pathOpt->empty()) return;
                    auto added = PetManager::get().addAudioToLibrary(*pathOpt);
                    if (added.empty()) {
                        PaimonNotify::show("Formato de audio no compatible.", NotificationIcon::Error);
                        return;
                    }
                    auto* p = static_cast<PetDialogueEditPopup*>(popup.data());
                    p->m_entry.audioFile = added;
                    p->refreshAudioLabel();
                });
            });
        fileBtn->setPosition({205.f, 66.f});
        menu->addChild(fileBtn);

        auto* gdSpr = paimon::ui::makeButtonSprite("SFX", paimon::ui::Btn::Gray, 40.f, 0.5f, "bigFont.fnt");
        auto* gdBtn = CCMenuItemExt::createSpriteExtra(
            gdSpr, [this](CCMenuItemSpriteExtra*) {
                WeakRef<PetDialogueEditPopup> self = this;
                if (auto* picker = PetGdSfxPickerPopup::create([self](std::string const& chosen) {
                        if (auto popup = self.lock()) {
                            auto* p = static_cast<PetDialogueEditPopup*>(popup.data());
                            p->m_entry.audioFile = chosen;
                            p->refreshAudioLabel();
                        }
                    })) {
                    picker->show();
                }
            });
        gdBtn->setPosition({256.f, 66.f});
        menu->addChild(gdBtn);

        auto* clearSpr = paimon::ui::makeButtonSprite("X", paimon::ui::Btn::Red, 24.f, 0.5f, "bigFont.fnt");
        auto* clearBtn = CCMenuItemExt::createSpriteExtra(
            clearSpr, [this](CCMenuItemSpriteExtra*) {
                m_entry.audioFile.clear();
                refreshAudioLabel();
            });
        clearBtn->setPosition({293.f, 66.f});
        menu->addChild(clearBtn);

        auto* saveSpr = paimon::ui::makeButtonSprite("Guardar", paimon::ui::Btn::Green, 88.f, 0.65f, "bigFont.fnt");
        auto* saveBtn = CCMenuItemExt::createSpriteExtra(
            saveSpr, [this](CCMenuItemSpriteExtra*) {
                std::string text = m_textInput ? std::string(m_textInput->getString()) : "";
                while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
                while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
                if (text.empty()) {
                    PaimonNotify::show("Escribe un texto para el dialogo.", NotificationIcon::Warning);
                    return;
                }
                int weightInt = 10;
                if (m_weightInput) {
                    weightInt = geode::utils::numFromString<int>(m_weightInput->getString()).unwrapOr(10);
                }
                m_entry.text = std::move(text);
                m_entry.weight = static_cast<float>(std::clamp(weightInt, 1, 100));
                if (m_onSave) m_onSave(m_entry);
                this->onClose(nullptr);
            });
        saveBtn->setPosition({cx - 52.f, 24.f});
        menu->addChild(saveBtn);

        auto* cancelSpr = paimon::ui::makeButtonSprite("Cancelar", paimon::ui::Btn::Gray, 84.f, 0.65f, "bigFont.fnt");
        auto* cancelBtn = CCMenuItemExt::createSpriteExtra(
            cancelSpr, [this](CCMenuItemSpriteExtra*) { this->onClose(nullptr); });
        cancelBtn->setPosition({cx + 52.f, 24.f});
        menu->addChild(cancelBtn);

        return true;
    }

    void refreshAudioLabel() {
        if (!m_audioLabel) return;
        m_audioLabel->setString(formatAudioDisplayName(m_entry.audioFile).c_str());
        m_audioLabel->limitLabelWidth(170.f, 0.52f, 0.2f);
    }

public:
    static PetDialogueEditPopup* create(
        PetDialogueEntry initial,
        bool isNew,
        std::function<void(PetDialogueEntry const&)> onSave
    ) {
        auto* ret = new PetDialogueEditPopup();
        if (ret && ret->init(std::move(initial), isNew, std::move(onSave))) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

} // namespace

// ===================== PetDialoguesPopup =====================

PetDialoguesPopup* PetDialoguesPopup::create(PetDialogueCategory initialCategory) {
    auto* ret = new PetDialoguesPopup();
    if (ret && ret->init(initialCategory)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool PetDialoguesPopup::init(PetDialogueCategory initialCategory) {
    if (!PaimonPopup::init(430.f, 290.f)) return false;
    m_category = initialCategory;
    setTitle("Dialogos y Probabilidades");

    auto content = m_mainLayer->getContentSize();
    buildTabs();

    m_summaryLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_summaryLabel->setColor({255, 224, 150});
    m_summaryLabel->setPosition({content.width * 0.5f, content.height - 62.f});
    m_mainLayer->addChild(m_summaryLabel, 5);

    auto* listPanel = paimon::ui::makeInset({content.width - 24.f, content.height - 114.f});
    listPanel->setPosition({12.f, 40.f});
    m_mainLayer->addChild(listPanel, 2);

    m_scroll = ScrollLayer::create({content.width - 32.f, content.height - 122.f});
    m_scroll->setAnchorPoint({0.f, 0.f});
    m_scroll->setPosition({16.f, 44.f});
    m_mainLayer->addChild(m_scroll, 5);

    auto* footerMenu = CCMenu::create();
    footerMenu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(footerMenu, 10);

    float const cx = content.width * 0.5f;
    auto* addSpr = paimon::ui::makeButtonSprite("+ Anadir", paimon::ui::Btn::Green, 88.f, 0.6f, "bigFont.fnt");
    auto* addBtn = CCMenuItemExt::createSpriteExtra(
        addSpr, [this](CCMenuItemSpriteExtra*) { openEntryEditor(-1); });
    addBtn->setPosition({cx - 115.f, 21.f});
    footerMenu->addChild(addBtn);

    auto* testSpr = paimon::ui::makeButtonSprite("Probar", paimon::ui::Btn::Cyan, 76.f, 0.6f, "bigFont.fnt");
    auto* testBtn = CCMenuItemExt::createSpriteExtra(
        testSpr, [this](CCMenuItemSpriteExtra*) {
            auto& pet = PetManager::get();
            int idx = pet.showWeightedSpeech(m_category, nullptr, true);
            if (idx < 0) {
                PaimonNotify::show("No hay dialogos con peso > 0 en esta categoria.", NotificationIcon::Warning);
            } else {
                auto const& list = pet.config().dialoguesFor(m_category);
                if (idx < static_cast<int>(list.size())) {
                    PaimonNotify::show(fmt::format("Mascota: \"{}\"", list[static_cast<size_t>(idx)].text), NotificationIcon::Info);
                }
            }
        });
    testBtn->setPosition({cx, 21.f});
    footerMenu->addChild(testBtn);

    auto* resetSpr = paimon::ui::makeButtonSprite("Restaurar", paimon::ui::Btn::Red, 90.f, 0.6f, "bigFont.fnt");
    auto* resetBtn = CCMenuItemExt::createSpriteExtra(
        resetSpr, [this](CCMenuItemSpriteExtra*) {
            auto& pet = PetManager::get();
            pet.config().dialoguesFor(m_category) = makeDefaultPetDialogues(m_category);
            pet.saveConfig();
            rebuildList();
            PaimonNotify::show("Dialogos restaurados.", NotificationIcon::Info);
        });
    resetBtn->setPosition({cx + 115.f, 21.f});
    footerMenu->addChild(resetBtn);

    selectCategory(m_category);
    return true;
}

void PetDialoguesPopup::buildTabs() {
    auto content = m_mainLayer->getContentSize();
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 10);

    constexpr int kCount = static_cast<int>(PetDialogueCategory::Count);
    float const totalW = content.width - 24.f;
    float const tabW = (totalW - (kCount - 1) * 4.f) / static_cast<float>(kCount);
    float const y = content.height - 42.f;

    for (int i = 0; i < kCount; ++i) {
        auto cat = static_cast<PetDialogueCategory>(i);
        auto* spr = paimon::ui::makeButtonSprite(petDialogueCategoryName(cat), paimon::ui::Btn::Gray, tabW, 0.48f, "bigFont.fnt");
        auto* btn = CCMenuItemExt::createSpriteExtra(
            spr, [this, cat](CCMenuItemSpriteExtra*) { selectCategory(cat); });
        btn->setTag(i);
        btn->setPosition({12.f + tabW * 0.5f + i * (tabW + 4.f), y});
        menu->addChild(btn);
        m_tabButtons.push_back(btn);
    }
}

void PetDialoguesPopup::selectCategory(PetDialogueCategory cat) {
    m_category = cat;
    for (auto* btn : m_tabButtons) {
        bool selected = btn->getTag() == static_cast<int>(cat);
        paimon::ui::setButtonSkin(btn, selected ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }
    rebuildList();
}

void PetDialoguesPopup::rebuildList() {
    if (!m_scroll || !m_scroll->m_contentLayer) return;
    auto* sc = m_scroll->m_contentLayer;
    sc->removeAllChildren();

    auto& list = PetManager::get().config().dialoguesFor(m_category);
    float totalWeight = 0.f;
    for (auto const& e : list) totalWeight += std::max(0.f, e.weight);

    if (m_summaryLabel) {
        m_summaryLabel->setString(fmt::format(
            "{}: {} dialogos  |  Ajusta el peso para controlar la probabilidad de cada frase",
            petDialogueCategoryName(m_category), list.size()).c_str());
        m_summaryLabel->limitLabelWidth(m_mainLayer->getContentSize().width - 30.f, 0.45f, 0.2f);
    }

    float const w = m_scroll->getContentSize().width;
    float const h = m_scroll->getContentSize().height;
    float const rowH = 30.f;
    float const totalH = std::max(h, static_cast<float>(list.size()) * rowH + 8.f);
    sc->setContentSize({w, totalH});

    if (list.empty()) {
        auto* empty = CCLabelBMFont::create("No hay dialogos en esta categoria.\nUsa '+ Anadir' para crear uno.", "chatFont.fnt");
        empty->setAlignment(kCCTextAlignmentCenter);
        empty->setColor({255, 224, 160});
        empty->setScale(0.55f);
        empty->setPosition({w * 0.5f, h * 0.5f});
        sc->addChild(empty);
        m_scroll->scrollToTop();
        return;
    }

    float y = totalH - 18.f;
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        auto const& e = list[static_cast<size_t>(i)];
        float const pct = totalWeight > 0.f ? (std::max(0.f, e.weight) / totalWeight * 100.f) : 0.f;

        auto* rowBg = paimon::ui::makeInset({w - 4.f, rowH - 3.f}, 55);
        rowBg->setPosition({2.f, y - (rowH - 3.f) * 0.5f});
        sc->addChild(rowBg);

        std::string titleText = e.text;
        if (!e.audioFile.empty()) titleText += "  [SFX]";
        auto* txt = CCLabelBMFont::create(titleText.c_str(), "chatFont.fnt");
        txt->setAnchorPoint({0.f, 0.5f});
        txt->limitLabelWidth(w - 208.f, 0.56f, 0.22f);
        txt->setPosition({10.f, y});
        sc->addChild(txt, 2);

        auto* pctLbl = CCLabelBMFont::create(
            fmt::format("{:.0f}% (p:{:.0f})", pct, e.weight).c_str(), "bigFont.fnt");
        pctLbl->setColor({255, 225, 130});
        pctLbl->limitLabelWidth(58.f, 0.28f, 0.16f);
        pctLbl->setPosition({w - 160.f, y});
        sc->addChild(pctLbl, 2);

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        sc->addChild(menu, 5);

        auto* minusSpr = paimon::ui::makeButtonSprite("-", paimon::ui::Btn::Gray, 20.f, 0.5f, "bigFont.fnt");
        auto* minusBtn = CCMenuItemExt::createSpriteExtra(
            minusSpr, [this, i](CCMenuItemSpriteExtra*) {
                auto& l = PetManager::get().config().dialoguesFor(m_category);
                if (i < 0 || i >= static_cast<int>(l.size())) return;
                l[static_cast<size_t>(i)].weight = std::max(1.f, l[static_cast<size_t>(i)].weight - 5.f);
                PetManager::get().saveConfig();
                rebuildList();
            });
        minusBtn->setPosition({w - 118.f, y});
        menu->addChild(minusBtn);

        auto* plusSpr = paimon::ui::makeButtonSprite("+", paimon::ui::Btn::Gray, 20.f, 0.5f, "bigFont.fnt");
        auto* plusBtn = CCMenuItemExt::createSpriteExtra(
            plusSpr, [this, i](CCMenuItemSpriteExtra*) {
                auto& l = PetManager::get().config().dialoguesFor(m_category);
                if (i < 0 || i >= static_cast<int>(l.size())) return;
                l[static_cast<size_t>(i)].weight = std::min(100.f, l[static_cast<size_t>(i)].weight + 5.f);
                PetManager::get().saveConfig();
                rebuildList();
            });
        plusBtn->setPosition({w - 94.f, y});
        menu->addChild(plusBtn);

        auto* editSpr = paimon::ui::makeButtonSprite("Edit", paimon::ui::Btn::Cyan, 38.f, 0.45f, "bigFont.fnt");
        auto* editBtn = CCMenuItemExt::createSpriteExtra(
            editSpr, [this, i](CCMenuItemSpriteExtra*) { openEntryEditor(i); });
        editBtn->setPosition({w - 60.f, y});
        menu->addChild(editBtn);

        auto* delSpr = paimon::ui::makeButtonSprite("X", paimon::ui::Btn::Red, 22.f, 0.48f, "bigFont.fnt");
        auto* delBtn = CCMenuItemExt::createSpriteExtra(
            delSpr, [this, i](CCMenuItemSpriteExtra*) {
                auto& l = PetManager::get().config().dialoguesFor(m_category);
                if (i < 0 || i >= static_cast<int>(l.size())) return;
                l.erase(l.begin() + i);
                PetManager::get().saveConfig();
                rebuildList();
            });
        delBtn->setPosition({w - 24.f, y});
        menu->addChild(delBtn);

        y -= rowH;
    }

    m_scroll->scrollToTop();
}

void PetDialoguesPopup::openEntryEditor(int editIndex) {
    auto& list = PetManager::get().config().dialoguesFor(m_category);
    bool const isNew = editIndex < 0 || editIndex >= static_cast<int>(list.size());
    PetDialogueEntry initial = isNew ? PetDialogueEntry{"", 15.f, ""} : list[static_cast<size_t>(editIndex)];

    WeakRef<PetDialoguesPopup> self = this;
    if (auto* editor = PetDialogueEditPopup::create(initial, isNew,
            [self, editIndex, isNew](PetDialogueEntry const& saved) {
                auto popup = self.lock();
                if (!popup) return;
                auto* p = static_cast<PetDialoguesPopup*>(popup.data());
                auto& l = PetManager::get().config().dialoguesFor(p->m_category);
                if (isNew) {
                    l.push_back(saved);
                } else if (editIndex >= 0 && editIndex < static_cast<int>(l.size())) {
                    l[static_cast<size_t>(editIndex)] = saved;
                }
                PetManager::get().saveConfig();
                p->rebuildList();
            })) {
        editor->show();
    }
}

// ===================== PetAudioGesturesPopup =====================

PetAudioGesturesPopup* PetAudioGesturesPopup::create(int initialSlot) {
    auto* ret = new PetAudioGesturesPopup();
    if (ret && ret->init(initialSlot)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool PetAudioGesturesPopup::init(int initialSlot) {
    if (!PaimonPopup::init(440.f, 290.f)) return false;
    m_selectedSlot = std::clamp(initialSlot, 0, static_cast<int>(PetGesture::Count));
    setTitle("Audios de Click y Gestos");

    auto content = m_mainLayer->getContentSize();
    buildTabs();

    m_summaryLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_summaryLabel->setColor({255, 224, 150});
    m_summaryLabel->setPosition({content.width * 0.5f, content.height - 62.f});
    m_mainLayer->addChild(m_summaryLabel, 5);

    auto* listPanel = paimon::ui::makeInset({content.width - 24.f, content.height - 114.f});
    listPanel->setPosition({12.f, 40.f});
    m_mainLayer->addChild(listPanel, 2);

    m_scroll = ScrollLayer::create({content.width - 32.f, content.height - 122.f});
    m_scroll->setAnchorPoint({0.f, 0.f});
    m_scroll->setPosition({16.f, 44.f});
    m_mainLayer->addChild(m_scroll, 5);

    auto* footerMenu = CCMenu::create();
    footerMenu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(footerMenu, 10);

    float const cx = content.width * 0.5f;
    auto* addFileSpr = paimon::ui::makeButtonSprite("+ Archivo", paimon::ui::Btn::Green, 86.f, 0.56f, "bigFont.fnt");
    auto* addFileBtn = CCMenuItemExt::createSpriteExtra(
        addFileSpr, [this](CCMenuItemSpriteExtra*) {
            WeakRef<PetAudioGesturesPopup> self = this;
            pt::pickAudio([self](geode::Result<std::optional<std::filesystem::path>> res) {
                auto popup = self.lock();
                if (!popup) return;
                auto pathOpt = std::move(res).unwrapOr(std::nullopt);
                if (!pathOpt || pathOpt->empty()) return;
                auto added = PetManager::get().addAudioToLibrary(*pathOpt);
                if (added.empty()) {
                    PaimonNotify::show("Formato de audio no soportado.", NotificationIcon::Error);
                    return;
                }
                auto* p = static_cast<PetAudioGesturesPopup*>(popup.data());
                p->currentPool().push_back(added);
                PetManager::get().saveConfig();
                p->rebuildList();
                PaimonNotify::show("Audio anadido!", NotificationIcon::Success);
            });
        });
    addFileBtn->setPosition({cx - 152.f, 21.f});
    footerMenu->addChild(addFileBtn);

    auto* addGdSpr = paimon::ui::makeButtonSprite("+ SFX GD", paimon::ui::Btn::Cyan, 82.f, 0.56f, "bigFont.fnt");
    auto* addGdBtn = CCMenuItemExt::createSpriteExtra(
        addGdSpr, [this](CCMenuItemSpriteExtra*) { openGdSfxPicker(); });
    addGdBtn->setPosition({cx - 54.f, 21.f});
    footerMenu->addChild(addGdBtn);

    auto* testSpr = paimon::ui::makeButtonSprite("Probar", paimon::ui::Btn::Green, 74.f, 0.56f, "bigFont.fnt");
    auto* testBtn = CCMenuItemExt::createSpriteExtra(
        testSpr, [this](CCMenuItemSpriteExtra*) {
            auto& pet = PetManager::get();
            if (m_selectedSlot == 0) {
                if (!pet.playRandomFromPool(currentPool(), currentPoolKey())) {
                    PaimonNotify::show("No hay audios o el audio esta desactivado.", NotificationIcon::Warning);
                }
            } else {
                auto g = static_cast<PetGesture>(m_selectedSlot - 1);
                if (pet.config().enabled) {
                    pet.triggerGesture(g, pet.config().enableSpeech, true);
                } else {
                    pet.playRandomFromPool(currentPool(), currentPoolKey());
                }
            }
        });
    testBtn->setPosition({cx + 42.f, 21.f});
    footerMenu->addChild(testBtn);

    auto* resetSpr = paimon::ui::makeButtonSprite("Restaurar", paimon::ui::Btn::Red, 86.f, 0.56f, "bigFont.fnt");
    auto* resetBtn = CCMenuItemExt::createSpriteExtra(
        resetSpr, [this](CCMenuItemSpriteExtra*) {
            if (m_selectedSlot == 0) {
                currentPool() = {"gd:chestClick.ogg", "gd:gold01.ogg"};
            } else {
                auto g = static_cast<PetGesture>(m_selectedSlot - 1);
                currentPool() = makeDefaultGestureAudioPool(g);
            }
            PetManager::get().saveConfig();
            rebuildList();
            PaimonNotify::show("Audios restaurados.", NotificationIcon::Info);
        });
    resetBtn->setPosition({cx + 146.f, 21.f});
    footerMenu->addChild(resetBtn);

    selectSlot(m_selectedSlot);
    return true;
}

void PetAudioGesturesPopup::buildTabs() {
    auto content = m_mainLayer->getContentSize();
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 10);

    constexpr int kCount = 1 + static_cast<int>(PetGesture::Count);
    float const totalW = content.width - 20.f;
    float const tabW = (totalW - (kCount - 1) * 3.f) / static_cast<float>(kCount);
    float const y = content.height - 42.f;

    for (int i = 0; i < kCount; ++i) {
        char const* label = (i == 0) ? "Click" : petGestureName(static_cast<PetGesture>(i - 1));
        auto* spr = paimon::ui::makeButtonSprite(label, paimon::ui::Btn::Gray, tabW, 0.44f, "bigFont.fnt");
        auto* btn = CCMenuItemExt::createSpriteExtra(
            spr, [this, i](CCMenuItemSpriteExtra*) { selectSlot(i); });
        btn->setTag(i);
        btn->setPosition({10.f + tabW * 0.5f + i * (tabW + 3.f), y});
        menu->addChild(btn);
        m_tabButtons.push_back(btn);
    }
}

void PetAudioGesturesPopup::selectSlot(int slot) {
    m_selectedSlot = std::clamp(slot, 0, static_cast<int>(PetGesture::Count));
    for (auto* btn : m_tabButtons) {
        bool selected = btn->getTag() == m_selectedSlot;
        paimon::ui::setButtonSkin(btn, selected ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }
    rebuildList();
}

std::vector<std::string>& PetAudioGesturesPopup::currentPool() {
    auto& cfg = PetManager::get().config();
    if (m_selectedSlot == 0) return cfg.clickAudioPool;
    return cfg.audioPoolForGesture(static_cast<PetGesture>(m_selectedSlot - 1));
}

std::string PetAudioGesturesPopup::currentPoolKey() const {
    if (m_selectedSlot == 0) return "click";
    return std::string("gesture:") + petGestureKey(static_cast<PetGesture>(m_selectedSlot - 1));
}

void PetAudioGesturesPopup::rebuildList() {
    if (!m_scroll || !m_scroll->m_contentLayer) return;
    auto* sc = m_scroll->m_contentLayer;
    sc->removeAllChildren();

    auto& pool = currentPool();
    char const* slotName = (m_selectedSlot == 0)
        ? "Al hacer click"
        : petGestureName(static_cast<PetGesture>(m_selectedSlot - 1));
    if (m_summaryLabel) {
        m_summaryLabel->setString(fmt::format(
            "{}: {} audio(s)  |  Si hay varios, se elige uno al azar cada vez",
            slotName, pool.size()).c_str());
        m_summaryLabel->limitLabelWidth(m_mainLayer->getContentSize().width - 30.f, 0.46f, 0.2f);
    }

    float const w = m_scroll->getContentSize().width;
    float const h = m_scroll->getContentSize().height;
    float const rowH = 28.f;
    float const totalH = std::max(h, static_cast<float>(pool.size()) * rowH + 8.f);
    sc->setContentSize({w, totalH});

    if (pool.empty()) {
        auto* empty = CCLabelBMFont::create(
            "Sin audios asignados (silencioso).\nAnade archivos propios o SFX de GD abajo.",
            "chatFont.fnt");
        empty->setAlignment(kCCTextAlignmentCenter);
        empty->setColor({255, 224, 160});
        empty->setScale(0.55f);
        empty->setPosition({w * 0.5f, h * 0.5f});
        sc->addChild(empty);
        m_scroll->scrollToTop();
        return;
    }

    float y = totalH - 17.f;
    for (int i = 0; i < static_cast<int>(pool.size()); ++i) {
        std::string const entry = pool[static_cast<size_t>(i)];

        auto* rowBg = paimon::ui::makeInset({w - 4.f, rowH - 3.f}, 55);
        rowBg->setPosition({2.f, y - (rowH - 3.f) * 0.5f});
        sc->addChild(rowBg);

        auto* lbl = CCLabelBMFont::create(
            fmt::format("{}. {}", i + 1, formatAudioDisplayName(entry)).c_str(), "chatFont.fnt");
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->limitLabelWidth(w - 110.f, 0.58f, 0.22f);
        lbl->setPosition({10.f, y});
        sc->addChild(lbl, 2);

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        sc->addChild(menu, 5);

        auto* playSpr = paimon::ui::makeButtonSprite("Oir", paimon::ui::Btn::Cyan, 42.f, 0.48f, "bigFont.fnt");
        auto* playBtn = CCMenuItemExt::createSpriteExtra(
            playSpr, [entry](CCMenuItemSpriteExtra*) {
                PetManager::get().playAudioEntry(entry, false);
            });
        playBtn->setPosition({w - 64.f, y});
        menu->addChild(playBtn);

        auto* delSpr = paimon::ui::makeButtonSprite("X", paimon::ui::Btn::Red, 24.f, 0.48f, "bigFont.fnt");
        auto* delBtn = CCMenuItemExt::createSpriteExtra(
            delSpr, [this, i](CCMenuItemSpriteExtra*) {
                auto& p = currentPool();
                if (i < 0 || i >= static_cast<int>(p.size())) return;
                p.erase(p.begin() + i);
                PetManager::get().saveConfig();
                rebuildList();
            });
        delBtn->setPosition({w - 24.f, y});
        menu->addChild(delBtn);

        y -= rowH;
    }

    m_scroll->scrollToTop();
}

void PetAudioGesturesPopup::openGdSfxPicker() {
    WeakRef<PetAudioGesturesPopup> self = this;
    if (auto* picker = PetGdSfxPickerPopup::create([self](std::string const& chosen) {
            auto popup = self.lock();
            if (!popup) return;
            auto* p = static_cast<PetAudioGesturesPopup*>(popup.data());
            p->currentPool().push_back(chosen);
            PetManager::get().saveConfig();
            p->rebuildList();
            PaimonNotify::show("SFX de GD anadido!", NotificationIcon::Success);
        })) {
        picker->show();
    }
}
