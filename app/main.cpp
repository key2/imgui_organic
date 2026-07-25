// Organic ImGui demo application.
// Recreates the core experience of juce_organicui / juce_timeline apps
// (Chataigne-style tooling) with Dear ImGui (docking) + ImPlot.
#include "Organic.h"
#include "imgui.h"
#include "imgui_internal.h" // BeginViewportSideBar (status bar)
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <cstdio>

namespace fs = std::filesystem;
using namespace organic;

// ---------------------------------------------------------------- app model
struct App
{
    Sequence  seq{ "Demo Sequence" };
    MediaPool pool;
    Container settings{ "Settings" };
    Parameter* autosaveP = nullptr;
    Parameter* autosaveIntervalP = nullptr;
    Parameter* logParamChangesP = nullptr;

    TimelineUI   tui;
    DockManager  dock;
    ScopeBuffers scope;

    std::string projectPath = "project.organic.json";
    char pathBuf[512] = "project.organic.json";
    double lastAutosave = 0;

    bool showImGuiDemo = false;
    bool showImPlotDemo = false;
    bool wantOpenPopup = false, wantSaveAsPopup = false, wantAboutPopup = false;
};

// ---------------------------------------------------------------- style
static void setupOrganicStyle()
{
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    ImVec4* c = s.Colors;

    const ImVec4 accent(1.00f, 0.573f, 0.184f, 1.f); // organicui HIGHLIGHT_COLOR #FF922F
    const ImVec4 bg(0.129f, 0.129f, 0.133f, 1.f);    // #212122-ish

    c[ImGuiCol_WindowBg]            = bg;
    c[ImGuiCol_ChildBg]             = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]             = ImVec4(0.10f, 0.10f, 0.11f, 0.98f);
    c[ImGuiCol_Border]              = ImVec4(1, 1, 1, 0.08f);
    c[ImGuiCol_FrameBg]             = ImVec4(0.20f, 0.20f, 0.21f, 1.f);
    c[ImGuiCol_FrameBgHovered]      = ImVec4(0.27f, 0.27f, 0.28f, 1.f);
    c[ImGuiCol_FrameBgActive]       = ImVec4(0.32f, 0.32f, 0.33f, 1.f);
    c[ImGuiCol_TitleBg]             = ImVec4(0.10f, 0.10f, 0.11f, 1.f);
    c[ImGuiCol_TitleBgActive]       = ImVec4(0.16f, 0.16f, 0.17f, 1.f);
    c[ImGuiCol_MenuBarBg]           = ImVec4(0.14f, 0.14f, 0.15f, 1.f);
    c[ImGuiCol_ScrollbarBg]         = ImVec4(0.10f, 0.10f, 0.11f, 0.6f);
    c[ImGuiCol_ScrollbarGrab]       = ImVec4(0.31f, 0.31f, 0.33f, 1.f);
    c[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.41f, 0.41f, 0.43f, 1.f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.53f, 1.f);
    c[ImGuiCol_CheckMark]           = accent;
    c[ImGuiCol_SliderGrab]          = ImVec4(accent.x, accent.y, accent.z, 0.85f);
    c[ImGuiCol_SliderGrabActive]    = accent;
    c[ImGuiCol_Button]              = ImVec4(0.23f, 0.23f, 0.24f, 1.f);
    c[ImGuiCol_ButtonHovered]       = ImVec4(0.32f, 0.32f, 0.34f, 1.f);
    c[ImGuiCol_ButtonActive]        = ImVec4(accent.x, accent.y, accent.z, 0.65f);
    c[ImGuiCol_Header]              = ImVec4(accent.x, accent.y, accent.z, 0.22f);
    c[ImGuiCol_HeaderHovered]       = ImVec4(accent.x, accent.y, accent.z, 0.38f);
    c[ImGuiCol_HeaderActive]        = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    c[ImGuiCol_Separator]           = ImVec4(1, 1, 1, 0.08f);
    c[ImGuiCol_SeparatorHovered]    = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    c[ImGuiCol_SeparatorActive]     = accent;
    c[ImGuiCol_ResizeGrip]          = ImVec4(1, 1, 1, 0.08f);
    c[ImGuiCol_ResizeGripHovered]   = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    c[ImGuiCol_ResizeGripActive]    = accent;
    c[ImGuiCol_Tab]                 = ImVec4(0.15f, 0.15f, 0.16f, 1.f);
    c[ImGuiCol_TabHovered]          = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    c[ImGuiCol_TabSelected]         = ImVec4(0.26f, 0.26f, 0.27f, 1.f);
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TabDimmed]           = ImVec4(0.12f, 0.12f, 0.13f, 1.f);
    c[ImGuiCol_TabDimmedSelected]   = ImVec4(0.18f, 0.18f, 0.19f, 1.f);
    c[ImGuiCol_DockingPreview]      = ImVec4(accent.x, accent.y, accent.z, 0.45f);
    c[ImGuiCol_DragDropTarget]      = accent;
    c[ImGuiCol_NavCursor]           = accent;
    c[ImGuiCol_TextSelectedBg]      = ImVec4(accent.x, accent.y, accent.z, 0.35f);

    s.WindowRounding    = 4.f;
    s.ChildRounding     = 4.f;
    s.FrameRounding     = 3.f;
    s.PopupRounding     = 4.f;
    s.GrabRounding      = 3.f;
    s.TabRounding       = 4.f;
    s.ScrollbarRounding = 6.f;
    s.WindowPadding     = ImVec2(8, 8);
    s.FramePadding      = ImVec2(7, 4);
    s.ItemSpacing       = ImVec2(7, 5);
    s.DockingSeparatorSize = 2.f;

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        s.WindowRounding = 0.f;
        c[ImGuiCol_WindowBg].w = 1.f;
    }
}

