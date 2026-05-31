#pragma once
//
// ProjectEditorLayer.hpp - Edits a single slot: lets the user tweak the
// colors, brightness and toggles, then triggers a pack export. Minimal
// viable surface for the MVP — sprite-by-sprite painting will land in a
// follow-up version once we have user feedback on the auto-detection
// quality.
//

#include "../persist/TextureProject.hpp"
#include "ColorPickerRow.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/Popup.hpp>

#include <atomic>
#include <memory>
#include <string>

namespace paimon::texture_studio {

class ProjectEditorLayer : public geode::Popup {
public:
    static ProjectEditorLayer* create(std::string slotId);

protected:
    bool init(std::string slotId);

    void onSave(cocos2d::CCObject*);
    void onGenerate(cocos2d::CCObject*);
    void onCredits(cocos2d::CCObject*);

    // Fired by ColorPickerRow whenever the user changes a color/brightness.
    void onColorChange(ColorField field, ColorPickerRowState const& state);

    // Carga el primer sheet del slot como preview visual y aplica el
    // tinte actual (color1) para que el usuario vea como se ve el
    // asset que va a recolorear. Si no hay sheets validos cae a un
    // placeholder.
    void buildPreview(cocos2d::CCNode* parent, cocos2d::CCSize const& slot);

    // Refresca el color/tinte del sprite de preview cuando el usuario
    // cambia color1 desde el picker o un preset.
    void refreshPreviewTint();

    // Habilita o deshabilita los botones que disparan operaciones largas
    // (Save / Generate / Preset). Usado por la generacion asincrona para
    // evitar reentrancia mientras el thread de fondo procesa los sheets.
    void setBusy(bool busy);

private:
    std::string         m_slotId;
    TextureProject      m_project;
    ColorPickerRow*     m_pickerRow = nullptr;
    cocos2d::CCLabelBMFont* m_statusLbl = nullptr;
    cocos2d::CCSprite*  m_previewSprite = nullptr;  // se tinta segun color1
    int                 m_presetIndex = 0;

    // Estado / referencias para la generacion asincrona del pack.
    CCMenuItemSpriteExtra* m_genBtn    = nullptr;
    CCMenuItemSpriteExtra* m_saveBtn   = nullptr;
    CCMenuItemSpriteExtra* m_presetBtn = nullptr;
    // Flag compartido con el thread de fondo. Permite que el callback en
    // main thread sepa si el popup todavia esta vigente sin tocar `this`
    // (que ya pudo haber sido destruido). El raw pointer lo despachamos
    // por WeakRef.
    std::shared_ptr<std::atomic<bool>> m_generating
        = std::make_shared<std::atomic<bool>>(false);
};

}  // namespace paimon::texture_studio
