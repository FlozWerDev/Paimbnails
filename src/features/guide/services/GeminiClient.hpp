#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include <string>
#include <vector>

// "max" mode stub: the embedded key shipped in the .dll, so it was removed and the mode stays off.

namespace paimon::guide {

class GeminiClient {
public:
    static GeminiClient& get();

    // message in the chat format (role: "user" / "model").
    struct ChatMessage {
        std::string role;
        std::string text;
    };

    // false while the mode has no key behind it.
    static bool available();

    // callback runs on the main thread.
    using ReplyCallback = geode::CopyableFunction<void(bool success, std::string const& reply)>;
    void complete(std::vector<ChatMessage> const& history,
                  std::string const& systemPrompt,
                  ReplyCallback callback);

private:
    GeminiClient() = default;
};

} // namespace paimon::guide
