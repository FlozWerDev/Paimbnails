#include "../../../utils/FilesystemPath.hpp"
#include "FeedbackService.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/ImageConverter.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/RenderTexture.hpp"
#include "../../../utils/WebHelper.hpp"

#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::feedback {

namespace {

constexpr int kMaxDim = ImageLoadHelper::kMaxImageDim;

void flipVerticalInPlace(uint8_t* data, int width, int height) {
    int const rowSize = width * 4;
    std::vector<uint8_t> tmp(rowSize);
    for (int y = 0; y < height / 2; ++y) {
        uint8_t* top = data + static_cast<size_t>(y) * rowSize;
        uint8_t* bot = data + static_cast<size_t>(height - 1 - y) * rowSize;
        std::memcpy(tmp.data(), top, rowSize);
        std::memcpy(top, bot, rowSize);
        std::memcpy(bot, tmp.data(), rowSize);
    }
}

void forceAlphaOpaque(uint8_t* data, size_t bytes) {
    for (size_t i = 3; i < bytes; i += 4) data[i] = 255;
}

std::string timestampForFile() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
#ifdef GEODE_IS_WINDOWS
    localtime_s(&tmBuf, &tt);
#else
    localtime_r(&tt, &tmBuf);
#endif
    std::ostringstream out;
    out << std::put_time(&tmBuf, "%Y%m%d-%H%M%S");
    return out.str();
}

FeedbackMeta collectMeta() {
    FeedbackMeta meta;
    if (auto* gm = GameManager::get()) {
        meta.username = gm->m_playerName;
    }
    if (auto* am = GJAccountManager::get()) {
        meta.accountID = am->m_accountID;
    }
    if (auto* pl = PlayLayer::get()) {
        meta.scene = "play";
        if (auto* level = pl->m_level) {
            meta.levelName = level->m_levelName;
            meta.levelID = level->m_levelID.value();
        }
    } else if (auto* ed = LevelEditorLayer::get()) {
        meta.scene = "editor";
        if (auto* level = ed->m_level) {
            meta.levelName = level->m_levelName;
            meta.levelID = level->m_levelID.value();
        }
    } else {
        meta.scene = "menu";
    }
    return meta;
}

} // namespace

FeedbackMeta collectCurrentMeta() {
    return collectMeta();
}

FeedbackShot captureScreen() {
    FeedbackShot shot;

    auto* director = CCDirector::get();
    auto* view = director ? director->getOpenGLView() : nullptr;
    auto* scene = director ? director->getRunningScene() : nullptr;
    if (!director || !view || !scene) return shot;
    if (paimon::isRuntimeShuttingDown()) return shot;

    auto frame = view->getFrameSize();
    float wF = std::max(1.f, frame.width);
    float hF = std::max(1.f, frame.height);
    float const longest = std::max(wF, hF);
    if (longest > static_cast<float>(kMaxDim)) {
        float const scale = static_cast<float>(kMaxDim) / longest;
        wF = std::round(wF * scale);
        hF = std::round(hF * scale);
    }
    int const w = std::max(1, static_cast<int>(wF));
    int const h = std::max(1, static_cast<int>(hF));

    RenderTexture rt(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    if (!rt.isValid() || !rt.begin()) return shot;

    scene->visit();
    rt.end();

    auto data = rt.getData();
    if (!data) return shot;

    size_t const bytes = static_cast<size_t>(w) * h * 4;
    auto rgba = std::shared_ptr<uint8_t>(new uint8_t[bytes], std::default_delete<uint8_t[]>());
    std::memcpy(rgba.get(), data.get(), bytes);
    // readback is bottom-up; the texture + png paths expect top-down rows.
    flipVerticalInPlace(rgba.get(), w, h);
    forceAlphaOpaque(rgba.get(), bytes);

    auto loaded = ImageLoadHelper::createFromRGBA(rgba.get(), w, h, /*copyBuffer=*/false);
    if (!loaded.success || !loaded.texture) return shot;
    loaded.texture->setAntiAliasTexParameters();

    shot.texture = geode::Ref<CCTexture2D>::adopt(loaded.texture);
    shot.rgba = std::move(rgba);
    shot.width = w;
    shot.height = h;
    shot.valid = true;
    return shot;
}

std::filesystem::path outboxDir() {
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "feedback_outbox";
}

void submit(
    std::string const& message,
    std::vector<uint8_t> const& annotatedPng,
    bool hasImage,
    FeedbackMeta const& meta,
    SubmitCallback callback
) {
    // always archive locally first: a failed post must not eat the report.
    std::filesystem::path dir;
    try {
        dir = outboxDir() / timestampForFile();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (!ec) {
            if (hasImage && !annotatedPng.empty()) {
                auto res = geode::utils::file::writeBinary(paimon::paths::toSdkPath(dir / "screenshot.png"), annotatedPng);
                if (res.isErr()) {
                    log::warn("[Feedback] Could not archive screenshot: {}", res.unwrapErr());
                }
            }
            matjson::Value json = matjson::makeObject({
                {"message", message},
                {"username", meta.username},
                {"accountID", meta.accountID},
                {"levelName", meta.levelName},
                {"levelID", meta.levelID},
                {"scene", meta.scene},
                {"hasImage", hasImage},
                {"modVersion", Mod::get()->getVersion().toVString()},
                {"createdAt", timestampForFile()},
            });
            auto res = geode::utils::file::writeString(paimon::paths::toSdkPath(dir / "feedback.json"), json.dump(4));
            if (res.isErr()) {
                log::warn("[Feedback] Could not archive report: {}", res.unwrapErr());
                dir.clear();
            }
        } else {
            dir.clear();
        }
    } catch (std::exception const& e) {
        log::warn("[Feedback] Archive failed: {}", e.what());
        dir.clear();
    }

    std::string const url = HttpClient::get().getServerURL() + "/api/feedback";

    web::MultipartForm form;
    form.param("message", message);
    form.param("username", meta.username);
    form.param("accountID", std::to_string(meta.accountID));
    form.param("levelName", meta.levelName);
    form.param("levelID", std::to_string(meta.levelID));
    form.param("scene", meta.scene);
    form.param("modVersion", Mod::get()->getVersion().toVString());
    if (hasImage && !annotatedPng.empty()) {
        form.file("image", std::span<uint8_t const>(annotatedPng), "feedback.png", "image/png");
    }

    web::WebRequest req;
    req.timeout(asp::Duration::fromSecs(30));
    req.acceptEncoding("gzip, deflate");
    req.header("X-API-Key", HttpClient::get().getApiKey());
    req.bodyMultipart(form);

    std::string archivedDir = dir.empty() ? std::string() : paimon::paths::toUtf8(dir);
    WebHelper::dispatch(std::move(req), "POST", url,
        [callback = std::move(callback), archivedDir](web::WebResponse res) mutable {
            if (res.ok()) {
                if (callback) callback(true, "ok");
                return;
            }
            std::string body = res.string().unwrapOr("");
            log::warn("[Feedback] Submit failed (HTTP {}): {}", res.code(),
                body.empty() ? std::string("no body") : body);
            if (callback) {
                callback(false, archivedDir);
            }
        }
    );
}

} // namespace paimon::feedback
