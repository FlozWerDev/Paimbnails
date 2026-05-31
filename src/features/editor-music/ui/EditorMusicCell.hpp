#pragma once

// EditorMusicCell — mini reproductor que flota arriba-izquierda del editor.
// Version simple: prev / play-pause / next / modo / playlist + nombre del
// track y modo activo. Toda la logica de audio vive en EditorMusicPlayer.

#include <Geode/Geode.hpp>

namespace paimon::editormusic {

class EditorMusicCell : public cocos2d::CCNode {
public:
    static EditorMusicCell* create();
    void refresh();
    void animateIn();   // entrada fluida (slide + pop)
    void animateOut();  // salida fluida; al terminar para el player y se elimina

protected:
    bool init() override;

    void onPrev(cocos2d::CCObject*);
    void onPlayPause(cocos2d::CCObject*);
    void onNext(cocos2d::CCObject*);
    void onMode(cocos2d::CCObject*);
    void onPlaylist(cocos2d::CCObject*);

    void tickMonitor(float);  // pausa/reanuda segun el canal principal del editor

    cocos2d::CCLabelBMFont* m_trackLabel = nullptr;
    cocos2d::CCLabelBMFont* m_modeLabel = nullptr;
    cocos2d::CCSprite* m_playSprite = nullptr;
};

} // namespace paimon::editormusic
