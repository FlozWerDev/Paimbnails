#pragma once

#include "ProjectStorage.hpp"
#include <memory>

namespace paimon::file {

// owns a private output directory. Publication stays reversible until commit().
// callers serialize publication and keep readers out of the swap on the main thread.
class StagedDirectory final {
public:
    StagedDirectory() = default;
    StagedDirectory(StagedDirectory const&) = delete;
    StagedDirectory& operator=(StagedDirectory const&) = delete;

    static std::shared_ptr<StagedDirectory> create(
        std::filesystem::path const& root, std::error_code& error) {
        auto staged = std::make_shared<StagedDirectory>();
        staged->m_staging = storage::reserveProjectDirectory(root, error);
        return staged->m_staging.empty() ? nullptr : staged;
    }

    ~StagedDirectory() {
        std::error_code error;
        rollback(error);
        removeOwnedDirectory(m_staging);
        if (!m_preserveBackup) removeOwnedDirectory(m_backup);
    }

    std::filesystem::path const& path() const { return m_staging; }
    std::filesystem::path const& backupPath() const { return m_backup; }

    bool publishTo(std::filesystem::path const& destination, std::error_code& error) {
        error.clear();
        if (m_staging.empty() || m_published || m_preserveBackup || destination.empty() ||
            destination.filename().empty() || destination.filename() == "." ||
            destination.filename() == "..") {
            error = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
        auto target = std::filesystem::absolute(destination, error);
        if (error) return false;
        auto status = std::filesystem::symlink_status(target, error);
        bool missing = error == std::errc::no_such_file_or_directory;
        if (missing) error.clear();
        if (error) return false;
        if (!missing && std::filesystem::exists(status)) {
            if (!std::filesystem::is_directory(status) || std::filesystem::is_symlink(status)) {
                error = std::make_error_code(std::errc::not_a_directory);
                return false;
            }
            m_backup = storage::reserveProjectDirectory(target.parent_path(), error);
            if (m_backup.empty()) return false;
            std::filesystem::rename(target, m_backup / "previous", error);
            if (error) {
                removeOwnedDirectory(m_backup);
                m_backup.clear();
                return false;
            }
        }
        std::filesystem::rename(m_staging, target, error);
        if (error) {
            restorePrevious(target, error);
            return false;
        }
        m_destination = std::move(target);
        m_published = true;
        return true;
    }

    bool rollback(std::error_code& error) {
        error.clear();
        if (!m_published) return !m_preserveBackup;
        std::filesystem::rename(m_destination, m_staging, error);
        if (error) {
            m_preserveBackup = true;
            return false;
        }
        m_published = false;
        m_preserveBackup = false;
        restorePrevious(m_destination, error);
        return !error;
    }

    void commit() {
        if (!m_published) return;
        m_published = false;
        removeOwnedDirectory(m_backup);
        m_backup.clear();
    }

private:
    static void removeOwnedDirectory(std::filesystem::path const& directory) {
        if (directory.empty()) return;
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }

    void restorePrevious(std::filesystem::path const& destination, std::error_code& error) {
        if (m_backup.empty()) return;
        std::error_code restoreError;
        std::filesystem::rename(m_backup / "previous", destination, restoreError);
        if (restoreError) {
            // keep the last good output available for recovery if rollback is blocked.
            m_preserveBackup = true;
            error = restoreError;
            return;
        }
        removeOwnedDirectory(m_backup);
        m_backup.clear();
    }

    std::filesystem::path m_staging;
    std::filesystem::path m_destination;
    std::filesystem::path m_backup;
    bool m_published = false;
    bool m_preserveBackup = false;
};

} // namespace paimon::file
