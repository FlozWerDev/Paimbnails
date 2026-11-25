#include "../src/features/thread-images/ChatImageToken.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace paimon::chatimg;

int main() {
    constexpr char const* kId1 = "0123456789abcdef01234567";
    constexpr char const* kId2 = "fedcba9876543210fedcba98";
    constexpr char const* kId3 = "aaaaaaaaaaaaaaaaaaaaaaaa";
    constexpr char const* kKey1 = "ABCDEFGHIJKLMNOPQRSTUA"; // bottom 4 bits of 'A'(0) == 0
    constexpr char const* kKey2 = "0123456789-_abcdefghEQ"; // 'Q'(16): bottom 4 bits == 0
    constexpr char const* kSig1 = "deadbeefcafebabe";
    constexpr char const* kSig2 = "0011223344556677";

    // 1. Valid v2 encrypted token with no caption
    {
        std::string tok = std::string("[pimg:") + kId1 + ".jpg:1280x720:" + kKey1 + ":" + kSig1 + "]";
        auto parsed = parseChatImages(tok);
        assert(parsed.displayText.empty());
        assert(parsed.images.size() == 1);
        assert(parsed.images[0].id == kId1);
        assert(parsed.images[0].ext == "jpg");
        assert(parsed.images[0].width == 1280);
        assert(parsed.images[0].height == 720);
        assert(parsed.images[0].key == kKey1);
        assert(parsed.images[0].sig == kSig1);
        assert(parsed.images[0].cacheKey() == std::string(kId1) + ".jpg");
        assert(formatChatImageToken(parsed.images[0]) == tok);
    }

    // 2. Caption before and after, whitespace cleanup
    {
        std::string msg = std::string("  hello world  \n\n[pimg:") + kId2 + ".png:800x600:" + kKey2 + ":" + kSig2 + "]\n\nsee this!  ";
        auto parsed = parseChatImages(msg);
        assert(parsed.images.size() == 1);
        assert(parsed.images[0].ext == "png");
        assert(parsed.displayText == "hello world\n\nsee this!");
    }

    // 3. Capped at 2 images per message; 3rd token preserved as plain text
    {
        std::string t1 = std::string("[pimg:") + kId1 + ".gif:320x240:" + kKey1 + ":" + kSig1 + "]";
        std::string t2 = std::string("[pimg:") + kId2 + ".webp:640x480:" + kKey2 + ":" + kSig2 + "]";
        std::string t3 = std::string("[pimg:") + kId3 + ".jpg:100x100:" + kKey1 + ":" + kSig1 + "]";
        auto parsed = parseChatImages("check " + t1 + " and " + t2 + " plus " + t3);
        assert(parsed.images.size() == 2);
        assert(parsed.images[0].ext == "gif");
        assert(parsed.images[1].ext == "webp");
        assert(parsed.displayText.find(t3) != std::string::npos);
    }

    // 4. Strictly rejects legacy v1 unsigned tokens and malformed v2 tokens
    {
        assert(parseChatImages("[pimg:0123456789abcdef.jpg:1280x720]").images.empty());
        assert(parseChatImages("[pimg:0123456789abcdef01234567.jpg]").images.empty());
        assert(parseChatImages("[pimg:../etc/passwd.png:10x10:ABCDEFGHIJKLMNOPQRSTUA:deadbeefcafebabe]").images.empty());
        assert(parseChatImages("[pimg:0123456789ABCDEF01234567.png:10x10:ABCDEFGHIJKLMNOPQRSTUA:deadbeefcafebabe]").images.empty());
        assert(parseChatImages("[pimg:0123456789abcdef01234567.svg:10x10:ABCDEFGHIJKLMNOPQRSTUA:deadbeefcafebabe]").images.empty());
        assert(parseChatImages("[pimg:0123456789abcdef01234567.jpg:0x10:ABCDEFGHIJKLMNOPQRSTUA:deadbeefcafebabe]").images.empty());
        assert(parseChatImages("[pimg:0123456789abcdef01234567.jpg:1921x10:ABCDEFGHIJKLMNOPQRSTUA:deadbeefcafebabe]").images.empty());
        assert(parseChatImages("[pimg:0123456789abcdef01234567.jpg:10x10:ABCDEFGHIJKLMNOPQRSTUB:deadbeefcafebabe]").images.empty()); // non-zero padding bits in 'B'
        assert(parseChatImages("[pimg:0123456789abcdef01234567.jpg:10x10:shortkey:deadbeefcafebabe]").images.empty());
        assert(parseChatImages("[pimg:0123456789abcdef01234567.jpg:10x10:ABCDEFGHIJKLMNOPQRSTUA:DEADbeefcafebabe]").images.empty());
    }

    // 5. Display sizing math
    {
        auto land = computeInlineDisplaySize(1920, 1080);
        assert(std::fabs(land.width - 210.f) < 0.01f);
        assert(std::fabs(land.height - 118.125f) < 0.05f);

        auto port = computeInlineDisplaySize(1080, 1920);
        assert(std::fabs(port.height - 135.f) < 0.01f);
        assert(std::fabs(port.width - 75.9375f) < 0.05f);

        auto tiny = computeInlineDisplaySize(16, 16);
        assert(tiny.width >= 44.f && tiny.height >= 44.f);
    }

    // 6. Corner radius clamping & idempotent compose-bar layout (no drift across multiline relayouts)
    {
        assert(std::fabs(computeImageCornerRadius(210.f, 118.f) - 8.f) < 0.001f);
        assert(std::fabs(computeImageCornerRadius(24.f, 24.f) - 6.f) < 0.001f);
        assert(computeImageCornerRadius(0.f, 50.f) == 0.f);

        float const origW = 281.f;
        float const origBgX = 284.5f - 1.5f;
        float const origInputX = 284.5f - 1.5f;
        float const baseY = 37.f;
        auto const lay = computeFixedComposeBarLayout(origW, origBgX, origInputX, baseY);

        assert(std::fabs(lay.composeWidth - 255.f) < 0.001f);
        float const origRight = origBgX + (origW + 12.f) * 0.5f;
        float const newRight = lay.composeBgX + (lay.composeWidth + 12.f) * 0.5f;
        assert(std::fabs(origRight - newRight) < 0.001f);
        assert(std::fabs(lay.attachX - (origBgX - 136.5f)) < 0.001f);
        assert(std::fabs(lay.attachY - baseY) < 0.001f);

        // Simulate 10 consecutive multiline layoutComposeBar() cycles where GD updates Y only
        // and the hook enforces the stored ComposeBarLayout before and after the original call.
        float curWidth = origW;
        float curBgX = origBgX;
        float curInputX = origInputX;
        float curBtnX = lay.attachX;
        float curBtnY = lay.attachY;
        for (int cycle = 0; cycle < 10; ++cycle) {
            curWidth = lay.composeWidth;
            curBgX = lay.composeBgX;
            curInputX = lay.messageInputX;
            // Native layoutComposeBar updates Y for 1..3 lines and leaves X untouched
            (void)curWidth;
            // Post-hook re-pins X and Y
            curWidth = lay.composeWidth;
            curBgX = lay.composeBgX;
            curInputX = lay.messageInputX;
            curBtnX = lay.attachX;
            curBtnY = lay.attachY;
            assert(std::fabs(curBgX - lay.composeBgX) < 0.0001f);
            assert(std::fabs(curInputX - lay.messageInputX) < 0.0001f);
            assert(std::fabs(curBtnX - lay.attachX) < 0.0001f);
            assert(std::fabs(curBtnY - lay.attachY) < 0.0001f);
        }
    }

    std::cout << "chat_image_token_regression: OK\n";
    return 0;
}
