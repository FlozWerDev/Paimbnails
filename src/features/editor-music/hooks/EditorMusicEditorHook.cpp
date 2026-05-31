// EditorMusicEditorHook — conecta el EditorMusicPlayer con el editor.
//
//   * Ctrl+M (editorMusicToggleKeybind) crea/oculta la celda mini-player
//     dentro del EditorUI (espacio de pantalla, arriba-izquierda). Solo
//     actua si estamos en el editor y la opcion esta habilitada.
//   * Al destruirse el LevelEditorLayer detenemos el player para liberar
//     el canal FMOD propio.

#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <Geode/binding/EditorUI.hpp>

#include "../services/EditorMusicPlayer.hpp"
#include "../ui/EditorMusicCell.hpp"

using namespace geode::prelude;
using namespace paimon::editormusic;

namespace {
    constexpr char const* kCellID = "editor-music-cell";
}

class $modify(PaimonEditorMusicHook, LevelEditorLayer) {
    struct Fields {
        // Al salir del editor liberamos el canal de musica propio.
        ~Fields() { EditorMusicPlayer::get().stop(); }
    };

    $override
    bool init(GJGameLevel* level, bool p1) {
        if (!LevelEditorLayer::init(level, p1)) return false;
        // Forzamos la construccion de Fields para que su destructor corra.
        m_fields.self();
        return true;
    }
};

$execute {
    KeybindSettingPressedEventV3(Mod::get(), "editorMusicToggleKeybind").listen(
        +[](Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat) return;
            if (!Mod::get()->getSettingValue<bool>("editorMusicEnable")) return;

            auto* editor = LevelEditorLayer::get();
            if (!editor) return;

            CCNode* parent = editor->m_editorUI
                ? static_cast<CCNode*>(editor->m_editorUI)
                : static_cast<CCNode*>(editor);

            // Toggle: si ya existe, salida animada (al terminar para y se elimina).
            if (auto* existing = parent->getChildByID(kCellID)) {
                if (auto* cell = typeinfo_cast<EditorMusicCell*>(existing)) {
                    cell->animateOut();
                } else {
                    existing->removeFromParent();
                    EditorMusicPlayer::get().stop();
                }
                return;
            }

            auto cell = EditorMusicCell::create();
            if (!cell) return;
            cell->setID(kCellID);
            auto win = CCDirector::get()->getWinSize();
            cell->setPosition({8.f, win.height - 8.f});
            parent->addChild(cell, 1000);

            EditorMusicPlayer::get().playNext();
            cell->refresh();
            cell->animateIn();
        }
    ).leak();
}
