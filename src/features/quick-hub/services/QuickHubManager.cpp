#include "QuickHubManager.hpp"
#include "../data/QuickHubCategories.hpp"
#include <matjson.hpp>

using namespace geode::prelude;

namespace paimon::quickhub {

std::vector<std::string> QuickHubManager::getActiveOptions() const {
    auto saved = Mod::get()->getSavedValue<matjson::Value>(kSavedKey, matjson::Value());

    if (!saved.isArray()) {
        return getDefaultRadialOrder();
    }

    auto arrRes = saved.asArray();
    if (!arrRes.isOk()) {
        return getDefaultRadialOrder();
    }

    auto arr = arrRes.unwrap();
    if (arr.empty()) {
        return getDefaultRadialOrder();
    }

    std::vector<std::string> result;
    for (auto const& item : arr) {
        auto strRes = item.asString();
        if (strRes.isOk()) {
            result.push_back(strRes.unwrap());
        }
    }

    if (result.empty()) {
        return getDefaultRadialOrder();
    }

    return result;
}

void QuickHubManager::setActiveOptions(std::vector<std::string> const& options) {
    auto arr = matjson::Value::array();
    for (auto const& id : options) {
        arr.push(matjson::Value(id));
    }
    Mod::get()->setSavedValue(kSavedKey, arr);
}

void QuickHubManager::resetToDefault() {
    setActiveOptions(getDefaultRadialOrder());
}

} // namespace paimon::quickhub
