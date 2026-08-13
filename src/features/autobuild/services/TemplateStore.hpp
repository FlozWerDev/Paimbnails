#pragma once

// Templates on disk: one text file per template under config/autobuild, plus
// the importer for the .tblib libraries older autobuild mods produced.

#include <Geode/Geode.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include "../AutobuildTypes.hpp"

namespace paimon::autobuild {

class TemplateStore {
public:
    static TemplateStore& get();

    void load();
    void reload();
    std::vector<Template> const& all() const { return m_items; }

    int selectedIndex() const { return m_selected; }
    void select(int index);
    Template const* selected() const;

    int add(Template tpl);              // returns the new index
    void replace(int index, Template tpl);
    void rename(int index, std::string name);
    void remove(int index);
    void persist(int index);

    geode::Result<int> importFile(std::filesystem::path const& path);

    static std::filesystem::path directory();

private:
    std::vector<Template> m_items;
    int m_selected = -1;
    bool m_loaded = false;
};

geode::Result<std::string> serialize(Template const& tpl);
geode::Result<Template> deserialize(std::string const& text);

} // namespace paimon::autobuild
