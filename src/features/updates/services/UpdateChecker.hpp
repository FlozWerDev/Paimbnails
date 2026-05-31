#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include <string>
#include <functional>
#include <atomic>
#include <filesystem>

namespace paimon::updates {

// ─────────────────────────────────────────────────────────────────────────────
// UpdateChecker — Servicio de actualizaciones de Paimbnails.
//
// Al arranque del mod consulta GitHub Releases (latest) y compara la version
// remota con la version local de mod.json. Si la remota es mayor (orden
// jerarquico semver), `hasUpdate()` devuelve true y la UI puede pintar un
// indicador (alerta encima del boton del Hub, color celeste, etc).
//
// La descarga del .geode se hace bajo demanda desde el popup, exponiendo
// progreso 0..1 + bytes para que la UI muestre la barra.
// ─────────────────────────────────────────────────────────────────────────────

class UpdateChecker {
public:
    enum class State {
        Idle,
        Checking,
        UpToDate,
        UpdateAvailable,
        Failed,
    };

    static UpdateChecker& get();

    // Lanza la peticion a GitHub si no se ha hecho aun. Idempotente.
    void checkAsync();

    // True si ya completamos un check exitoso y la version remota es mayor.
    bool hasUpdate() const { return m_state.load() == State::UpdateAvailable; }
    State state() const { return m_state.load(); }

    std::string const& localVersion() const { return m_localVersion; }
    std::string const& remoteVersion() const { return m_remoteVersion; }
    std::string const& remoteTag() const { return m_remoteTag; }
    std::string const& downloadUrl() const { return m_downloadUrl; }
    std::string const& lastError() const { return m_lastError; }

    // Descarga el .geode reportando progreso. Todos los callbacks corren
    // en el main thread. `onProgress(received, total)` puede llegar varias
    // veces; `onDone(ok, errOrPath)` se llama una sola vez al final.
    void downloadUpdate(
        std::function<void(uint64_t, uint64_t)> onProgress,
        std::function<void(bool, std::string)> onDone
    );

    bool hasPendingInstall() const;
    bool restartToApplyPendingUpdate() const;

    // Aplica el update pendiente SIN relanzar el juego. Ideal para auto-update
    // silencioso al cerrar: el PowerShell helper espera a que el proceso
    // termine, reemplaza el .geode y se va. Al siguiente arranque normal
    // del usuario, el juego ya tiene la nueva version.
    bool applyPendingUpdateInPlace() const;

    // Si auto-update esta habilitado y hay una actualizacion disponible,
    // arranca la descarga en silencio. Idempotente: no re-descarga si ya
    // hay un install pendiente o una descarga en curso.
    void autoDownloadIfNeeded();

    // Cancela una descarga en curso (si la hay).
    void cancelDownload();

private:
    UpdateChecker() = default;

    void onCheckResponse(geode::utils::web::WebResponse& res);

    std::atomic<State> m_state{State::Idle};
    bool m_checkLaunched = false;

    std::string m_localVersion;
    std::string m_remoteVersion;
    std::string m_remoteTag;
    std::string m_downloadUrl;
    std::string m_lastError;
    std::filesystem::path m_pendingUpdatePath;

    geode::async::TaskHolder<geode::utils::web::WebResponse> m_checkTask;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_downloadTask;
    std::atomic<bool> m_downloadCancelled{false};
    std::atomic<bool> m_autoDownloadStarted{false};
};

} // namespace paimon::updates
