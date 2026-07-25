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

- **Timeline** (center) — sequences in tabs ("+" adds one). "Demo Sequence" has
  audio clips with waveforms & fades, block clips, an automation curve
  ("Energy") and a gradient track ("Mood"); "Show" demonstrates trigger flags,
  time cues, a loop range and looped audio. Audio is audible (see Settings).
- **Inspector** (right) — auto-generated editors for whatever is selected;
  homogeneous multi-selections get true multi-editing.
- **Detective** (right-bottom) — plots of watched parameters (right-click any
  parameter widget → *Watch in Detective*).
- **Scope** (right-bottom) — rolling plot of every automation layer's output.
- **Outliner** (left) — the whole Container hierarchy, filterable.
- **Media Pool** (left-bottom) — drag entries onto the timeline to create clips.
- **Logger** (bottom) — app log with severity filters (trigger fires land here).
- **Board / Board List / Motion Path / Settings** (open via *Panels* menu) —
  the generic 2D canvas + list manager demo, the Curve2D editor, and app
  settings (autosave, audio output, master volume).

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
| Play mode | toolbar combo: Once / Loop / **Ping-Pong** (audio plays reversed) |
| Scrub playhead | click/drag in the **ruler** |
| Go to start / end | `Home` / `End` |
| Previous / next cue | `PageUp` / `PageDown` |
| Zoom around mouse | `Ctrl` + mouse wheel |
| Pan view | `Shift` + wheel, **middle-drag**, or bottom scrollbar |
| Vertical scroll | mouse wheel |
| Fit content | `F` or the *Fit* button |
| Follow playhead | *Follow* toggle |
| Grid | *Time* or *Beats* mode (Beats uses the sequence BPM + beats/bar; division combo) |
| Snap | *Snap* toggle + grid combo. Hold `Alt` to bypass while dragging |
| Magnet | *Magnet* toggle — snaps drags to clip edges, keys, cues, loop points and the playhead (orange guide lines) |

Ruler extras (right-click the ruler):

| Action | Control |
|---|---|
| Add a **time cue** | right-click → *Add Cue Here*; drag the blue diamond to move, double-click to jump, right-click it to rename/delete |
| **Loop range** | right-click → *Set Loop In/Out Here* (or drag the orange handles); *Clear Loop Range* removes it; playback loops/ping-pongs inside it |
| **Insert time** | right-click → duration field + *Insert Time Here* — ripples clips, keys, triggers, cues and the loop range |
| **Remove time** | set a loop range → right-click → *Remove Loop Range Time* |

Clips ("boxes"):

| Action | Control |
|---|---|
| Create | drag an item from **Media Pool** onto a clip lane, double-click an empty lane spot, or right-click → *Add Clip Here* |
| Create clip + new layer | drop media **below** the last layer |
| Select | click (`Ctrl` = multi-select; rubber-band drag **preselects** and commits on release) |
| Edit contents | select → **Inspector** (name, start, length, color, file, gain, fades, loop...) |
| Move in time | drag the clip body (grid + magnet snap; all selected clips move together) |
| Move to another layer | drag vertically across clip layers |
| Resize | drag the left/right clip edge (left-trimming an audio clip also shifts its media offset) |
| **Fades** (audio) | select the clip → drag the two round handles near its top corners |
| **Loop media** (audio) | Inspector → *Loop Media* — tiles the file to fill the clip (wrap separators shown) |
| **Split** | right-click → *Split Here* / *Split At Playhead* |
| Copy / paste | `Ctrl+C`, then `Ctrl+V` pastes at the playhead (into the original layers) |
| Duplicate | `Ctrl+D` or right-click menu |
| Delete | `Del` / `Backspace` or right-click menu |
| Nudge by one grid step | `Left` / `Right` arrows |
| Select all clips | `Ctrl+A` |
| Enable/bypass | Inspector *Enabled* (clip or whole layer) — dimmed and muted |

Audio clips **play back** through miniaudio (gain, fades, offset, media loop,
speed, reversed ping-pong) and render their waveform (min/max peaks); set the
*File* parameter to any `.wav` (PCM 8/16/24/32 or float32).

Automation layers:

