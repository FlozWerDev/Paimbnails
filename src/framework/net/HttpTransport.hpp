#pragma once

// HttpTransport.hpp — Capa de transporte HTTP de bajo nivel.
// Extrae las primitivas de comunicacion de HttpClient para que los
// servicios de cada feature puedan usarlas sin depender de la fachada completa.
//
// Durante la migracion, HttpClient.cpp sigue existiendo y delega aqui.
// Los servicios nuevos (ThumbnailTransportClient, ModerationService, etc.)
// pueden llamar directamente a HttpTransport.

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/function.hpp>
#include "../WebHelper.hpp"
#include "../Debug.hpp"
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <span>

namespace paimon::net {

class HttpTransport {
public:
    using TextCallback   = geode::CopyableFunction<void(bool success, std::string const& response)>;
    using BinaryCallback = geode::CopyableFunction<void(bool success, std::vector<uint8_t> const& data)>;

    static HttpTransport& get() {
        static HttpTransport instance;
        return instance;
    }

    // ── Configuracion ───────────────────────────────────────────────

    std::string const& serverURL()  const { return m_serverURL; }
    std::string const& modCode()    const { return m_modCode; }
    std::string const& apiKey()     const { return m_apiKey; }

    void setServerURL(std::string const& url) {
        m_serverURL = url;
        if (!m_serverURL.empty() && m_serverURL.back() == '/') {
            m_serverURL.pop_back();
        }
    }

    void setModCode(std::string const& code) {
        m_modCode = code;
        geode::Mod::get()->setSavedValue("mod-code", code);
    }

    // ── Primitivas de request ───────────────────────────────────────

    // GET/POST con respuesta texto.
    void request(
        std::string const& url,
        std::string const& method,
        std::string const& body,
        std::vector<std::string> const& headers,
        TextCallback callback,
        bool includeModCode = true
    ) {
        auto req = geode::utils::web::WebRequest();
        req.timeout(std::chrono::seconds(10));
        req.userAgent("Paimbnails/2.x (Geode)");
        req.acceptEncoding("gzip, deflate");

        bool hasExplicitModCode = false;
        applyHeaders(req, headers, &hasExplicitModCode);

        if (includeModCode && !hasExplicitModCode && !m_modCode.empty()) {
            req.header("X-Mod-Code", m_modCode);
        }

        if (method == "POST" && !body.empty()) {
            req.bodyString(body);
        }

        WebHelper::dispatch(std::move(req), method, url,
            [callback](geode::utils::web::WebResponse res) {
                bool ok = res.ok();
                std::string text = ok
                    ? res.string().unwrapOr("")
                    : ("HTTP " + std::to_string(res.code()) + ": " + res.string().unwrapOr("Unknown error"));
                if (callback) callback(ok, text);
            });
    }

    // GET binario con validacion de magic bytes de imagen.
    void requestBinary(
        std::string const& url,
        std::vector<std::string> const& headers,
        BinaryCallback callback,
        bool validateImage = true
    ) {
        auto req = geode::utils::web::WebRequest();
        req.timeout(std::chrono::seconds(15));
        req.userAgent("Paimbnails/2.x (Geode)");
        req.acceptEncoding("gzip, deflate");

        applyHeaders(req, headers);

        if (!m_modCode.empty()) {
            req.header("X-Mod-Code", m_modCode);
        }

        WebHelper::dispatch(std::move(req), "GET", url,
            [callback, validateImage, url](geode::utils::web::WebResponse res) {
                bool ok = res.ok();
                std::vector<uint8_t> data = ok ? res.data() : std::vector<uint8_t>{};

                if (ok && !data.empty()) {
                    // Rechazar JSON/HTML disfrazado de binario
                    auto ct = res.header("Content-Type");
                    std::string contentType = ct.has_value() ? std::string(ct.value()) : "";
                    if (contentType.find("application/json") != std::string::npos ||
                        contentType.find("text/html") != std::string::npos) {
                        ok = false;
                        data.clear();
                    }

                    // Validar magic bytes
                    if (validateImage && ok && data.size() >= 4) {
                        if (!hasImageMagicBytes(data)) {
                            ok = false;
                            data.clear();
                        }
                    }
                }

                if (callback) callback(ok, data);
            });
    }

