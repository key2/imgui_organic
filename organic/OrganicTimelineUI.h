// OrganicTimelineUI.h - the timeline editor window.
// Draws a Sequence: time ruler (time or musical grid), cues, loop range, layer
// headers (drag-reorder), clip lanes with drag & drop / resizing / fades /
// media looping / splitting, automation curve editing with easings + recorder
// + multi-key transform box, gradient tracks, trigger layers, audio waveforms,
// rubber-band (pre)selection, grid & magnet snapping, zoom & pan, playhead
// scrubbing, clipboard copy/paste - all undoable.
#pragma once

#include "OrganicTimeline.h"
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

    // options
    bool  snapEnabled  = true;
    int   snapChoice   = 0;      // 0 = adaptive, then fixed steps
    bool  magnetEnabled = true;  // snap to clip edges / keys / cues / playhead
    bool  followPlayhead = true;
    int   gridMode = 0;          // 0 = time, 1 = beats
    int   beatDivision = 1;      // subdivision index (1, 1/2, 1/4)

private:
    // interaction state
    enum class Drag
    {
        None, Scrub, Rubber, MoveClips, ResizeL, ResizeR,
        AutoKey, BezierA1, BezierA2, EaseHandle, GradKey, TriggerKey,
        LayerHeight, LayerReorder, HScroll, PanH,
        Cue, LoopIn, LoopOut, FadeIn, FadeOut,
        KeyBoxMove, KeyBoxL, KeyBoxR, KeyBoxT, KeyBoxB
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
    double   ctxTime = 0;
    float    ctxValue = 0;
    char     renameBuf[128] = {};
    bool     wantRenamePopup = false;
    bool     gkeyWasOpen = false;
    bool     fitRequested = false;
    float    insertTimeBuf = 2.f;

    void   toolbar(Sequence& seq);
    double snapStep(const Sequence& seq, double pps) const;
    double snapTime(const Sequence& seq, double t, bool bypass) const;
    double magnetTime(const Sequence& seq, double t, double pps,
                      const std::vector<uint64_t>& ignoreClips, bool& snapped) const;
};

} // namespace organic
