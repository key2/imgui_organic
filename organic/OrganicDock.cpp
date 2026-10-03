#include "OrganicDock.h"
#include "OrganicCore.h"
#include "imgui.h"
#include "imgui_internal.h" // DockBuilder API
#include <filesystem>
#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;

namespace organic
{

void DockManager::addPanel(const std::string& name, DockZone zone,
                           std::function<void(bool*)> draw, bool defaultOpen)
{
    panels.push_back({ name, zone, std::move(draw), defaultOpen });
}

DockManager::Panel* DockManager::find(const std::string& name)
{
    for (auto& p : panels) if (p.name == name) return &p;
    return nullptr;
}

void DockManager::preNewFrame()
{
    if (pendingLayoutFile.empty()) return;
    if (fs::exists(pendingLayoutFile))
    {
        ImGui::LoadIniSettingsFromDisk(pendingLayoutFile.c_str());
        // sidecar: panel open states
        std::string sidecar = pendingLayoutFile + ".panels.json";
        if (fs::exists(sidecar))
        {
            try
            {
                std::ifstream f(sidecar);
                nlohmann::json j; f >> j;
                for (auto& [k, v] : j.items())
                    if (Panel* p = find(k)) p->open = v.get<bool>();
            }
            catch (...) {}
        }
        OLOG("Layout", "Loaded layout '" << pendingLayoutFile << "'");
    }
    pendingLayoutFile.clear();
}

void DockManager::buildDefaultLayout(unsigned int dockspaceId)
{
    ImGuiID id = (ImGuiID)dockspaceId;
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(id, ImGui::GetMainViewport()->WorkSize);

    ImGuiID center = id;
    ImGuiID bottom      = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.24f, nullptr, &center);
    ImGuiID left        = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.19f, nullptr, &center);
    ImGuiID leftBottom  = ImGui::DockBuilderSplitNode(left,   ImGuiDir_Down, 0.45f, nullptr, &left);
    ImGuiID right       = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, nullptr, &center);
    ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right,  ImGuiDir_Down, 0.42f, nullptr, &right);

    auto nodeFor = [&](DockZone z) -> ImGuiID
    {
        switch (z)
        {
        case DockZone::Center:      return center;
        case DockZone::Left:        return left;
        case DockZone::LeftBottom:  return leftBottom;
        case DockZone::Right:       return right;
        case DockZone::RightBottom: return rightBottom;
        case DockZone::Bottom:      return bottom;
        }
        return center;
    };
    for (auto& p : panels)
        ImGui::DockBuilderDockWindow(p.name.c_str(), nodeFor(p.zone));
    ImGui::DockBuilderFinish(id);
    resetRequested = false;
    OLOG("Layout", "Default layout applied");
}

void DockManager::gui()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus;
    ImGui::Begin("##OrganicDockHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceId = ImGui::GetID("OrganicDockSpace");
    if (resetRequested || ImGui::DockBuilderGetNode(dockspaceId) == nullptr)
        buildDefaultLayout(dockspaceId);
    ImGui::DockSpace(dockspaceId, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();

    for (auto& p : panels)
        if (p.open && p.draw)
            p.draw(&p.open);
}

void DockManager::panelsMenu(const std::function<void()>& extra)
{
    if (ImGui::BeginMenu(translate("Panels")))
    {
        for (auto& p : panels)
        {
            // "<translated>###<name>" keeps the MenuItem's ID stable across languages.
            std::string label = std::string(translate(p.name.c_str())) + "###" + p.name;
            ImGui::MenuItem(label.c_str(), nullptr, &p.open);
        }
        if (extra) extra(); // app-specific entries (e.g. lightshow's NDI submenu)
        ImGui::Separator();
        if (ImGui::MenuItem(translate("Open All")))
            for (auto& p : panels) p.open = true;
        ImGui::EndMenu();
    }
}

void DockManager::viewMenu()
{
    if (ImGui::BeginMenu(translate("View")))
    {
        if (ImGui::MenuItem(translate("Reset Layout"))) requestReset();
        if (ImGui::MenuItem(translate("Save Layout As..."))) wantSaveLayoutPopup = true;
        auto layouts = listLayouts();
        if (!layouts.empty())
        {
            ImGui::Separator();
            int n = 1;
            for (auto& l : layouts)
            {
                std::string sc = n <= 9 ? "Ctrl+" + std::to_string(n) : "";
                if (ImGui::MenuItem(l.c_str(), sc.c_str()))
                    requestLoadLayout(l);
                n++;
            }
            if (ImGui::BeginMenu(translate("Delete Layout")))
            {
                for (auto& l : layouts)
                    if (ImGui::MenuItem(l.c_str()))
                        deleteLayout(l);
                ImGui::EndMenu();
            }
        }
        ImGui::EndMenu();
    }
}

void DockManager::popupsGui()
{
    if (wantSaveLayoutPopup)
    {
        ImGui::OpenPopup("###SaveLayoutAs");
        wantSaveLayoutPopup = false;
    }
    if (ImGui::BeginPopupModal((std::string(translate("Save Layout As")) + "###SaveLayoutAs").c_str(),
                               nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText(translate("Name"), layoutNameBuf, sizeof(layoutNameBuf));
        if (ImGui::Button(translate("Save"), ImVec2(120, 0)))
        {
            if (layoutNameBuf[0]) saveLayoutToFile(layoutNameBuf);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(translate("Cancel"), ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void DockManager::shortcuts()
{
    ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl || io.WantTextInput) return;
    auto layouts = listLayouts();
    for (int i = 0; i < 9 && i < (int)layouts.size(); i++)
        if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + i), false))
            requestLoadLayout(layouts[i]);
}

void DockManager::saveLayoutToFile(const std::string& name)
{
    try { fs::create_directories(layoutsDir); }
    catch (...) {}
    std::string path = layoutsDir + "/" + name + ".ini";
    ImGui::SaveIniSettingsToDisk(path.c_str());
    nlohmann::json j;
    for (auto& p : panels) j[p.name] = p.open;
    std::ofstream f(path + ".panels.json");
    f << j.dump(2);
    OLOG("Layout", "Saved layout '" << name << "' to " << path);
}

void DockManager::requestLoadLayout(const std::string& name)
{
    pendingLayoutFile = layoutsDir + "/" + name + ".ini";
}

void DockManager::deleteLayout(const std::string& name)
{
    std::error_code ec;
    fs::remove(layoutsDir + "/" + name + ".ini", ec);
    fs::remove(layoutsDir + "/" + name + ".ini.panels.json", ec);
    OLOG("Layout", "Deleted layout '" << name << "'");
}

std::vector<std::string> DockManager::listLayouts() const
{
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::exists(layoutsDir, ec)) return out;
    for (auto& e : fs::directory_iterator(layoutsDir, ec))
    {
        if (e.path().extension() == ".ini")
            out.push_back(e.path().stem().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

nlohmann::json DockManager::saveState() const
{
    nlohmann::json j;
    for (auto& p : panels) j["panels"][p.name] = p.open;
    return j;
}

void DockManager::loadState(const nlohmann::json& j)
{
    if (!j.contains("panels")) return;
    for (auto& [k, v] : j["panels"].items())
        if (Panel* p = find(k)) p->open = v.get<bool>();
}

} // namespace organic
