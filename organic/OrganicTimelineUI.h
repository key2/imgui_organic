// OrganicTimelineUI.h - the timeline editor window.
// Draws a Sequence: time ruler (time or musical grid), cues, loop range, layer
// headers (drag-reorder), clip lanes with drag & drop / resizing / fades /
// media looping / splitting, automation curve editing with easings + recorder
// + multi-key transform box, gradient tracks, trigger layers, audio waveforms,
// rubber-band (pre)selection, grid & magnet snapping, zoom & pan, playhead
// scrubbing, clipboard copy/paste - all undoable.
#pragma once

#include "OrganicTimeline.h"
#include <functional>
#include <map>

namespace organic
{

class TimelineUI
{
public:
    TimelineUI();

    // Draw the timeline editor for a sequence INSIDE the current window
    // (window management is up to the caller; see gui()).
    void body(Sequence& seq);

    // Convenience: Begin/End a window and draw body().
    void gui(Sequence& seq, bool* open = nullptr, const char* windowName = "Timeline");

    // Host hook: invoked right after each transport button item ("play",
    // "stop") so the embedding app can attach its own context menu /
    // extra affordances to the button (e.g. copy a remote-control
    // address). The last ImGui item is the button when this runs.
    std::function<void(const char* transportControl)> transportItemContextMenu;

    // Host hook: the "Remove Automation" context item of an embedded
    // automation row delegates here when set. Hosts that DERIVE the rows
    // from external state (a node graph flag, a parameter registry...)
    // clear the owning flag instead — otherwise their reconciliation would
    // just recreate the row next frame. Return true when handled; false
    // falls back to the built-in undoable local removal.
    std::function<bool(Clip&, ClipAutomation&)> removeAutomationHook;

    // options
    bool  snapEnabled  = true;
    int   snapChoice   = 0;      // 0 = adaptive, then fixed steps
    bool  magnetEnabled = true;  // snap to clip edges / keys / cues / playhead
    bool  followPlayhead = true;
    int   gridMode = 0;          // 0 = time, 1 = beats
    int   beatDivision = 1;      // subdivision index (1, 1/2, 1/4)
    // Pencil / Draw: ALWAYS ON by default — a click on empty curve space
    // places a key, a drag draws the curve freehand (keys generated from
    // the stroke, RDP-simplified). Existing keys/handles still hit-test
    // first, so editing them is unaffected. Hosts wanting the classic
    // double-click-only editing can turn it off.
    bool  pencilMode = true;
    // Which layer types the "+ Layer" button and the "Add Layer" context
    // menus offer. Hosts that embed automations INSIDE the clips (Timeline
    // v2) hide the standalone Automation/Gradient lanes — the rows in the
    // blocks replace them. Existing layers of a hidden type still render
    // and edit normally.
    bool  offerClipLayers = true;
    bool  offerAutomationLayers = true;
    bool  offerGradientLayers = true;
    bool  offerTriggerLayers = true;
    // Layer renaming (double-click the header / context "Rename Layer").
    // Hosts that auto-name their layers turn it off.
    bool  offerLayerRename = true;
    // STICKY layer: this layer renders PINNED at the top of the lanes area
    // (right under the ruler) and never scrolls out of view — made for the
    // audio/waveform lane the user aligns everything against. It leaves
    // the normal flow (others scroll beneath), cannot be reordered, and
    // refuses non-audio drops. 0 = none.
    uint64_t stickyLayerId = 0;

    // Host hook: a Block clip was double-clicked — open/edit the thing it
    // references (lightshow: the effect graph in the Node Graph panel).
    std::function<void(Clip&)> clipDoubleClicked;

    // Zoom cluster (right-aligned on the toolbar row): zoom out / 1:1 /
    // zoom in / fit content (F and Ctrl+wheel still work). Hosts with an
    // icon font override the labels (e.g. Phosphor glyphs).
    const char* zoomOutLabel = "-";
    const char* zoomOneLabel = "1:1";
    const char* zoomInLabel  = "+";
    const char* zoomFitLabel = "Fit";

private:
    // interaction state
    enum class Drag
    {
        None, Scrub, Rubber, MoveClips, ResizeL, ResizeR,
        AutoKey, BezierA1, BezierA2, EaseHandle, GradKey, TriggerKey,
        LayerHeight, LayerReorder, HScroll, PanH,
        Cue, LoopIn, LoopOut, FadeIn, FadeOut,
        KeyBoxMove, KeyBoxL, KeyBoxR, KeyBoxT, KeyBoxB,
        // embedded clip-automation rows (Timeline v2)
        CAKey, CABez1, CABez2, CAEase, CAGKey,
        PencilLane, PencilClip
    };

    struct ClipRef { uint64_t clip = 0, layer = 0; double start = 0; };

    Drag     drag = Drag::None;
    uint64_t dragLayerId = 0, dragItemId = 0;
    ImVec2   dragStartMouse;
    double   dragGrabDT = 0;
    double   dragOrigA = 0, dragOrigB = 0, dragOrigC = 0;
    float    dragOrigF = 0;
    std::vector<ClipRef> dragClips;
    json     preEditJson;
    bool     dragMoved = false;
    int      reorderTarget = -1;

    // key transform box (per automation layer with >= 2 selected keys)
    struct KeyBoxRef { uint64_t id; double t; float v; };
    std::vector<KeyBoxRef> keyBoxRefs;
    double keyBoxT0 = 0, keyBoxT1 = 0;
    float  keyBoxV0 = 0, keyBoxV1 = 0;

    // rubber band
    ImVec2 rubberStart;
    bool   rubberAdd = false;
    std::vector<Inspectable*> rubberBaseSel;
    std::map<uint64_t, std::set<uint64_t>> rubberBaseKeys;

    // context menu / rename targets
    uint64_t ctxLayerId = 0, ctxItemId = 0;
    uint64_t ctxClipId = 0, ctxAutoId = 0; // embedded automation targets
    int ctxAaRow = -1, ctxAaSection = -1;  // audio structure row targets
    double   ctxTime = 0;
    float    ctxValue = 0;
    char     renameBuf[128] = {};
    bool     wantRenamePopup = false;
    bool     gkeyWasOpen = false;
    bool     cagkeyWasOpen = false;
    bool     fitRequested = false;
    int      zoomRequest = 0;          // +1 / -1 steps from the toolbar
    bool     zoomOneRequested = false; // reset to the 1:1 scale (100 px/s)
    float    insertTimeBuf = 2.f;

    // embedded automation drag state
    uint64_t dragClipId = 0, dragAutoId = 0;
    json     dragClipPre;                              // full-clip undo snapshot
    std::vector<std::pair<double, float>> pencilPts;   // pencil stroke (time, value)
    bool     dragBelowLanes = false; // clip drag hovers below the last lane
                                     // → release creates a new layer there

    void   toolbar(Sequence& seq);
    double snapStep(const Sequence& seq, double pps) const;
    double snapTime(const Sequence& seq, double t, bool bypass) const;
    double magnetTime(const Sequence& seq, double t, double pps,
                      const std::vector<uint64_t>& ignoreClips, bool& snapped) const;
};

} // namespace organic
