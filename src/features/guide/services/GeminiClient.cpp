#include "GeminiClient.hpp"

#include "../../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;

namespace paimon::guide {

GeminiClient& GeminiClient::get() {
    static GeminiClient instance;
    return instance;
}

bool GeminiClient::available() {
    return false;
}

void GeminiClient::complete(std::vector<ChatMessage> const& /*history*/,
                            std::string const& /*systemPrompt*/,
                            ReplyCallback callback)
{
    if (!callback) return;
    Loader::get()->queueInMainThread([callback = std::move(callback)]() {
        if (paimon::isRuntimeShuttingDown()) return;
        callback(false, "unavailable");
    });
}

} // namespace paimon::guide
