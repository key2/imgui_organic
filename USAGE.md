# imgui_organic — organicui-style tooling for Dear ImGui

`imgui_organic` recreates the core experience of
[benkuper/juce_organicui](https://github.com/benkuper/juce_organicui) +
[juce_timeline](https://github.com/benkuper/juce_timeline) (the framework behind
Chataigne, LGML, BenTo...) on top of **Dear ImGui (docking branch)** and **ImPlot**:

| juce_organicui concept | imgui_organic equivalent |
|---|---|
| ShapeShifter panels / layouts / floating windows | `DockManager` + ImGui docking + multi-viewport (drag a tab out of the app to make an OS window) |
| Controllable / Parameter / ControllableContainer | `Parameter` / `Container` (`organic/OrganicCore.h`) |
| Inspector + InspectableSelectionManager | `InspectorPanel` + `Selection` |
| Outliner | `OutlinerPanel` |
| Custom Logger panel | `Logger` + `LoggerPanel` (`OLOG/OLOGW/OLOGE` macros) |
| UndoMaster | `UndoManager` (lambda-based actions, owner purging) |
| juce_timeline Sequence / layers / clips | `Sequence` / `ClipLayer` / `Clip` + `TimelineUI` |
| Automation + easings (Linear, Bezier, Hold, Sine, Elastic, Bounce, Steps, Noise) | `AutomationLayer` + `organic/OrganicEasing.h` |
| GradientColorManager (time→color track) | `GradientLayer` |
| AudioLayer waveforms (juce::AudioThumbnail) | WAV loader + `Peaks` min/max pyramid, drawn inside audio clips |
| Detective (value scope) | `ScopePanel` (ImPlot rolling plot of automation outputs) |
| Engine save/load (JSON documents) | JSON projects via nlohmann::json (`project.organic.json`) + autosave |

---

## 1. Building

Dependencies (Linux): CMake ≥ 3.16, a C++17 compiler, GLFW 3.3+, OpenGL.

```bash
sudo apt install cmake g++ libglfw3-dev   # if needed

git clone --recurse-submodules <url-of-imgui_organic>
cd imgui_organic
git clone --branch docking https://github.com/ocornut/imgui.git   # ImGui is NOT vendored

cmake -B build            # add -DORGANIC_IMGUI_DIR=... if ImGui lives elsewhere
cmake --build build -j
./build/organic_demo
```

See [README.md](README.md) for integrating the library into your own project
(`add_subdirectory` + `organic::organic`).

The repo layout:

```
imgui_organic/
├── implot/             [submodule] ImPlot (v1.0 API, pinned)
├── third_party/json/   [submodule] nlohmann/json (pinned)
├── (imgui/)            your own clone of Dear ImGui, docking branch
├── organic/            the reusable "organic" library
│   ├── Organic.h           umbrella header
│   ├── OrganicCore.*       Parameter/Container/Selection/Undo/Logger
│   ├── OrganicEasing.*     easing math
│   ├── OrganicAudio.*      WAV IO, peaks, procedural demo sounds, AudioCache
│   ├── OrganicTimeline.*   Sequence/Layer/Clip/Automation/Gradient/MediaPool model
│   ├── OrganicTimelineUI.* the timeline editor window
│   ├── OrganicPanels.*     Inspector/Outliner/Logger/MediaPool/Scope + param widgets
│   └── OrganicDock.*       dockspace, default layout, named layout presets
├── app/main.cpp        full demo application
├── README.md           setup + integration guide
└── USAGE.md            this file
```

Files created at runtime (next to the executable's working dir):
`assets/*.wav` (generated demo audio), `organic_imgui.ini` (window/dock state),
`organic_app.json` (panel open states, prefs), `layouts/*.ini` (named layouts),
`project.organic.json` (your project), `autosave.organic.json`.

---

## 2. Demo tour

Run `./build/organic_demo`. You get a Chataigne-style workspace:

- **Timeline** (center) — the sequence editor: audio clips with waveforms, block
  clips, an automation curve ("Energy") and a color gradient track ("Mood").
- **Inspector** (right) — auto-generated editors for whatever is selected.
- **Scope** (right-bottom) — rolling plot of every automation layer's output.
- **Outliner** (left) — the whole Container hierarchy, filterable.
- **Media Pool** (left-bottom) — drag entries onto the timeline to create clips.
- **Logger** (bottom) — app log with severity filters.
- **Settings** (hidden, open via *Panels* menu).

### 2.1 Window management (the "ShapeShifter" part)

- **Drag a window tab** to re-dock it anywhere: drop indicators show the 5 zones
  (left/right/top/bottom/center-as-tab), exactly like organicui's panels.
- **Drag a tab outside the OS window** to detach it into a real floating window
  (multi-viewport), like `ShapeShifterWindow`.
- **Splitters** between panels resize them.
- **Panels menu** — toggle every panel on/off.
- **View menu**:
  - *Reset Layout* — rebuilds the default layout.
  - *Save Layout As...* — snapshots the current arrangement into `layouts/<name>.ini`.
  - Saved layouts are listed in the menu; the first 9 get **Ctrl+1..Ctrl+9** hotkeys.
  - *Delete Layout* submenu.
- Everything is remembered between runs (`organic_imgui.ini` + `organic_app.json`).

### 2.2 Timeline controls

Transport / view:

| Action | Control |
|---|---|
| Play / pause | `Space` or the Play button |
| Scrub playhead | click/drag in the **ruler** |
| Go to start / end | `Home` / `End` |
| Zoom around mouse | `Ctrl` + mouse wheel |
| Pan view | `Shift` + wheel, **middle-drag**, or bottom scrollbar |
| Vertical scroll | mouse wheel |
| Fit content | `F` or the *Fit* button |
| Follow playhead | *Follow* toggle |
| Snap | *Snap* toggle + grid combo (Adaptive or fixed). Hold `Alt` to bypass while dragging |

Clips ("boxes"):

| Action | Control |
|---|---|
| Create | drag an item from **Media Pool** onto a clip lane, double-click an empty lane spot, or right-click → *Add Clip Here* |
| Create clip + new layer | drop media **below** the last layer |
| Select | click (`Ctrl` = multi-select, rubber-band drag on empty space selects many) |
| Edit contents | select → **Inspector** (name, start, length, color, file, gain...) |
| Move in time | drag the clip body (snaps; all selected clips move together) |
| Move to another layer | drag vertically across clip layers |
| Resize | drag the left/right clip edge (left-trimming an audio clip also shifts its media offset) |
| Duplicate | `Ctrl+D` or right-click menu |
| Delete | `Del` / `Backspace` or right-click menu |
| Nudge by one grid step | `Left` / `Right` arrows |
| Select all clips | `Ctrl+A` |

Audio clips render their **waveform** (min/max peaks) tinted by the *Gain*
parameter; set the *File* parameter to any `.wav` (PCM 8/16/24/32 or float32).

Automation layers:

| Action | Control |
|---|---|
| Add key | double-click the lane, or right-click → *Add Key Here* |
| Move key | drag it (time snaps, value clamps to the layer range) |
| Multi-select keys | rubber-band or `Ctrl`+click |
| Change easing | right-click a key → pick **Linear / Bezier / Hold / Sine / Elastic / Bounce / Steps / Noise** (applies to all selected keys) |
| Edit Bezier curve | select a key with Bezier easing → drag the two white square handles |
| Fine-edit key / easing params (freq, amplitude, steps...) | select key → **Inspector** |
| Delete keys | `Del` or right-click → *Delete Key* |

The orange dot on the curve is the live output value at the playhead — watch the
same value scroll in the **Scope** panel.

Gradient (color) layers:

| Action | Control |
|---|---|
| Add color key | double-click the strip |
| Move key | drag the diamond marker |
| Edit color | right-click the marker (inline picker) or select → Inspector |
| Delete | `Del` or right-click → *Delete Key* |

Layers:

| Action | Control |
|---|---|
| Add | *+ Layer* button, or right-click anywhere in the lanes |
| Select | click its header (then edit color/range in Inspector) |
| Rename | double-click the header, or header right-click → *Rename...* |
| Reorder / duplicate / delete | header right-click menu |
| Resize height | drag the bottom edge of the header |

Everything above is **undoable** (`Ctrl+Z` / `Ctrl+Shift+Z` or `Ctrl+Y`), including
drags, drops, renames, key edits and parameter changes (continuous edits collapse
into a single undo step, like organicui).

### 2.3 Projects

*File → Save / Save As / Open* (also `Ctrl+S`). Projects are readable JSON
containing the sequence (layers, clips, keys), the media pool and settings.
If `project.organic.json` exists next to the app it is loaded at startup;
autosave writes `autosave.organic.json` at the interval set in **Settings**.

---

## 3. Using the library in your own app

Link the `organic` target (it pulls in imgui + implot). Include `Organic.h`.

### 3.1 Minimal application skeleton

```cpp
#include "Organic.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
using namespace organic;

int main()
{
    glfwInit();
    GLFWwindow* win = glfwCreateWindow(1280, 800, "My Organic App", nullptr, nullptr);
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable
                   |  ImGuiConfigFlags_ViewportsEnable   // floating OS windows
                   |  ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // --- model -------------------------------------------------
    Sequence seq("My Sequence");
    seq.addLayer(Layer::LType::Clips, "Clips");
    registerRoot(&seq);                       // appears in the Outliner

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
        dock.preNewFrame();                    // applies pending layout loads

        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

        double now = glfwGetTime();
        seq.update(now - last); last = now;    // advances the playhead

        if (ImGui::BeginMainMenuBar())
        {
            dock.viewMenu();                   // layouts (reset/save/load)
            dock.panelsMenu();                 // panel toggles
            ImGui::EndMainMenuBar();
        }
        dock.gui();                            // dockspace + all open panels
        dock.popupsGui();
        dock.shortcuts();                      // Ctrl+1..9 layout hotkeys
        CommitPendingParamEdits();             // folds widget edits into undo steps

        ImGui::Render();
        int w, h; glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h); glClearColor(0.08f, 0.08f, 0.085f, 1); glClear(GL_COLOR_BUFFER_BIT);
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
    // ... shutdown backends/contexts
}
```

### 3.2 Parameters & containers (the organicui data model)

```cpp
class MyModule : public organic::Container
{
public:
    MyModule() : Container("My Module")
    {
        renamable = true; // editable name field in the Inspector

        speed  = addFloat ("Speed", 1.f, 0.f, 10.f, "Playback speed");
        speed->unit = "x";
        count  = addInt   ("Count", 4, 1, 16);
        active = addBool  ("Active", true);
        label  = addString("Label", "hello");
        mode   = addEnum  ("Mode", { "Off", "Auto", "Manual" }, 1);
        tint   = addColor ("Tint", ImVec4(1, 0.5f, 0, 1));
        pos    = addPoint2D("Position", ImVec2(0, 0));
        bang   = addTrigger("Bang", "Fire something");

        // per-parameter callback (like organicui's onContainerParameterChanged)
        bang->onChange = [](organic::Parameter&){ OLOG("MyModule", "bang!"); };
    }

    // change notification hook, bubbles up the parent chain too
    void onParamChanged(organic::Parameter* p) override
    {
        if (p == speed) OLOG("MyModule", "speed is now " << p->floatValue());
    }

    organic::Parameter *speed, *count, *active, *label, *mode, *tint, *pos, *bang;
};

MyModule mod;
registerRoot(&mod);   // visible in Outliner; click it -> auto Inspector editors
```

Parameter values:

```cpp
float s   = mod.speed->floatValue();
mod.speed->setValue(2.5f);          // clamped to range, notifies listeners
mod.speed->setUndoable(2.5f);       // same, but recorded in the undo stack
mod.speed->resetToDefault();
bool edited = mod.speed->isOverriden();
std::string addr = mod.speed->controlAddress();   // "/myModule/speed"
```

Serialization (parameters by shortName, recursive through subclasses):

```cpp
organic::json j = mod.save();
mod.load(j);
```

Custom Inspector content — override `inspectorGui()`:

```cpp
void inspectorGui() override
{
    Container::inspectorGui();      // draws all parameter widgets (with undo)
    ImGui::Separator();
    if (ImGui::Button("Do something")) doSomething();
}
```

To draw a single parameter widget anywhere (any panel, not just the Inspector):

```cpp
organic::DrawParamWidget(*mod.speed);   // right widget for the type + undo + context menu
```

### 3.3 Selection & Inspector

Anything deriving from `Inspectable` (all `Container`s, so also clips and layers)
can be selected and inspected:

```cpp
mod.select();                 // exclusive select
mod.select(true);             // add/toggle (Ctrl-click semantics)
Selection::get().clear();
auto clips = Selection::get().getAs<organic::Clip>();   // typed multi-selection
```

`InspectorPanel()` renders every selected item: type header, name field (when
`renamable`) and `inspectorGui()`.

### 3.4 Undo / redo

```cpp
auto& um = organic::UndoManager::get();

// execute + record
um.perform("Add Thing",
    [&]{ things.push_back(makeThing()); },
    [&]{ things.pop_back(); },
    { &things });                 // owners: actions are dropped if the owner dies

// record something that already happened (end of a mouse drag)
um.pushDone("Move Thing", redoFn, undoFn, { &things });

um.undo();  um.redo();
um.undoName();                    // for the Edit menu label
```

All built-in interactions (clip moves, resizes, drops, key edits, renames,
parameter widgets) already go through this. Continuous widget edits are coalesced
by `CommitPendingParamEdits()` — call it once per frame.

### 3.5 Building a sequence in code

```cpp
using namespace organic;
Sequence seq("Show");
seq.lengthP->setValue(60.f);

// clip layer with an audio clip
auto* tracks = static_cast<ClipLayer*>(seq.addLayer(Layer::LType::Clips, "Audio"));
Clip* c = tracks->addClip(Clip::CType::Audio, "Music", /*start*/0.0, /*len*/10.0);
c->setAudioFile("assets/beat.wav", /*adjustLength*/true);   // loads waveform peaks
c->gainP->setValue(1.5f);

// automation
auto* fade = static_cast<AutomationLayer*>(seq.addLayer(Layer::LType::Automation, "Master"));
fade->rangeMinP->setValue(0.f);
fade->rangeMaxP->setValue(100.f);
fade->addKey(0.0,  0.f,  EasingType::Linear);
AutoKey* k = fade->addKey(5.0, 100.f, EasingType::Bezier);
k->ep.a1 = ImVec2(0.5f, 0.f);          // bezier anchors (segment-relative)
fade->addKey(10.0, 20.f, EasingType::Bounce);

// gradient
auto* colors = static_cast<GradientLayer*>(seq.addLayer(Layer::LType::Gradient, "Lights"));
colors->addKey(0.0, ImVec4(0, 0, 1, 1));
colors->addKey(5.0, ImVec4(1, 0.5f, 0, 1));

// playback + sampling outputs (drive your engine from these!)
seq.play();
// each frame:
seq.update(dt);
float  level = fade->valueAt(seq.currentTime);     // 0..100
ImVec4 light = colors->colorAt(seq.currentTime);
```

`Sequence::save()/load()` round-trips the whole structure (used by the demo's
project files).

### 3.6 Drag & drop media

Any ImGui drag source can create clips by carrying a `MediaPayload`:

```cpp
if (ImGui::BeginDragDropSource())
{
    organic::MediaPayload p{};
    snprintf(p.name, sizeof(p.name), "Impact");
    snprintf(p.file, sizeof(p.file), "sfx/impact.wav");  // empty for block clips
    p.kind = 1;                    // 0 = block, 1 = audio
    p.duration = 0;                // <= 0 => use the audio file's duration
    p.color[0] = 1; p.color[1] = 0.4f; p.color[2] = 0.1f; p.color[3] = 1;
    ImGui::SetDragDropPayload(ORGANIC_MEDIA_PAYLOAD, &p, sizeof(p));
    ImGui::Text("Impact");
    ImGui::EndDragDropSource();
}
```

Drop it on a clip lane (ghost preview shows the snapped position) or below the
layers to spawn a new layer. The built-in **Media Pool** panel does exactly this.

### 3.7 Dock zones & layouts

```cpp
DockManager dock;
dock.addPanel("Main View", DockZone::Center,      drawFn);
dock.addPanel("Tools",     DockZone::Left,        drawFn);
dock.addPanel("Props",     DockZone::Right,       drawFn, /*defaultOpen*/true);
dock.addPanel("Console",   DockZone::Bottom,      drawFn, false);   // hidden by default

dock.requestReset();                    // rebuild the default layout
dock.saveLayoutToFile("mixing");        // -> layouts/mixing.ini (+ panel states)
dock.requestLoadLayout("mixing");       // applied before next NewFrame
dock.listLayouts();                     // for menus

// persist panel-open states in your own prefs file:
json state = dock.saveState();  dock.loadState(state);
```

Zones: `Center, Left, LeftBottom, Right, RightBottom, Bottom` — used only when
building the default layout; users can rearrange everything afterwards.

### 3.8 Logging

```cpp
OLOG ("Engine", "loaded " << n << " things");
OLOGW("Audio",  "file not found: " << path);
OLOGE("Net",    "connection lost");
```

Shown color-coded in `LoggerPanel()` with filters, copy and clear.

### 3.9 Audio & waveforms

```cpp
organic::AudioBuffer buf;
std::string err;
if (organic::loadWav("drums.wav", buf, &err))    // PCM 8/16/24/32 + float32
{
    organic::Peaks peaks;
    peaks.build(buf);                            // min/max per 512-sample bin
    float mn, mx;
    peaks.query(0.5, 0.51, mn, mx);              // aggregate over a time range
}
organic::saveWavPcm16("out.wav", buf);

// procedural test material
auto beat  = organic::makeBeat (4.f, 120.f);
auto tone  = organic::makeTone (2.f, 440.f);
auto sweep = organic::makeSweep(4.f, 80.f, 2400.f);

// shared, cached assets (what clips use internally)
auto asset = organic::AudioCache::get().load("drums.wav");
```

Note: this framework **draws** audio (like organicui's thumbnails); it does not
output sound. Hook your own audio engine to `seq.currentTime` if you need playback.

### 3.10 Scope (Detective)

```cpp
organic::ScopeBuffers scope;
// once per frame, before drawing panels:
scope.push(seq, ImGui::GetTime());
// panel:
organic::ScopePanel(scope, &open);
```

Every `AutomationLayer` output is plotted over the last 10 s with the layer's color.

---

## 4. Extending

- **New parameter types**: add a `PType`, extend `Parameter` getters,
  `valueToJson/valueFromJson` and one `case` in `DrawParamWidget`.
- **New layer kinds**: subclass `Layer` (see `GradientLayer` — ~120 lines of model),
  add a `case` in `Sequence::addLayer`, and a drawing/interaction branch in
  `OrganicTimelineUI.cpp` (hit-test → input → draw follows a clear 3-pass pattern).
- **New panels**: any `void(bool*)` function; register it with
  `dock.addPanel(...)` and it participates in docking, layouts and the Panels menu.
- **Remote control / scripting** (organicui's OSC/JS): the model is fully
  addressable (`controlAddress()`), so an OSC bridge only needs a map from
  addresses to `Parameter*`.

## 5. Known limitations

- No audio *playback* (waveform display only).
- Media Pool item removal is not undoable (everything on the timeline is).
- Automation keys use one value per key (no 2D curves yet); Bezier X anchors are
  clamped to the segment like CSS easings.
- Screenshots of layouts store ImGui ini data; project files don't embed layouts.

Shortcuts recap: `Space` play · `F` fit · `Ctrl+Wheel` zoom · `Shift+Wheel`/middle-drag pan ·
`Del` delete · `Ctrl+D` duplicate · `Ctrl+A` select all clips · `Alt` bypass snap ·
`Ctrl+Z`/`Ctrl+Shift+Z` undo/redo · `Ctrl+S` save · `Ctrl+1..9` layouts.
