#include "TranslatorConfig.hpp"

#include "TranslatorLanguages.hpp"
#include "../../platform/PaimSettings.hpp"
#include "../../utils/Localization.hpp"

namespace paimon::translate {
namespace {

constexpr char kEnabledKey[] = "translator-enabled";
constexpr char kAutoKey[] = "translator-auto";
constexpr char kTargetKey[] = "translator-target";
constexpr char kComposeTargetKey[] = "translator-compose-target";
constexpr char kEndpointKey[] = "translator-endpoint";

}

std::string defaultTarget() {
    return Localization::get().getLanguage() == Localization::Language::SPANISH ? "es" : "en";
}

std::string defaultEndpoint() {
    return "https://translate.googleapis.com/translate_a/single?client=gtx";
}

std::string normalizeEndpoint(std::string const& endpoint) {
    auto const begin = endpoint.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    auto const end = endpoint.find_last_not_of(" \t\r\n");
    auto trimmed = endpoint.substr(begin, end - begin + 1);

    bool const https = trimmed.rfind("https://", 0) == 0;
    bool const http = trimmed.rfind("http://", 0) == 0;
    if (!https && !http) return {};
    // a bare scheme with no host is not a usable endpoint.
    std::size_t const schemeLength = https ? 8 : 7;
    if (trimmed.size() <= schemeLength) return {};
    if (trimmed.find_first_of(" \t\r\n") != std::string::npos) return {};
    return trimmed;
}

Settings loadSettings() {
    Settings settings;
    settings.enabled = paimon::platform::gsaved<bool>(kEnabledKey, false);
    settings.autoTranslate = paimon::platform::gsaved<bool>(kAutoKey, false);

    auto savedTarget = paimon::platform::gsaved<std::string>(kTargetKey, std::string());
    settings.target = savedTarget.empty() ? defaultTarget() : savedTarget;
    if (!findLanguage(settings.target)) settings.target = defaultTarget();

    auto savedCompose = paimon::platform::gsaved<std::string>(kComposeTargetKey, std::string());
    settings.composeTarget = findLanguage(savedCompose) ? savedCompose : settings.target;

    auto savedEndpoint = paimon::platform::gsaved<std::string>(kEndpointKey, std::string());
    settings.endpoint = normalizeEndpoint(savedEndpoint);
    if (settings.endpoint.empty()) settings.endpoint = defaultEndpoint();
    return settings;
}

void saveEnabled(bool enabled) {
    paimon::platform::ssaved<bool>(kEnabledKey, enabled);
}

void saveAutoTranslate(bool enabled) {
    paimon::platform::ssaved<bool>(kAutoKey, enabled);
}

void saveTarget(std::string const& target) {
    if (!findLanguage(target)) return;
    paimon::platform::ssaved<std::string>(kTargetKey, target);
}

void saveComposeTarget(std::string const& target) {
    if (!findLanguage(target)) return;
    paimon::platform::ssaved<std::string>(kComposeTargetKey, target);
}

void saveEndpoint(std::string const& endpoint) {
    // saving an empty string restores the built-in endpoint.
    paimon::platform::ssaved<std::string>(kEndpointKey, normalizeEndpoint(endpoint));
}

}
