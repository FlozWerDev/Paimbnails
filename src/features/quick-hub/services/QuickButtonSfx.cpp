#include "QuickButtonSfx.hpp"

#include "../data/QuickHubCategories.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>

#include <fmod.hpp>

using namespace geode::prelude;

namespace paimon::quickhub {

namespace {

constexpr float kMinSpeed = 0.4f;
constexpr float kMaxSpeed = 2.5f;

std::atomic<bool> s_suppressArmed{false};
std::chrono::steady_clock::time_point s_suppressUntil{};

// One-shot fire: se corta el anterior si se spamea el radial.
FMOD::Sound* s_fireSound = nullptr;
FMOD::Channel* s_fireChannel = nullptr;

// Generacion del disparo en curso: las lambdas capturan la suya por valor, asi
// un timer rezagado no corta ni retoca el volumen del sonido nuevo.
std::atomic<unsigned> s_fireGen{0};

bool channelAlive(FMOD::Channel* ch) {
    if (!ch) return false;
    bool playing = false;
    return ch->isPlaying(&playing) == FMOD_OK && playing;
}

void stopFire() {
    if (s_fireChannel) {
        if (channelAlive(s_fireChannel)) s_fireChannel->stop();
        s_fireChannel = nullptr;
    }
    if (s_fireSound) {
        s_fireSound->release();
        s_fireSound = nullptr;
    }
}

// Rampa de volumen en 5 pasos desde `from` hasta `to`, empezando en
// startDelaySec y durando fadeSec. Solo la generacion vigente toca el canal.
void rampVolume(float from, float to, float startDelaySec, float fadeSec, unsigned gen) {
    if (fadeSec <= 0.001f) return;
    constexpr int kSteps = 5;
    for (int i = 1; i <= kSteps; ++i) {
        float t = startDelaySec + fadeSec * static_cast<float>(i) / static_cast<float>(kSteps);
        float v = from + (to - from) * static_cast<float>(i) / static_cast<float>(kSteps);
        paimon::scheduleMainThreadDelay(t, [v, gen]() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (gen != s_fireGen.load()) return;
            if (channelAlive(s_fireChannel)) s_fireChannel->setVolume(std::max(0.f, v));
        });
    }
}

void scheduleFireStop(float delaySec, float fadeOutMs, float baseVolume, unsigned gen) {
    if (delaySec < 0.f) delaySec = 0.f;
    // Fade-out en 5 pasos antes del stop; fade-in se aplica al arrancar.
    if (fadeOutMs > 0.f && delaySec > 0.01f) {
        float fadeSec = std::min(fadeOutMs / 1000.f, delaySec);
        rampVolume(baseVolume, 0.f, delaySec - fadeSec, fadeSec, gen);
    }
    paimon::scheduleMainThreadDelay(delaySec, [gen]() {
        if (paimon::isRuntimeShuttingDown()) return;
        if (gen != s_fireGen.load()) return;
        stopFire();
    });
}

} // namespace

std::filesystem::path quickHubImagesDir() {
    return Mod::get()->getConfigDir() / "quick-hub" / "images";
}

std::filesystem::path quickHubSfxDir() {
    return Mod::get()->getConfigDir() / "quick-hub" / "sfx";
}

bool isQuickHubAudioFile(std::filesystem::path const& path) {
    auto ext = geode::utils::string::toLower(
        geode::utils::string::pathToString(path.extension()));
    static constexpr std::array<std::string_view, 7> supported = {
        ".mp3", ".ogg", ".wav", ".flac", ".oga", ".m4a", ".opus"};
    return std::find(supported.begin(), supported.end(), ext) != supported.end();
}

std::string resolveQuickButtonSfxPath(CustomQuickButton const& b) {
    auto kind = static_cast<QuickButtonSfxKind>(b.sfxKind);
    if (kind == QuickButtonSfxKind::Game) {
        if (b.sfxPath.empty()) return "";
        auto* utils = cocos2d::CCFileUtils::sharedFileUtils();
        std::string full = utils ? utils->fullPathForFilename(b.sfxPath.c_str(), false) : "";
        if (full.empty()) return "";
        std::error_code ec;
        if (!std::filesystem::is_regular_file(full, ec) || ec) return "";
        return full;
    }
    if (kind == QuickButtonSfxKind::File) {
        if (b.sfxPath.empty()) return "";
        std::error_code ec;
        if (!std::filesystem::is_regular_file(b.sfxPath, ec) || ec) return "";
        return b.sfxPath;
    }
    if (kind == QuickButtonSfxKind::Online) {
        if (b.sfxId <= 0) return "";
        auto* mdm = MusicDownloadManager::sharedState();
        if (!mdm) return "";
        if (!mdm->isSFXDownloaded(b.sfxId)) {
            mdm->downloadSFX(b.sfxId);
            PaimonNotify::create("Descargando SFX... pulsa Probar de nuevo.", NotificationIcon::Warning)->show();
            return "";
        }
        std::string p(mdm->pathForSFX(b.sfxId));
        if (p.empty()) return "";
        return p;
    }
    return "";
}