    // Upload multipart.
    void upload(
        std::string const& url,
        std::string const& fieldName,
        std::string const& filename,
        std::vector<uint8_t> const& data,
        std::vector<std::pair<std::string, std::string>> const& formFields,
        std::vector<std::string> const& headers,
        TextCallback callback,
        std::string const& contentType = "image/png"
    ) {
        geode::utils::web::MultipartForm form;
        for (auto const& f : formFields) {
            form.param(f.first, f.second);
        }
        form.file(fieldName, std::span<uint8_t const>(data), filename, contentType);

        auto req = geode::utils::web::WebRequest();
        req.timeout(std::chrono::seconds(30));
        req.userAgent("Paimbnails/2.x (Geode)");
        req.acceptEncoding("gzip, deflate");

        applyHeaders(req, headers);

        if (!m_modCode.empty()) {
            req.header("X-Mod-Code", m_modCode);
        }

        req.bodyMultipart(form);

        WebHelper::dispatch(std::move(req), "POST", url,
            [callback](geode::utils::web::WebResponse res) {
                bool ok = res.ok();
                std::string text = ok
                    ? res.string().unwrapOr("")
                    : ("HTTP " + std::to_string(res.code()) + ": " + res.string().unwrapOr("Unknown error"));
                if (callback) callback(ok, text);
            });
    }

    // ── Utilidades ──────────────────────────────────────────────────

    // URL-encode de un parametro de query.
    static std::string encodeQueryParam(std::string const& value) {
        std::ostringstream encoded;
        encoded << std::uppercase << std::hex;
        for (unsigned char ch : value) {
            if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
                encoded << static_cast<char>(ch);
            } else {
                encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
            }
        }
        return encoded.str();
    }

    // Valida que una URL sea segura (previene SSRF).
    static bool isUrlSafe(std::string const& url) {
        if (url.empty()) return false;

        std::string lower = url;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // Solo HTTPS (rechazar HTTP plano y cualquier otro esquema)
        bool isHttps = lower.starts_with("https://");
        bool isHttp  = lower.starts_with("http://");
        if (!isHttps && !isHttp) return false;

        // Extraer host
        size_t hostStart = lower.find("://") + 3;

        // Rechazar credentials en URL
        size_t at = lower.find('@', hostStart);
        size_t slash = lower.find('/', hostStart);
        if (at != std::string::npos && (slash == std::string::npos || at < slash)) return false;

        std::string hostPort = (slash != std::string::npos)
            ? lower.substr(hostStart, slash - hostStart)
            : lower.substr(hostStart);

        size_t colon = hostPort.rfind(':');
        std::string host = (colon != std::string::npos) ? hostPort.substr(0, colon) : hostPort;

        if (host.empty()) return false;

        // Bloquear localhost y rangos privados
        if (host == "localhost" || host == "127.0.0.1" || host == "::1" ||
            host == "[::1]" || host == "0.0.0.0") return false;

        if (host.starts_with("10.") || host.starts_with("192.168.") ||
            host.starts_with("169.254.")) return false;

        if (host.starts_with("172.")) {
            size_t dot = host.find('.', 4);
            if (dot != std::string::npos) {
                auto parsed = geode::utils::numFromString<int>(host.substr(4, dot - 4));
                if (parsed.isOk()) {
                    int octet2 = parsed.unwrap();
                    if (octet2 >= 16 && octet2 <= 31) return false;
                }
            }
        }

        return true;
    }

    // Comprueba si los primeros bytes son de una imagen conocida.
    static bool hasImageMagicBytes(std::vector<uint8_t> const& data) {
        if (data.size() < 4) return false;
        // PNG
        if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) return true;
        // JPEG
        if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) return true;
        // GIF
        if (data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8') return true;
        // WEBP
        if (data.size() >= 12 &&
            data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
            data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') return true;
        // BMP
        if (data[0] == 'B' && data[1] == 'M') return true;
        return false;
    }

private:
    HttpTransport() {
        m_serverURL = "https://api.flozwer.org";
        m_apiKey    = "074b91c9-6631-4670-a6f08a2ce970-0183-471b";
        m_modCode   = geode::Mod::get()->getSavedValue<std::string>("mod-code", "");
    }
    ~HttpTransport() = default;
    HttpTransport(HttpTransport const&) = delete;
    HttpTransport& operator=(HttpTransport const&) = delete;

    // Aplica headers en formato "Key: Value" a un WebRequest.
    static void applyHeaders(geode::utils::web::WebRequest& req,
                             std::vector<std::string> const& headers,
                             bool* outHasModCode = nullptr) {
        for (auto const& h : headers) {
            auto sep = h.find(':');
            if (sep == std::string::npos) continue;
            std::string key = h.substr(0, sep);
            std::string val = h.substr(sep + 1);
            val.erase(0, val.find_first_not_of(" \t"));
            req.header(key, val);
            if (outHasModCode &&
                (key == "X-Mod-Code" || key == "x-mod-code")) {
                *outHasModCode = true;
            }
        }
    }

    std::string m_serverURL;
    std::string m_apiKey;
    std::string m_modCode;
};

} // namespace paimon::net
