// OrganicPanels.h - standard dockable panels: Inspector, Outliner, Logger,
// Media Pool and Scope (value oscilloscope, organicui's "Detective").
#pragma once

#include "OrganicCore.h"
#include "OrganicTimeline.h"

namespace organic
{

// Commit pending (coalesced) parameter edits into the undo stack.
// Call once per frame, after all UI has been drawn.
void CommitPendingParamEdits();

void InspectorPanel(bool* open = nullptr);
void OutlinerPanel(bool* open = nullptr);
void LoggerPanel(bool* open = nullptr);
void MediaPoolPanel(MediaPool& pool, bool* open = nullptr);

// Scope: rolling plot of every automation layer output (uses ImPlot)
struct ScopeBuffers
{
    struct Channel
    {
        uint64_t layerId = 0;
        std::string name;
        ImVec4 color;
        std::vector<float> values;
    };
    std::vector<Channel> channels;
    std::vector<float>   times;
    int    maxPoints = 900;
    double timeWindow = 10.0;

    void push(Sequence& seq, double now); // call once per frame (app update)
};

void ScopePanel(ScopeBuffers& buffers, bool* open = nullptr);

} // namespace organic