// ---------------------------------------------------------------- demo assets & content
static void ensureDemoAssets()
{
    try { fs::create_directories("assets"); } catch (...) {}
    if (!fs::exists("assets/beat.wav"))  saveWavPcm16("assets/beat.wav",  makeBeat(4.f, 120.f));
    if (!fs::exists("assets/tone.wav"))  saveWavPcm16("assets/tone.wav",  makeTone(2.f, 440.f));
    if (!fs::exists("assets/sweep.wav")) saveWavPcm16("assets/sweep.wav", makeSweep(4.f, 80.f, 2400.f));
    OLOG("Assets", "Demo audio files ready in ./assets");
}

static void populateDemoMedia(App& app)
{
    auto* beat = app.pool.addMedia("Beat Loop", 1);
    beat->fileP->setValue(std::string("assets/beat.wav"), false);
    auto* tone = app.pool.addMedia("Tone", 1);
    tone->fileP->setValue(std::string("assets/tone.wav"), false);
    auto* sweep = app.pool.addMedia("Sweep", 1);
    sweep->fileP->setValue(std::string("assets/sweep.wav"), false);

    auto* cue = app.pool.addMedia("Cue Block", 0);
    cue->colorP->setValue(ImVec4(0.85f, 0.45f, 0.20f, 1.f), false);
    cue->durP->setValue(2.f, false);
    auto* fx = app.pool.addMedia("FX Block", 0);
    fx->colorP->setValue(ImVec4(0.45f, 0.35f, 0.80f, 1.f), false);
    fx->durP->setValue(3.f, false);
}

static void populateDemoSequence(App& app)
{
    Sequence& seq = app.seq;
    seq.lengthP->setValue(16.f, false);

    auto* audio = static_cast<ClipLayer*>(seq.addLayer(Layer::LType::Clips, "Audio A"));
    Clip* b1 = audio->addClip(Clip::CType::Audio, "Beat", 0.0, 4.0);
    b1->setAudioFile("assets/beat.wav");
    Clip* b2 = audio->addClip(Clip::CType::Audio, "Beat", 4.0, 4.0);
    b2->setAudioFile("assets/beat.wav");
    Clip* sw = audio->addClip(Clip::CType::Audio, "Sweep", 8.0, 4.0);
    sw->setAudioFile("assets/sweep.wav");
    Clip* tn = audio->addClip(Clip::CType::Audio, "Tone", 12.5, 2.0);
    tn->setAudioFile("assets/tone.wav");

    auto* blocks = static_cast<ClipLayer*>(seq.addLayer(Layer::LType::Clips, "Blocks"));
    Clip* c1 = blocks->addClip(Clip::CType::Block, "Intro", 0.0, 3.0);
    c1->colorP->setValue(ImVec4(0.20f, 0.55f, 0.55f, 1.f), false);
    Clip* c2 = blocks->addClip(Clip::CType::Block, "Verse", 3.0, 5.0);
    c2->colorP->setValue(ImVec4(0.45f, 0.35f, 0.80f, 1.f), false);
    Clip* c3 = blocks->addClip(Clip::CType::Block, "Chorus", 8.0, 4.0);
    c3->colorP->setValue(ImVec4(0.85f, 0.45f, 0.20f, 1.f), false);

    auto* energy = static_cast<AutomationLayer*>(seq.addLayer(Layer::LType::Automation, "Energy"));
    energy->addKey(0.0, 0.1f, EasingType::Linear);
    AutoKey* bez = energy->addKey(2.0, 0.9f, EasingType::Bezier);
    bez->ep.a1 = ImVec2(0.4f, 0.0f);
    bez->ep.a2 = ImVec2(-0.4f, 0.0f);
    energy->addKey(4.0, 0.35f, EasingType::Elastic);
    energy->addKey(6.0, 0.8f, EasingType::Bounce);
    energy->addKey(8.0, 0.5f, EasingType::Steps);
    energy->addKey(10.0, 0.95f, EasingType::Sine);
    energy->addKey(12.0, 0.2f, EasingType::Linear);
    energy->addKey(15.5, 0.75f, EasingType::Linear);

    auto* mood = static_cast<GradientLayer*>(seq.addLayer(Layer::LType::Gradient, "Mood"));
    mood->addKey(0.0,  ImVec4(0.15f, 0.25f, 0.60f, 1.f));
    mood->addKey(3.0,  ImVec4(0.10f, 0.60f, 0.55f, 1.f));
    mood->addKey(6.0,  ImVec4(0.95f, 0.55f, 0.15f, 1.f));
    mood->addKey(9.0,  ImVec4(0.80f, 0.20f, 0.55f, 1.f));
    mood->addKey(12.0, ImVec4(0.20f, 0.15f, 0.45f, 1.f));

    OLOG("Engine", "Demo project created");
}

