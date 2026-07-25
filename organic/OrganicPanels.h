// OrganicPanels.h - standard dockable panels: Inspector (with multi-editing),
// Outliner, Logger, Media Pool, Scope (automation outputs) and Detective
// (watch ANY parameter over time, organicui's Detective).
#pragma once

#include "OrganicCore.h"
#include "OrganicTimeline.h"
#include "OrganicManager.h"

namespace organic
{

// Commit pending (coalesced) parameter edits into the undo stack.
// Call once per frame, after all UI has been drawn.
void CommitPendingParamEdits();

void InspectorPanel(bool* open = nullptr);
void OutlinerPanel(bool* open = nullptr);
void LoggerPanel(bool* open = nullptr);
void MediaPoolPanel(MediaPool& pool, bool* open = nullptr);

// ---------------------------------------------------------------- Scope
// rolling plot of every automation layer output of a sequence (zero config)
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

// ---------------------------------------------------------------- Detective
// watch arbitrary parameters (added from any parameter's right-click menu)
class DetectiveWatcher : public BaseItem
{
public:
    DetectiveWatcher();

    Parameter* addressP = nullptr; // watched parameter address
    Parameter* windowP  = nullptr; // time window in seconds

    std::vector<float> times, values; // rolling buffer (not serialized)

    void sample(double now);
    std::string inspectableTypeName() const override { return "Watcher"; }
};

class Detective : public BaseManager
{
public:
    Detective();
    static Detective* main; // set by the app; enables "Watch in Detective"

    void update(double now); // call once per frame
    DetectiveWatcher* watch(const std::string& address); // undoable add
};

void DetectivePanel(Detective& d, bool* open = nullptr);

} // namespace organic
