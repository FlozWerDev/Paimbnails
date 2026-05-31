#pragma once

// PopupBlurService — servicio compartido para aplicar blur detras de popups.
// Extraido del hook PaimonDynamicPopupHook para poder reusarlo con otros popups
// cuyo show() no pasa por FLAlertLayer (ProfilePage, SetupTriggerPopup, etc).
//
// Uso tipico:
//   1) En show(): paimon::popupblur::captureAndApply(this);
//   2) En destructor: paimon::popupblur::cleanup(this);
//
// El servicio adjunta el blur node como hijo del parent del popup (en z-order
// justo debajo del popup) y guarda la referencia en un user flag para cleanup.

#include <Geode/utils/cocos.hpp>
#include <string>
#include <utility>
#include <vector>

namespace paimon::popupblur {

struct Config {
    bool enabled = false;
    std::string style = "gaussian"; // "gaussian" | "paimonblur"
    float intensity = 4.f;
    float darkness = 0.28f;
};

// Lee la config desde Settings con defaults seguros.
Config getConfig();

// ─────────────────────────────────────────────────────────────────────
// Captura de escena centralizada
// ─────────────────────────────────────────────────────────────────────

// Captura la escena actual a una textura, ocultando el popup y todos los
// blur nodes activos para evitar feedback loop. Usa glFinish() para
// garantizar que los pixeles esten escritos antes de retornar (fix para
// cuadrados blancos en GPUs con deferred rendering).
//
// Crea el FBO con depth+stencil buffer para que CCClippingNode (usado por
// thumbnails en LevelCell) funcione correctamente durante la captura.
//
// @param popupToHide  popup a ocultar durante la captura (puede ser nullptr)
// @param outSize      [out] tamaño de la textura capturada (== winSize)
// @return textura capturada, o nullptr si fallo
cocos2d::CCTexture2D* captureSceneTexture(cocos2d::CCNode* popupToHide, cocos2d::CCSize& outSize);

// Construye el blur node completo (backing + sprite blurreado + overlay) listo
// para insertar en la escena. Retorna nullptr si falla.
cocos2d::CCLayerColor* buildBlurNode(cocos2d::CCSprite* blurred, cocos2d::CCSize const& winSize, Config const& cfg);

// ── FIX (popup blur thumbnail en esquina) ──
// Re-renderiza el sprite blureado a un FBO de exactamente winSize. Esto
// garantiza un sprite final con:
//   - contentSize == winSize (en puntos logicos, no pixeles fisicos)
//   - flipY=true (orientacion correcta respecto al backbuffer)
//   - rendering visual cubriendo exactamente toda la pantalla
//
// Necesario porque sprites de FBO pueden llegar con dimensiones en pixeles
// fisicos (RobTop modifico CCRenderTexture con m_fInternalScaleX/Y) o con
// flipY en cualquier estado, causando scaling incorrecto en buildBlurNode.
//
// Si la creacion del FBO falla, retorna el sprite input tal cual.
cocos2d::CCSprite* normalizeBlurSpriteToWinSize(cocos2d::CCSprite* input, cocos2d::CCSize const& winSize);

// ─────────────────────────────────────────────────────────────────────
// API principal
// ─────────────────────────────────────────────────────────────────────

// Captura la escena actual con FBO correctamente limpiado y aplica el blur
// detras del popup especificado. Si ya existe un blur node previo para este
// popup, lo reemplaza.
//
// @param popup el popup al que se le asocia el blur (necesita un parent para
//   funcionar — la captura se hace tras FLAlertLayer::show / equivalente).
// @return true si se instalo blur, false si la feature esta desactivada, el
//   popup no tiene parent, o la captura fallo.
bool captureAndApply(cocos2d::CCNode* popup);

// Remueve el blur node asociado a este popup si existe. Seguro de llamar
// multiples veces o en popups sin blur.
void cleanup(cocos2d::CCNode* popup);

// Igual que cleanup() pero con fade-out suave. El blur se desvanece en
// 'duration' segundos antes de removerse. Util para matchear con la
// animacion de salida del popup — evita el "pop" visual cuando el popup
// desaparece y el blur se corta abruptamente.
// Si duration <= 0, equivale a cleanup() instant.
void cleanupWithFade(cocos2d::CCNode* popup, float duration);

// ─────────────────────────────────────────────────────────────────────
// Tracking de flash overlays (perf — evita scan recursivo de la escena)
// ─────────────────────────────────────────────────────────────────────
//
// El bug del "cuadrado blanco" en thumbnails se produce cuando captureScene
// agarra un flash CCLayerColor con blend additive durante su fade-out.
// La solucion antigua hacia un DFS recursivo del scene graph en CADA
// captura — O(N) sobre todos los nodos de la pantalla, costoso con
// listas largas.
//
// Nuevo enfoque: el codigo que crea el flash llama registerFlashOverlay()
// y al destruirse llama unregisterFlashOverlay(). El scan ahora es O(K)
// sobre solo los flashes activos — tipicamente 0-2 nodos. Si la lista
// esta vacia (caso comun) el scan se salta entero.
void registerFlashOverlay(cocos2d::CCNode* flashLayer);
void unregisterFlashOverlay(cocos2d::CCNode* flashLayer);

// Barre TODOS los blurs registrados y los remueve con un fade corto.
// Llamado automaticamente por el hook CCScene::cleanup para garantizar
// que ningun blur sobreviva a una transicion de escena.
void cleanupAllActive(float fadeDuration = 0.15f);

// Registra un blur node externo (creado por otro codigo, p.ej. el hook
// FLAlertLayer) para que sea ocultado automaticamente durante capturas
// posteriores — evita el feedback loop "blur sobre blur" cuando se abre un
// segundo popup encima.
void registerExternalBlur(cocos2d::CCNode* popup, cocos2d::CCNode* blurNode);

// Oculta todos los blur nodes registrados actualmente y devuelve la lista
// de nodos + su visibilidad previa. Llamado internamente por captureAndApply;
// expuesto para que codigo con captura custom (FLAlertLayer hook) pueda
// hacer lo mismo.
std::vector<std::pair<cocos2d::CCNode*, bool>> hideAllActiveBlurs();

// Restaura la visibilidad de los blurs ocultados previamente.
void restoreHiddenBlurs(std::vector<std::pair<cocos2d::CCNode*, bool>> const& hidden);

// ─────────────────────────────────────────────────────────────────────
// Blur result cache compartido (perf — evita recomputar blur)
// ─────────────────────────────────────────────────────────────────────
//
// Permite al hook FLAlertLayer reusar el blur ya computado entre popups
// que comparten el mismo snapshot de la escena. Si dos popups se abren
// en cascada (uno encima del otro), el segundo puede reusar el blur del
// primero ahorrando 4-6 passes de FBO.
//
// La firma del cache es (snapshot, style, intensity, darkness). Si todos
// matchean retorna un sprite nuevo apuntando a la textura cacheada.
cocos2d::CCSprite* reuseBlurForSnapshot(cocos2d::CCTexture2D* snapshot,
                                         std::string const& style,
                                         float intensity,
                                         float darkness);

// Guarda el resultado de un blur recien computado para reuso futuro.
// Solo cachea la textura — el sprite se construye fresco cada vez (un
// CCSprite no puede tener multiples parents).
void storeBlurForSnapshot(cocos2d::CCTexture2D* snapshot,
                           cocos2d::CCSprite* blurredSprite,
                           std::string const& style,
                           float intensity,
                           float darkness);

} // namespace paimon::popupblur
