#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include <string>
#include <vector>

// Client for the guide's "Max" mode. It shipped with an embedded Google AI key,
// which anyone could pull out of the .dll, so the key is gone and the mode is
// off: available() answers false and complete() reports it without any request.

namespace paimon::guide {

class GeminiClient {
public:
    static GeminiClient& get();

    // Message in the chat format (role: "user" / "model").
    struct ChatMessage {
        std::string role;
        std::string text;
    };

    // False while the mode has no key behind it.
    static bool available();

    // Callback runs on the main thread.
    using ReplyCallback = geode::CopyableFunction<void(bool success, std::string const& reply)>;
    void complete(std::vector<ChatMessage> const& history,
                  std::string const& systemPrompt,
                  ReplyCallback callback);

private:
    GeminiClient() = default;
};

} // namespace paimon::guide