// ---------------------------------------------------------------- project io
static bool saveProject(App& app, const std::string& path)
{
    json j;
    j["app"] = "imgui_organic";
    j["version"] = 1;
    j["sequence"] = app.seq.save();
    j["media"] = app.pool.save();
    j["settings"] = app.settings.save();
    std::ofstream f(path);
    if (!f.is_open())
    {
        OLOGE("Engine", "Cannot write '" << path << "'");
        return false;
    }
    f << j.dump(2);
    app.projectPath = path;
    OLOG("Engine", "Project saved to '" << path << "'");
    return true;
}

static bool loadProject(App& app, const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
    {
        OLOGE("Engine", "Cannot open '" << path << "'");
        return false;
    }
    try
    {
        json j;
        f >> j;
        Selection::get().clear();
        UndoManager::get().clear();
        if (j.contains("media"))    app.pool.load(j["media"]);
        if (j.contains("sequence")) app.seq.load(j["sequence"]);
        if (j.contains("settings")) app.settings.load(j["settings"]);
        app.projectPath = path;
        snprintf(app.pathBuf, sizeof(app.pathBuf), "%s", path.c_str());
        OLOG("Engine", "Project loaded from '" << path << "'");
        return true;
    }
    catch (std::exception& e)
    {
        OLOGE("Engine", "Failed to load '" << path << "': " << e.what());
        return false;
    }
}

static void newProject(App& app)
{
    Selection::get().clear();
    UndoManager::get().clear();
    app.seq.layers.clear();
    app.seq.setNiceName("Sequence");
    app.seq.lengthP->resetToDefault(false);
    app.seq.currentTime = 0;
    app.seq.playing = false;
    OLOG("Engine", "New project");
}

// ---------------------------------------------------------------- app state (window prefs)
static void saveAppState(App& app)
{
    json j;
    j["dock"] = app.dock.saveState();
    j["projectPath"] = app.projectPath;
    j["timeline"] = { { "snap", app.tui.snapEnabled }, { "snapChoice", app.tui.snapChoice },
                      { "follow", app.tui.followPlayhead } };
    std::ofstream f("organic_app.json");
    f << j.dump(2);
}

static void loadAppState(App& app)
{
    std::ifstream f("organic_app.json");
    if (!f.is_open()) return;
    try
    {
        json j; f >> j;
        if (j.contains("dock")) app.dock.loadState(j["dock"]);
        if (j.contains("projectPath"))
        {
            app.projectPath = j["projectPath"].get<std::string>();
            snprintf(app.pathBuf, sizeof(app.pathBuf), "%s", app.projectPath.c_str());
        }
        if (j.contains("timeline"))
        {
            app.tui.snapEnabled    = j["timeline"].value("snap", true);
            app.tui.snapChoice     = j["timeline"].value("snapChoice", 0);
            app.tui.followPlayhead = j["timeline"].value("follow", true);
        }
    }
    catch (...) {}
}

