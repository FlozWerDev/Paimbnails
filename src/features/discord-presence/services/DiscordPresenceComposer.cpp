#include "DiscordPresenceManager.hpp"
#include "DiscordPresenceSettings.hpp"
#include <ctime>

namespace paimon::discord {

DiscordActivity DiscordPresenceManager::buildActivity() {
    auto configuration = readPresenceConfiguration();
    auto context = captureContext(configuration);
    bool privateMode = configuration.flag("discord-rpc-private-mode");
    auto now = static_cast<int64_t>(std::time(nullptr));
    if (!m_startTimestamp) m_startTimestamp = now;
    if (context.activityKey != m_activityKey) { m_activityKey = context.activityKey; m_activityTimestamp = now; }
    if (!context.levelKey.empty() && context.levelKey != m_levelKey) { m_levelKey = context.levelKey; m_levelTimestamp = now; }
    if (!context.attemptKey.empty() && context.attemptKey != m_attemptKey) { m_attemptKey = context.attemptKey; m_attemptTimestamp = now; }
    context.tokens.emplace_back("activity", context.automatic.state);

    DiscordActivity activity;
    auto scope = configuration.text("discord-rpc-sharing-scope");
    activity.hidden = (scope == "Gameplay only" && context.category != "gameplay") ||
        (scope == "Gameplay & editor" && context.category == "menu") ||
        (scope == "Menus only" && context.category != "menu");
    auto appID = trimText(configuration.text("discord-rpc-application-id"));
    activity.applicationId = !privateMode && validApplicationID(appID) ? appID : kDefaultApplicationID;
    auto type = configuration.text("discord-rpc-activity-type");
    activity.type = type == "Listening" ? DiscordActivityType::Listening : type == "Watching"
        ? DiscordActivityType::Watching : type == "Competing" ? DiscordActivityType::Competing : DiscordActivityType::Playing;
    activity.details = cleanText(context.automatic.details);
    activity.state = cleanText(context.automatic.state);
    if (!privateMode) {
        if (configuration.flag("discord-rpc-override-details")) {
            auto text = configuration.text("discord-rpc-custom-details");
            if (!trimText(text).empty()) activity.details = expandTemplate(text, context.tokens);
        }
        if (configuration.flag("discord-rpc-override-state")) {
            auto text = configuration.text("discord-rpc-custom-state");
            if (!trimText(text).empty()) activity.state = expandTemplate(text, context.tokens);
        }
        auto prefix = "discord-rpc-" + context.category;
        auto details = configuration.text(prefix + "-details");
        auto state = configuration.text(prefix + "-state");
        if (!trimText(details).empty()) activity.details = expandTemplate(details, context.tokens);
        if (!trimText(state).empty()) activity.state = expandTemplate(state, context.tokens);
    }
    if (configuration.flag("discord-rpc-show-timestamp")) {
        auto mode = privateMode ? std::string("Session") : configuration.text("discord-rpc-timer-mode");
        if (mode == "Activity") activity.startTimestamp = m_activityTimestamp;
        else if (mode == "Level") activity.startTimestamp = context.levelKey.empty() ? m_activityTimestamp : m_levelTimestamp;
        else if (mode == "Attempt") activity.startTimestamp = context.attemptKey.empty()
            ? (context.levelKey.empty() ? m_activityTimestamp : m_levelTimestamp) : m_attemptTimestamp;
        else if (mode != "Off") activity.startTimestamp = m_startTimestamp;
    }
    bool defaultApp = activity.applicationId == kDefaultApplicationID;
    if (configuration.flag("discord-rpc-show-large-image")) {
        activity.largeImage = privateMode || defaultApp ? "paimbnails" : "";
        activity.largeText = privateMode ? "Geometry Dash" : "Paimbnails Rich Presence";
        if (!privateMode) {
            auto reference = imageReference(configuration.text("discord-rpc-large-image-key"));
            if (!reference.empty()) activity.largeImage = reference;
            auto text = configuration.text("discord-rpc-large-text");
            if (!trimText(text).empty()) activity.largeText = expandTemplate(text, context.tokens);
        }
    }
    if (!privateMode && configuration.flag("discord-rpc-show-small-image")) {
        activity.smallImage = defaultApp ? context.automatic.smallImage : "";
        auto reference = imageReference(configuration.text("discord-rpc-small-image-key"));
        if (!reference.empty()) activity.smallImage = reference;
        activity.smallText = cleanText(context.automatic.smallImageText);
        auto text = configuration.text("discord-rpc-small-text");
        if (!trimText(text).empty()) activity.smallText = expandTemplate(text, context.tokens);
    }
    if (!privateMode) {
        activity.partySize = context.partySize;
        activity.partyMax = context.partyMax;
    }
    // fixed links only: users can't attach their own URLs to the presence.
    activity.button1Label = "Paimbnails Page";
    activity.button1Url = "https://github.com/FlozWerDev/Paimbnails";
    activity.button2Label = "Paimbnails Discord";
    activity.button2Url = "https://discord.gg/5N5vpSfZwY";
    if (activity.largeImage.empty()) activity.largeText.clear();
    if (activity.smallImage.empty()) activity.smallText.clear();
    return activity;
}

} // namespace paimon::discord
