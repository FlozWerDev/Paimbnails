#include "GdUserResolver.hpp"
#include "../../../utils/GDRobTopCache.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include <Geode/utils/general.hpp>
#include <map>

using namespace geode::prelude;

namespace {

constexpr char const* kGdSecret = "Wmfd2893gb7";

// "k:v:k:v..." -> map. GD user entries are colon separated.
std::map<std::string, std::string> parseKV(std::string const& s) {
    auto parts = geode::utils::string::split(s, ":");
    std::map<std::string, std::string> m;
    for (size_t i = 0; i + 1 < parts.size(); i += 2) m[parts[i]] = parts[i + 1];
    return m;
}

} // namespace

namespace paimon::moderation {

void resolveUsername(
    std::string const& username,
    geode::CopyableFunction<void(bool, int, std::string const&)> cb
) {
    std::string trimmed = geode::utils::string::trim(username);
    if (trimmed.empty()) {
        cb(false, 0, "");
        return;
    }

    std::string body = fmt::format("str={}&page=0&secret={}", trimmed, kGdSecret);

    paimon::gd::postCached("getGJUsers20.php", body,
        [cb = std::move(cb), trimmed](bool ok, std::string response) mutable {
            if (paimon::isRuntimeShuttingDown()) { cb(false, 0, ""); return; }
            if (!ok || response.empty() || response[0] == '-') {
                cb(false, 0, "");
                return;
            }

            // Response is "user1|user2|...". The search is ranked, so the first
            // entry is the best match. Pick an exact (case-insensitive) name
            // match if present, otherwise the first result.
            auto entries = geode::utils::string::split(response, "|");
            std::string wantLower = geode::utils::string::toLower(trimmed);

            std::map<std::string, std::string> chosen;
            std::string chosenName;
            int chosenAccountID = 0;

            for (auto const& entry : entries) {
                auto kv = parseKV(entry);
                auto nameIt = kv.find("1");
                auto accIt = kv.find("16");
                if (nameIt == kv.end() || accIt == kv.end()) continue;

                auto parsed = geode::utils::numFromString<int>(accIt->second);
                if (!parsed) continue;
                int accountID = parsed.unwrap();
                if (accountID <= 0) continue;

                if (chosenAccountID == 0) {
                    chosen = kv;
                    chosenName = nameIt->second;
                    chosenAccountID = accountID;
                }
                if (geode::utils::string::toLower(nameIt->second) == wantLower) {
                    chosenName = nameIt->second;
                    chosenAccountID = accountID;
                    break;
                }
            }

            if (chosenAccountID <= 0) {
                cb(false, 0, "");
                return;
            }
            cb(true, chosenAccountID, chosenName);
        },
        paimon::gd::policyForEndpoint("getGJUsers20.php"));
}

} // namespace paimon::moderation
