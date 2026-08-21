// OrganicDock.h - dockable panel management on top of ImGui docking.
// The ImGui equivalent of organicui's ShapeShifter: panel registry ("factory"),
// a main dockspace, a default layout, named layout presets saved to disk
// (View > Layouts, Ctrl+1..9) and a Panels menu to toggle windows.
#pragma once

#include <string>
#include <vector>
#include <functional>
#include "json.hpp"

namespace organic
{

enum class DockZone { Center, Left, LeftBottom, Right, RightBottom, Bottom };

class DockManager
{
public:
    struct Panel
    {
        std::string name;
        DockZone    zone = DockZone::Center;
        std::function<void(bool*)> draw;
        bool open = true;
    };

    void addPanel(const std::string& name, DockZone zone,
                  std::function<void(bool*)> draw, bool defaultOpen = true);
    Panel* find(const std::string& name);

    // Call once per frame BEFORE ImGui::NewFrame (applies pending layout file loads).
    void preNewFrame();
    // Call once per frame inside the frame: draws dockspace host + all open panels.
    void gui();
    // Menus (call between BeginMainMenuBar/EndMainMenuBar).
    // `extra` (optional) draws app-specific entries inside the Panels menu,
    // between the panel toggles and the Open All row — lightshow adds its
    // NDI stream-window submenu there (Panels ▸ NDI ▸ <stream>).
    void panelsMenu(const std::function<void()>& extra = {});
    void viewMenu();
    // Modal popups host (call at top level, after menus).
    void popupsGui();
    // Ctrl+1..9 layout shortcuts (call once per frame).
    void shortcuts();

    void requestReset() { resetRequested = true; }
    void saveLayoutToFile(const std::string& name);
    void requestLoadLayout(const std::string& name);
    void deleteLayout(const std::string& name);
    std::vector<std::string> listLayouts() const;

    nlohmann::json saveState() const;
    void loadState(const nlohmann::json& j);

    std::string layoutsDir = "layouts";

private:
    std::vector<Panel> panels;
    bool resetRequested = false;
    bool wantSaveLayoutPopup = false;
    char layoutNameBuf[64] = "my layout";
    std::string pendingLayoutFile;

    void buildDefaultLayout(unsigned int dockspaceId);
};

} // namespace organic
