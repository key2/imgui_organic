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
- **Timeline editor** (`TimelineUI`) — multiple sequences in tabs:
  - Clip layers: drag & drop boxes, move across layers, resize with edge grips,
    multi-select (rubber band with preselection / `Ctrl`), duplicate, split,
    copy/paste at the playhead, grid **and magnet snapping** (clip edges, keys,
    cues, playhead), context menus everywhere. Two clips on one layer **never
    overlap in time** — moves/resizes/drops seat flush against neighbours.
  - **Embedded clip automations** (`ClipAutomation`): a block clip *contains*
    its automations as internal rows — value curves or color gradients with
    **clip-local** key times (nothing extends beyond the block; keys travel
    with it, split with it, die with it). Rows collapse to a compact named
    header (sparkline preview, live value chip, record dot) and expand — with
    a smooth animation that grows the block *and* the track — into a full
    curve/gradient editor (keys, easings, bezier handles, host-fed recorder).
  - **Pencil / Draw mode** (`P` or the toolbar toggle): click-drag across any
    automation editor (embedded row or classic lane) to draw the curve
    freehand — keys are generated from the stroke (RDP-simplified).
  - Host-configurable "Add Layer" menus (`offerClipLayers`,
    `offerAutomationLayers`, `offerGradientLayers`, `offerTriggerLayers`):
    apps embedding automations inside clips hide the standalone lane types;
    existing layers of a hidden type still render and edit. Layer renaming
    can be disabled too (`offerLayerRename`) for hosts that auto-name.
  - **Sticky layer** (`stickyLayerId`): one layer renders PINNED under the
    ruler and never scrolls out of view (the audio/waveform lane everything
    is aligned against). It leaves the scroll flow, cannot be reordered,
    and only accepts audio drops.
  - Dragging clips **below the last layer creates a new layer** on release
    (ghost preview while hovering) — no pre-creating tracks to stack
    another effect under the first. `clipDoubleClicked` host hook: open the
    thing a block references (e.g. its effect graph) on double-click.
  - Right-aligned **zoom cluster** on the toolbar (zoom out / 1:1 / zoom
    in / fit content, view-center anchored; `F` and Ctrl+wheel still
    work). Hosts with an icon font override `zoomOutLabel` /
    `zoomOneLabel` / `zoomInLabel` / `zoomFitLabel`.
  - **Audio clips play back** (miniaudio) and draw their waveform (WAV: PCM
    8/16/24/32 & float32) with gain, media offset, **fade in/out handles** and
    **media looping** (tiling); left-trimming keeps the audio in place.
  - **Automation layers**: keyframe curves with per-segment easings — Linear,
    **Bezier (draggable handles)**, Hold, Sine, Elastic, Bounce, Steps, Noise,
    Perlin — plus in-lane frequency/amplitude/steps handles, shape-preserving
    key insertion, a **multi-key transform box** (move/scale selections),
    proportional range remapping, and a **live recorder** (arm, record any
    parameter, simplify to RDP lines or fitted Beziers).
  - **Gradient layers**: time→color tracks with draggable keys + hold mode.
  - **Trigger layers**: cue flags that fire callbacks (and log) when the
    playhead crosses them.
  - Ruler with **time cues** (jump/navigate), **loop in/out range**,
    play modes (Once / Loop / **Ping-Pong**, audio plays reversed), musical
    **BPM grid** (bars/beats snapping), ripple **insert/remove time**,
    scrubbing, zoom-at-mouse, pan, fit, follow-playhead (`Alt` bypasses snap).
