#include "UpdateChecker.hpp"
#include "../../../utils/WebHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/string.hpp>
#include <matjson.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace paimon::updates {

namespace {

constexpr auto kReleasesApiUrl =
    "https://api.github.com/repos/FlozWerDev/Paimbnails/releases/latest";
constexpr auto kAssetName = "flozwer.paimbnails2.geode";

// Limpia un string de version: quita prefijos 'v'/'V' y espacios.
std::string sanitizeVersion(std::string v) {
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
    if (!v.empty() && (v.front() == 'v' || v.front() == 'V')) v.erase(v.begin());
    return v;
}

// Compara semver hierarquicamente: major.minor.patch[-pre].
// Devuelve >0 si remote > local, 0 si iguales, <0 si remote < local.
// Usa VersionInfo de Geode si la cadena es parseable; sino cae a un
// comparador manual numerico componente a componente.
int compareVersions(std::string const& localStr, std::string const& remoteStr) {
    auto local  = sanitizeVersion(localStr);
    auto remote = sanitizeVersion(remoteStr);

    auto localRes  = VersionInfo::parse("v" + local);
    auto remoteRes = VersionInfo::parse("v" + remote);

    if (localRes.isOk() && remoteRes.isOk()) {
        auto const& l = localRes.unwrap();
        auto const& r = remoteRes.unwrap();
        if (r > l) return 1;
        if (r < l) return -1;
        return 0;
    }

    // Fallback: compara componentes numericos.
    auto split = [](std::string const& s) {
        std::vector<int> out;
        std::string cur;
        for (char c : s) {
            if (std::isdigit((unsigned char)c)) {
                cur.push_back(c);
            } else if (c == '.' || c == '-' || c == '+') {
                if (!cur.empty()) { out.push_back(std::atoi(cur.c_str())); cur.clear(); }
                if (c != '.') break;
            } else {
                break;
            }
        }
        if (!cur.empty()) out.push_back(std::atoi(cur.c_str()));
        return out;
    };

    auto la = split(local);
    auto ra = split(remote);
    size_t n = std::max(la.size(), ra.size());
    la.resize(n, 0);
    ra.resize(n, 0);
    for (size_t i = 0; i < n; i++) {
        if (ra[i] > la[i]) return 1;
        if (ra[i] < la[i]) return -1;
    }
    return 0;
}

} // namespace

UpdateChecker& UpdateChecker::get() {
    static UpdateChecker s;
    return s;
}

void UpdateChecker::checkAsync() {
    if (m_checkLaunched) return;
    m_checkLaunched = true;
    m_state.store(State::Checking);

    m_localVersion = Mod::get()->getVersion().toVString(false);

    auto req = web::WebRequest()
        .timeout(std::chrono::seconds(15))
        .userAgent("Paimbnails-UpdateChecker/1.0")
        .header("Accept", "application/vnd.github+json");

    WebHelper::dispatchOwned(
        m_checkTask,
        std::move(req),
        "GET",
        kReleasesApiUrl,
        [this](web::WebResponse res) {
            this->onCheckResponse(res);
        }
    );
}

