#pragma once

#include <string>

namespace paimon::translate {

struct Settings {
    bool enabled = false;
    bool autoTranslate = false;
    std::string target = "en";
    // language picked last in Add Comment. Kept apart from `target`: translating
    // what you write into another language must not change how comments read.
    std::string composeTarget = "en";
    // per-user endpoint. Every player can point the translator at their own
    // instance or proxy, so requests are not shared between users.
    std::string endpoint;
};

// saved values, not mod.json settings: the feature is configured from the
// comment options popup instead of the Geode settings list.
Settings loadSettings();
void saveEnabled(bool enabled);
void saveAutoTranslate(bool enabled);
void saveTarget(std::string const& target);
void saveComposeTarget(std::string const& target);
// empty or invalid input falls back to the built-in Google gtx endpoint.
void saveEndpoint(std::string const& endpoint);

std::string defaultTarget();
std::string defaultEndpoint();
// accepts only http(s) URLs with a host; returns "" when the text is invalid.
std::string normalizeEndpoint(std::string const& endpoint);

}
