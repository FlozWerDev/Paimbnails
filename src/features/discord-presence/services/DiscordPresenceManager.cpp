#include "DiscordPresenceManager.hpp"
#include "DiscordPresenceSettings.hpp"
#include "../../../features/onboarding/WelcomeFlow.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include <Geode/Geode.hpp>
#include <ctime>
#ifdef PAIMON_HAS_DISCORD_RPC
#include "DiscordActivityMailbox.hpp"
#endif

using namespace geode::prelude;

namespace paimon::discord {

namespace {
constexpr char const* kApplicationID = kDefaultApplicationID;
std::atomic<bool> s_discordInitialized{false};
}

bool DiscordPresenceManager::isInitialized() {
    return s_discordInitialized.load(std::memory_order_acquire);
}

DiscordPresenceManager& DiscordPresenceManager::get() {
    // never freed: worker can outlive cocos teardown.
    static auto* instance = []() {
        auto* mgr = new DiscordPresenceManager();
        s_discordInitialized.store(true, std::memory_order_release);
        return mgr;
    }();
    return *instance;
}

bool DiscordPresenceManager::isSupported() {
#ifdef PAIMON_HAS_DISCORD_RPC
    auto* mod = geode::Mod::get();
    return mod && mod->hasSetting("discord-rpc-enabled");
#else
    return false;
#endif
}

void DiscordPresenceManager::ensureWorker() {
#ifdef PAIMON_HAS_DISCORD_RPC
    if (!paimon::onboarding::isAccepted()) return;
    if (m_shutdown || paimon::isRuntimeShuttingDown()) return;
    if (m_ipcMailbox && !m_ipcMailbox->stopped()) return;
    auto mailbox = std::make_shared<DiscordActivityMailbox>();
    m_ipcMailbox = mailbox;
    bool started = false;
    try {
        started = paimon::ThreadTracker::get().spawn([this, mailbox]() {
            geode::utils::thread::setName("Paimon Discord RPC");
            using namespace std::chrono_literals;
            auto& ipc = DiscordIpcClient::get();
            try {
                ipc.setClientID(kApplicationID);
                auto nextRefresh = std::chrono::steady_clock::now();
                auto nextRetry = nextRefresh + 15s;
                auto nextSend = nextRefresh;
                std::optional<DiscordActivity> activity;
                bool pending = false;
                uint64_t lastGeneration = ipc.connectionGeneration();
                while (!mailbox->stopped() && !paimon::isRuntimeShuttingDown()) {
                    bool const changed = mailbox->take(activity, activity ? 250ms : 2000ms);
                    if (mailbox->stopped() || paimon::isRuntimeShuttingDown()) break;
                    ipc.poll();
                    bool const cleared = mailbox->takeClearRequest();
                    if (cleared) ipc.clear();
                    auto const now = std::chrono::steady_clock::now();
                    auto const generation = ipc.connectionGeneration();
                    if (generation != lastGeneration && ipc.isConnected()) pending = true;
                    lastGeneration = generation;
                    if (changed) {
                        pending = true;
                        if (activity) ipc.setClientID(activity->applicationId.empty() ? kApplicationID : activity->applicationId);
                        else { if (!cleared) ipc.clear(); pending = false; }
                    }
                    if (!ipc.isConnected() && activity && now >= nextRetry) pending = true;
                    // latest-value mailbox coalesces progress/typing; clears bypass the throttle.
                    if (pending && activity && now >= nextSend) {
                        ipc.update(*activity);
                        lastGeneration = ipc.connectionGeneration();
                        pending = false;
                        nextSend = now + 5s;
                        nextRetry = now + 15s;
                    }
                    if (now >= nextRefresh) {
                        nextRefresh = now + std::chrono::milliseconds(mailbox->refreshInterval());
                        if (mailbox->claimRefresh()) {
                            Loader::get()->queueInMainThread([this, mailbox]() {
                                mailbox->finishRefresh();
                                if (m_shutdown || mailbox->stopped() || paimon::isRuntimeShuttingDown()) return;
                                refreshNow();
                            });
                        }
                    }
                }
            } catch (std::exception const& error) {
                geode::log::warn("[DiscordPresence] IPC worker: {}", error.what());
            } catch (...) {
                geode::log::warn("[DiscordPresence] IPC worker stopped unexpectedly");
            }
            ipc.close();
            mailbox->stop();
        });
    } catch (std::exception const& error) {
        geode::log::warn("[DiscordPresence] Cannot start IPC worker: {}", error.what());
    }
    if (!started) { mailbox->stop(); m_ipcMailbox.reset(); }
#endif
}

void DiscordPresenceManager::init() {
    if (m_initialized || m_shutdown) return;
    m_startTimestamp = static_cast<int64_t>(std::time(nullptr));

    if (!isSupported()) {
        m_initialized = true;
        return;
    }


    static bool s_listenersRegistered = false;
    if (!s_listenersRegistered) {
        s_listenersRegistered = true;
        geode::listenForSettingChanges<bool>("discord-rpc-enabled", +[](bool enabled) {
            if (enabled) DiscordPresenceManager::get().ensureWorker();
            DiscordPresenceManager::get().refreshSoon();
        });
    }

    if (paimon::settings::discord_rpc::enabled()) {
        ensureWorker();
    }

    m_initialized = true;
    refreshSoon();
}

void DiscordPresenceManager::resumeAfterConsent() {
    if (!m_initialized) init();
    if (!paimon::onboarding::isAccepted() || !isSupported()) return;
    if (paimon::settings::discord_rpc::enabled()) ensureWorker();
    refreshSoon();
}

void DiscordPresenceManager::shutdown() {
    if (m_shutdown) return;
    m_shutdown = true;
    m_refreshScheduled = false;
#ifdef PAIMON_HAS_DISCORD_RPC
    if (m_ipcMailbox) m_ipcMailbox->stop();
#endif
}

void DiscordPresenceManager::refreshSoon() {
    if (m_shutdown || paimon::isRuntimeShuttingDown()) return;
    if (!isSupported()) return;
    if (!m_initialized) init();
    if (m_refreshScheduled) return;
    m_refreshScheduled = true;
    Loader::get()->queueInMainThread([this]() {
        if (paimon::isRuntimeShuttingDown()) return;
        m_refreshScheduled = false;
        if (m_shutdown) return;
        refreshNow();
    });
}

void DiscordPresenceManager::refreshNow(bool force) {
    if (m_shutdown || !m_initialized || paimon::isRuntimeShuttingDown()) return;
#ifdef PAIMON_HAS_DISCORD_RPC
    try {
        if (!paimon::onboarding::isAccepted() || !isSupported() || !paimon::settings::discord_rpc::enabled()) {
            if (!m_presenceCleared) {
                if (m_ipcMailbox) { m_ipcMailbox->requestClear(); m_ipcMailbox->publish(std::nullopt); }
                m_presenceCleared = true;
                m_lastActivity = {};
            }
            return;
        }
        ensureWorker();
        if (!m_ipcMailbox) return;
        auto configuration = readPresenceConfiguration();
        auto refreshMode = configuration.text("discord-rpc-refresh-mode");
        m_ipcMailbox->setRefreshInterval(refreshMode == "Fast" ? 2000 : refreshMode == "Low power" ? 15000 : 5000);
        auto activity = buildActivity();
        if (activity.hidden) {
            if (!m_presenceCleared || force) { m_ipcMailbox->requestClear(); m_ipcMailbox->publish(std::nullopt); }
            m_presenceCleared = true;
            m_lastActivity = std::move(activity);
            return;
        }
        auto sharingScope = configuration.text("discord-rpc-sharing-scope");
        bool permissionsChanged = configuration.flags != m_lastPrivacyFlags || sharingScope != m_lastSharingScope;
        if (!force && !m_presenceCleared && activity == m_lastActivity) {
            m_lastPrivacyFlags = configuration.flags;
            m_lastSharingScope = sharingScope;
            return;
        }
        if (!m_presenceCleared && !m_lastActivity.applicationId.empty() && permissionsChanged) m_ipcMailbox->requestClear();
        if (m_ipcMailbox->publish(activity)) {
            m_lastActivity = std::move(activity);
            m_presenceCleared = false;
            m_lastPrivacyFlags = configuration.flags;
            m_lastSharingScope = std::move(sharingScope);
        }
    } catch (std::exception const& error) {
        geode::log::warn("[DiscordPresence] {}", error.what());
    }
#endif
}

DiscordActivity DiscordPresenceManager::previewActivity() {
    return buildActivity();
}

std::string DiscordPresenceManager::connectionStatus() const {
    if (!isSupported()) return "Unavailable on this platform";
    if (!paimon::onboarding::isAccepted()) return "Waiting for onboarding consent";
    if (!paimon::settings::discord_rpc::enabled()) return "Disabled - preview only";
#ifdef PAIMON_HAS_DISCORD_RPC
    if (!m_ipcMailbox || m_ipcMailbox->stopped()) return "Discord worker unavailable - press Update to retry";
    auto& ipc = DiscordIpcClient::get();
    auto error = ipc.lastError();
    if (!error.empty()) return cleanText("Discord: " + error, 100);
    if (ipc.isReady()) return "Discord connected / updates coalesced every 5s";
    if (ipc.isConnected()) return "Waiting for Discord handshake";
#endif
    return "Waiting for the Discord desktop app";
}

std::vector<std::string> DiscordPresenceManager::validationMessages() const {
    auto configuration = readPresenceConfiguration();
    std::vector<std::string> messages;
    auto app = trimText(configuration.text("discord-rpc-application-id"));
    if (!app.empty() && !validApplicationID(app)) messages.emplace_back("Application ID must contain 17-20 digits.");
    for (auto key : {"discord-rpc-large-image-key", "discord-rpc-small-image-key"}) {
        auto image = trimText(configuration.text(key));
        if (!image.empty() && imageReference(image).empty()) messages.emplace_back("Image: use an asset key or a complete HTTP(S) URL (max 256 bytes).");
    }
    return messages;
}

} // namespace paimon::discord