| Action | Control |
|---|---|
| Add key | double-click the lane (inside a Bezier segment the curve **shape is preserved**), or right-click → *Add Key Here* |
| Move key | drag it (time snaps + magnet, value clamps to the layer range) |
| Multi-select keys | rubber-band or `Ctrl`+click |
| **Transform box** | with ≥2 selected keys: drag the box border to move, the L/R handles to scale in time, T/B to scale values |
| Change easing | right-click a key → **Linear / Bezier / Hold / Sine / Elastic / Bounce / Steps / Noise / Perlin** (applies to all selected keys) |
| Edit Bezier curve | select a key with Bezier easing → drag the two white square handles |
| Edit Sine/Elastic/Noise/Perlin/Steps | select the key → drag the round **mid-segment handle** (x = frequency/steps, y = amplitude), or use the Inspector |
| Copy / paste keys | `Ctrl+C` / `Ctrl+V` (pastes at the playhead into a matching layer) |
| Delete keys | `Del` or right-click → *Delete Key* |
| Range | Inspector: *Range Min/Max* + *Range Remap* (Absolute keeps values, Proportional rescales keys) |

**Recording live values** (organicui's AutomationRecorder): select the layer →
*Pick record source...* (any parameter address) → enable *Record Arm* → play.
Recorded points draw in red; on stop/loop the recording replaces that time range
with keys, simplified as raw *Points*, *Linear (RDP)* or *Bezier Fit* with the
*Record Tolerance* parameter. One undo step.

The orange dot on the curve is the live output value at the playhead — it is
also exposed as the read-only *Output* parameter (watch it in the Detective).

Trigger layers:

| Action | Control |
|---|---|
| Add trigger | double-click the lane, or right-click → *Add Trigger Here* |
| Move | drag the flag (time + vertical flag position) |
| Rename / fire manually / delete | right-click the flag |
| React from code | `triggerLayer->onTriggered = [](TriggerLayer&, TimeTrigger& t) { ... };` (fires also land in the Logger, with a flash on the flag) |

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
drags, drops, renames, key edits, recordings, ripple edits and parameter changes
(continuous edits collapse into a single undo step, like organicui).

### 2.3 Board (generic manager framework)

The **Board** panel is `ManagerCanvasUI` on a `BaseManager` — organicui's
`BaseManagerViewUI`: pan with middle/Alt-drag, `Ctrl+Wheel` zoom, drag cards by
their **title bar**, resize at the bottom-right corner, rubber-band preselect,
snap-to-item guide lines, align/distribute toolbar (L C R T M B + spread H/V),
minimap navigation (click/drag it), `F` frames, right-click to add items from
the factory menu, `Ctrl+C/V` copies across compatible managers, `Del`,
`Ctrl+D`, lock items via context menu. **Board List** shows the same manager as
a list: search, drag rows to reorder, enable/color/mini-mode per row,
`Up/Down` select, `Ctrl+Up/Down` move.

The "Fader" cards host live widgets — wiggle one while recording an automation
layer from its address, or watch it in the Detective.

### 2.4 Detective & Motion Path

- **Detective**: right-click any parameter widget → *Watch in Detective*
  (or *+ Watcher* and pick an address). Each watcher has enable, color, time
  window and its own scrolling plot. Watchers persist in the project.
- **Motion Path**: a `Curve2D` editor — double-click to add/insert points
  (Bezier segments split shape-preserving), drag points/handles, right-click a
  point to toggle Bezier or delete, rubber-band select, `Del`. The orange dot
  travels along the curve (arc-length parameterized, i.e. constant speed) as
  the current sequence plays.

### 2.5 Projects

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

### 3.10 Scope & Detective

```cpp
organic::ScopeBuffers scope;             // automation outputs of one sequence
scope.push(seq, ImGui::GetTime());       // once per frame
organic::ScopePanel(scope, &open);

organic::Detective detective;            // watch ANY parameter
organic::Detective::main = &detective;   // enables "Watch in Detective" everywhere
detective.update(ImGui::GetTime());      // once per frame
organic::DetectivePanel(detective, &open);
detective.watch("/board/faderA/value");  // programmatic watch
```

### 3.11 Generic managers (list + 2D canvas)

```cpp
using namespace organic;

class MyItem : public BaseItem
{
public:
    MyItem() : BaseItem("MyItem", "My Item")
    {
        amountP = addFloat("Amount", 0.5f, 0.f, 1.f);
        viewSize = ImVec2(200, 90);          // canvas card size
    }
    Parameter* amountP = nullptr;
    void canvasGui() override                // live widgets inside the card
    {
        DrawParamWidget(*amountP);
    }
};

BaseManager things("Things");
things.selectionScopeName = "things";        // own selection scope (optional)
things.addDef("Basics/My Item", "MyItem", []{ return std::make_unique<MyItem>(); });
registerRoot(&things);

// in panels:
ManagerCanvasUI(things);   // infinite canvas: snap guides, align tools, minimap
ManagerListUI(things);     // list: search, reorder, enable/color/mini-mode

// programmatic (all undoable):
BaseItem* it = things.undoableAdd("MyItem", ImVec2(100, 40));
things.undoableDuplicate({ it });
things.copyToClipboard(things.selectedItems());
things.pasteFromClipboard();
json state = things.save();  things.load(state);
```

### 3.12 Triggers, cues, loop ranges, recording

```cpp
auto* tl = static_cast<TriggerLayer*>(seq.addLayer(Layer::LType::Triggers, "Cues"));
tl->addTrigger(2.0, "Lights Up");
tl->onTriggered = [](TriggerLayer&, TimeTrigger& t)
{
    // fire your engine here (also logged automatically)
};

seq.addCue(6.0, "Drop");                      // ruler markers, PageUp/Down navigation
seq.loopIn = 2.0; seq.loopOut = 14.0;         // loop range (used by Loop / Ping-Pong)
seq.playModeP->setValue(2);                   // 0 Once, 1 Loop, 2 Ping-Pong
seq.insertTime(4.0, 2.0);                     // ripple edit across all layers
seq.removeTimespan(4.0, 6.0);

// automation recording from any parameter address:
auto* rec = static_cast<AutomationLayer*>(seq.addLayer(Layer::LType::Automation, "Rec"));
rec->recSourceP->setValue(std::string("/board/faderA/value"));
rec->recArmP->setValue(true);                 // records while the sequence plays
// ... on stop/loop the keys are created (RDP or Bezier-fitted) in one undo step
```

### 3.13 Audio playback

```cpp
AudioEngine::get().init();                    // once at startup (safe to fail)
// each frame:
AudioEngine::get().setMasterVolume(0.8f);
AudioEngine::get().syncFromSequence(seq);     // nullptr = silence
// at shutdown:
AudioEngine::get().shutdown();
```

The engine mixes the sequence's enabled audio clips at the transport position
(gain, fades, offset, media looping, speed, reversed ping-pong) and follows the
UI clock with drift correction.

### 3.14 Curve2D (2D spatial curves)

```cpp
Curve2D path("Motion Path");
path.addKey(ImVec2(0, 0));
Curve2DKey* k = path.addKey(ImVec2(0.5f, 1.f));
k->bezier = true;                              // cubic segment to the next key
path.addKey(ImVec2(1, 0));
path.rebuild();                                // arc-length tables

ImVec2 pos = path.valueAtNorm(0.25f);          // constant-speed traversal
Curve2DEditor(path, /*normPos*/ 0.25f);        // pan/zoom editor + moving dot
```

### 3.15 Selection scopes, preselection & links

```cpp
Selection& boardSel = Selection::scope("board");   // per-panel selection
boardSel.set(item);
Selection* active = Selection::active();           // what the Inspector follows

sel.setPreselection(candidates);                   // while rubber-banding
sel.commitPreselection();                          // on release

linkInspectables(clip, mediaItem);                 // selecting one highlights the other
```

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

- Audio playback follows the UI clock with drift correction; loop wraps and
  seeks resync with a small click, and there is no scrub preview.
- Media Pool item removal is not undoable (everything on the timeline is).
- Bezier X anchors are clamped to the segment like CSS easings.
- Layout files store ImGui ini data; project files don't embed layouts.

Shortcuts recap: `Space` play · `F` fit · `PageUp/Down` cues · `Ctrl+Wheel` zoom ·
`Shift+Wheel`/middle-drag pan · `Del` delete · `Ctrl+C/V` copy/paste ·
`Ctrl+D` duplicate · `Ctrl+A` select all · `Alt` bypass snap ·
`Ctrl+Z`/`Ctrl+Shift+Z` undo/redo · `Ctrl+S` save · `Ctrl+1..9` layouts.
