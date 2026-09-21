#pragma once
// Biblioteca de pinturas: guarda como esta pintada una capa para reutilizarla.
// Todo desde el hilo principal, como IconProjectStore.

#include "../data/FillSpec.hpp"

#include <Geode/Geode.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace paimon::icon_maker {

struct SavedStyle {
    std::string id;
    std::string name;
    FillSpec fill;
};

class StyleStore final {
public:
    static constexpr std::size_t kMaxStyles = 60;

    static StyleStore& get();

    // Idempotente.
    void load();

    std::vector<SavedStyle> const& list() const { return m_styles; }
    SavedStyle const* find(std::string_view id) const;

    geode::Result<> add(std::string name, FillSpec const& fill);
    geode::Result<> rename(std::string_view id, std::string name);
    geode::Result<> remove(std::string_view id);

private:
    StyleStore() = default;
    ~StyleStore() = default;
    StyleStore(StyleStore const&) = delete;
    StyleStore& operator=(StyleStore const&) = delete;

    geode::Result<> save();

    bool m_loaded = false;
    std::vector<SavedStyle> m_styles;
};

}  // namespace paimon::icon_maker
