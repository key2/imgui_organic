# Feature parity with juce_organicui — what's missing & implementation complexity

`imgui_organic` re-implements the *core* of
[juce_organicui](https://github.com/benkuper/juce_organicui) (+ its companion
[juce_timeline](https://github.com/benkuper/juce_timeline)). This document lists
everything the original framework offers that is **not (or only partially)
implemented here**, with an estimate of what it would take to add it on top of
the current codebase.

### Complexity legend

| Rating | Rough effort | Meaning |
|---|---|---|
| **Trivial** | < 1 h | Few lines, no design work |
| **Low** | hours → 1 day | Straightforward, existing patterns cover it |
| **Medium** | 2 – 5 days | New subsystem or careful UI/undo work |
| **High** | 1 – 3 weeks | Significant subsystem, new dependencies or math |
| **Very High** | > 3 weeks | Whole product area (server, web app, scripting...) |

Prerequisite chains are noted — several "big" features become much cheaper once
a shared prerequisite exists. The generic **2D canvas view** and a basic
**parameter picker** are now implemented; the main remaining enabler is a full
**TargetParameter** type.

### Status overview by subsystem

| organicui subsystem | Status here |
|---|---|
| ShapeShifter docking / layouts | ✅ mostly done (ImGui docking) — few extras missing |
| Controllable / Parameter model | 🟡 core done — advanced types & control modes missing |
| Manager / BaseItem framework | ✅ **done** — generic list UI + 2D canvas (snap/align/minimap), factory, clipboard |
| Inspector / Selection | ✅ **done** — multi-edit, preselection, linked highlight, scopes, scroll memory |
| Automation / curves | ✅ **done** — recorder + RDP/Bezier simplification, transform box, Curve2D, all easings |
| Timeline (juce_timeline) | ✅ **done** — triggers, cues, loop range, ping-pong, BPM grid, audio playback, fades, multi-sequence |
| Undo / Logger / Outliner | ✅ done (minor gaps) |
| Engine / document lifecycle | 🟡 save/load/autosave done — dirty flag, versioning, async load missing |
| Detective (value scope) | ✅ **done** — watch any parameter (right-click → Watch), per-watcher windows |
| Warnings system | ❌ missing |
| Scripting (JS) | ❌ missing |
| OSC remote control / OSCQuery | ❌ missing |
| Dashboard (remote control surfaces) | ❌ missing |
| Parrot (record/replay) | ❌ missing |
| Help system | ❌ missing (tooltips only) |
| Updater / CrashHandler / Progress | ❌ missing |
| Global settings / keymappings | 🟡 minimal settings — keymap editor & prefs missing |

---

## 1. Controllable / Parameter system

| Missing feature | organicui reference | Complexity | Notes for an ImGui implementation |
|---|---|---|---|
| Point3D parameter | `Point3DParameter`, `TripleSliderUI` | **Low** | Clone of Point2D; `DragFloat3`; add `ImVec4`/array storage to `Value` variant |
| File parameter (+ browser, project-relative paths) | `FileParameter` | **Medium** | Needs a file dialog: embed a small ImGui file browser or `portable-file-dialogs`; relative-path resolution vs. project file is easy |
| **Target parameter** (pick any Controllable/Container in the tree) | `TargetParameter`, `TargetParameterUI` | **Medium–High** | Breadcrumb/tree popup picker with type filters, ghost value for broken links, re-link on rename. *Prerequisite for: REFERENCE mode, Detective watchers, Parrot, Dashboard bindings, any routing/mapping UI* |
| Control mode: REFERENCE (param follows another param) | `Parameter::ControlMode::REFERENCE` | **Medium** | Value forwarding + loop guard; needs TargetParameter first |
| Control mode: EXPRESSION (live-evaluated expression) | `ScriptExpression` | **Medium** (math-only via `exprtk`/`tinyexpr`) / **High** (JS, dependency-listening) | Math-only expressions referencing other params by address is the pragmatic 80 % |
| Control mode: AUTOMATION (attach an automation to any param) | `ParameterAutomation`, `PlayableParameterAutomation` | **Medium** | Reuse `AutomationLayer` data standalone; per-param transport (loop/ping-pong), inspector mini curve editor; drive from app update |
| Value interpolation ("animate to value over N seconds") | `Parameter::ValueInterpolator` | **Low–Medium** | Central interpolator list ticked each frame; easing reuse |
| Weighted/morph values | `setWeightedValue(values, weights)` | **Low** | Per-type lerp helper |
| User-created parameters at runtime ("custom variables") | `GenericControllableManager`, `userCanAddControllables` | **Medium** | "+ Add parameter" menu (type list), user params flagged & serialized with full definition (type/range/name) |
| Save only overridden values (+ `forceSaveValue`) | `Parameter::isOverriden` saving | **Low** | We already track `isOverriden`; change `Container::save` policy |
| Enum: user-editable options, button-bar UI | `EnumParameter` extras | **Low** | Options editor in context menu; radio-row widget |
| Int hex display, float unit-step snapping, h:m:s:ms time labels (editable) | `IntParameter` hexMode, `FloatParameter` unitSteps, `TimeLabel` | **Low** each | Formatting/parsing helpers in `DrawParamWidget` |
| Per-parameter custom UI colors, feedback-only styling | `ParameterUI` custom colors | **Low** | Optional color fields honored by `DrawParamWidget` |
| XY pad widget for Point2D | `P2DUI` | **Low** | Custom canvas widget (~150 lines) with undo capture |
| Color status lamp (value→color map) | `ColorStatusUI` | **Low** | Map + colored circle widget |
| Trigger blink-on-fire / image buttons | `TriggerBlinkUI`, `TriggerImageUI` | **Low** | Timestamp of last fire + fading highlight; texture buttons |

## 2. Manager / BaseItem framework — ✅ COMPLETED

Implemented in `OrganicManager.h/.cpp`:

- ✅ `BaseItem` (enable toggle, color, mini-mode, `uiLocked`, canvas pos/size,
  `canvasGui()` for live widgets inside cards) and `BaseManager` (typed factory
  with menu paths, undoable add/remove/duplicate/reorder, JSON round-trip,
  per-manager selection scope).
- ✅ `ManagerListUI` — search bar, factory "+ Add" menus, drag-to-reorder rows
  (single undo step), enable/color/mini-mode/remove per row, shift-range &
  ctrl selection, Up/Down select, Ctrl+Up/Down move, Del/Ctrl+D/C/V/A.
- ✅ `ManagerCanvasUI` — infinite 2D canvas: pan (middle/Alt-drag), zoom at
  mouse, grid, movable/resizable cards (title-bar drag; body hosts real ImGui
  widgets), **snap-to-item guides**, align/distribute toolbar (L C R T M B H V),
  **minimap** navigation, rubber-band **preselection**, context menus, `F` frame.
- ✅ System-clipboard copy/paste of items (works across managers sharing types);
  timeline clips/keys have their own copy/paste at the playhead.
- The demo "Board" panel shows both UIs on one manager (Note/Value items).

Remaining gap: per-item scripts (`canHaveScripts`) — see Scripting section.

## 3. Inspectable / Selection / Inspector — ✅ COMPLETED

- ✅ Preselection: rubber bands preselect (lighter outline) and commit on release
  (`Selection::setPreselection/commitPreselection`).
- ✅ Linked inspectables: `linkInspectables(a, b)`; selecting one highlights the
  other (blue outline in timeline/board, tinted rows in Outliner/Media Pool —
  demo: first audio clip ↔ its media item).
- ✅ Multiple selection scopes: `Selection::scope("name")` per panel (the Board
  uses its own), `Selection::active()` — the Inspector follows the last active
  scope, like organicui's `activeSelectionManager`.
- ✅ True multi-editing: homogeneous selections render one widget set writing to
  all objects (single undo step, "(mixed)" indicator, multi tooltips).
- ✅ Per-selection Inspector scroll memory.
- Rubber band remains per-view code (timeline/canvas) rather than a standalone
  overlay class — behavior parity is complete.

## 4. Docking / ShapeShifter extras

| Missing feature | organicui reference | Complexity | Notes |
|---|---|---|---|
| Temporary maximize one panel + restore previous layout | `temporary full content` / ghost layout | **Low–Medium** | Save ini to memory, single-window layout, restore on toggle |
| Layout lock mode (prevent re-docking) | `lockMode` | **Low** | `ImGuiDockNodeFlags_NoDockingSplit/NoUndocking` on the dockspace |
| Per-tab custom rename | `ShapeShifterPanelTab` customName | **Low–Medium** | ImGui window titles are IDs — use `Title###id` trick + persistence |
| Multiple instances of the same panel type | ShapeShifter contents | **Low** | Panel registry entries with numbered window names |
| Panels grouped in sub-menus | `isInViewSubMenu` | **Trivial** | |

## 5. Automation & curves — ✅ COMPLETED

- ✅ Shape-preserving key insertion (`AutomationLayer::insertKeyAt`, de Casteljau
  split with anchor re-normalization) — double-clicking inside a Bezier segment
  keeps the curve identical.
- ✅ Ripple **insert/remove timespan** across all layers + cues + loop range
  (`Sequence::insertTime/removeTimespan`, ruler context menu, content-snapshot undo).
- ✅ **Multi-key transform box**: ≥2 selected keys get a box with move border and
  L/R (time scale) + T/B (value scale) handles, one undo step.
- ✅ **Recorder** (`recArm`, source address with picker, normalization from the
  source range, auto-disarm, live red overlay): simplification as raw Points,
  **RDP** (`simplifyRDP`) or **cubic Bezier fitting** (`fitCubicBeziers`,
  Graphics-Gems style with Newton reparameterization) with tolerance parameter.
- ✅ Perlin easing (gradient noise, on top of Linear/Bezier/Hold/Sine/Elastic/
  Bounce/Steps/Noise) and **in-lane handles** for Sine/Elastic/Noise/Perlin
  (freq/amp) and Steps (count) at the segment midpoint.
- ✅ Range remap modes (Absolute / Proportional) on range changes; sequence
  length change modes (Keep / Stretch / Stick To End) — both undo-symmetric.
- ✅ **Curve2D** (`OrganicCurve2D`): arc-length parameterized 2D paths with
  linear/Bezier segments, shape-preserving insertion, pan/zoom editor with
  handles/rubber-band/undo, travelling position dot ("Motion Path" panel).
- ✅ Gradient per-key hold (NONE interpolation) + live automation `output`
  parameter (watchable in the Detective, recordable by other layers).
- N/A: background-thread gradient rendering (per-frame drawing is cheap in ImGui).

## 6. Timeline (juce_timeline parity) — ✅ COMPLETED

- ✅ **Trigger layers**: draggable cue flags (time + flag height), fired states
  with crossing detection (both directions, loop-aware), flash feedback, log +
  `onTriggered` callback, rename/fire-now/delete menus, full undo.
- ✅ **Time cues** on the ruler: add/drag/rename/delete, double-click to jump,
  `PageUp/PageDown` navigation, magnet targets.
- ✅ **Loop range** (in/out) with draggable handles, shading, context-menu set/clear.
- ✅ Play modes: Once / Loop / **Ping-Pong** (direction-aware triggers & audio).
- ✅ **Multiple sequences**: `SequenceManager` + timeline tabs (add "+", close,
  undoable), per-sequence view state; Scope/audio/status follow the active tab.
- ✅ **Audio playback** (`AudioEngine`, miniaudio): mixes the current sequence's
  audio clips with gain, fades, media offset, media looping, speed and reversed
  ping-pong; drift-corrected against the UI clock; Settings: output on/off +
  master volume. (Loop wraps resync with a small click; no scrub preview.)
- ✅ Per-clip **fade in/out** with draggable handles, envelope applied to the
  waveform display and playback.
- ✅ Musical grid: BPM + beats-per-bar sequence parameters, Beats ruler mode
  (bar labels) with beat-subdivision snapping.
- ✅ **Magnet snapping** to clip edges, trigger/cue times, loop points and the
  playhead, with orange guide lines (toolbar toggle, `Alt` bypasses).
- ✅ Clip **split** (at mouse / at playhead, offset-correct for audio).
- ✅ **Media looping** inside clips (waveform tiling with wrap separators).
- ✅ Layer **drag-reorder** with the mouse (insertion line preview) + menu moves.
- ✅ Clips/keys **copy/paste** at the playhead through the system clipboard.

## 7. Engine / document lifecycle

| Missing feature | organicui reference | Complexity | Notes |
|---|---|---|---|
| Dirty tracking (`*` in title, "save changes?" prompt on quit/new/open) | `FileBasedDocument` | **Low** | Bump a revision on undoable actions; compare at exits |
| Recent files menu | `OrganicApplication` | **Low** | Persist list in `organic_app.json` |
| Rotating autosaves + crash-recovery prompt at startup | `Engine` autosave | **Low–Medium** | `autosave/<n>.json` ring; offer restore when newer than project |
| File format versioning & migration hooks | `checkFileVersion`, `ProjectSettings` | **Low** | Version int + ordered migration lambdas |
| Compressed saves (gzip) | `compressOnSave` | **Low** | `miniz` single-file dep |
| Threaded/async project loading + progress window | `Engine` + `ProgressTask`/`ProgressWindow` | **Medium** | Parse JSON on a worker, build model on main thread in chunks; modal progress overlay |
| Command-line handling (open file, headless flags) | `OrganicApplication` | **Low** | |

## 8. Detective, Parrot, Warnings, Comments

| Feature | organicui reference | Status / Complexity | Notes |
|---|---|---|---|
| Watch **any** parameter (right-click → "Watch in Detective") with per-watcher enable/window/color, address picker, live value readout | `Detective`, `ControllableDetectiveWatcher` | ✅ **done** | `Detective` is a `BaseManager` of watcher items (persisted in projects); automation layers expose a watchable `output` parameter |
| Parrot: record & replay timed value streams of selected controllables (macro recorder with transport) | `Parrot`, `ParrotRecord` | **Medium** | Recorder = timestamped `(address, value)` list; playback sets values; UI = transport + record list |
| Warnings: per-object warning state, ⚠ icons on items/editors, global warnings panel with "resolve" navigation | `WarningTarget`, `WarningReporter` | **Low–Medium** | Mixin on `Container` + icon in Inspector/Outliner/clip headers + table panel |
| Free-floating comment notes (color, size, per-project) | `CommentManager`, `CommentItem` | ✅ mostly (via Board) | The generic canvas + `NoteItem` covers organicui's comment boards; a dedicated per-project overlay would be **Low** |

## 9. Scripting & remote control (the big ones)

| Missing feature | organicui reference | Complexity | Notes |
|---|---|---|---|
| JS scripting: script items with file watching/hot-reload, `script.update(dt)`, per-script custom params, whole-tree JS API (`root.sequence.energy.set(...)`) | `Script`, `ScriptTarget`, `ScriptManager`, `ScriptUtil` | **High** | Embed QuickJS or Duktape; auto-generate bindings from `Container`/`Parameter` reflection (addresses make this systematic); sandboxing + error routing to Logger |
| OSC input/output (address ↔ parameter, feedback) | `OSCRemoteControl` (input part) | **Medium** | OSC 1.0 encode/decode is ~a day (or `oscpack`); UDP socket; map via `controlAddress()`; settings UI |
| OSCQuery server (HTTP JSON tree + WebSocket LISTEN/feedback) | `OSCRemoteControl` + `SimpleWeb` | **High** | Needs an HTTP+WS server dep (`mongoose`, `uWebSockets`); tree JSON already derivable from the model |
| Zeroconf advertising (`_osc._udp`, `_oscjson._tcp`) | Servus | **Medium** | Platform mDNS pain (avahi/bonjour), or vendor a tiny mDNS responder |
| Wake-on-LAN, crypto helpers (HMAC/SHA) | `helpers/` | **Low** | Only if needed |

## 10. Dashboard (remote control surfaces)

| Missing feature | organicui reference | Complexity | Notes |
|---|---|---|---|
| In-app dashboard editor: user-composed pages of widgets bound to parameters (custom colors/images/styles), groups, links between dashboards, edit mode with snapping | `DashboardManager`, `Dashboard*Item` | **Medium–High** (canvas now exists) | Specialize `ManagerCanvasUI` items that bind to parameter addresses (`DrawParamWidget` in `canvasGui()`); TargetParameter would polish the binding UX |
| Serving dashboards to web browsers (websocket sync both ways, static web app, password lock) | dashboard server mode | **Very High** | Full web front-end + WS protocol; only worth it if remote control is a product goal |
| Shared texture (Spout/Syphon) dashboard item | `SharedTextureDashboardItem` | **High** | Platform GPU interop |

## 11. App shell & misc

| Missing feature | organicui reference | Complexity | Notes |
|---|---|---|---|
| Rebindable keymappings editor (all shortcuts as settings) | `KeyMappingsContainer` | **Medium** | Central shortcut registry (action id → chord) consulted everywhere + capture-style editor UI + persistence |
| Global settings: font size / UI scale, tooltip toggle, refresh throttling, always-on-top | `GlobalSettings` | **Low–Medium** | UI scale = font rebuild + `style.ScaleAllSizes` |
| Auto-updater (check JSON endpoint, changelog dialog, download & install) | `AppUpdater` | **Medium–High** | HTTP dep + per-platform install/relaunch; simplest useful version: "new version available" notice (**Low**) |
| Crash handler (signal/SEH handlers, dump + last state, restart/recover actions) | `CrashHandler` | **Low–Medium** (backtrace + autosave on signal) / **Medium** (minidumps, upload) | |
| Progress task tree + modal progress window | `ProgressTask`, `ProgressWindow` | **Low–Medium** | Needed by async loading & updater |
| Contextual help (per-widget helpID → downloadable localized help text in a panel) | `HelpBox`, `HelpPanel` | **Low–Medium** | Hover registry + JSON file; we already show tooltips with descriptions |
| System tray icon, close-to-tray | `OrganicApplication` | **Medium** | Not covered by GLFW; needs per-platform code or a tray lib |
| Icon assets / image buttons | `AssetManager` | **Low** | Bake an icon font (e.g. ForkAwesome) instead of PNGs |

## 12. Not applicable in an ImGui port (by design)

These organicui mechanisms exist to solve JUCE/retained-mode problems that
immediate-mode UI doesn't have — porting them would add nothing:

- **`QueuedNotifier` / async event queues, `*.AsyncListener`** — panels poll the
  model every frame; revision counters already cover "something changed".
- **`UITimers` / shared repaint clocks, `shouldRepaint` flags** — everything
  redraws each frame.
- **`LookAndFeelOO`** (100 KB custom widget painting) — `ImGuiStyle` + a few
  custom draws replace it.
- **`WeakReference` lifetime idiom** — ownership is `unique_ptr` + id lookups;
  undo uses id/JSON resolution instead of weak pointers.
- **`GapGrabber`, `ShapeShifterWindow`, dock drop-zone painting** — native to
  ImGui docking.
- **BinaryData resource compiler** — assets are files/fonts.
- **OpenGL context attachment plumbing** — the backend owns the GL context.

---

## Suggested roadmap (updated after the parity pass)

**Done in the latest pass:** the whole Manager/BaseItem framework (list + 2D
canvas), Inspector/Selection extras (multi-edit, preselection, links, scopes),
all Automation/curves items (recorder, simplification, transform box, Curve2D),
all Timeline items (triggers, cues, loop range, ping-pong, BPM grid, audio
playback, fades, media looping, split, magnet, multi-sequence, copy/paste) and
the full Detective. A basic parameter picker popup (`ParamPickerPopup`) now
exists and serves the recorder & Detective.

**Quick wins (each ≤ 1 day, high value):**
Point3D & XY pad · dirty flag + save prompt + recent files ·
save-only-overridden values · enum option editing.

**Medium projects (unlock a lot):**
1. **TargetParameter + picker widget** → enables REFERENCE mode, Parrot,
   Dashboard bindings (the picker popup already exists; wrap it as a type).
2. **OSC in/out** → remote control with ~1 small dependency (the address
   resolution layer is already in place).
3. **Parrot** (multi-parameter record/replay; the Detective + recorder
   infrastructure covers most of it).
4. **Keymappings registry + editor.**
5. **Warnings system.**

**Big rocks (plan deliberately):**
JS scripting (QuickJS) · OSCQuery/WebSocket server · web dashboards
(in-app Dashboard editor is now mostly a Board specialization with parameter
bindings) · auto-updater/crash pipeline.
