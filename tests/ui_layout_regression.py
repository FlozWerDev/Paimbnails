"""Static layout contracts for the popups/layers fixed from screenshots.

GD Slider grooves are centred on the node position, so a slider placed at
the left edge of its zone spills half its width outside. These checks keep
the geometry invariants that the screenshots violated.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace")


def main():
    node = read("src/features/liquid-glass/services/GlassNode.cpp")
    capture = read("src/features/liquid-glass/services/GlassCapture.cpp")
    decorator = read("src/features/liquid-glass/services/GlassDecorator.cpp")
    shader = read("resources/shaders/liquid_glass_ui.glsl")
    hub = read("src/layers/PaimonHubLayer.cpp")
    # The backdrop is copied from the bound framebuffer while the scene
    # draws; the scene is never rendered a second time for the glass.
    assert "glCopyTexSubImage2D(" in capture
    assert "CCRenderTexture" not in capture and "->visit()" not in capture
    # Plates map local space through the transformed framebuffer axes.
    assert "u_fbAxisX" in shader and "u_fbAxisY" in shader
    assert '"u_fbAxisX"' in node and '"u_fbAxisY"' in node
    # Only the slice art is hidden, never the host with its labels/children,
    # and the plate draws below everything the host owns.
    assert "art->setVisible(false);" in node
    assert "m_host->setVisible" not in node
    assert "constexpr int kPlateZ = -10000;" in decorator
    # Hub buttons come from the global ButtonSprite hook; panels get plates.
    assert "setGlassButtonOpacity" not in hub
    assert "attachGlass(m_sidebarBg" in hub and "attachGlass(m_detailsBg" in hub

    glass = read("src/features/liquid-glass/ui/GlassConfigPopup.cpp")
    # sliders centred in their zone, labels/values on the same line,
    # and nothing anchored at the popup's left edge any more.
    assert "slider->setPosition({247.f, y});" in glass
    assert "slider->setAnchorPoint({0.f, 0.5f});" not in glass
    assert "previewBg->setPosition({260.f, 8.f});" in glass
    assert 'resetBtn->setPosition({78.f, 22.f});' in glass

    live = read("src/features/texture-studio/ui/LiveSlotEditor.cpp")
    # pages scroll inside the panel below the tab strip instead of spilling
    # into the footer; a scrollbar makes overflow discoverable.
    assert "220.f" not in live.split("float contentHeight")[1].split(";")[0]
    assert "Scrollbar::create(m_controlsScroll)" in live
    assert "CCSize view{m_left.size.width - 18.f, m_left.size.height - kTabStrip - 8.f};" in live
    assert "m_controlsScroll->setPosition({m_left.getMinX() + 6.f, m_left.getMinY() + 6.f});" in live
    # body sits between header and footer; footer actions stay out of the panels.
    assert "float bodyBottom = kFooterH;" in live and "float bodyTop = win.height - kHeaderH;" in live
    assert "float y = kFooterH * .5f;" in live

    hub = read("src/features/community/ui/CommunityHubLayer.cpp")
    # title icon parked fully left of the title, search icon fitted to the
    # toolbar strip, podium crowns capped inside the podium panel.
    assert "winSize.width / 2.f - half - iconHalf - 6.f" in hub
    assert "magnifier->setScale(22.f / magH);" in hub
    assert "crown->setScale(30.f / crownH * iconScale[i]);" in hub
    assert "float tabsY = winSize.height - 78.f;" in hub

    emotes = read("src/features/emotes/ui/EmotePickerPopup.cpp")
    # slider zone must cover the ~97px groove; tab strip shrinks to fit;
    # preview bar leaves the resize corner free.
    assert emotes.count("float sliderW = 104.f;") == 2
    assert "availW / naturalW" in emotes
    assert "contentW - RESIZE_SZ - 4.f" in emotes

    print("PASS: ui layout contracts (glass, texture live, community, emotes).")


if __name__ == "__main__":
    main()
