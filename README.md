# imgui_organic

**organicui-style application tooling for Dear ImGui.**

`imgui_organic` re-creates the core experience of
[benkuper/juce_organicui](https://github.com/benkuper/juce_organicui) +
[juce_timeline](https://github.com/benkuper/juce_timeline) (the framework behind
Chataigne, LGML, BenTo...) as a small, reusable C++17 library on top of
**Dear ImGui (docking branch)** and **ImPlot** — so you can build "creative
tooling" apps where the user decides which window goes where, and edit time-based
content on rich timelines.

## Features

- **Dockable window management** — arrange panels freely (5-zone drop preview),
  tear tabs off into real OS windows (multi-viewport), save/load **named layouts**
  (`Ctrl+1..9`), reset to a default layout, Panels menu. The ImGui take on
  organicui's *ShapeShifter*.
- **Timeline editor** (`TimelineUI`)
  - Clip layers: drag & drop boxes, move across layers, resize with edge grips,
    multi-select (rubber band / `Ctrl`), duplicate, snapping grid, context menus.
  - **Audio clips draw their waveform** (WAV: PCM 8/16/24/32 & float32) with gain
    and media offset; trimming the left edge keeps the audio in place.
  - **Automation layers**: keyframe curves with per-segment easings — Linear,
    **Bezier (draggable handles)**, Hold, Sine, Elastic, Bounce, Steps, Noise.
  - **Gradient layers**: time→color tracks with draggable color keys.
  - Time ruler with scrubbing, playhead, loop, zoom-at-mouse, pan, fit,
    follow-playhead, adaptive snapping (`Alt` bypasses).
- **Parameter / Container data model** — typed parameters (Trigger, Bool, Int,
  Float, String, Enum, Color, Point2D) with ranges, defaults, descriptions,
  control addresses (`/root/child/param`), change notification bubbling and JSON
  serialization.
- **Auto-generated Inspector** — select anything (clips, layers, keys, media,
  your own containers) and edit it; multi-selection supported.
- **Undo/redo everywhere** — drags, drops, renames, key edits and parameter
  widgets are undoable; continuous edits coalesce into single steps.
- **Standard panels** — Outliner (filterable hierarchy tree), Logger
  (`OLOG/OLOGW/OLOGE`), Media Pool (drag sources for the timeline), Scope
  (ImPlot rolling plot of automation outputs — organicui's *Detective*).
- **JSON projects** — save/load the whole document; autosave.

The full user manual (every shortcut and interaction, plus in-depth API
examples) lives in **[USAGE.md](USAGE.md)**.

---

## Repository layout & dependencies

```
imgui_organic/
├── organic/            the library (this is what you link)
├── app/main.cpp        full demo application (GLFW + OpenGL3)
├── implot/             [submodule]  epezent/implot        (pinned, v1.0 API)
├── third_party/json/   [submodule]  nlohmann/json         (pinned, v3.12.0)
├── CMakeLists.txt
├── README.md           this file
└── USAGE.md            full manual + API examples
```

| Dependency | How it is provided |
|---|---|
| **Dear ImGui — docking branch** | **You clone it yourself** (not a submodule, so you control the exact version and can share it with the rest of your app). Known-good commit: `9b4eb24` (v1.92.9 docking). |
| ImPlot | git submodule, pinned |
| nlohmann/json | git submodule, pinned (single header, shallow) |
| GLFW + OpenGL | system packages — **only needed by the demo**, not by the library |

The `organic` library itself depends only on `imgui` + `implot` + the json
header. It contains **no windowing/backend code**, so it works with any ImGui
backend (GLFW, SDL2/3, Win32, Metal, ...).

---

## Getting the sources

```bash
# 1. this repository, with submodules
git clone --recurse-submodules <url-of-imgui_organic>
cd imgui_organic

#    (if you cloned without --recurse-submodules:)
git submodule update --init --recursive

# 2. Dear ImGui, DOCKING branch, cloned separately (next to the project is easiest)
git clone --branch docking https://github.com/ocornut/imgui.git
```

CMake looks for ImGui in `./imgui`, `./imgui_docking`, `../imgui`,
`../imgui_docking` — or wherever `-DORGANIC_IMGUI_DIR=/path/to/imgui` points.
A non-docking checkout is rejected at configure time with a clear error.

## Building & running the demo

Requirements: CMake ≥ 3.16, a C++17 compiler, GLFW 3.3+ and OpenGL
(`sudo apt install cmake g++ libglfw3-dev` on Debian/Ubuntu).

```bash
cmake -B build
cmake --build build -j
./build/organic_demo
```

You get a Chataigne-style workspace: Timeline (audio clips with waveforms,
blocks, an automation curve, a gradient track), Inspector, Outliner, Media Pool,
Scope, Logger. Try:

- `Space` to play; drag media from the **Media Pool** onto the timeline;
- drag clips around (also vertically between layers), resize their edges;
- double-click an automation lane to add keys, right-click a key to change its
  easing, drag the white squares of a Bezier key;
- drag any window tab to re-dock it, or pull it outside the app to detach it;
- **View → Save Layout As...**, then reload it with `Ctrl+1`;
- `Ctrl+Z` undoes *everything*, `Ctrl+S` saves `project.organic.json`.

First run generates demo WAVs in `./assets` and a demo project.

---

## Using imgui_organic in your own software

### 1. Add it (and ImGui) to your project

```bash
cd your_app
git submodule add <url-of-imgui_organic> extern/imgui_organic
git submodule update --init --recursive
git clone --branch docking https://github.com/ocornut/imgui.git extern/imgui   # or your existing copy
```

### 2. CMake

```cmake
# your_app/CMakeLists.txt
set(ORGANIC_IMGUI_DIR ${CMAKE_SOURCE_DIR}/extern/imgui)  # your ImGui (docking)
set(ORGANIC_BUILD_DEMO OFF)                              # library only
add_subdirectory(extern/imgui_organic)

add_executable(your_app
    src/main.cpp
    # your ImGui backend pair, e.g.:
    ${CMAKE_SOURCE_DIR}/extern/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_SOURCE_DIR}/extern/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(your_app PRIVATE ${CMAKE_SOURCE_DIR}/extern/imgui/backends)
target_link_libraries(your_app PRIVATE organic::organic glfw OpenGL::GL)
```

That's it — `organic::organic` transitively provides the `imgui`, `implot` and
json include paths and libraries.

**Already building ImGui/ImPlot yourself?** If a target named `imgui` (and/or
`implot`) exists before `add_subdirectory(imgui_organic)`, it is reused instead
of being built again — just make sure your `imgui` target is the docking branch,
exposes its include directories, and compiles `misc/cpp/imgui_stdlib.cpp`
(if it doesn't, still set `ORGANIC_IMGUI_DIR` and organic compiles the stdlib
helper itself).

#### CMake options

| Option | Default | Meaning |
|---|---|---|
| `ORGANIC_IMGUI_DIR` | *(auto-detect)* | Path to your Dear ImGui (docking) checkout |
| `ORGANIC_BUILD_DEMO` | `ON` when top-level, `OFF` as subproject | Build `organic_demo` (needs GLFW/OpenGL) |

### 3. Minimal application

```cpp
#include "Organic.h"                 // umbrella header of the organic library
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
using namespace organic;

int main()
{
    glfwInit();
    GLFWwindow* win = glfwCreateWindow(1280, 800, "My App", nullptr, nullptr);
    glfwMakeContextCurrent(win); glfwSwapInterval(1);

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable      // choose what window goes where
                   |  ImGuiConfigFlags_ViewportsEnable;   // tabs tear off into OS windows
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // ---- model ----
    Sequence seq("My Sequence");
    seq.addLayer(Layer::LType::Clips, "Clips");
    seq.addLayer(Layer::LType::Automation, "Curve");
    registerRoot(&seq);                                   // shows up in the Outliner

    TimelineUI  timeline;
    DockManager dock;
    dock.addPanel("Timeline",  DockZone::Center, [&](bool* o){ timeline.gui(seq, o); });
    dock.addPanel("Inspector", DockZone::Right,  [](bool* o){ InspectorPanel(o); });
    dock.addPanel("Outliner",  DockZone::Left,   [](bool* o){ OutlinerPanel(o); });
    dock.addPanel("Logger",    DockZone::Bottom, [](bool* o){ LoggerPanel(o); });

    double last = glfwGetTime();
    while (!glfwWindowShouldClose(win))
    {
        glfwPollEvents();
        dock.preNewFrame();                               // apply pending layout loads

        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

        double now = glfwGetTime();
        seq.update(now - last); last = now;               // advance playhead

        if (ImGui::BeginMainMenuBar())
        {
            dock.viewMenu();                              // layouts: reset/save/load
            dock.panelsMenu();                            // toggle panels
            ImGui::EndMainMenuBar();
        }
        dock.gui();                                       // dockspace + open panels
        dock.popupsGui();
        dock.shortcuts();                                 // Ctrl+1..9 layouts
        CommitPendingParamEdits();                        // coalesce widget edits -> undo

        ImGui::Render();
        int w, h; glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h);
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
        glfwSwapBuffers(win);
    }

    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext(); ImGui::DestroyContext();
    glfwDestroyWindow(win); glfwTerminate();
}
```

### 4. Expose your own objects to the Inspector/Outliner

```cpp
class MyModule : public organic::Container
{
public:
    MyModule() : Container("My Module")
    {
        renamable = true;
        speed = addFloat("Speed", 1.f, 0.f, 10.f, "Playback speed");
        mode  = addEnum ("Mode", { "Off", "Auto", "Manual" }, 1);
        fire  = addTrigger("Fire");
        fire->onChange = [](organic::Parameter&){ OLOG("MyModule", "fired!"); };
    }
    void onParamChanged(organic::Parameter* p) override
    {
        if (p == speed) OLOG("MyModule", "speed = " << p->floatValue());
    }
    organic::Parameter *speed, *mode, *fire;
};

MyModule mod;
organic::registerRoot(&mod);   // -> Outliner tree; click it -> auto Inspector
```

Undoable edits, JSON round-trip (`mod.save()` / `mod.load(j)`), control
addresses (`mod.speed->controlAddress()` → `"/myModule/speed"`) and change
callbacks all come for free.

### 5. Drive your engine from the timeline

```cpp
seq.play();
// every frame:
seq.update(dt);
float  level = myAutomationLayer->valueAt(seq.currentTime);   // eased value
ImVec4 color = myGradientLayer->colorAt(seq.currentTime);
```

Anything can create timeline clips by drag & drop — fill an
`organic::MediaPayload` and ship it with `ImGui::SetDragDropPayload
(ORGANIC_MEDIA_PAYLOAD, ...)`; the built-in Media Pool panel is ~40 lines using
the same mechanism.

### Public API map

| Header | Contents |
|---|---|
| `Organic.h` | umbrella include |
| `OrganicCore.h` | `Parameter`, `Container`, `Selection`, `UndoManager`, `Logger`, `OLOG` macros, `DrawParamWidget` |
| `OrganicTimeline.h` | `Sequence`, `ClipLayer`/`Clip`, `AutomationLayer`/`AutoKey`, `GradientLayer`, `MediaPool`, `MediaPayload` |
| `OrganicTimelineUI.h` | `TimelineUI` (the editor window) |
| `OrganicPanels.h` | `InspectorPanel`, `OutlinerPanel`, `LoggerPanel`, `MediaPoolPanel`, `ScopePanel`, `CommitPendingParamEdits` |
| `OrganicDock.h` | `DockManager`, `DockZone` |
| `OrganicEasing.h` | `EasingType`, `ease()` |
| `OrganicAudio.h` | `loadWav`/`saveWavPcm16`, `Peaks`, `AudioCache`, tone/beat/sweep generators |

### Runtime files

Created in the working directory of the app: `organic_imgui.ini` (dock/window
state), `layouts/*.ini` (named layouts), `organic_app.json` (panel states,
prefs), `assets/*.wav` (demo audio), `project.organic.json` / `autosave.organic.json`.

---

## Quick control reference

`Space` play · ruler-drag scrub · `Ctrl+Wheel` zoom · `Shift+Wheel`/middle-drag pan ·
`F` fit · drag clips to move (vertically = change layer) · edges resize ·
rubber-band select · `Ctrl+D` duplicate · `Del` delete · arrows nudge ·
`Alt` bypass snap · double-click lanes to create clips/keys · right-click for
context menus (easings, layer ops) · `Ctrl+Z`/`Ctrl+Shift+Z` undo/redo ·
`Ctrl+S` save · `Ctrl+1..9` layouts. Full list in [USAGE.md](USAGE.md).

## Notes & limitations

- The library draws waveforms but does **not** play audio — bind your own audio
  engine to `Sequence::currentTime`.
- Requires the ImGui **docking** branch (checked at configure time).
- Tested with ImGui docking `9b4eb24` (1.92.9), ImPlot `d65a2be` (v1.0 API,
  pinned submodule), nlohmann/json v3.12.0 (pinned submodule), GCC 14, GLFW 3.4.

## Credits & licenses

- Concept and feature set inspired by **Ben Kuper**'s
  [juce_organicui](https://github.com/benkuper/juce_organicui) /
  [juce_timeline](https://github.com/benkuper/juce_timeline) (GPLv3) — this
  project is an independent re-implementation for Dear ImGui and shares no code.
- [Dear ImGui](https://github.com/ocornut/imgui) (MIT),
  [ImPlot](https://github.com/epezent/implot) (MIT),
  [nlohmann/json](https://github.com/nlohmann/json) (MIT).
- This repository has no license file yet — add one before publishing.
