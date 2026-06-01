#include "../framework/FeatureRegistry.hpp"
#include "../framework/PermissionPolicy.hpp"
#include "../framework/HookInterceptor.hpp"
#include "../framework/EventBus.hpp"
#include "../framework/ModEvents.hpp"
#include "../utils/AudioInterop.hpp"
#include "../utils/ExtendedKeybind.hpp"
#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace paimon {

// Registra los 15 features del mod con sus tiers de permiso.
static void registerAllFeatures() {
    auto& reg = FeatureRegistry::get();

    reg.registerFeature({"thumbnails",     "1.0.4", {},              PermissionTier::Viewer});
    reg.registerFeature({"emotes",         "1.0.4", {},              PermissionTier::Viewer});
    reg.registerFeature({"backgrounds",    "1.0.4", {},              PermissionTier::Viewer});
    reg.registerFeature({"badges",         "1.0.4", {},              PermissionTier::Viewer});
    reg.registerFeature({"audio",          "1.0.4", {},              PermissionTier::Viewer});
    reg.registerFeature({"capture",        "1.0.4", {"thumbnails"},  PermissionTier::Contributor});
    reg.registerFeature({"community",      "1.0.4", {},              PermissionTier::User});
    reg.registerFeature({"dynamic-songs",  "1.0.4", {"audio"},       PermissionTier::Viewer});
    reg.registerFeature({"moderation",     "1.0.4", {"thumbnails"},  PermissionTier::Moderator});
    reg.registerFeature({"pet",            "1.0.4", {},              PermissionTier::Viewer});
    reg.registerFeature({"custom-cursor",  "1.0.4", {},              PermissionTier::Viewer});
    reg.registerFeature({"profile-music",  "1.0.4", {"audio"},       PermissionTier::User});
    reg.registerFeature({"profiles",       "1.0.4", {},              PermissionTier::Viewer});
    reg.registerFeature({"settings-panel", "1.0.4", {},              PermissionTier::Viewer});
    reg.registerFeature({"transitions",    "1.0.4", {},              PermissionTier::Viewer});
    reg.registerFeature({"visuals",        "1.0.4", {},              PermissionTier::Viewer});
    reg.registerFeature({"progressbar",    "1.0.4", {},              PermissionTier::Viewer});
    reg.registerFeature({"mod-previews",   "1.0.0", {},              PermissionTier::Viewer});

    log::info("[PaimonFramework] Registered {} features", reg.featureCount());
}

// Hooks de seguridad y validacion para uploads.
static void registerDefaultHooks() {
    auto& hooks = HookInterceptor::get();

    // ── Pre-upload: validacion de tamaño ────────────────────────────
    hooks.addPreHook("upload", [](HookContext const& ctx) -> HookResult {
        constexpr size_t MAX_PNG = 5 * 1024 * 1024;   // 5 MB
        constexpr size_t MAX_GIF = 10 * 1024 * 1024;  // 10 MB
        constexpr size_t MAX_MP4 = 25 * 1024 * 1024;  // 25 MB

        size_t limit = MAX_PNG;
        if (ctx.format == "gif") limit = MAX_GIF;
        else if (ctx.format == "mp4") limit = MAX_MP4;

        if (ctx.dataSize > limit) {
            return HookResult::deny(
                fmt::format("Archivo demasiado grande: {} bytes (limite: {} bytes)",
                            ctx.dataSize, limit)
            );
        }
        return HookResult::allow();
    });

    // ── Pre-upload: verificacion de permisos ────────────────────────
    hooks.addPreHook("upload", [](HookContext const& ctx) -> HookResult {
        // GIF y MP4 requieren VIP/Mod/Admin
        if (ctx.format == "gif" || ctx.format == "mp4") {
            auto auth = PermissionPolicy::get().authorize(PermissionTier::Contributor);
            if (!auth) return HookResult::deny(auth.reason);
        }
        return HookResult::allow();
    });

    // ── Pre-upload: validacion de magic bytes ───────────────────────
    hooks.addPreHook("validate", [](HookContext const& ctx) -> HookResult {
        if (!ctx.data || ctx.data->size() < 4) {
            return HookResult::deny("Datos vacios o insuficientes");
        }
        auto const* bytes = ctx.data->data();
        auto sz = ctx.data->size();
        bool validPNG = (bytes[0] == 0x89 && bytes[1] == 0x50 &&
                         bytes[2] == 0x4E && bytes[3] == 0x47);
        bool validGIF = (bytes[0] == 0x47 && bytes[1] == 0x49 && bytes[2] == 0x46);
        bool validMP4 = (sz >= 8 &&
                         bytes[4] == 0x66 && bytes[5] == 0x74 &&
                         bytes[6] == 0x79 && bytes[7] == 0x70);

        if (ctx.format == "png" && !validPNG) return HookResult::deny("Magic bytes invalidos para PNG");
        if (ctx.format == "gif" && !validGIF) return HookResult::deny("Magic bytes invalidos para GIF");
        if (ctx.format == "mp4" && !validMP4) return HookResult::deny("Magic bytes invalidos para MP4");

        return HookResult::allow();
    });

    // ── Post-upload: publicar evento ────────────────────────────────
    hooks.addPostHook("upload", [](HookContext const& ctx, bool success) {
        EventBus::get().publish(UploadCompletedEvent{
            ctx.levelID, ctx.format, ctx.username, success, {}
        });
    });

    // ── Pre-security: rate-limit basico por sesion ──────────────────
    static std::atomic<int> s_uploadCount{0};
    hooks.addPreHook("security-check", [](HookContext const& ctx) -> HookResult {
        constexpr int MAX_UPLOADS_PER_SESSION = 50;
        if (s_uploadCount.load(std::memory_order_relaxed) >= MAX_UPLOADS_PER_SESSION) {
            return HookResult::deny("Limite de uploads por sesion alcanzado");
        }
        s_uploadCount.fetch_add(1, std::memory_order_relaxed);
        return HookResult::allow();
    });

    log::info("[PaimonFramework] Default hooks registered (upload/validate/security-check)");
}

// Hooks para dynamic song playback.
static void registerDynamicSongHooks() {
    auto& hooks = HookInterceptor::get();

    // Pre-hook: deny dynamic playback if another audio owner has priority
    hooks.addPreHook("dynamic-play", [](HookContext const& ctx) -> HookResult {
        if (paimon::isVideoAudioInteropActive()) {
            return HookResult::deny("Video audio is active");
        }
        if (paimon::isProfileMusicInteropActive()) {
            return HookResult::deny("Profile music is active");
        }
        return HookResult::allow();
    });

    log::info("[PaimonFramework] Dynamic-song hooks registered");
}

void initFramework() {
    registerAllFeatures();
    registerDefaultHooks();
    registerDynamicSongHooks();

    // Sistema de keybinds extendidos (mouse + scroll). Registra listeners
    // globales para detectar clicks y scroll y dispatchar a los settings
    // del mod. Vease src/utils/ExtendedKeybind.{hpp,cpp}.
    paimon::keybinds::initExtendedKeybindSystem();

    log::info("[PaimonFramework] Framework initialized");
}

} // namespace paimon
