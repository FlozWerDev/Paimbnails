#include <Geode/Geode.hpp>
#include <Geode/modify/EndLevelLayer.hpp>

#include "../services/ProgressionService.hpp"
#include "../ui/ProgressionToast.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../../utils/MainThreadDelay.hpp"

using namespace geode::prelude;
using namespace paimon::progression;

class $modify(ProgressionEndLevelLayer, EndLevelLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EndLevelLayer::customSetup");
    }

    $override
    void customSetup() {
        EndLevelLayer::customSetup();

        auto& service = ProgressionService::get();
        if (!service.enabled()) return;

        // stats settle while the end screen builds; defer so the toast has a scene.
        paimon::scheduleMainThreadDelay(0.45f, []() {
            if (paimon::isRuntimeShuttingDown()) return;

            auto& service = ProgressionService::get();
            auto const delta = service.consumeDelta();
            if (!delta) return;

            ProgressionToast::present(*delta, service.ownContext());
        });
    }
};