void UpdateChecker::onCheckResponse(web::WebResponse& res) {
    if (!res.ok()) {
        m_lastError = fmt::format("HTTP {}", res.code());
        log::warn("[UpdateChecker] check failed: {}", m_lastError);
        m_state.store(State::Failed);
        return;
    }

    auto body = res.string().unwrapOr("");
    if (body.empty()) {
        m_lastError = "empty body";
        m_state.store(State::Failed);
        return;
    }

    auto parsed = matjson::parse(body);
    if (!parsed.isOk()) {
        m_lastError = "invalid json";
        m_state.store(State::Failed);
        return;
    }
    auto json = parsed.unwrap();

    std::string tag;
    if (json["tag_name"].isString()) {
        tag = json["tag_name"].asString().unwrapOr("");
    }
    if (tag.empty()) {
        m_lastError = "no tag_name";
        m_state.store(State::Failed);
        return;
    }
    m_remoteTag = tag;
    m_remoteVersion = sanitizeVersion(tag);

    // Construye URL de descarga: prioriza el asset con el nombre esperado;
    // si no aparece, cae al pattern conocido /releases/download/<tag>/<file>.
    m_downloadUrl.clear();
    if (json["assets"].isArray()) {
        for (auto const& asset : json["assets"]) {
            std::string name = asset["name"].isString()
                ? asset["name"].asString().unwrapOr("") : "";
            std::string url  = asset["browser_download_url"].isString()
                ? asset["browser_download_url"].asString().unwrapOr("") : "";
            if (name == kAssetName && !url.empty()) {
                m_downloadUrl = url;
                break;
            }
        }
    }
    if (m_downloadUrl.empty()) {
        m_downloadUrl = fmt::format(
            "https://github.com/FlozWerDev/Paimbnails/releases/download/{}/{}",
            tag, kAssetName
        );
    }

    int cmp = compareVersions(m_localVersion, m_remoteVersion);
    log::info("[UpdateChecker] local={} remote={} cmp={}",
        m_localVersion, m_remoteVersion, cmp);

    if (cmp > 0) {
        m_state.store(State::UpdateAvailable);
    } else {
        m_state.store(State::UpToDate);
    }
}

void UpdateChecker::downloadUpdate(
    std::function<void(uint64_t, uint64_t)> onProgress,
    std::function<void(bool, std::string)> onDone
) {
    if (m_downloadUrl.empty()) {
        if (onDone) onDone(false, "no download url");
        return;
    }

    m_downloadCancelled.store(false);

    // Captura el progreso. El callback de onProgress lo invoca el worker thread
    // de la libreria HTTP, asi que despachamos al main thread antes de tocar UI.
    auto progressShared = std::make_shared<std::function<void(uint64_t, uint64_t)>>(std::move(onProgress));
    auto doneShared     = std::make_shared<std::function<void(bool, std::string)>>(std::move(onDone));

    auto req = web::WebRequest()
        .timeout(std::chrono::minutes(5))
        .userAgent("Paimbnails-UpdateChecker/1.0");

    req.onProgress([progressShared, this](web::WebProgress const& p) {
        if (!progressShared || !*progressShared) return;
        uint64_t cur = static_cast<uint64_t>(p.downloaded());
        uint64_t tot = static_cast<uint64_t>(p.downloadTotal());
        Loader::get()->queueInMainThread([progressShared, cur, tot]() {
            if (progressShared && *progressShared) (*progressShared)(cur, tot);
        });
    });

    WebHelper::dispatchOwned(
        m_downloadTask,
        std::move(req),
        "GET",
        m_downloadUrl,
        [this, doneShared](web::WebResponse res) {
            auto fail = [doneShared](std::string err) {
                if (doneShared && *doneShared) (*doneShared)(false, std::move(err));
            };

            if (m_downloadCancelled.load()) {
                fail("cancelled");
                return;
            }
            if (!res.ok()) {
                fail(fmt::format("HTTP {}", res.code()));
                return;
            }

            auto bytes = std::move(res).data();
            if (bytes.empty()) {
                fail("empty payload");
                return;
            }

            // Destino: reemplaza el .geode actual del mod.
            std::filesystem::path dest = Mod::get()->getPackagePath();
            std::error_code ec;

            // Backup (best-effort) por si el rename falla en Windows.
            auto backup = dest;
            backup += ".old";
            std::filesystem::remove(backup, ec);
            std::filesystem::rename(dest, backup, ec);

            std::ofstream out(dest, std::ios::binary | std::ios::trunc);
            if (!out) {
                // restaurar backup si no se pudo abrir destino
                std::filesystem::rename(backup, dest, ec);
                fail("cannot open dest file");
                return;
            }
            out.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
            out.close();

            // borra backup si la escritura tuvo exito
            std::filesystem::remove(backup, ec);

            if (doneShared && *doneShared) {
                (*doneShared)(true, geode::utils::string::pathToString(dest));
            }
        }
    );
}

void UpdateChecker::cancelDownload() {
    m_downloadCancelled.store(true);
}

} // namespace paimon::updates