bool probeQuickButtonSfxDuration(std::string const& absPath, unsigned int* outMs) {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system || absPath.empty()) return false;
    FMOD::Sound* sound = nullptr;
    if (engine->m_system->createSound(
            absPath.c_str(), FMOD_OPENONLY | FMOD_ACCURATETIME, nullptr, &sound) != FMOD_OK ||
        !sound) {
        return false;
    }
    if (outMs) {
        *outMs = 0;
        sound->getLength(outMs, FMOD_TIMEUNIT_MS);
    }
    sound->release();
    return true;
}

bool playQuickButtonSfx(CustomQuickButton const& b) {
    if (static_cast<QuickButtonSfxKind>(b.sfxKind) == QuickButtonSfxKind::None) return false;
    std::string path = resolveQuickButtonSfxPath(b);
    if (path.empty()) return false;

    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return false;
    if (paimon::isRuntimeShuttingDown()) return false;

    stopFire();
    unsigned gen = ++s_fireGen;

    float volume = std::clamp(b.sfxVolume, 0.f, 1.f) * engine->getEffectsVolume();
    float speed = std::clamp(b.sfxSpeed, kMinSpeed, kMaxSpeed);
    int startMs = std::max(0, b.sfxStartMs);
    int endMs = b.sfxEndMs;

    FMOD::Sound* sound = nullptr;
    if (engine->m_system->createSound(path.c_str(), FMOD_DEFAULT, nullptr, &sound) != FMOD_OK || !sound) {
        return false;
    }
    FMOD::Channel* channel = nullptr;
    if (engine->m_system->playSound(sound, nullptr, true, &channel) != FMOD_OK || !channel) {
        sound->release();
        return false;
    }

    // Fade-in: arrancar bajo y rampear al volumen objetivo.
    float fadeInMs = static_cast<float>(std::max(0, b.sfxFadeInMs));
    float fadeOutMs = static_cast<float>(std::max(0, b.sfxFadeOutMs));
    if (fadeInMs > 0.f) channel->setVolume(0.f);
    else channel->setVolume(volume);
    channel->setPitch(speed);
    if (startMs > 0) channel->setPosition(static_cast<unsigned int>(startMs), FMOD_TIMEUNIT_MS);
    channel->setPaused(false);

    s_fireSound = sound;
    s_fireChannel = channel;

    if (fadeInMs > 0.f) {
        rampVolume(0.f, volume, 0.f, fadeInMs / 1000.f, gen);
    }

    if (endMs > startMs) {
        scheduleFireStop(static_cast<float>(endMs - startMs) / 1000.f, fadeOutMs, volume, gen);
    } else if (fadeOutMs > 0.f) {
        // Sin fin explicito: fundir al final real del archivo.
        unsigned int lenMs = 0;
        if (sound->getLength(&lenMs, FMOD_TIMEUNIT_MS) == FMOD_OK && lenMs > static_cast<unsigned int>(startMs) + 200) {
            float totalSec = static_cast<float>(lenMs - static_cast<unsigned int>(startMs)) / 1000.f;
            scheduleFireStop(totalSec, fadeOutMs, volume, gen);
        }
    }
    return true;
}

void stopQuickButtonSfx() {
    // Invalidar primero: las lambdas pendientes de un disparo anterior se
    // vuelven no-ops aunque el canal FMOD se reutilice para otro sonido.
    ++s_fireGen;
    stopFire();
}

void beginQuickButtonSfxSuppress() {
    s_suppressArmed.store(true);
    s_suppressUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
}

bool consumeQuickButtonSfxSuppress() {
    if (!s_suppressArmed.load()) return false;
    if (std::chrono::steady_clock::now() > s_suppressUntil) {
        s_suppressArmed.store(false);
        return false;
    }
    s_suppressArmed.store(false);
    return true;
}

void clearQuickButtonSfxSuppress() {
    s_suppressArmed.store(false);
}

void activateItemWithQuickButtonSfx(cocos2d::CCMenuItem* item, CustomQuickButton const& def) {
    if (!item) return;
    if (static_cast<QuickButtonSfxKind>(def.sfxKind) == QuickButtonSfxKind::None) {
        item->activate();
        return;
    }
    beginQuickButtonSfxSuppress();
    item->activate();
    // Un solo consumo: lo que el activate disparo ya cayo en el hook.
    // Lo que quede armado se desarma para no tragarse SFX ajenos.
    (void)consumeQuickButtonSfxSuppress();
    clearQuickButtonSfxSuppress();
    playQuickButtonSfx(def);
}

class $modify(QuickHubSfxEngine, FMODAudioEngine) {
    $override int playEffect(gd::string path, float speed, float unknown, float volume) {
        if (consumeQuickButtonSfxSuppress()) return 1;
        return FMODAudioEngine::playEffect(path, speed, unknown, volume);
    }
    $override int playEffect(gd::string path) {
        if (consumeQuickButtonSfxSuppress()) return 1;
        return FMODAudioEngine::playEffect(path);
    }
};

} // namespace paimon::quickhub