// ---------------------------------------------------------------- menus / popups / status bar
static void mainMenuBar(App& app)
{
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New")) newProject(app);
        if (ImGui::MenuItem("Open...")) app.wantOpenPopup = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S")) saveProject(app, app.projectPath);
        if (ImGui::MenuItem("Save As...")) app.wantSaveAsPopup = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4"))
            glfwSetWindowShouldClose(glfwGetCurrentContext(), 1);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit"))
    {
        auto& um = UndoManager::get();
        std::string ul = "Undo" + (um.canUndo() ? " " + um.undoName() : "");
        std::string rl = "Redo" + (um.canRedo() ? " " + um.redoName() : "");
        if (ImGui::MenuItem(ul.c_str(), "Ctrl+Z", false, um.canUndo())) um.undo();
        if (ImGui::MenuItem(rl.c_str(), "Ctrl+Shift+Z", false, um.canRedo())) um.redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Clear Undo History")) um.clear();
        ImGui::EndMenu();
    }
    app.dock.viewMenu();
    app.dock.panelsMenu();
    if (ImGui::BeginMenu("Help"))
    {
        if (ImGui::MenuItem("About")) app.wantAboutPopup = true;
        ImGui::Separator();
        ImGui::MenuItem("ImGui Demo", nullptr, &app.showImGuiDemo);
        ImGui::MenuItem("ImPlot Demo", nullptr, &app.showImPlotDemo);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

static void appPopups(App& app)
{
    if (app.wantOpenPopup)  { ImGui::OpenPopup("Open Project");    app.wantOpenPopup = false; }
    if (app.wantSaveAsPopup){ ImGui::OpenPopup("Save Project As"); app.wantSaveAsPopup = false; }
    if (app.wantAboutPopup) { ImGui::OpenPopup("About Organic");   app.wantAboutPopup = false; }

    if (ImGui::BeginPopupModal("Open Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::SetNextItemWidth(420);
        ImGui::InputText("Path", app.pathBuf, sizeof(app.pathBuf));
        ImGui::TextDisabled("Projects in current directory:");
        ImGui::BeginChild("##plist", ImVec2(430, 120), ImGuiChildFlags_Borders);
        std::error_code ec;
        for (auto& e : fs::directory_iterator(".", ec))
        {
            std::string n = e.path().filename().string();
            if (n.size() > 5 && n.substr(n.size() - 5) == ".json" && n.find("organic") != std::string::npos)
                if (ImGui::Selectable(n.c_str()))
                    snprintf(app.pathBuf, sizeof(app.pathBuf), "%s", n.c_str());
        }
        ImGui::EndChild();
        if (ImGui::Button("Open", ImVec2(120, 0)))
        {
            loadProject(app, app.pathBuf);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Save Project As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::SetNextItemWidth(420);
        ImGui::InputText("Path", app.pathBuf, sizeof(app.pathBuf));
        if (ImGui::Button("Save", ImVec2(120, 0)))
        {
            saveProject(app, app.pathBuf);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("About Organic", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("imgui_organic - organicui-style tooling for Dear ImGui");
        ImGui::Separator();
        ImGui::BulletText("Dockable panels with saveable layouts (ShapeShifter)");
        ImGui::BulletText("Parameter/Container model + auto Inspector");
        ImGui::BulletText("Timeline: clips, drag & drop, waveforms, automation, gradients");
        ImGui::BulletText("Undo/redo everywhere, JSON projects");
        ImGui::TextDisabled("Inspired by benkuper/juce_organicui + juce_timeline.");
        ImGui::TextDisabled("Built with Dear ImGui (docking) %s + ImPlot.", IMGUI_VERSION);
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static void statusBar(App& app)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float h = ImGui::GetFrameHeight();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_MenuBar;
    if (ImGui::BeginViewportSideBar("##OrganicStatus", vp, ImGuiDir_Down, h, flags))
    {
        if (ImGui::BeginMenuBar())
        {
            ImVec4 accent(1.f, 0.573f, 0.184f, 1.f);
            ImGui::TextColored(app.seq.playing ? ImVec4(0.3f, 0.9f, 0.4f, 1.f) : ImVec4(0.6f, 0.6f, 0.62f, 1.f),
                               app.seq.playing ? "PLAYING" : "STOPPED");
            ImGui::Separator();
            ImGui::TextColored(accent, "%s", formatTime(app.seq.currentTime).c_str());
            ImGui::Separator();
            ImGui::Text("%d selected", (int)Selection::get().items.size());
            ImGui::Separator();
            auto& um = UndoManager::get();
            ImGui::TextDisabled("undo: %s", um.canUndo() ? um.undoName().c_str() : "-");
            ImGui::Separator();
            ImGui::TextDisabled("%s", app.projectPath.c_str());

            char fps[48];
            snprintf(fps, sizeof(fps), "%.0f fps", ImGui::GetIO().Framerate);
            float w = ImGui::CalcTextSize(fps).x + 16;
            ImGui::SameLine(ImGui::GetWindowWidth() - w);
            ImGui::TextDisabled("%s", fps);
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------- main
static void glfwErrorCb(int e, const char* d) { fprintf(stderr, "GLFW error %d: %s\n", e, d); }

int main(int, char**)
{
    glfwSetErrorCallback(glfwErrorCb);
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(1680, 940, "Organic ImGui - docking + timeline demo", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // floating OS windows (ShapeShifterWindow)
    io.IniFilename = "organic_imgui.ini";
    io.ConfigDockingWithShift = false;

    // font
    {
        std::string fontPath = std::string(ORGANIC_FONT_DIR) + "/Roboto-Medium.ttf";
        if (fs::exists(fontPath)) io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 16.f);
    }

    setupOrganicStyle();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // ------------------------------------------------------------ model setup
    App app;
    app.settings.renamable = false;
    app.autosaveP = app.settings.addBool("Autosave", true, "Periodically save to autosave.organic.json");
    app.autosaveIntervalP = app.settings.addFloat("Autosave Interval", 60.f, 5.f, 600.f, "Seconds between autosaves");
    app.autosaveIntervalP->unit = "s";
    app.logParamChangesP = app.settings.addBool("Verbose Media Log", false, "Log every media payload drop");

    registerRoot(&app.seq);
    registerRoot(&app.pool);
    registerRoot(&app.settings);

    ensureDemoAssets();
    populateDemoMedia(app);
    if (fs::exists(app.projectPath))
        loadProject(app, app.projectPath);
    else
        populateDemoSequence(app);

    // ------------------------------------------------------------ panels
    app.dock.addPanel("Timeline", DockZone::Center, [&](bool* o) { app.tui.gui(app.seq, o); });
    app.dock.addPanel("Inspector", DockZone::Right, [](bool* o) { InspectorPanel(o); });
    app.dock.addPanel("Scope", DockZone::RightBottom, [&](bool* o) { ScopePanel(app.scope, o); });
    app.dock.addPanel("Outliner", DockZone::Left, [](bool* o) { OutlinerPanel(o); });
    app.dock.addPanel("Media Pool", DockZone::LeftBottom, [&](bool* o) { MediaPoolPanel(app.pool, o); });
    app.dock.addPanel("Logger", DockZone::Bottom, [](bool* o) { LoggerPanel(o); });
    app.dock.addPanel("Settings", DockZone::RightBottom, [&](bool* o)
    {
        if (ImGui::Begin("Settings", o))
        {
            app.settings.inspectorGui();
            ImGui::Separator();
            ImGui::TextDisabled("Layouts dir: ./layouts");
            ImGui::TextDisabled("ImGui ini:  ./organic_imgui.ini");
        }
        ImGui::End();
    }, false);

    loadAppState(app);
    OLOG("Engine", "Ready. Space = play, drag media onto the timeline, Ctrl+Z = undo.");

    // ------------------------------------------------------------ main loop
    double lastTime = glfwGetTime();
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        app.dock.preNewFrame(); // apply pending layout loads before NewFrame

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        double now = glfwGetTime();
        double dt = now - lastTime;
        lastTime = now;

        app.seq.update(dt);
        app.scope.push(app.seq, now);

        mainMenuBar(app);
        statusBar(app);
        app.dock.gui();
        appPopups(app);
        app.dock.popupsGui();
        app.dock.shortcuts();

        if (app.showImGuiDemo)  ImGui::ShowDemoWindow(&app.showImGuiDemo);
        if (app.showImPlotDemo) ImPlot::ShowDemoWindow(&app.showImPlotDemo);

        // global shortcuts
        if (!io.WantTextInput)
        {
            if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) UndoManager::get().undo();
            if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
                (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)))) UndoManager::get().redo();
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) saveProject(app, app.projectPath);
        }

        CommitPendingParamEdits();

        // autosave
        if (app.autosaveP->boolValue() && now - app.lastAutosave > app.autosaveIntervalP->floatValue())
        {
            app.lastAutosave = now;
            json j;
            j["sequence"] = app.seq.save();
            j["media"] = app.pool.save();
            j["settings"] = app.settings.save();
            std::ofstream f("autosave.organic.json");
            if (f.is_open()) { f << j.dump(); OLOG("Engine", "Autosaved"); }
        }

        ImGui::Render();
        int dw, dh;
        glfwGetFramebufferSize(window, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.08f, 0.08f, 0.085f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow* backup = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup);
        }
        glfwSwapBuffers(window);
    }

    saveAppState(app);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
