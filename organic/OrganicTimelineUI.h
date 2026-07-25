// OrganicTimelineUI.h - the timeline editor window.
// Draws a Sequence: time ruler, layer headers, clip lanes with drag & drop and
// resizing, automation curve editing with easings, gradient tracks, audio
// waveforms, rubber-band selection, snapping, zoom & pan, playhead scrubbing.
#pragma once

#include "OrganicTimeline.h"
#include <map>

namespace organic
{

class TimelineUI
{
public:
    TimelineUI();

    // Draw the timeline window for a sequence. Call every frame.
    void gui(Sequence& seq, bool* open = nullptr, const char* windowName = "Timeline");

    // options
    bool  snapEnabled  = true;
    int   snapChoice   = 0;      // 0 = adaptive, then fixed steps
    bool  followPlayhead = true;

private:
    // interaction state
    enum class Drag
    {
        None, Scrub, Rubber, MoveClips, ResizeL, ResizeR,
        AutoKey, BezierA1, BezierA2, GradKey, LayerHeight, HScroll, PanH
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

    void toolbar(Sequence& seq);
    double snapStep(double pps) const;
    double snapTime(const Sequence& seq, double t, bool bypass) const;
};

} // namespace organic
