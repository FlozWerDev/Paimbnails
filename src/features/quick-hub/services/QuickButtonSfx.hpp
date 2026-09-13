#pragma once

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::quickhub {

struct CustomQuickButton;

// Directorios en config para copias persistentes.
std::filesystem::path quickHubImagesDir();
std::filesystem::path quickHubSfxDir();

bool isQuickHubAudioFile(std::filesystem::path const& path);

// Resuelve la ruta absoluta reproducible del SFX del boton, o "" si no hay.
// kind Online no descargado: dispara downloadSFX + notify y devuelve "".
std::string resolveQuickButtonSfxPath(CustomQuickButton const& b);

// Duracion en ms via FMOD OPENONLY. false si FMOD no lo abre.
bool probeQuickButtonSfxDuration(std::string const& absPath, unsigned int* outMs);

// Reproduce el SFX custom (volumen/pitch/inicio/fin/fades). false si no hay nada.
bool playQuickButtonSfx(CustomQuickButton const& b);

// Corta cualquier preview/disparo en curso.
void stopQuickButtonSfx();

// Ventana de supresion del sonido original (solo durante activate sincrono).
void beginQuickButtonSfxSuppress();
bool consumeQuickButtonSfxSuppress();
void clearQuickButtonSfxSuppress();

// Activa el item suprimiendo su playEffect sincrono y sonando el custom.
// Si el boton no tiene SFX custom, equivale a item->activate().
void activateItemWithQuickButtonSfx(cocos2d::CCMenuItem* item, CustomQuickButton const& def);

} // namespace paimon::quickhub
