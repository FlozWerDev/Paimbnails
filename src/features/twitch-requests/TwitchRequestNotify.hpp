#pragma once

// Aviso en pantalla cuando entra un request nuevo. Todo lo que se ve (el sitio,
// el tamano, los segundos y las dos animaciones) sale de NotifyConfig, y el
// popup de ajustes dibuja exactamente la misma tarjeta dentro de su pantalla de
// mentira, asi que lo que previsualizas es lo que sale en el stream.

#include <Geode/Geode.hpp>

#include "sources/ChatSource.hpp"

#include <functional>
#include <string>
#include <vector>

namespace paimon::twitch {

struct LevelRequest;

// Las nueve esquinas/lados de la pantalla, en el orden en que se leen:
// fila = indice / 3 (0 arriba, 1 medio, 2 abajo), columna = indice % 3.
enum class NotifySpot : int {
    TopLeft, TopCenter, TopRight,
    MidLeft, Center, MidRight,
    BottomLeft, BottomCenter, BottomRight,
};
constexpr int kNotifySpotCount = 9;

enum class NotifyEnter : int { None, Slide, Fade, Pop, Drop, Spin };
constexpr int kNotifyEnterCount = 6;

enum class NotifyExit : int { None, Slide, Fade, Shrink, Rise, Spin };
constexpr int kNotifyExitCount = 6;

enum class NotifySound : int { None, Soft, Coin, Crystal, Achievement };
constexpr int kNotifySoundCount = 5;

struct NotifyConfig {
    bool enabled = false;
    NotifySpot spot = NotifySpot::TopRight;
    float offsetX = 0.f;   // ajuste fino en pixeles sobre el sitio elegido
    float offsetY = 0.f;
    float scale = 1.f;
    float seconds = 3.f;
    NotifyEnter enter = NotifyEnter::Slide;
    NotifyExit exit = NotifyExit::Fade;
    NotifySound sound = NotifySound::Soft;
    bool showLevel = true;
    bool showRequester = true;
    bool overLayer = false;  // avisar tambien con la lista de requests abierta
};

// Limites de los deslizadores, compartidos con el popup.
constexpr float kNotifyMinScale = 0.6f;
constexpr float kNotifyMaxScale = 1.8f;
constexpr float kNotifyMinSeconds = 1.f;
constexpr float kNotifyMaxSeconds = 10.f;
constexpr float kNotifyMaxOffsetX = 140.f;
constexpr float kNotifyMaxOffsetY = 90.f;

std::vector<std::string> notifySpotNames();
std::vector<std::string> notifyEnterNames();
std::vector<std::string> notifyExitNames();
std::vector<std::string> notifySoundNames();

NotifyConfig const& notifyConfig();
void setNotifyConfig(NotifyConfig config);
void loadNotifyConfig();

// Color de cada chat; lo usa la tarjeta y tambien la lista de requests.
cocos2d::ccColor3B platformAccent(Platform platform);

// La tarjeta tal cual se ve, sin escalar ni colocar (anchor en el centro).
// `levelName` vacio deja la linea del nivel en la ID.
cocos2d::CCNodeRGBA* buildNotifyCard(
    NotifyConfig const& config,
    Platform platform,
    std::string levelName,
    int levelID,
    std::string requester);

// Sitio de reposo en coordenadas de pantalla; `slot` apila los avisos que ya
// estan puestos. `card` es el tamano sin escalar de la tarjeta.
cocos2d::CCPoint notifyRestPoint(
    NotifyConfig const& config, cocos2d::CCSize card, int slot = 0);

// Animan la tarjeta que les pases: leen la escala y el tamano que ya tiene, asi
// que valen igual para la pantalla de verdad y para la del previsualizador.
void runNotifyEnter(cocos2d::CCNodeRGBA* card, NotifyConfig const& config, cocos2d::CCPoint rest);
void runNotifyExit(cocos2d::CCNodeRGBA* card, NotifyConfig const& config, cocos2d::CCPoint rest,
    std::function<void()> onDone);
// Cuanto tarda la entrada, para saber cuando empieza la cuenta de segundos.
float notifyEnterSeconds(NotifyConfig const& config);

void playNotifySound(NotifyConfig const& config);

// Aviso de verdad, desde la cola.
void showRequestNotify(LevelRequest const& request);
// Aviso de mentira del boton "Probar"; sale aunque los avisos esten apagados.
void showNotifyDemo();

} // namespace paimon::twitch
