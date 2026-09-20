#pragma once

#include "../persist/TextureProject.hpp"

#include <atomic>
#include <memory>
#include <unordered_map>

namespace paimon::texture_studio {

class LiveSlotRuntime final {
public:
    static LiveSlotRuntime& get();
    static void normalize(TextureProject& project);
    void restoreSaved();
    void start() { if (!m_restored) restoreSaved(); }
    void showOriginal() { m_enabled = false; }
    geode::Result<> preview(TextureProject project);
    geode::Result<> activate(TextureProject const& project);
    geode::Result<> disable();
    void onTextureLoaded(char const* path, cocos2d::CCTexture2D* texture, bool skipSuffix);
    void refreshTextures();
    bool cellColor(int row, cocos2d::ccColor3B* out);
    cocos2d::CCGLProgram* prepareDraw(cocos2d::CCTexture2D* texture,
                                     cocos2d::CCGLProgram* original);
    void onGLContextReload();
    std::string status() const;

private:
    struct Source {
        std::string png, plist, base;
        geode::Ref<cocos2d::CCTexture2D> texture;
        geode::Ref<cocos2d::CCTexture2D> mask;
        int width = 0, height = 0;
    };
    void collectSources();
    void prepareMasks();

    geode::Ref<cocos2d::CCGLProgram> m_shader;
    bool m_uniformsDirty = true;
    bool m_premultiplied = false;
    float m_maskScaleX = 1.f, m_maskScaleY = 1.f;
    TextureProject m_project;
    std::vector<Source> m_sources;
    std::unordered_map<cocos2d::CCTexture2D*, std::size_t> m_textures;
    std::shared_ptr<std::atomic<unsigned>> m_generation =
        std::make_shared<std::atomic<unsigned>>(0);
    bool m_enabled = false;
    bool m_restored = false;
    bool m_pending = false;
    std::size_t m_failures = 0;
    std::string m_error;
};

}