- **Generic manager framework** (organicui's BaseManager/BaseItem):
  - `ManagerListUI` — searchable list with drag-reorder, enable toggles, color
    swatches, mini-mode, factory "+ Add" menus, full keyboard control.
  - `ManagerCanvasUI` — infinite **2D canvas**: pan/zoom, movable/resizable
    cards with live widgets, **snap-to-item guides**, align/distribute toolbar,
    **minimap**, rubber-band preselection, clipboard copy/paste across managers.
- **Curve2D** — arc-length parameterized 2D spatial curves (linear/Bezier
  segments) with a pan/zoom editor; drive positions from any timeline.
- **Parameter / Container data model** — typed parameters (Trigger, Bool, Int,
  Float, String, Enum, Color, Point2D) with ranges, defaults, descriptions,
  control addresses (`/root/child/param`), address resolution, change
  notification bubbling and JSON serialization.
- **Inspector with true multi-editing** — homogeneous multi-selections edit all
  objects through one widget set (single undo step, "(mixed)" indicators);
  per-selection scroll memory.
- **Selection system** — multiple named scopes (per-panel), preselection while
  rubber-banding, and **linked inspectables** (selecting one highlights the other).
- **Undo/redo everywhere** — drags, drops, renames, key edits, recordings and
  parameter widgets are undoable; continuous edits coalesce into single steps.
- **Standard panels** — Outliner (filterable hierarchy tree), Logger
  (`OLOG/OLOGW/OLOGE`), Media Pool (drag sources for the timeline), Scope
  (automation outputs), and the **Detective**: watch *any* parameter over time
  (right-click a widget → *Watch in Detective*).
- **JSON projects** — save/load the whole document; autosave.

The full user manual (every shortcut and interaction, plus in-depth API
examples) lives in **[USAGE.md](USAGE.md)**. For a feature-parity audit against
juce_organicui (what's not ported yet and how hard each piece would be), see
**[MISSING_FEATURES.md](MISSING_FEATURES.md)**.

---

## Repository layout & dependencies

```
imgui_organic/
├── organic/                the library (this is what you link)
├── app/main.cpp            full demo application (GLFW + OpenGL3)
├── tests/model_test.cpp    headless model tests (-DORGANIC_BUILD_TESTS=ON)
├── implot/                 [submodule]  epezent/implot     (pinned, v1.0 API)
├── third_party/json/       [submodule]  nlohmann/json      (pinned, v3.12.0)
├── third_party/miniaudio/  [submodule]  mackron/miniaudio  (pinned, audio playback)
├── CMakeLists.txt
├── README.md               this file
├── USAGE.md                full manual + API examples
└── MISSING_FEATURES.md     parity audit vs juce_organicui
```

| Dependency | How it is provided |
|---|---|
| **Dear ImGui — docking branch** | **You clone it yourself** (not a submodule, so you control the exact version and can share it with the rest of your app). Known-good commit: `9b4eb24` (v1.92.9 docking). |
| ImPlot | git submodule, pinned |
| nlohmann/json | git submodule, pinned (single header, shallow) |
| miniaudio | git submodule, pinned (single header; timeline audio playback) |
| GLFW + OpenGL | system packages — **only needed by the demo**, not by the library |

The `organic` library depends only on `imgui` + `implot` + the json and
miniaudio headers (plus pthread/dl on Linux). It contains **no windowing/backend
code**, so it works with any ImGui backend (GLFW, SDL2/3, Win32, Metal, ...).

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

You get a Chataigne-style workspace: a tabbed Timeline ("Demo Sequence" with
audio/blocks/automation/gradient, and "Show" with triggers, cues and a loop
range), Inspector, Outliner, Media Pool, Detective, Scope, Logger — plus a
**Board** canvas and **Motion Path** editor in the Panels menu. Try:

- `Space` to play — the audio clips are audible (Settings panel: volume/off);
- drag media from the **Media Pool** onto the timeline; drag clips around
  (also vertically between layers), resize edges, drag the fade handles of a
  selected audio clip, right-click → *Split Here*;
- double-click an automation lane to add keys (inside a Bezier segment the
  shape is preserved), right-click a key to change its easing, drag Bezier
  squares or the mid-segment handle of Sine/Steps keys; select several keys and
  use the **transform box** to move/scale them together;
- arm a recording: select the "Energy" layer → *Pick record source...* →
  e.g. `/board/faderA/value` → enable *Record Arm* → play, wiggle the fader in
  the **Board** panel, stop: the movement becomes keys (Bezier-fitted);
- switch to the "Show" tab: `PageUp/PageDown` jump between cues, the trigger
  flags fire in the Logger as the playhead crosses them;
- right-click any parameter → *Watch in Detective*;
- drag any window tab to re-dock it, or pull it outside the app to detach it;
- **View → Save Layout As...**, then reload it with `Ctrl+1`;
- `Ctrl+Z` undoes *everything*, `Ctrl+S` saves `project.organic.json`.

First run generates demo WAVs in `./assets` and a demo project.
Run the headless test suite with `-DORGANIC_BUILD_TESTS=ON` → `./build/organic_tests`.

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
| `OrganicCore.h` | `Parameter`, `Container`, `Selection` (scopes/preselection), `linkInspectables`, `resolveParamAddress`, `UndoManager`, `Logger`, `OLOG` macros, `DrawParamWidget(Multi)`, `ParamPickerPopup` |
| `OrganicManager.h` | `BaseItem`, `BaseManager` (factory, clipboard, undoable ops), `ManagerListUI`, `ManagerCanvasUI` |
| `OrganicTimeline.h` | `SequenceManager`, `Sequence` (cues, loop range, play modes, ripple edits), `ClipLayer`/`Clip` (fades, media loop, overlap-free seating via `spanFree`/`resolveOverlap`), `ClipAutomation` (embedded per-clip curves/gradients, clip-local keys, host-fed recorder), `AutomationLayer` (easings, recorder, `applyDrawnPoints`), `GradientLayer`, `TriggerLayer`, `MediaPool`, `MediaPayload` |
| `OrganicTimelineUI.h` | `TimelineUI` (the editor; `body()` for embedding, `gui()` for a window) |
| `OrganicCurve2D.h` | `Curve2D` (arc-length 2D curves) + `Curve2DEditor` |
| `OrganicPanels.h` | `InspectorPanel` (multi-edit), `OutlinerPanel`, `LoggerPanel`, `MediaPoolPanel`, `ScopePanel`, `Detective`/`DetectivePanel`, `CommitPendingParamEdits` |
| `OrganicDock.h` | `DockManager`, `DockZone` |
| `OrganicEasing.h` | `EasingType`, `ease()`, `splitCubic`, `simplifyRDP`, `fitCubicBeziers` |
| `OrganicAudio.h` | `loadWav`/`saveWavPcm16`, `Peaks`, `AudioCache`, tone/beat/sweep generators |
| `OrganicAudioEngine.h` | `AudioEngine` (miniaudio playback of sequence clips) |

### Runtime files

Created in the working directory of the app: `organic_imgui.ini` (dock/window
state), `layouts/*.ini` (named layouts), `organic_app.json` (panel states,
prefs), `assets/*.wav` (demo audio), `project.organic.json` / `autosave.organic.json`.

---

## Quick control reference

`Space` play · ruler-drag scrub · `PageUp/PageDown` prev/next cue ·
`Ctrl+Wheel` zoom · `Shift+Wheel`/middle-drag pan · `F` fit ·
drag clips to move (vertically = change layer) · edges resize · fade handles on
selected audio clips · rubber-band (pre)select · `Ctrl+C/V` copy/paste at
playhead · `Ctrl+D` duplicate · `Del` delete · arrows nudge · `Alt` bypass
snap · *Magnet* snaps to clip edges/keys/cues/playhead · double-click lanes to
create clips/keys/triggers · right-click for context menus (easings, split,
layer ops, loop range, insert/remove time) · `Ctrl+Z`/`Ctrl+Shift+Z` undo/redo ·
`Ctrl+S` save · `Ctrl+1..9` layouts. Full list in [USAGE.md](USAGE.md).

## Notes & limitations

- Audio playback covers the current sequence's clips (gain, fades, offset,
  media looping, speed, reversed ping-pong). Loop wraps/seeks resync the audio
  clock and may produce a small click; there is no per-clip scrub preview.
- Requires the ImGui **docking** branch (checked at configure time).
- Tested with ImGui docking `9b4eb24` (1.92.9), ImPlot `d65a2be` (v1.0 API),
  nlohmann/json v3.12.0, miniaudio 0.11.25 (all pinned submodules), GCC 14, GLFW 3.4.

## Credits & licenses

- Concept and feature set inspired by **Ben Kuper**'s
  [juce_organicui](https://github.com/benkuper/juce_organicui) /
  [juce_timeline](https://github.com/benkuper/juce_timeline) (GPLv3) — this
  project is an independent re-implementation for Dear ImGui and shares no code.
- [Dear ImGui](https://github.com/ocornut/imgui) (MIT),
  [ImPlot](https://github.com/epezent/implot) (MIT),
  [nlohmann/json](https://github.com/nlohmann/json) (MIT).
- This repository has no license file yet — add one before publishing.
