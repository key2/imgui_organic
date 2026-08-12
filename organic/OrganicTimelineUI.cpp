#include "OrganicTimelineUI.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace organic
{

// ---------------------------------------------------------------- constants
static const float HEADER_W  = 170.f;
static const float RULER_H   = 34.f;   // cue strip + loop strip + ticks
static const float HSCROLL_H = 14.f;
static const float LANE_GAP  = 2.f;
static const float VAL_MARGIN = 5.f;

static const double RULER_STEPS[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5,
                                      1, 2, 5, 10, 15, 30, 60, 120, 300, 600 };

// ---------------------------------------------------------------- small helpers
static ImU32 col32(const ImVec4& c, float aMul = 1.f)
{
    ImVec4 cc = c; cc.w *= aMul;
    return ImGui::ColorConvertFloat4ToU32(cc);
}

static ImVec4 lighten(const ImVec4& c, float f)
{
    return ImVec4(std::min(1.f, c.x + f), std::min(1.f, c.y + f), std::min(1.f, c.z + f), c.w);
}

static bool inRect(const ImVec2& p, const ImVec2& mn, const ImVec2& mx)
{
    return p.x >= mn.x && p.x < mx.x && p.y >= mn.y && p.y < mx.y;
}

static float dist2(const ImVec2& a, const ImVec2& b)
{
    float dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

static void drawDiamond(ImDrawList* dl, ImVec2 c, float r, ImU32 fill, ImU32 border)
{
    ImVec2 pts[4] = { ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y) };
    dl->AddConvexPolyFilled(pts, 4, fill);
    dl->AddPolyline(pts, 4, border, ImDrawFlags_Closed, 1.f);
}

struct LaneGeom
{
    Layer* layer = nullptr;
    int    index = -1;
    float  y0 = 0, y1 = 0;
};

struct Hit
{
    enum Kind
    {
        None, Corner, Header, HeaderGrip, Lane, ClipBody, ClipL, ClipR,
        AKey, BezA1, BezA2, EaseHandle, GKey, TKey, FadeIn, FadeOut,
        KeyBoxL, KeyBoxR, KeyBoxT, KeyBoxB, KeyBoxMove,
        // embedded clip-automation rows
        CAHeader, CAArm, CAKey, CABezA1, CABezA2, CAEase, CAGKey,
        CACurve, CAGrad
    };
    Kind   kind = None;
    Layer* layer = nullptr;
    int    laneIdx = -1;
    Clip*  clip = nullptr;
    ClipAutomation* cauto = nullptr;
    uint64_t keyId = 0;
};

// ---------------------------------------------------------------- embedded automation rows
// Layout of the rows INSIDE an effect block (Timeline v2): a compact header
// per automation (expand triangle + name + live chip) and, when expanded,
// the editor body (curve or gradient). All heights ride the row's smooth
// open/close animation.
static const float CA_TITLE_H = 19.f; // block title strip
static const float CA_HEAD_H  = 16.f; // automation row header (collapsed height)
static const float CA_CURVE_H = 52.f; // expanded curve editor
static const float CA_GRAD_H  = 22.f; // expanded gradient editor

static float caBodyH(const ClipAutomation& a)
{
    float target = a.akind == ClipAutomation::AKind::Gradient ? CA_GRAD_H : CA_CURVE_H;
    float anim = a.uiAnim < 0 ? (a.expanded ? 1.f : 0.f) : a.uiAnim;
    return anim * target;
}

static float caRowsHeight(const Clip& c)
{
    float h = 0;
    for (auto& a : c.automations) h += CA_HEAD_H + caBodyH(*a) + 1.f;
    return h;
}

// content height a Block clip wants (title + rows); clips without
// automations keep the classic full-lane look
static float clipDesiredH(const Clip& c)
{
    if (c.ctype != Clip::CType::Block || c.automations.empty()) return 0;
    return CA_TITLE_H + caRowsHeight(c) + 4.f;
}

struct AutoRowGeom
{
    ClipAutomation* a = nullptr;
    float hy0 = 0, hy1 = 0; // header strip
    float by0 = 0, by1 = 0; // editor body (by1 == by0 when collapsed)
};

// rows laid out under the block's title strip; blockY0 = clip rect top
static void buildAutoRows(Clip& c, float blockY0, std::vector<AutoRowGeom>& out)
{
    out.clear();
    float y = blockY0 + CA_TITLE_H;
    for (auto& a : c.automations)
    {
        AutoRowGeom g;
        g.a = a.get();
        g.hy0 = y;
        g.hy1 = y + CA_HEAD_H;
        g.by0 = g.hy1;
        g.by1 = g.hy1 + caBodyH(*a);
        out.push_back(g);
        y = g.by1 + 1.f;
    }
}

static float caRowNormToY(const AutoRowGeom& rg, float norm)
{
    float h = (rg.by1 - rg.by0) - 8.f;
    return rg.by1 - 4.f - norm * std::max(0.f, h);
}
static float caRowYToNorm(const AutoRowGeom& rg, float y)
{
    float h = (rg.by1 - rg.by0) - 8.f;
    if (h <= 0) return 0;
    float n = (rg.by1 - 4.f - y) / h;
    return std::max(0.f, std::min(1.f, n));
}

static float normToY(const LaneGeom& g, float norm)
{
    float h = (g.y1 - g.y0) - 2 * VAL_MARGIN;
    return g.y1 - VAL_MARGIN - norm * h;
}
static float yToNorm(const LaneGeom& g, float y)
{
    float h = (g.y1 - g.y0) - 2 * VAL_MARGIN;
    if (h <= 0) return 0;
    float n = (g.y1 - VAL_MARGIN - y) / h;
    return std::max(-1.f, std::min(2.f, n));
}

static bool easingHasHandle(EasingType e)
{
    return e == EasingType::Sine || e == EasingType::Elastic ||
           e == EasingType::Noise || e == EasingType::Perlin || e == EasingType::Steps;
}

// ---------------------------------------------------------------- structural undo helpers
static void physMoveClip(Sequence& seq, uint64_t clipId, uint64_t dstLayerId)
{
    ClipLayer* src = nullptr;
    Clip* clip = seq.findClip(clipId, &src);
    ClipLayer* dst = dynamic_cast<ClipLayer*>(seq.findLayer(dstLayerId));
    if (!clip || !src || !dst || src == dst) return;

    std::unique_ptr<Clip> holder;
    for (size_t i = 0; i < src->clips.size(); i++)
    {
        if (src->clips[i]->id == clipId)
        {
            holder = std::move(src->clips[i]);
            src->clips.erase(src->clips.begin() + i);
            break;
        }
    }
    if (!holder) return;
    src->removeChild(holder.get());
    dst->addChild(holder.get());
    holder->layer = dst;
    dst->clips.push_back(std::move(holder));
    dst->sortClips();
}

static void pushClipAdded(Sequence* seq, uint64_t layerId, const json& clipData, const std::string& name)
{
    uint64_t clipId = clipData.value("id", (uint64_t)0);
    UndoManager::get().pushDone(name,
        [seq, layerId, clipData]
        {
            if (auto* cl = dynamic_cast<ClipLayer*>(seq->findLayer(layerId)))
                cl->addClipFromJson(clipData);
        },
        [seq, layerId, clipId]
        {
            if (auto* cl = dynamic_cast<ClipLayer*>(seq->findLayer(layerId)))
                cl->removeClip(clipId);
        },
        { seq });
}

// key edits: works for automation / gradient / trigger layers
static void pushKeysEdit(Sequence* seq, uint64_t layerId, const json& pre, const json& post, const std::string& name)
{
    if (pre == post) return;
    auto apply = [seq, layerId](const json& data)
    {
        Layer* l = seq->findLayer(layerId);
        if (auto* al = dynamic_cast<AutomationLayer*>(l)) al->keysFromJson(data);
        else if (auto* gl = dynamic_cast<GradientLayer*>(l)) gl->keysFromJson(data);
        else if (auto* tl = dynamic_cast<TriggerLayer*>(l)) tl->keysFromJson(data);
    };
    UndoManager::get().pushDone(name,
        [apply, post] { apply(post); },
        [apply, pre]  { apply(pre); },
        { seq });
}

static json layerKeysJson(Layer* l)
{
    if (auto* al = dynamic_cast<AutomationLayer*>(l)) return al->keysToJson();
    if (auto* gl = dynamic_cast<GradientLayer*>(l)) return gl->keysToJson();
    if (auto* tl = dynamic_cast<TriggerLayer*>(l)) return tl->keysToJson();
    return json();
}

// key edits inside an embedded clip automation
static void pushClipAutoKeysEdit(Sequence* seq, uint64_t clipId, uint64_t autoId,
                                 const json& pre, const json& post, const std::string& name)
{
    if (pre == post) return;
    auto apply = [seq, clipId, autoId](const json& data)
    {
        if (Clip* c = seq->findClip(clipId))
            if (ClipAutomation* a = c->findAutomation(autoId))
                a->keysFromJson(data);
    };
    UndoManager::get().pushDone(name,
        [apply, post] { apply(post); },
        [apply, pre]  { apply(pre); },
        { seq });
}

// whole-clip edits (resize gestures that also trim embedded automations):
// restore the full clip json in place — identity (pointer/id) is preserved
static void pushClipEdit(Sequence* seq, uint64_t clipId,
                         const json& pre, const json& post, const std::string& name)
{
    if (pre == post) return;
    auto apply = [seq, clipId](const json& data)
    {
        if (Clip* c = seq->findClip(clipId)) c->load(data);
    };
    UndoManager::get().pushDone(name,
        [apply, post] { apply(post); },
        [apply, pre]  { apply(pre); },
        { seq });
}

static void pushCuesEdit(Sequence* seq, const json& pre, const std::string& name)
{
    json post = seq->cuesToJson();
    if (post == pre) return;
    UndoManager::get().pushDone(name,
        [seq, post] { seq->cuesFromJson(post); },
        [seq, pre]  { seq->cuesFromJson(pre); },
        { seq });
}

static void pushContentEdit(Sequence* seq, const json& pre, const std::string& name)
{
    json post = seq->contentToJson();
    if (post == pre) return;
    UndoManager::get().pushDone(name,
        [seq, post] { seq->contentFromJson(post); },
        [seq, pre]  { seq->contentFromJson(pre); },
        { seq });
}

// ---------------------------------------------------------------- TimelineUI
TimelineUI::TimelineUI() {}

double TimelineUI::snapStep(const Sequence& seq, double pps) const
{
    if (gridMode == 1) // beats
    {
        double beatDur = 60.0 / std::max(20.f, seq.bpmP->floatValue());
        static const int subs[] = { 1, 2, 4 };
        int sub = subs[std::max(0, std::min(2, beatDivision))];
        double step = beatDur / sub;
        while (step * pps < 5.0) step *= 2.0;
        return step;
    }
    if (snapChoice > 0)
    {
        static const double fixed[] = { 1.0, 0.5, 0.25, 0.1, 0.05, 0.01 };
        int i = snapChoice - 1;
        if (i >= 0 && i < 6) return fixed[i];
    }
    double major = RULER_STEPS[IM_ARRAYSIZE(RULER_STEPS) - 1];
    for (double s : RULER_STEPS)
        if (s * pps >= 80.0) { major = s; break; }
    return major / 5.0;
}

double TimelineUI::snapTime(const Sequence& seq, double t, bool bypass) const
{
    if (!snapEnabled || bypass) return std::max(0.0, t);
    double s = snapStep(seq, seq.pixelsPerSecond);
    return std::max(0.0, std::round(t / s) * s);
}

double TimelineUI::magnetTime(const Sequence& seq, double t, double pps,
                              const std::vector<uint64_t>& ignoreClips, bool& snapped) const
{
    snapped = false;
    if (!magnetEnabled) return t;
    double best = t;
    double bestD = 8.0 / pps; // 8 px capture radius
    auto tryCand = [&](double c)
    {
        double d = std::fabs(t - c);
        if (d < bestD) { bestD = d; best = c; snapped = true; }
    };
    for (auto& l : seq.layers)
    {
        if (auto* cl = dynamic_cast<ClipLayer*>(l.get()))
        {
            for (auto& c : cl->clips)
            {
                bool ignored = false;
                for (uint64_t ig : ignoreClips) if (ig == c->id) ignored = true;
                if (ignored) continue;
                tryCand(c->start());
                tryCand(c->end());
            }
        }
        else if (auto* tl = dynamic_cast<TriggerLayer*>(l.get()))
            for (auto& tr : tl->triggers) tryCand(tr.time);
    }
    for (auto& c : seq.cues) tryCand(c.time);
    if (seq.loopIn >= 0) { tryCand(seq.loopIn); tryCand(seq.loopOut); }
    tryCand(seq.currentTime);
    tryCand(0.0);
    tryCand(seq.totalTime());
    return best;
}

void TimelineUI::toolbar(Sequence& seq)
{
    ImVec4 accent = ImVec4(1.f, 0.573f, 0.184f, 1.f);

    if (ImGui::Button("|<")) seq.setTime(seq.loopIn >= 0 ? seq.loopIn : 0);
    ImGui::SetItemTooltip("Go to start (Home)");
    ImGui::SameLine();

    bool playing = seq.playing;
    if (playing) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.45f, 0.22f, 1.f));
    if (ImGui::Button(playing ? "Pause" : "Play ")) seq.togglePlay();
    if (playing) ImGui::PopStyleColor();
    ImGui::SetItemTooltip("Play / Pause (Space)");
    if (transportItemContextMenu) transportItemContextMenu("play");
    ImGui::SameLine();

    if (ImGui::Button("Stop")) seq.stop();
    if (transportItemContextMenu) transportItemContextMenu("stop");
    ImGui::SameLine();

    ImGui::SetNextItemWidth(86);
    int pm = seq.playModeP->intValue();
    const char* pmNames[] = { "Once", "Loop", "Ping-Pong" };
    if (ImGui::Combo("##playmode", &pm, pmNames, 3)) seq.playModeP->setUndoable(pm);
    ImGui::SetItemTooltip("Play mode (applies to the loop range when set)");
    ImGui::SameLine();

    // recording indicator
    bool anyArmed = false, anyRecording = false;
    for (auto& l : seq.layers)
        if (auto* al = dynamic_cast<AutomationLayer*>(l.get()))
        {
            anyArmed |= al->recArmP->boolValue();
            anyRecording |= al->recording;
        }
    if (anyArmed || anyRecording)
    {
        float blink = anyRecording ? (0.6f + 0.4f * std::sin((float)ImGui::GetTime() * 8.f)) : 0.8f;
        ImGui::TextColored(ImVec4(1.f, 0.25f * blink, 0.25f * blink, 1.f),
                           anyRecording ? "REC" : "ARM");
        ImGui::SameLine();
    }

    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::Text("%s", formatTime(seq.currentTime).c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("/ %s", formatTime(seq.totalTime()).c_str());

    ImGui::SameLine(0, 14);
    ImGui::Checkbox("Snap", &snapEnabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(84);
    if (gridMode == 0)
    {
        const char* snapNames[] = { "Adaptive", "1 s", "1/2 s", "1/4 s", "1/10 s", "1/20 s", "1/100 s" };
        ImGui::Combo("##snapstep", &snapChoice, snapNames, IM_ARRAYSIZE(snapNames));
        ImGui::SetItemTooltip("Snap grid (hold Alt to bypass while dragging)");
    }
    else
    {
        const char* divNames[] = { "1 beat", "1/2 beat", "1/4 beat" };
        ImGui::Combo("##beatdiv", &beatDivision, divNames, IM_ARRAYSIZE(divNames));
        ImGui::SetItemTooltip("Beat subdivision for the grid");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(66);
    const char* gridNames[] = { "Time", "Beats" };
    ImGui::Combo("##gridmode", &gridMode, gridNames, 2);
    ImGui::SetItemTooltip("Ruler & grid mode (Beats uses the sequence BPM)");
    ImGui::SameLine();
    ImGui::Checkbox("Magnet", &magnetEnabled);
    ImGui::SetItemTooltip("Snap to clip edges, keys, cues, loop points and the playhead");
    ImGui::SameLine();
    ImGui::Checkbox("Follow", &followPlayhead);

    ImGui::SameLine(0, 14);
    if (ImGui::Button("+ Layer")) ImGui::OpenPopup("add_layer_toolbar");
    if (ImGui::BeginPopup("add_layer_toolbar"))
    {
        Sequence* sp = &seq;
        auto addL = [sp](Layer::LType t, const char* n)
        {
            Layer* l = sp->addLayer(t, n);
            uint64_t lid = l->id;
            json data = l->save();
            data["_index"] = sp->layerIndex(l);
            UndoManager::get().pushDone("Add Layer",
                [sp, data] { sp->addLayerFromJson(data, data.value("_index", -1)); },
                [sp, lid]  { sp->removeLayer(lid); },
                { sp });
            l->select();
        };
        if (offerClipLayers && ImGui::MenuItem("Clip Layer"))
            addL(Layer::LType::Clips, "Clips");
        if (offerAutomationLayers && ImGui::MenuItem("Automation Layer"))
            addL(Layer::LType::Automation, "Automation");
        if (offerGradientLayers && ImGui::MenuItem("Gradient Layer"))
            addL(Layer::LType::Gradient, "Gradient");
        if (offerTriggerLayers && ImGui::MenuItem("Trigger Layer"))
            addL(Layer::LType::Triggers, "Triggers");
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Sequence")) seq.select();
    ImGui::SetItemTooltip("Edit sequence settings in the Inspector");

    // zoom cluster, right-aligned: zoom out / 1:1 / zoom in / fit content
    // (F and Ctrl+wheel still work)
    {
        const ImGuiStyle& st = ImGui::GetStyle();
        auto bw = [&](const char* s)
        { return ImGui::CalcTextSize(s).x + st.FramePadding.x * 2; };
        const float cluster = bw(zoomOutLabel) + bw(zoomOneLabel) + bw(zoomInLabel) +
                              bw(zoomFitLabel) + st.ItemSpacing.x * 3;
        ImGui::SameLine();
        const float rightX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        if (rightX - cluster > ImGui::GetCursorPosX())
            ImGui::SetCursorPosX(rightX - cluster);
        if (ImGui::Button(zoomOutLabel)) zoomRequest--;
        ImGui::SetItemTooltip("Zoom out (Ctrl+wheel)");
        ImGui::SameLine();
        if (ImGui::Button(zoomOneLabel)) zoomOneRequested = true;
        ImGui::SetItemTooltip("Zoom 1:1 (default scale)");
        ImGui::SameLine();
        if (ImGui::Button(zoomInLabel)) zoomRequest++;
        ImGui::SetItemTooltip("Zoom in (Ctrl+wheel)");
        ImGui::SameLine();
        if (ImGui::Button(zoomFitLabel)) fitRequested = true;
        ImGui::SetItemTooltip("Fit content in view (F)");
    }
}

void TimelineUI::gui(Sequence& seq, bool* open, const char* windowName)
{
    ImGui::SetNextWindowSize(ImVec2(1000, 460), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(windowName, open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::End();
        return;
    }
    body(seq);
    ImGui::End();
}

void TimelineUI::body(Sequence& seq)
{
    ImGui::PushID(&seq);
    ImGuiIO& io = ImGui::GetIO();
    Sequence* sp = &seq;

    toolbar(seq);

    double& viewStart = seq.viewStart;
    double& pps       = seq.pixelsPerSecond;
    pps = std::max(2.0, std::min(4000.0, pps));

    ImVec2 avail  = ImGui::GetContentRegionAvail();
    if (avail.x < 80 || avail.y < 70) { ImGui::PopID(); return; }
    ImVec2 origin = ImGui::GetCursorScreenPos();

    float laneX0 = origin.x + HEADER_W;
    float laneW  = avail.x - HEADER_W;
    auto timeToX = [&](double t) { return laneX0 + (float)((t - viewStart) * pps); };
    auto xToTime = [&](float x)  { return viewStart + (x - laneX0) / pps; };

    std::vector<double> magnetGuides; // vertical guide lines to draw this frame

    // content extent (for fit & scrollbar)
    double contentEnd = seq.totalTime();
    for (auto& l : seq.layers)
    {
        if (auto* cl = dynamic_cast<ClipLayer*>(l.get()))
            for (auto& c : cl->clips) contentEnd = std::max(contentEnd, c->end());
        else if (auto* al = dynamic_cast<AutomationLayer*>(l.get()))
            { if (!al->keys.empty()) contentEnd = std::max(contentEnd, al->keys.back().time); }
        else if (auto* gl = dynamic_cast<GradientLayer*>(l.get()))
            { if (!gl->keys.empty()) contentEnd = std::max(contentEnd, gl->keys.back().time); }
        else if (auto* tl = dynamic_cast<TriggerLayer*>(l.get()))
            { if (!tl->triggers.empty()) contentEnd = std::max(contentEnd, tl->triggers.back().time); }
    }

    if (fitRequested)
    {
        fitRequested = false;
        viewStart = 0;
        pps = std::max(2.0, std::min(4000.0, laneW / std::max(1.0, contentEnd * 1.02)));
    }
    if (zoomRequest != 0 || zoomOneRequested)
    {
        // toolbar zoom: keep the CENTER of the view anchored so the
        // material under the eye stays put while the scale changes
        const double center = viewStart + laneW * 0.5 / pps;
        if (zoomOneRequested) pps = 80.0; // 1:1 = the model's default scale
        else pps = std::max(2.0, std::min(4000.0, pps * std::pow(1.45, (double)zoomRequest)));
        viewStart = std::max(0.0, center - laneW * 0.5 / pps);
        zoomRequest = 0;
        zoomOneRequested = false;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 accent(1.f, 0.573f, 0.184f, 1.f);
    ImU32 accentU = col32(accent);

    // ============================================================ RULER
    ImVec2 rulerMin(origin.x, origin.y);
    ImVec2 rulerMax(origin.x + avail.x, origin.y + RULER_H);
    float cueStripY1 = rulerMin.y + 11.f;   // cue markers strip
    float loopStripY1 = cueStripY1 + 6.f;   // loop range strip
    dl->AddRectFilled(rulerMin, rulerMax, IM_COL32(28, 28, 30, 255));
    dl->AddRectFilled(rulerMin, ImVec2(laneX0, rulerMax.y), IM_COL32(24, 24, 26, 255));

    dl->PushClipRect(rulerMin, ImVec2(laneX0 - 4, rulerMax.y), true);
    dl->AddText(ImVec2(rulerMin.x + 6, rulerMin.y + RULER_H - ImGui::GetFontSize() - 4),
                IM_COL32(170, 170, 175, 255), seq.niceName.c_str());
    dl->PopClipRect();

    // grid steps (time or beats)
    double majorStep, minorStep;
    int beatsPerBar = seq.beatsPerBarP->intValue();
    double beatDur = 60.0 / std::max(20.f, seq.bpmP->floatValue());
    if (gridMode == 1)
    {
        minorStep = beatDur;
        while (minorStep * pps < 9.0) minorStep *= 2.0;
        majorStep = beatDur * beatsPerBar;
        while (majorStep * pps < 70.0) majorStep *= 2.0;
    }
    else
    {
        majorStep = RULER_STEPS[IM_ARRAYSIZE(RULER_STEPS) - 1];
        for (double s : RULER_STEPS)
            if (s * pps >= 80.0) { majorStep = s; break; }
        minorStep = majorStep / 5.0;
    }

    dl->PushClipRect(ImVec2(laneX0, rulerMin.y), rulerMax, true);
    {
        double t0v = std::max(0.0, viewStart);
        double tEnd = xToTime(origin.x + avail.x);
        double tm = std::floor(t0v / minorStep) * minorStep;
        for (; tm <= tEnd; tm += minorStep)
        {
            float x = timeToX(tm);
            bool isMajor = std::fabs(std::fmod(tm + minorStep * 0.5, majorStep) - minorStep * 0.5) < minorStep * 0.25;
            float h = isMajor ? 11.f : 5.f;
            dl->AddLine(ImVec2(x, rulerMax.y - h), ImVec2(x, rulerMax.y),
                        IM_COL32(140, 140, 145, isMajor ? 200 : 110), 1.f);
            if (isMajor)
            {
                std::string lbl;
                if (gridMode == 1)
                {
                    int bar = (int)std::round(tm / (beatDur * beatsPerBar)) + 1;
                    lbl = "b" + std::to_string(bar);
                }
                else lbl = formatTime(tm, majorStep < 1.0);
                dl->AddText(ImVec2(x + 3, rulerMax.y - 12 - ImGui::GetFontSize()),
                            IM_COL32(150, 150, 155, 220), lbl.c_str());
            }
        }
        float xe = timeToX(seq.totalTime());
        if (xe < rulerMax.x)
            dl->AddRectFilled(ImVec2(std::max(xe, laneX0), rulerMin.y), rulerMax, IM_COL32(0, 0, 0, 90));

        // loop range strip
        if (seq.loopIn >= 0 && seq.loopOut > seq.loopIn)
        {
            float lx0 = timeToX(seq.loopIn), lx1 = timeToX(seq.loopOut);
            dl->AddRectFilled(ImVec2(lx0, cueStripY1), ImVec2(lx1, loopStripY1), col32(accent, 0.55f));
            dl->AddRectFilled(ImVec2(lx0 - 3, cueStripY1), ImVec2(lx0 + 3, loopStripY1 + 3), accentU, 2.f);
            dl->AddRectFilled(ImVec2(lx1 - 3, cueStripY1), ImVec2(lx1 + 3, loopStripY1 + 3), accentU, 2.f);
        }

        // cues
        for (auto& c : seq.cues)
        {
            float x = timeToX(c.time);
            if (x < laneX0 - 8 || x > rulerMax.x + 8) continue;
            drawDiamond(dl, ImVec2(x, rulerMin.y + 5.5f), 5.f,
                        IM_COL32(90, 170, 255, 230), IM_COL32(230, 240, 255, 200));
            dl->AddText(ImVec2(x + 7, rulerMin.y + 0.5f), IM_COL32(140, 180, 230, 200), c.name.c_str());
        }
    }
    dl->PopClipRect();

    // ruler interaction
    ImGui::SetCursorScreenPos(rulerMin);
    ImGui::InvisibleButton("##ruler", ImVec2(avail.x, RULER_H),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool rulerHovered = ImGui::IsItemHovered();
    ImVec2 mouse = io.MousePos;

    // cue / loop-handle hit in ruler
    uint64_t hitCue = 0;
    int hitLoopHandle = 0; // 1 = in, 2 = out
    if (rulerHovered && mouse.x >= laneX0)
    {
        for (auto& c : seq.cues)
            if (dist2(mouse, ImVec2(timeToX(c.time), rulerMin.y + 5.5f)) < 49) { hitCue = c.id; break; }
        if (!hitCue && seq.loopIn >= 0 && mouse.y <= loopStripY1 + 4)
        {
            if (std::fabs(mouse.x - timeToX(seq.loopIn)) < 6) hitLoopHandle = 1;
            else if (std::fabs(mouse.x - timeToX(seq.loopOut)) < 6) hitLoopHandle = 2;
        }
    }

    if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (mouse.x < laneX0) seq.select();
        else if (hitCue)
        {
            drag = Drag::Cue;
            dragItemId = hitCue;
            preEditJson = seq.cuesToJson();
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (TimeCue* c = seq.findCue(hitCue)) seq.setTime(c->time);
                drag = Drag::None;
            }
        }
        else if (hitLoopHandle)
        {
            drag = hitLoopHandle == 1 ? Drag::LoopIn : Drag::LoopOut;
            dragOrigA = seq.loopIn;
            dragOrigB = seq.loopOut;
        }
        else drag = Drag::Scrub;
        dragStartMouse = mouse;
        dragMoved = false;
    }
    if (rulerHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && mouse.x >= laneX0)
    {
        ctxTime = snapTime(seq, xToTime(mouse.x), io.KeyAlt);
        ctxItemId = hitCue;
        ImGui::OpenPopup(hitCue ? "cue_ctx" : "ruler_ctx");
    }

    switch (drag)
    {
    case Drag::Scrub:
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            seq.setTime(std::max(0.0, xToTime(mouse.x)));
        else drag = Drag::None;
        break;
    case Drag::Cue:
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (TimeCue* c = seq.findCue(dragItemId))
            {
                c->time = snapTime(seq, xToTime(mouse.x), io.KeyAlt);
                seq.sortCues();
                dragMoved = true;
            }
        }
        else
        {
            if (dragMoved) pushCuesEdit(sp, preEditJson, "Move Cue");
            drag = Drag::None;
        }
        break;
    case Drag::LoopIn:
    case Drag::LoopOut:
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            double t = snapTime(seq, xToTime(mouse.x), io.KeyAlt);
            if (drag == Drag::LoopIn) seq.loopIn = std::min(t, seq.loopOut - 0.05);
            else seq.loopOut = std::max(t, seq.loopIn + 0.05);
            dragMoved = true;
        }
        else
        {
            if (dragMoved)
            {
                double oi = dragOrigA, oo = dragOrigB;
                double ni = seq.loopIn, no = seq.loopOut;
                UndoManager::get().pushDone("Edit Loop Range",
                    [sp, ni, no] { sp->loopIn = ni; sp->loopOut = no; },
                    [sp, oi, oo] { sp->loopIn = oi; sp->loopOut = oo; },
                    { sp });
            }
            drag = Drag::None;
        }
        break;
    default: break;
    }

    // ruler popups
    if (ImGui::BeginPopup("ruler_ctx"))
    {
        if (ImGui::MenuItem("Add Cue Here"))
        {
            json pre = seq.cuesToJson();
            seq.addCue(ctxTime);
            pushCuesEdit(sp, pre, "Add Cue");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Set Loop In Here"))
        {
            double oi = seq.loopIn, oo = seq.loopOut;
            seq.loopIn = ctxTime;
            if (seq.loopOut <= seq.loopIn) seq.loopOut = seq.totalTime();
            double ni = seq.loopIn, no = seq.loopOut;
            UndoManager::get().pushDone("Set Loop In",
                [sp, ni, no] { sp->loopIn = ni; sp->loopOut = no; },
                [sp, oi, oo] { sp->loopIn = oi; sp->loopOut = oo; }, { sp });
        }
        if (ImGui::MenuItem("Set Loop Out Here"))
        {
            double oi = seq.loopIn, oo = seq.loopOut;
            seq.loopOut = ctxTime;
            if (seq.loopIn < 0 || seq.loopIn >= seq.loopOut) seq.loopIn = 0;
            double ni = seq.loopIn, no = seq.loopOut;
            UndoManager::get().pushDone("Set Loop Out",
                [sp, ni, no] { sp->loopIn = ni; sp->loopOut = no; },
                [sp, oi, oo] { sp->loopIn = oi; sp->loopOut = oo; }, { sp });
        }
        if (ImGui::MenuItem("Clear Loop Range", nullptr, false, seq.loopIn >= 0))
        {
            double oi = seq.loopIn, oo = seq.loopOut;
            seq.loopIn = seq.loopOut = -1;
            UndoManager::get().pushDone("Clear Loop Range",
                [sp] { sp->loopIn = sp->loopOut = -1; },
                [sp, oi, oo] { sp->loopIn = oi; sp->loopOut = oo; }, { sp });
        }
        ImGui::Separator();
        ImGui::SetNextItemWidth(90);
        ImGui::DragFloat("##instime", &insertTimeBuf, 0.1f, 0.1f, 600.f, "%.2f s");
        ImGui::SameLine();
        if (ImGui::MenuItem("Insert Time Here"))
        {
            json pre = seq.contentToJson();
            seq.insertTime(ctxTime, insertTimeBuf);
            pushContentEdit(sp, pre, "Insert Time");
        }
        if (ImGui::MenuItem("Remove Loop Range Time", nullptr, false, seq.loopIn >= 0))
        {
            json pre = seq.contentToJson();
            seq.removeTimespan(seq.loopIn, seq.loopOut);
            pushContentEdit(sp, pre, "Remove Timespan");
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("cue_ctx"))
    {
        TimeCue* c = seq.findCue(ctxItemId);
        if (c)
        {
            char buf[96];
            snprintf(buf, sizeof(buf), "%s", c->name.c_str());
            ImGui::SetNextItemWidth(140);
            if (ImGui::InputText("##cuename", buf, sizeof(buf))) c->name = buf;
            if (ImGui::MenuItem("Jump To Cue")) seq.setTime(c->time);
            if (ImGui::MenuItem("Delete Cue"))
            {
                json pre = seq.cuesToJson();
                seq.removeCue(c->id);
                pushCuesEdit(sp, pre, "Delete Cue");
            }
        }
        ImGui::EndPopup();
    }

    // ============================================================ LANES CHILD
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + RULER_H));
    float lanesH = avail.y - RULER_H - HSCROLL_H;
    ImGui::BeginChild("##lanes", ImVec2(avail.x, lanesH), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* cdl = ImGui::GetWindowDrawList();

    // advance the embedded automation open/close animations (smooth expand:
    // block AND track heights follow the eased value below)
    for (auto& l : seq.layers)
        if (auto* cl0 = dynamic_cast<ClipLayer*>(l.get()))
            for (auto& c : cl0->clips)
                for (auto& a : c->automations)
                {
                    float target = a->expanded ? 1.f : 0.f;
                    if (a->uiAnim < 0) a->uiAnim = target; // first sight: no anim
                    else
                    {
                        a->uiAnim += (target - a->uiAnim) *
                                     std::min(1.f, io.DeltaTime * 14.f);
                        if (std::fabs(a->uiAnim - target) < 0.01f) a->uiAnim = target;
                    }
                }

    // effective lane height: clip tracks auto-grow so the tallest block
    // (title + expanded automation rows) always fits; the header grip still
    // sets the BASE height
    auto laneH = [](Layer* l) -> float
    {
        float h = l->uiHeight;
        if (auto* cl = dynamic_cast<ClipLayer*>(l))
            for (auto& c : cl->clips)
            {
                float want = clipDesiredH(*c);
                if (want > 0) h = std::max(h, want + 6.f);
            }
        return h;
    };

    // sticky layer (the pinned audio/waveform lane): rendered at a FIXED
    // position under the ruler, outside the scroll flow — always visible
    Layer* stickyL = stickyLayerId ? seq.findLayer(stickyLayerId) : nullptr;
    const float winTop = ImGui::GetWindowPos().y;
    const float stickyBandH = stickyL ? laneH(stickyL) + LANE_GAP : 0.f;

    float contentH = LANE_GAP + stickyBandH;
    for (auto& l : seq.layers)
        if (l.get() != stickyL) contentH += laneH(l.get()) + LANE_GAP;
    contentH += 70;

    float canvasW = avail.x;
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::InvisibleButton("##canvas", ImVec2(canvasW, std::max(contentH, lanesH)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    bool canvasHovered = ImGui::IsItemHovered();
    ImVec2 canvasP0 = ImGui::GetItemRectMin();

    float cLaneX0 = canvasP0.x + HEADER_W;
    float cLaneX1 = canvasP0.x + canvasW;

    // geometry: flow lanes scroll (starting below the sticky band); the
    // sticky lane's geom is pinned to the visible top and pushed LAST so
    // it draws on top of anything scrolling beneath it
    std::vector<LaneGeom> geoms;
    float flowBottom = canvasP0.y + LANE_GAP + stickyBandH;
    {
        float y = flowBottom;
        int idx = 0;
        for (auto& l : seq.layers)
        {
            const int myIdx = idx++;
            if (l.get() == stickyL) continue;
            LaneGeom g; g.layer = l.get(); g.index = myIdx;
            g.y0 = y; g.y1 = y + laneH(l.get());
            geoms.push_back(g);
            y = g.y1 + LANE_GAP;
        }
        flowBottom = y;
        if (stickyL)
        {
            LaneGeom g; g.layer = stickyL; g.index = seq.layerIndex(stickyL);
            g.y0 = winTop + 1;
            g.y1 = g.y0 + laneH(stickyL);
            geoms.push_back(g);
        }
    }
    // a Block clip with automations draws at its OWN height (compact when
    // its rows are collapsed) — the shared vertical extent for hit tests
    // and drawing
    auto clipRectY = [&](const LaneGeom& g, const Clip& c, float& y0, float& y1)
    {
        y0 = g.y0 + 3;
        y1 = g.y1 - 3;
        float want = clipDesiredH(c);
        if (want > 0) y1 = std::min(y1, y0 + want);
    };
    auto laneAtY = [&](float y) -> const LaneGeom*
    {
        // the pinned lane overlays the flow — it owns its band exclusively
        if (stickyL && !geoms.empty() && geoms.back().layer == stickyL)
        {
            const LaneGeom& sg = geoms.back();
            if (y >= sg.y0 && y < sg.y1 + LANE_GAP) return &sg;
        }
        for (auto& g : geoms)
        {
            if (g.layer == stickyL) continue;
            if (y >= g.y0 && y < g.y1 + LANE_GAP) return &g;
        }
        return nullptr;
    };
    auto geomOf = [&](const Layer* l) -> const LaneGeom*
    {
        for (auto& g : geoms) if (g.layer == l) return &g;
        return nullptr;
    };

    // key transform boxes: per automation layer with >= 2 selected keys
    struct KeyBox { AutomationLayer* al = nullptr; const LaneGeom* g = nullptr; float x0, x1, yT, yB; };
    std::vector<KeyBox> keyBoxes;
    for (auto& g : geoms)
    {
        auto* al = dynamic_cast<AutomationLayer*>(g.layer);
        if (!al || al->selectedKeys.size() < 2) continue;
        double t0 = 1e18, t1 = -1e18;
        float vmin = 1e18f, vmax = -1e18f;
        float mn = al->rangeMinP->floatValue(), mx = al->rangeMaxP->floatValue();
        float range = (mx - mn) == 0 ? 1.f : (mx - mn);
        for (auto& k : al->keys)
        {
            if (!al->selectedKeys.count(k.id)) continue;
            t0 = std::min(t0, k.time); t1 = std::max(t1, k.time);
            float nv = (k.value - mn) / range;
            vmin = std::min(vmin, nv); vmax = std::max(vmax, nv);
        }
        if (t1 <= t0 + 1e-9) continue;
        KeyBox kb;
        kb.al = al; kb.g = &g;
        kb.x0 = timeToX(t0); kb.x1 = timeToX(t1);
        kb.yT = normToY(g, vmax); kb.yB = normToY(g, vmin);
        keyBoxes.push_back(kb);
    }

    // ============================================================ HIT TESTING
    Hit hit;
    if (canvasHovered)
    {
        // key transform boxes first (they float above keys)
        for (auto& kb : keyBoxes)
        {
            float xm = (kb.x0 + kb.x1) * 0.5f, ym = (kb.yT + kb.yB) * 0.5f;
            auto handleHit = [&](float hx, float hy) { return dist2(mouse, ImVec2(hx, hy)) < 36; };
            Hit h;
            h.layer = kb.al;
            if (handleHit(kb.x0, ym)) h.kind = Hit::KeyBoxL;
            else if (handleHit(kb.x1, ym)) h.kind = Hit::KeyBoxR;
            else if (handleHit(xm, kb.yT - 8)) h.kind = Hit::KeyBoxT;
            else if (handleHit(xm, kb.yB + 8)) h.kind = Hit::KeyBoxB;
            else
            {
                // near the border -> move
                bool nearX = (std::fabs(mouse.x - kb.x0) < 4 || std::fabs(mouse.x - kb.x1) < 4) &&
                             mouse.y > kb.yT - 6 && mouse.y < kb.yB + 6;
                bool nearY = (std::fabs(mouse.y - (kb.yT - 6)) < 4 || std::fabs(mouse.y - (kb.yB + 6)) < 4) &&
                             mouse.x > kb.x0 - 6 && mouse.x < kb.x1 + 6;
                if (nearX || nearY) h.kind = Hit::KeyBoxMove;
            }
            if (h.kind != Hit::None) { hit = h; break; }
        }

        const LaneGeom* g = laneAtY(mouse.y);
        if (hit.kind == Hit::None && g)
        {
            hit.layer = g->layer;
            hit.laneIdx = g->index;
            if (mouse.x < cLaneX0)
                hit.kind = (mouse.y > g->y1 - 5.f) ? Hit::HeaderGrip : Hit::Header;
            else
            {
                hit.kind = Hit::Lane;
                if (auto* cl = dynamic_cast<ClipLayer*>(g->layer))
                {
                    for (int i = (int)cl->clips.size() - 1; i >= 0; i--)
                    {
                        Clip* c = cl->clips[i].get();
                        float x0 = timeToX(c->start());
                        float x1 = timeToX(c->end());
                        float cy0, cy1;
                        clipRectY(*g, *c, cy0, cy1);
                        if (mouse.x >= x0 - 1 && mouse.x < x1 + 1 && mouse.y >= cy0 - 3 && mouse.y < cy1 + 1)
                        {
                            // fade handles (audio, selected clip only to reduce clutter)
                            if (c->ctype == Clip::CType::Audio && c->isSelected())
                            {
                                ImVec2 hIn(x0 + (float)(c->fadeInP->floatValue() * pps), g->y0 + 9);
                                ImVec2 hOut(x1 - (float)(c->fadeOutP->floatValue() * pps), g->y0 + 9);
                                if (dist2(mouse, hIn) < 36) { hit.kind = Hit::FadeIn; hit.clip = c; break; }
                                if (dist2(mouse, hOut) < 36) { hit.kind = Hit::FadeOut; hit.clip = c; break; }
                            }
                            float edge = std::min(7.f, (x1 - x0) * 0.25f);
                            if (mouse.x < x0 + edge)       hit.kind = Hit::ClipL;
                            else if (mouse.x > x1 - edge)  hit.kind = Hit::ClipR;
                            else                           hit.kind = Hit::ClipBody;
                            hit.clip = c;

                            // embedded automation rows (Block clips)
                            if (hit.kind == Hit::ClipBody && !c->automations.empty() &&
                                c->ctype == Clip::CType::Block)
                            {
                                std::vector<AutoRowGeom> rows;
                                buildAutoRows(*c, cy0, rows);
                                for (auto& rg : rows)
                                {
                                    ClipAutomation* a = rg.a;
                                    if (mouse.y >= rg.hy0 && mouse.y < rg.hy1)
                                    {
                                        // record dot on the right of the header
                                        ImVec2 dot(x1 - 10.f, (rg.hy0 + rg.hy1) * 0.5f);
                                        hit.kind = dist2(mouse, dot) < 30 ? Hit::CAArm
                                                                          : Hit::CAHeader;
                                        hit.cauto = a;
                                        break;
                                    }
                                    if (mouse.y >= rg.by0 && mouse.y < rg.by1 && rg.by1 > rg.by0 + 4)
                                    {
                                        hit.cauto = a;
                                        if (a->akind == ClipAutomation::AKind::Gradient)
                                        {
                                            hit.kind = Hit::CAGrad;
                                            for (auto& k : a->gkeys)
                                            {
                                                ImVec2 kp(x0 + (float)(k.time * pps), rg.by1 - 6.f);
                                                if (dist2(mouse, kp) < 42) { hit.kind = Hit::CAGKey; hit.keyId = k.id; break; }
                                            }
                                            break;
                                        }
                                        hit.kind = Hit::CACurve;
                                        float rmn = a->rangeMin, rmx = a->rangeMax;
                                        float rr = (rmx - rmn) == 0 ? 1.f : (rmx - rmn);
                                        // bezier / ease handles of selected keys first
                                        for (size_t ki = 0; ki + 1 < a->keys.size() && hit.kind == Hit::CACurve; ki++)
                                        {
                                            AutoKey& k = a->keys[ki];
                                            if (!a->selectedKeys.count(k.id)) continue;
                                            AutoKey& nk = a->keys[ki + 1];
                                            double segDur = nk.time - k.time;
                                            float na = (k.value - rmn) / rr, nb = (nk.value - rmn) / rr;
                                            if (k.easing == EasingType::Bezier)
                                            {
                                                ImVec2 h1(x0 + (float)((k.time + k.ep.a1.x * segDur) * pps), caRowNormToY(rg, na + k.ep.a1.y));
                                                ImVec2 h2(x0 + (float)((nk.time + k.ep.a2.x * segDur) * pps), caRowNormToY(rg, nb + k.ep.a2.y));
                                                if (dist2(mouse, h1) < 42) { hit.kind = Hit::CABezA1; hit.keyId = k.id; }
                                                else if (dist2(mouse, h2) < 42) { hit.kind = Hit::CABezA2; hit.keyId = k.id; }
                                            }
                                            else if (easingHasHandle(k.easing))
                                            {
                                                double midT = (k.time + nk.time) * 0.5;
                                                float midV = ease(k.easing, na, nb, 0.5f, k.ep);
                                                ImVec2 hm(x0 + (float)(midT * pps), caRowNormToY(rg, midV));
                                                if (dist2(mouse, hm) < 36) { hit.kind = Hit::CAEase; hit.keyId = k.id; }
                                            }
                                        }
                                        if (hit.kind == Hit::CACurve)
                                            for (auto& k : a->keys)
                                            {
                                                ImVec2 kp(x0 + (float)(k.time * pps), caRowNormToY(rg, (k.value - rmn) / rr));
                                                if (dist2(mouse, kp) < 42) { hit.kind = Hit::CAKey; hit.keyId = k.id; break; }
                                            }
                                        break;
                                    }
                                }
                            }
                            break;
                        }
                    }
                }
                else if (auto* al = dynamic_cast<AutomationLayer*>(g->layer))
                {
                    float mn = al->rangeMinP->floatValue(), mx = al->rangeMaxP->floatValue();
                    float range = (mx - mn) == 0 ? 1.f : (mx - mn);
                    // bezier handles of selected keys
                    for (size_t i = 0; i < al->keys.size(); i++)
                    {
                        AutoKey& k = al->keys[i];
                        if (!al->selectedKeys.count(k.id)) continue;
                        if (i + 1 >= al->keys.size()) continue;
                        AutoKey& nk = al->keys[i + 1];
                        double segDur = nk.time - k.time;
                        float na = (k.value - mn) / range, nb = (nk.value - mn) / range;
                        if (k.easing == EasingType::Bezier)
                        {
                            ImVec2 h1(timeToX(k.time + k.ep.a1.x * segDur), normToY(*g, na + k.ep.a1.y));
                            ImVec2 h2(timeToX(nk.time + k.ep.a2.x * segDur), normToY(*g, nb + k.ep.a2.y));
                            if (dist2(mouse, h1) < 49) { hit.kind = Hit::BezA1; hit.keyId = k.id; break; }
                            if (dist2(mouse, h2) < 49) { hit.kind = Hit::BezA2; hit.keyId = k.id; break; }
                        }
                        else if (easingHasHandle(k.easing))
                        {
                            double midT = (k.time + nk.time) * 0.5;
                            float midV = ease(k.easing, na, nb, 0.5f, k.ep);
                            ImVec2 hm(timeToX(midT), normToY(*g, midV));
                            if (dist2(mouse, hm) < 42) { hit.kind = Hit::EaseHandle; hit.keyId = k.id; break; }
                        }
                    }
                    if (hit.kind == Hit::Lane)
                    {
                        for (auto& k : al->keys)
                        {
                            float nv = (k.value - mn) / range;
                            ImVec2 kp(timeToX(k.time), normToY(*g, nv));
                            if (dist2(mouse, kp) < 56) { hit.kind = Hit::AKey; hit.keyId = k.id; break; }
                        }
                    }
                }
                else if (auto* gl = dynamic_cast<GradientLayer*>(g->layer))
                {
                    for (auto& k : gl->keys)
                    {
                        ImVec2 kp(timeToX(k.time), g->y1 - 7.f);
                        if (dist2(mouse, kp) < 64) { hit.kind = Hit::GKey; hit.keyId = k.id; break; }
                    }
                }
                else if (auto* tl = dynamic_cast<TriggerLayer*>(g->layer))
                {
                    float laneH = g->y1 - g->y0;
                    for (auto& t : tl->triggers)
                    {
                        float x = timeToX(t.time);
                        float yFlag = g->y0 + 5 + t.flagY * (laneH - 22);
                        // pole or flag box
                        if (std::fabs(mouse.x - x) < 5 && mouse.y > yFlag - 4 && mouse.y < g->y1)
                        { hit.kind = Hit::TKey; hit.keyId = t.id; break; }
                        ImVec2 ts = ImGui::CalcTextSize(t.name.c_str());
                        if (inRect(mouse, ImVec2(x, yFlag - 8), ImVec2(x + ts.x + 12, yFlag + 8)))
                        { hit.kind = Hit::TKey; hit.keyId = t.id; break; }
                    }
                }
            }
        }
    }

    // cursor feedback
    if (hit.kind == Hit::ClipL || hit.kind == Hit::ClipR || drag == Drag::ResizeL || drag == Drag::ResizeR ||
        hit.kind == Hit::KeyBoxL || hit.kind == Hit::KeyBoxR)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (hit.kind == Hit::HeaderGrip || drag == Drag::LayerHeight ||
        hit.kind == Hit::KeyBoxT || hit.kind == Hit::KeyBoxB)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (hit.kind == Hit::CAHeader || hit.kind == Hit::CAArm)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (pencilMode && (hit.kind == Hit::CACurve ||
                       (hit.kind == Hit::Lane && dynamic_cast<AutomationLayer*>(hit.layer))))
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    // ============================================================ OPERATION HELPERS
    auto deleteSelection = [&]()
    {
        struct ClipRec { uint64_t layer; json data; };
        std::vector<ClipRec> clipRecs;
        for (Clip* c : Selection::get().getAs<Clip>())
            if (c->layer && c->layer->sequence == sp)
                clipRecs.push_back({ c->layer->id, c->save() });

        // selected keys inside embedded clip automations
        struct CARec { uint64_t clip, cauto; json pre, post; };
        std::vector<CARec> caRecs;
        bool clipDeleted = !clipRecs.empty();
        for (auto& l : seq.layers)
        {
            auto* cl = dynamic_cast<ClipLayer*>(l.get());
            if (!cl) continue;
            for (auto& c : cl->clips)
            {
                if (clipDeleted && c->isSelected()) continue; // dies whole
                for (auto& a : c->automations)
                {
                    if (a->selectedKeys.empty()) continue;
                    json pre = a->keysToJson();
                    for (uint64_t kid : std::vector<uint64_t>(a->selectedKeys.begin(),
                                                              a->selectedKeys.end()))
                        a->removeKey(kid);
                    caRecs.push_back({ c->id, a->id, pre, a->keysToJson() });
                }
            }
        }

        struct KeyRec { uint64_t layer; json pre, post; };
        std::vector<KeyRec> keyRecs;
        for (auto& l : seq.layers)
        {
            if (auto* al = dynamic_cast<AutomationLayer*>(l.get()))
            {
                if (al->selectedKeys.empty()) continue;
                json pre = al->keysToJson();
                for (uint64_t kid : std::vector<uint64_t>(al->selectedKeys.begin(), al->selectedKeys.end()))
                    al->removeKey(kid);
                keyRecs.push_back({ al->id, pre, al->keysToJson() });
            }
            else if (auto* gl = dynamic_cast<GradientLayer*>(l.get()))
            {
                if (gl->selectedKeys.empty()) continue;
                json pre = gl->keysToJson();
                for (uint64_t kid : std::vector<uint64_t>(gl->selectedKeys.begin(), gl->selectedKeys.end()))
                    gl->removeKey(kid);
                keyRecs.push_back({ gl->id, pre, gl->keysToJson() });
            }
            else if (auto* tl = dynamic_cast<TriggerLayer*>(l.get()))
            {
                if (tl->selectedKeys.empty()) continue;
                json pre = tl->keysToJson();
                for (uint64_t kid : std::vector<uint64_t>(tl->selectedKeys.begin(), tl->selectedKeys.end()))
                    tl->removeTrigger(kid);
                keyRecs.push_back({ tl->id, pre, tl->keysToJson() });
            }
        }
        for (auto& r : clipRecs)
            if (auto* cl = dynamic_cast<ClipLayer*>(seq.findLayer(r.layer)))
                cl->removeClip(r.data.value("id", (uint64_t)0));

        if (clipRecs.empty() && keyRecs.empty() && caRecs.empty()) return;
        auto applyCa = [sp](const std::vector<CARec>& recs, bool post)
        {
            for (auto& r : recs)
                if (Clip* c = sp->findClip(r.clip))
                    if (ClipAutomation* a = c->findAutomation(r.cauto))
                        a->keysFromJson(post ? r.post : r.pre);
        };
        UndoManager::get().pushDone("Delete",
            [sp, clipRecs, keyRecs, caRecs, applyCa]
            {
                for (auto& r : clipRecs)
                    if (auto* cl = dynamic_cast<ClipLayer*>(sp->findLayer(r.layer)))
                        cl->removeClip(r.data.value("id", (uint64_t)0));
                for (auto& r : keyRecs)
                {
                    Layer* l = sp->findLayer(r.layer);
                    if (auto* al = dynamic_cast<AutomationLayer*>(l)) al->keysFromJson(r.post);
                    else if (auto* gl = dynamic_cast<GradientLayer*>(l)) gl->keysFromJson(r.post);
                    else if (auto* tl = dynamic_cast<TriggerLayer*>(l)) tl->keysFromJson(r.post);
                }
                applyCa(caRecs, true);
            },
            [sp, clipRecs, keyRecs, caRecs, applyCa]
            {
                for (auto& r : clipRecs)
                    if (auto* cl = dynamic_cast<ClipLayer*>(sp->findLayer(r.layer)))
                        cl->addClipFromJson(r.data);
                for (auto& r : keyRecs)
                {
                    Layer* l = sp->findLayer(r.layer);
                    if (auto* al = dynamic_cast<AutomationLayer*>(l)) al->keysFromJson(r.pre);
                    else if (auto* gl = dynamic_cast<GradientLayer*>(l)) gl->keysFromJson(r.pre);
                    else if (auto* tl = dynamic_cast<TriggerLayer*>(l)) tl->keysFromJson(r.pre);
                }
                applyCa(caRecs, false);
            },
            { sp });
    };

    auto duplicateSelection = [&]()
    {
        auto clips = Selection::get().getAs<Clip>();
        std::vector<Clip*> mine;
        for (Clip* c : clips) if (c->layer && c->layer->sequence == sp) mine.push_back(c);
        if (mine.empty()) return;
        struct Rec { uint64_t layer; json data; };
        std::vector<Rec> recs;
        Selection::get().clear();
        for (Clip* c : mine)
        {
            json j = c->save();
            j["id"] = seq.newId();
            // land the copy in the nearest free spot (blocks never stack)
            j["params"]["start"] = (float)c->layer->resolveOverlap(c->end(), c->length());
            Clip* nc = c->layer->addClipFromJson(j);
            Selection::get().add(nc);
            recs.push_back({ c->layer->id, j });
        }
        UndoManager::get().pushDone("Duplicate",
            [sp, recs]
            {
                for (auto& r : recs)
                    if (auto* cl = dynamic_cast<ClipLayer*>(sp->findLayer(r.layer)))
                        cl->addClipFromJson(r.data);
            },
            [sp, recs]
            {
                for (auto& r : recs)
                    if (auto* cl = dynamic_cast<ClipLayer*>(sp->findLayer(r.layer)))
                        cl->removeClip(r.data.value("id", (uint64_t)0));
            },
            { sp });
    };

    auto selectAllClips = [&]()
    {
        Selection::get().clear();
        for (auto& l : seq.layers)
            if (auto* cl = dynamic_cast<ClipLayer*>(l.get()))
                for (auto& c : cl->clips) Selection::get().add(c.get());
    };

    auto nudgeSelection = [&](double dt)
    {
        auto clips = Selection::get().getAs<Clip>();
        if (clips.empty()) return;
        struct R { uint64_t id; double o, n; };
        std::vector<R> rs;
        for (Clip* c : clips)
        {
            if (!c->layer || c->layer->sequence != sp) continue;
            double n = std::max(0.0, c->start() + dt);
            rs.push_back({ c->id, c->start(), n });
            c->startP->setValue((float)n);
        }
        if (rs.empty()) return;
        UndoManager::get().pushDone("Nudge",
            [sp, rs] { for (auto& r : rs) if (Clip* c = sp->findClip(r.id)) c->startP->setValue((float)r.n); },
            [sp, rs] { for (auto& r : rs) if (Clip* c = sp->findClip(r.id)) c->startP->setValue((float)r.o); },
            { sp });
    };

    auto splitClip = [&](Clip* c, double t)
    {
        if (!c || !c->layer) return;
        if (t < c->start() + 0.05 || t > c->end() - 0.05) return;
        ClipLayer* cl = c->layer;
        uint64_t layerId = cl->id;
        json orig = c->save();
        double cut = t - c->start();

        json a = orig;
        a["id"] = seq.newId();
        a["params"]["length"] = (float)cut;
        if (a["params"].contains("fadeOut")) a["params"]["fadeOut"] = 0.f;
        json b = orig;
        b["id"] = seq.newId();
        b["params"]["start"] = (float)t;
        b["params"]["length"] = (float)(c->length() - cut);
        if (b["params"].contains("offset"))
            b["params"]["offset"] = (float)(c->offsetP->floatValue() + cut);
        if (b["params"].contains("fadeIn")) b["params"]["fadeIn"] = 0.f;

        // embedded automations split with the block: the head keeps keys
        // before the cut, the tail's keys shift into its own local time
        auto splitAutos = [cut](json& half, bool head)
        {
            if (!half.contains("autos")) return;
            for (auto& aj : half["autos"])
            {
                if (!aj.contains("keys") || !aj["keys"].is_array()) continue;
                json kept = json::array();
                for (auto kj : aj["keys"])
                {
                    double kt = kj.value("t", 0.0);
                    if (head) { if (kt <= cut + 1e-9) kept.push_back(kj); }
                    else if (kt >= cut - 1e-9)
                    {
                        kj["t"] = kt - cut;
                        kept.push_back(kj);
                    }
                }
                aj["keys"] = kept;
            }
        };
        splitAutos(a, true);
        splitAutos(b, false);

        uint64_t origId = c->id;
        cl->removeClip(origId);
        cl->addClipFromJson(a);
        Clip* nb = cl->addClipFromJson(b);
        if (nb) nb->select();
        UndoManager::get().pushDone("Split Clip",
            [sp, layerId, origId, a, b]
            {
                if (auto* l = dynamic_cast<ClipLayer*>(sp->findLayer(layerId)))
                {
                    l->removeClip(origId);
                    l->addClipFromJson(a);
                    l->addClipFromJson(b);
                }
            },
            [sp, layerId, orig, a, b]
            {
                if (auto* l = dynamic_cast<ClipLayer*>(sp->findLayer(layerId)))
                {
                    l->removeClip(a.value("id", (uint64_t)0));
                    l->removeClip(b.value("id", (uint64_t)0));
                    l->addClipFromJson(orig);
                }
            },
            { sp });
    };

    auto copySelection = [&]()
    {
        auto clips = Selection::get().getAs<Clip>();
        std::vector<Clip*> mine;
        for (Clip* c : clips) if (c->layer && c->layer->sequence == sp) mine.push_back(c);
        if (!mine.empty())
        {
            double anchor = 1e18;
            for (Clip* c : mine) anchor = std::min(anchor, c->start());
            json arr = json::array();
            for (Clip* c : mine)
                arr.push_back({ { "layer", c->layer->id }, { "data", c->save() } });
            json env = { { "organic", "clips" }, { "anchor", anchor }, { "clips", arr } };
            ImGui::SetClipboardText(env.dump().c_str());
            OLOG("Timeline", "Copied " << mine.size() << " clip(s)");
            return;
        }
        // keys (first layer with a selection)
        for (auto& l : seq.layers)
        {
            json keysJ = layerKeysJson(l.get());
            std::set<uint64_t>* selKeys = nullptr;
            if (auto* al = dynamic_cast<AutomationLayer*>(l.get())) selKeys = &al->selectedKeys;
            else if (auto* gl = dynamic_cast<GradientLayer*>(l.get())) selKeys = &gl->selectedKeys;
            else if (auto* tl = dynamic_cast<TriggerLayer*>(l.get())) selKeys = &tl->selectedKeys;
            if (!selKeys || selKeys->empty()) continue;
            double anchor = 1e18;
            json arr = json::array();
            for (auto& kj : keysJ)
            {
                if (!selKeys->count(kj.value("id", (uint64_t)0))) continue;
                anchor = std::min(anchor, kj.value("t", 0.0));
                arr.push_back(kj);
            }
            json env = { { "organic", "tlkeys" }, { "ltype", (int)l->ltype },
                         { "layer", l->id }, { "anchor", anchor }, { "keys", arr } };
            ImGui::SetClipboardText(env.dump().c_str());
            OLOG("Timeline", "Copied " << arr.size() << " key(s)");
            return;
        }
    };

    auto pasteClipboard = [&]()
    {
        const char* txt = ImGui::GetClipboardText();
        if (!txt) return;
        json env;
        try { env = json::parse(txt); }
        catch (...) { return; }
        if (!env.is_object()) return;
        std::string kind = env.value("organic", "");
        double at = seq.currentTime;

        if (kind == "clips")
        {
            double anchor = env.value("anchor", 0.0);
            struct Rec { uint64_t layer; json data; };
            std::vector<Rec> recs;
            Selection::get().clear();
            for (auto& e : env["clips"])
            {
                uint64_t layerId = e.value("layer", (uint64_t)0);
                ClipLayer* cl = dynamic_cast<ClipLayer*>(seq.findLayer(layerId));
                if (!cl)
                {
                    for (auto& l : seq.layers)
                        if ((cl = dynamic_cast<ClipLayer*>(l.get()))) break;
                }
                if (!cl) return;
                json data = e["data"];
                data["id"] = seq.newId();
                const double wantT = data["params"].value("start", 0.f) - anchor + at;
                const double wantLen = data["params"].value("length", 4.f);
                data["params"]["start"] = (float)cl->resolveOverlap(wantT, wantLen);
                Clip* nc = cl->addClipFromJson(data);
                if (nc) Selection::get().add(nc);
                recs.push_back({ cl->id, data });
            }
            if (recs.empty()) return;
            UndoManager::get().pushDone("Paste Clips",
                [sp, recs]
                {
                    for (auto& r : recs)
                        if (auto* cl = dynamic_cast<ClipLayer*>(sp->findLayer(r.layer)))
                            cl->addClipFromJson(r.data);
                },
                [sp, recs]
                {
                    for (auto& r : recs)
                        if (auto* cl = dynamic_cast<ClipLayer*>(sp->findLayer(r.layer)))
                            cl->removeClip(r.data.value("id", (uint64_t)0));
                },
                { sp });
        }
        else if (kind == "tlkeys")
        {
            int lt = env.value("ltype", 1);
            Layer* target = seq.findLayer(env.value("layer", (uint64_t)0));
            if (!target || (int)target->ltype != lt)
            {
                target = nullptr;
                for (auto& l : seq.layers)
                    if ((int)l->ltype == lt && l->isSelected()) target = l.get();
                if (!target)
                    for (auto& l : seq.layers)
                        if ((int)l->ltype == lt) { target = l.get(); break; }
            }
            if (!target) return;
            json pre = layerKeysJson(target);
            double anchor = env.value("anchor", 0.0);
            json merged = pre;
            for (auto kj : env["keys"])
            {
                kj["id"] = 0; // new ids assigned on load
                kj["t"] = kj.value("t", 0.0) - anchor + at;
                merged.push_back(kj);
            }
            if (auto* al = dynamic_cast<AutomationLayer*>(target)) al->keysFromJson(merged);
            else if (auto* gl = dynamic_cast<GradientLayer*>(target)) gl->keysFromJson(merged);
            else if (auto* tl = dynamic_cast<TriggerLayer*>(target)) tl->keysFromJson(merged);
            pushKeysEdit(sp, target->id, pre, layerKeysJson(target), "Paste Keys");
        }
    };

    auto removeLayerUndoable = [&](uint64_t lid)
    {
        json data = seq.removeLayer(lid);
        if (data.is_null() || data.empty()) return;
        int idx = data.value("_index", -1);
        UndoManager::get().pushDone("Remove Layer",
            [sp, lid] { sp->removeLayer(lid); },
            [sp, data, idx] { sp->addLayerFromJson(data, idx); },
            { sp });
    };

    auto moveLayerUndoable = [&](int from, int to)
    {
        if (from < 0 || to < 0 || from >= (int)seq.layers.size() || to >= (int)seq.layers.size() || from == to)
            return;
        seq.moveLayer(from, to);
        UndoManager::get().pushDone("Move Layer",
            [sp, from, to] { sp->moveLayer(from, to); },
            [sp, from, to] { sp->moveLayer(to, from); },
            { sp });
    };

    auto stripIds = [](json& j)
    {
        if (j.contains("id")) j["id"] = 0;
        if (j.contains("clips")) for (auto& c : j["clips"]) if (c.contains("id")) c["id"] = 0;
        if (j.contains("keys")) for (auto& k : j["keys"]) if (k.contains("id")) k["id"] = 0;
    };

    auto duplicateLayerUndoable = [&](uint64_t lid)
    {
        Layer* l = seq.findLayer(lid);
        if (!l) return;
        json data = l->save();
        stripIds(data);
        int idx = seq.layerIndex(l) + 1;
        Layer* nl = seq.addLayerFromJson(data, idx);
        nl->setNiceName(nl->niceName + " Copy");
        json snap = nl->save();
        uint64_t nid = nl->id;
        UndoManager::get().pushDone("Duplicate Layer",
            [sp, snap, idx] { sp->addLayerFromJson(snap, idx); },
            [sp, nid] { sp->removeLayer(nid); },
            { sp });
        nl->select();
    };

    auto addLayerUndoable = [&](Layer::LType t, const char* n, int index)
    {
        Layer* l = seq.addLayer(t, n, index);
        json data = l->save();
        int idx = seq.layerIndex(l);
        uint64_t lid = l->id;
        UndoManager::get().pushDone("Add Layer",
            [sp, data, idx] { sp->addLayerFromJson(data, idx); },
            [sp, lid] { sp->removeLayer(lid); },
            { sp });
        l->select();
    };

    auto createClipFromPayload = [&](ClipLayer* cl, double t, const MediaPayload& mp)
    {
        const double len = mp.duration > 0 ? mp.duration : 2.0;
        t = cl->resolveOverlap(t, len); // blocks never stack on one track
        Clip* c = cl->addClip(mp.kind == 1 ? Clip::CType::Audio : Clip::CType::Block,
                              mp.name[0] ? mp.name : "Clip", t, len);
        ImVec4 col(mp.color[0], mp.color[1], mp.color[2], mp.color[3]);
        c->colorP->setValue(col, false);
        c->colorP->defaultValue = col;
        if (mp.kind == 1 && mp.file[0]) c->setAudioFile(mp.file, mp.duration <= 0);
        pushClipAdded(sp, cl->id, c->save(), "Drop Media");
        c->select();
        return c;
    };

    // ============================================================ WHEEL ZOOM / PAN
    if ((canvasHovered || rulerHovered) && io.MouseWheel != 0)
    {
        if (io.KeyCtrl)
        {
            double tAtMouse = xToTime(mouse.x);
            pps = std::max(2.0, std::min(4000.0, pps * std::pow(1.18, (double)io.MouseWheel)));
            viewStart = std::max(0.0, tAtMouse - (mouse.x - cLaneX0) / pps);
        }
        else if (io.KeyShift)
        {
            viewStart = std::max(0.0, viewStart - io.MouseWheel * 60.0 / pps);
        }
        else if (canvasHovered && contentH > lanesH)
        {
            ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * 40.f);
        }
    }
    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
    {
        drag = Drag::PanH;
        dragStartMouse = mouse;
        dragOrigA = viewStart;
    }
    if (drag == Drag::PanH)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            viewStart = std::max(0.0, dragOrigA - (mouse.x - dragStartMouse.x) / pps);
        else drag = Drag::None;
    }

    // ============================================================ LEFT PRESS
    bool bypassSnap = io.KeyAlt;
    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        bool dbl = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        dragStartMouse = mouse;
        dragMoved = false;

        switch (hit.kind)
        {
        case Hit::KeyBoxL: case Hit::KeyBoxR: case Hit::KeyBoxT: case Hit::KeyBoxB: case Hit::KeyBoxMove:
        {
            auto* al = static_cast<AutomationLayer*>(hit.layer);
            drag = hit.kind == Hit::KeyBoxL ? Drag::KeyBoxL
                 : hit.kind == Hit::KeyBoxR ? Drag::KeyBoxR
                 : hit.kind == Hit::KeyBoxT ? Drag::KeyBoxT
                 : hit.kind == Hit::KeyBoxB ? Drag::KeyBoxB : Drag::KeyBoxMove;
            dragLayerId = al->id;
            preEditJson = al->keysToJson();
            keyBoxRefs.clear();
            keyBoxT0 = 1e18; keyBoxT1 = -1e18; keyBoxV0 = 1e18f; keyBoxV1 = -1e18f;
            for (auto& k : al->keys)
            {
                if (!al->selectedKeys.count(k.id)) continue;
                keyBoxRefs.push_back({ k.id, k.time, k.value });
                keyBoxT0 = std::min(keyBoxT0, k.time);
                keyBoxT1 = std::max(keyBoxT1, k.time);
                keyBoxV0 = std::min(keyBoxV0, k.value);
                keyBoxV1 = std::max(keyBoxV1, k.value);
            }
            break;
        }
        case Hit::Header:
        {
            hit.layer->select(io.KeyCtrl);
            if (dbl)
            {
                if (offerLayerRename)
                {
                    ctxLayerId = hit.layer->id;
                    snprintf(renameBuf, sizeof(renameBuf), "%s", hit.layer->niceName.c_str());
                    wantRenamePopup = true;
                }
            }
            else if (hit.layer->id != stickyLayerId) // the pinned lane stays put
            {
                // possible drag-reorder (engages after threshold)
                drag = Drag::LayerReorder;
                dragLayerId = hit.layer->id;
                reorderTarget = -1;
            }
            break;
        }
        case Hit::HeaderGrip:
            drag = Drag::LayerHeight;
            dragLayerId = hit.layer->id;
            dragOrigF = hit.layer->uiHeight;
            break;

        case Hit::FadeIn:
        case Hit::FadeOut:
        {
            drag = hit.kind == Hit::FadeIn ? Drag::FadeIn : Drag::FadeOut;
            dragItemId = hit.clip->id;
            dragLayerId = hit.layer->id;
            dragOrigA = hit.clip->fadeInP->floatValue();
            dragOrigB = hit.clip->fadeOutP->floatValue();
            break;
        }
        case Hit::ClipBody:
        {
            if (dbl)
            {
                hit.clip->select();
                // host hook: open/edit what the block references (e.g. the
                // effect graph in the app's node editor)
                if (clipDoubleClicked) clipDoubleClicked(*hit.clip);
                break;
            }
            bool wasSelected = hit.clip->isSelected();
            if (io.KeyCtrl) { Selection::get().toggle(hit.clip); }
            else if (!wasSelected) { Selection::get().set(hit.clip); }
            if (!hit.clip->isSelected()) break;

            drag = Drag::MoveClips;
            dragItemId = hit.clip->id;
            dragLayerId = hit.layer->id;
            dragGrabDT = xToTime(mouse.x) - hit.clip->start();
            dragClips.clear();
            for (Clip* c : Selection::get().getAs<Clip>())
                if (c->layer && c->layer->sequence == sp)
                    dragClips.push_back({ c->id, c->layer->id, c->start() });
            break;
        }
        case Hit::ClipL:
        case Hit::ClipR:
        {
            if (!hit.clip->isSelected()) Selection::get().set(hit.clip);
            drag = (hit.kind == Hit::ClipL) ? Drag::ResizeL : Drag::ResizeR;
            dragItemId = hit.clip->id;
            dragLayerId = hit.layer->id;
            dragOrigA = hit.clip->start();
            dragOrigB = hit.clip->length();
            dragOrigC = hit.clip->ctype == Clip::CType::Audio ? hit.clip->offsetP->floatValue() : 0.0;
            dragClipPre = hit.clip->save(); // automations may get trimmed
            break;
        }
        case Hit::CAHeader:
        {
            // expand / collapse the automation row (smooth: uiAnim eases,
            // block + track heights follow)
            hit.cauto->expanded = !hit.cauto->expanded;
            hit.clip->select();
            break;
        }
        case Hit::CAArm:
        {
            hit.cauto->recArm = !hit.cauto->recArm;
            if (!hit.cauto->recArm && hit.cauto->recording)
                hit.cauto->stopRecordingAndApply();
            hit.clip->select();
            break;
        }
        case Hit::CAKey:
        {
            ClipAutomation* a = hit.cauto;
            if (io.KeyCtrl)
            {
                if (a->selectedKeys.count(hit.keyId)) a->selectedKeys.erase(hit.keyId);
                else a->selectedKeys.insert(hit.keyId);
            }
            else if (!a->selectedKeys.count(hit.keyId))
            {
                a->selectedKeys.clear();
                a->selectedKeys.insert(hit.keyId);
            }
            hit.clip->select();
            if (!a->selectedKeys.count(hit.keyId)) break;
            drag = Drag::CAKey;
            dragLayerId = hit.layer->id;
            dragClipId = hit.clip->id;
            dragAutoId = a->id;
            dragItemId = hit.keyId;
            preEditJson = a->keysToJson();
            break;
        }
        case Hit::CABezA1:
        case Hit::CABezA2:
        {
            drag = hit.kind == Hit::CABezA1 ? Drag::CABez1 : Drag::CABez2;
            dragLayerId = hit.layer->id;
            dragClipId = hit.clip->id;
            dragAutoId = hit.cauto->id;
            dragItemId = hit.keyId;
            preEditJson = hit.cauto->keysToJson();
            break;
        }
        case Hit::CAEase:
        {
            drag = Drag::CAEase;
            dragLayerId = hit.layer->id;
            dragClipId = hit.clip->id;
            dragAutoId = hit.cauto->id;
            dragItemId = hit.keyId;
            preEditJson = hit.cauto->keysToJson();
            if (AutoKey* k = hit.cauto->findKey(hit.keyId))
            {
                dragOrigA = k->ep.freq;
                dragOrigB = k->ep.amp;
                dragOrigC = (double)k->ep.steps;
            }
            break;
        }
        case Hit::CAGKey:
        {
            ClipAutomation* a = hit.cauto;
            if (io.KeyCtrl)
            {
                if (a->selectedKeys.count(hit.keyId)) a->selectedKeys.erase(hit.keyId);
                else a->selectedKeys.insert(hit.keyId);
            }
            else if (!a->selectedKeys.count(hit.keyId))
            {
                a->selectedKeys.clear();
                a->selectedKeys.insert(hit.keyId);
            }
            hit.clip->select();
            if (!a->selectedKeys.count(hit.keyId)) break;
            drag = Drag::CAGKey;
            dragLayerId = hit.layer->id;
            dragClipId = hit.clip->id;
            dragAutoId = a->id;
            dragItemId = hit.keyId;
            preEditJson = a->keysToJson();
            break;
        }
        case Hit::CACurve:
        {
            ClipAutomation* a = hit.cauto;
            double localT = std::max(0.0, std::min(hit.clip->length(),
                                                   xToTime(mouse.x) - hit.clip->start()));
            if (dbl && !pencilMode)
            {
                // double-click adds a key (curve-shape preserving)
                json pre = a->keysToJson();
                AutoKey* k = nullptr;
                if (a->keys.size() >= 2 && localT > a->keys.front().time &&
                    localT < a->keys.back().time)
                    k = a->insertKeyAt(localT);
                else
                {
                    std::vector<AutoRowGeom> rows;
                    float cy0, cy1;
                    const LaneGeom* g = geomOf(hit.layer);
                    clipRectY(*g, *hit.clip, cy0, cy1);
                    buildAutoRows(*hit.clip, cy0, rows);
                    float nv = 0.5f;
                    for (auto& rg : rows)
                        if (rg.a == a) nv = caRowYToNorm(rg, mouse.y);
                    k = a->addKey(localT, a->rangeMin + nv * (a->rangeMax - a->rangeMin));
                }
                a->selectedKeys.clear();
                a->selectedKeys.insert(k->id);
                hit.clip->select();
                pushClipAutoKeysEdit(sp, hit.clip->id, a->id, pre, a->keysToJson(), "Add Key");
                break;
            }
            if (pencilMode)
            {
                // pencil: draw the curve freehand across the editor
                drag = Drag::PencilClip;
                dragLayerId = hit.layer->id;
                dragClipId = hit.clip->id;
                dragAutoId = a->id;
                preEditJson = a->keysToJson();
                pencilPts.clear();
                break;
            }
            hit.clip->select();
            break;
        }
        case Hit::CAGrad:
        {
            ClipAutomation* a = hit.cauto;
            double localT = std::max(0.0, std::min(hit.clip->length(),
                                                   xToTime(mouse.x) - hit.clip->start()));
            if (dbl)
            {
                json pre = a->keysToJson();
                GradKey* k = a->addGradKey(localT, a->colorAt(localT));
                a->selectedKeys.clear();
                a->selectedKeys.insert(k->id);
                hit.clip->select();
                pushClipAutoKeysEdit(sp, hit.clip->id, a->id, pre, a->keysToJson(), "Add Color Key");
                break;
            }
            hit.clip->select();
            break;
        }
        case Hit::AKey:
        {
            auto* al = static_cast<AutomationLayer*>(hit.layer);
            if (io.KeyCtrl)
            {
                if (al->selectedKeys.count(hit.keyId)) al->selectedKeys.erase(hit.keyId);
                else al->selectedKeys.insert(hit.keyId);
            }
            else if (!al->selectedKeys.count(hit.keyId))
            {
                al->selectedKeys.clear();
                al->selectedKeys.insert(hit.keyId);
            }
            al->select();
            if (!al->selectedKeys.count(hit.keyId)) break;
            drag = Drag::AutoKey;
            dragLayerId = al->id;
            dragItemId = hit.keyId;
            preEditJson = al->keysToJson();
            if (AutoKey* k = al->findKey(hit.keyId)) { dragOrigA = k->time; dragOrigF = k->value; }
            break;
        }
        case Hit::BezA1:
        case Hit::BezA2:
        {
            auto* al = static_cast<AutomationLayer*>(hit.layer);
            drag = (hit.kind == Hit::BezA1) ? Drag::BezierA1 : Drag::BezierA2;
            dragLayerId = al->id;
            dragItemId = hit.keyId;
            preEditJson = al->keysToJson();
            break;
        }
        case Hit::EaseHandle:
        {
            auto* al = static_cast<AutomationLayer*>(hit.layer);
            drag = Drag::EaseHandle;
            dragLayerId = al->id;
            dragItemId = hit.keyId;
            preEditJson = al->keysToJson();
            if (AutoKey* k = al->findKey(hit.keyId))
            {
                dragOrigA = k->ep.freq;
                dragOrigB = k->ep.amp;
                dragOrigC = (double)k->ep.steps;
            }
            break;
        }
        case Hit::GKey:
        {
            auto* gl = static_cast<GradientLayer*>(hit.layer);
            if (io.KeyCtrl)
            {
                if (gl->selectedKeys.count(hit.keyId)) gl->selectedKeys.erase(hit.keyId);
                else gl->selectedKeys.insert(hit.keyId);
            }
            else if (!gl->selectedKeys.count(hit.keyId))
            {
                gl->selectedKeys.clear();
                gl->selectedKeys.insert(hit.keyId);
            }
            gl->select();
            if (!gl->selectedKeys.count(hit.keyId)) break;
            drag = Drag::GradKey;
            dragLayerId = gl->id;
            dragItemId = hit.keyId;
            preEditJson = gl->keysToJson();
            if (GradKey* k = gl->findKey(hit.keyId)) dragOrigA = k->time;
            break;
        }
        case Hit::TKey:
        {
            auto* tl = static_cast<TriggerLayer*>(hit.layer);
            if (io.KeyCtrl)
            {
                if (tl->selectedKeys.count(hit.keyId)) tl->selectedKeys.erase(hit.keyId);
                else tl->selectedKeys.insert(hit.keyId);
            }
            else if (!tl->selectedKeys.count(hit.keyId))
            {
                tl->selectedKeys.clear();
                tl->selectedKeys.insert(hit.keyId);
            }
            tl->select();
            if (!tl->selectedKeys.count(hit.keyId)) break;
            drag = Drag::TriggerKey;
            dragLayerId = tl->id;
            dragItemId = hit.keyId;
            preEditJson = tl->keysToJson();
            break;
        }
        case Hit::Lane:
        {
            double t = snapTime(seq, xToTime(mouse.x), bypassSnap);
            if (dbl && hit.layer)
            {
                if (auto* cl = dynamic_cast<ClipLayer*>(hit.layer))
                {
                    double len = std::max(snapStep(seq, pps) * 4.0, 1.0);
                    // blocks never stack: land in the nearest free spot
                    Clip* c = cl->addClip(Clip::CType::Block, "Clip",
                                          cl->resolveOverlap(t, len), len);
                    pushClipAdded(sp, cl->id, c->save(), "Add Clip");
                    c->select();
                }
                else if (auto* al = dynamic_cast<AutomationLayer*>(hit.layer))
                {
                    json pre = al->keysToJson();
                    AutoKey* k = nullptr;
                    if (al->keys.size() >= 2 && t > al->keys.front().time && t < al->keys.back().time)
                        k = al->insertKeyAt(t); // preserves curve shape
                    else
                    {
                        float mn = al->rangeMinP->floatValue(), mx = al->rangeMaxP->floatValue();
                        float v = mn + std::max(0.f, std::min(1.f, yToNorm(*laneAtY(mouse.y), mouse.y))) * (mx - mn);
                        k = al->addKey(t, v);
                    }
                    al->selectedKeys.clear();
                    al->selectedKeys.insert(k->id);
                    al->select();
                    pushKeysEdit(sp, al->id, pre, al->keysToJson(), "Add Key");
                }
                else if (auto* gl = dynamic_cast<GradientLayer*>(hit.layer))
                {
                    json pre = gl->keysToJson();
                    GradKey* k = gl->addKey(t, gl->colorAt(t));
                    gl->selectedKeys.clear();
                    gl->selectedKeys.insert(k->id);
                    gl->select();
                    pushKeysEdit(sp, gl->id, pre, gl->keysToJson(), "Add Color Key");
                }
                else if (auto* tl = dynamic_cast<TriggerLayer*>(hit.layer))
                {
                    json pre = tl->keysToJson();
                    TimeTrigger* tt = tl->addTrigger(t);
                    tl->selectedKeys.clear();
                    tl->selectedKeys.insert(tt->id);
                    tl->select();
                    pushKeysEdit(sp, tl->id, pre, tl->keysToJson(), "Add Trigger");
                }
                break;
            }
            // pencil mode draws on automation lanes too
            if (pencilMode && dynamic_cast<AutomationLayer*>(hit.layer))
            {
                auto* al = static_cast<AutomationLayer*>(hit.layer);
                drag = Drag::PencilLane;
                dragLayerId = al->id;
                preEditJson = al->keysToJson();
                pencilPts.clear();
                al->select();
                break;
            }
            // rubber band start
            drag = Drag::Rubber;
            rubberStart = mouse;
            rubberAdd = io.KeyCtrl;
            rubberBaseSel = rubberAdd ? Selection::get().items : std::vector<Inspectable*>();
            rubberBaseKeys.clear();
            for (auto& l : seq.layers)
            {
                if (auto* al = dynamic_cast<AutomationLayer*>(l.get()))
                    rubberBaseKeys[l->id] = rubberAdd ? al->selectedKeys : std::set<uint64_t>();
                else if (auto* gl = dynamic_cast<GradientLayer*>(l.get()))
                    rubberBaseKeys[l->id] = rubberAdd ? gl->selectedKeys : std::set<uint64_t>();
                else if (auto* tl = dynamic_cast<TriggerLayer*>(l.get()))
                    rubberBaseKeys[l->id] = rubberAdd ? tl->selectedKeys : std::set<uint64_t>();
            }
            if (!rubberAdd)
            {
                Selection::get().clear();
                for (auto& l : seq.layers)
                {
                    if (auto* al = dynamic_cast<AutomationLayer*>(l.get())) al->selectedKeys.clear();
                    else if (auto* gl = dynamic_cast<GradientLayer*>(l.get())) gl->selectedKeys.clear();
                    else if (auto* tl = dynamic_cast<TriggerLayer*>(l.get())) tl->selectedKeys.clear();
                    if (auto* cl2 = dynamic_cast<ClipLayer*>(l.get()))
                        for (auto& c : cl2->clips)
                            for (auto& a : c->automations) a->selectedKeys.clear();
                }
            }
            break;
        }
        default:
        {
            if (!io.KeyCtrl) Selection::get().clear();
            drag = Drag::Rubber;
            rubberStart = mouse;
            rubberAdd = io.KeyCtrl;
            rubberBaseSel = rubberAdd ? Selection::get().items : std::vector<Inspectable*>();
            rubberBaseKeys.clear();
            break;
        }
        }
    }

    // ============================================================ DRAG UPDATE
    if (drag != Drag::None && drag != Drag::Scrub && drag != Drag::PanH && drag != Drag::HScroll &&
        drag != Drag::Cue && drag != Drag::LoopIn && drag != Drag::LoopOut)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (std::fabs(mouse.x - dragStartMouse.x) + std::fabs(mouse.y - dragStartMouse.y) > 3.f)
                dragMoved = true;

            if (drag != Drag::Rubber && drag != Drag::LayerHeight && drag != Drag::LayerReorder && dragMoved)
            {
                if (mouse.x > cLaneX1 - 15) viewStart += (mouse.x - (cLaneX1 - 15)) * 0.4 / pps * (io.DeltaTime * 60.0);
                if (mouse.x < cLaneX0 + 15) viewStart = std::max(0.0, viewStart - ((cLaneX0 + 15) - mouse.x) * 0.4 / pps * (io.DeltaTime * 60.0));
            }

            switch (drag)
            {
            case Drag::MoveClips:
            {
                if (!dragMoved) break;
                ClipRef* grabRef = nullptr;
                for (auto& r : dragClips) if (r.clip == dragItemId) grabRef = &r;
                if (!grabRef) break;
                double target = snapTime(seq, xToTime(mouse.x) - dragGrabDT, bypassSnap);
                // magnet: try both edges of the grabbed clip
                if (!bypassSnap && magnetEnabled)
                {
                    Clip* grabbed = seq.findClip(dragItemId);
                    std::vector<uint64_t> ignore;
                    for (auto& r : dragClips) ignore.push_back(r.clip);
                    bool s1 = false, s2 = false;
                    double m1 = magnetTime(seq, xToTime(mouse.x) - dragGrabDT, pps, ignore, s1);
                    double m2 = grabbed ? magnetTime(seq, xToTime(mouse.x) - dragGrabDT + grabbed->length(), pps, ignore, s2) : 0;
                    if (s1) { target = m1; magnetGuides.push_back(m1); }
                    else if (s2 && grabbed) { target = m2 - grabbed->length(); magnetGuides.push_back(m2); }
                }
                double delta = target - grabRef->start;
                for (auto& r : dragClips)
                    delta = std::max(delta, -r.start);

                // blocks on one track never overlap in time (conflicting
                // effects): clamp the common delta so every dragged clip
                // lands flush against its neighbours instead of on top
                std::vector<uint64_t> draggedIds;
                for (auto& r : dragClips) draggedIds.push_back(r.clip);
                for (auto& r : dragClips)
                {
                    ClipLayer* owner = nullptr;
                    Clip* c = seq.findClip(r.clip, &owner);
                    if (!c || !owner) continue;
                    const double len = c->length();
                    for (auto& o : owner->clips)
                    {
                        bool isDragged = false;
                        for (uint64_t idd : draggedIds) if (idd == o->id) isDragged = true;
                        if (isDragged) continue;
                        if (o->start() >= r.start + len - 1e-9)      // ahead
                            delta = std::min(delta, o->start() - (r.start + len));
                        else if (o->end() <= r.start + 1e-9)         // behind
                            delta = std::max(delta, o->end() - r.start);
                    }
                }

                for (auto& r : dragClips)
                    if (Clip* c = seq.findClip(r.clip))
                        c->startP->setValue((float)(r.start + delta));

                const LaneGeom* tg = laneAtY(mouse.y);
                if (tg && tg->layer != stickyL && dynamic_cast<ClipLayer*>(tg->layer))
                {
                    Clip* grabbed = seq.findClip(dragItemId);
                    int curIdx = grabbed ? seq.layerIndex(grabbed->layer) : -1;
                    int lDelta = tg->index - curIdx;
                    if (lDelta != 0 && grabbed)
                    {
                        bool ok = true;
                        for (auto& r : dragClips)
                        {
                            Clip* c = seq.findClip(r.clip);
                            if (!c) { ok = false; break; }
                            int ni = seq.layerIndex(c->layer) + lDelta;
                            if (ni < 0 || ni >= (int)seq.layers.size() ||
                                seq.layers[ni]->id == stickyLayerId || // audio lane: no effects
                                !dynamic_cast<ClipLayer*>(seq.layers[ni].get())) { ok = false; break; }
                            // target track must have room — no overlaps
                            auto* tcl = static_cast<ClipLayer*>(seq.layers[ni].get());
                            if (!tcl->spanFree(c->start(), c->end(), draggedIds)) { ok = false; break; }
                        }
                        if (ok)
                        {
                            for (auto& r : dragClips)
                            {
                                Clip* c = seq.findClip(r.clip);
                                int ni = seq.layerIndex(c->layer) + lDelta;
                                physMoveClip(seq, r.clip, seq.layers[ni]->id);
                            }
                        }
                    }
                }
                // dragging a clip BELOW the last layer: releasing there
                // creates a fresh layer for it — no way otherwise to stack
                // a second effect under the first without pre-creating the
                // track by hand
                dragBelowLanes = !tg && mouse.y >= flowBottom - LANE_GAP &&
                                 mouse.x > cLaneX0;
                break;
            }
            case Drag::ResizeL:
            {
                Clip* c = seq.findClip(dragItemId);
                if (!c) break;
                double t = snapTime(seq, xToTime(mouse.x), bypassSnap);
                if (!bypassSnap && magnetEnabled)
                {
                    bool sn = false;
                    double m = magnetTime(seq, xToTime(mouse.x), pps, { c->id }, sn);
                    if (sn) { t = m; magnetGuides.push_back(m); }
                }
                t = std::min(t, dragOrigA + dragOrigB - 0.05);
                t = std::max(0.0, t);
                // stop flush against the previous block on this track
                if (c->layer)
                    for (auto& o : c->layer->clips)
                        if (o->id != c->id && o->end() <= dragOrigA + 1e-9)
                            t = std::max(t, o->end());
                double d = t - dragOrigA;
                c->startP->setValue((float)t);
                c->lengthP->setValue((float)(dragOrigB - d));
                if (c->ctype == Clip::CType::Audio)
                    c->offsetP->setValue((float)std::max(0.0, dragOrigC + d));
                // embedded automations stay glued to the timeline while the
                // left edge trims: locals shift by -d (restored from the
                // drag-start snapshot each frame — no cumulative drift)
                if (c->ctype == Clip::CType::Block && !c->automations.empty() &&
                    dragClipPre.contains("autos"))
                {
                    size_t ai = 0;
                    for (auto& a : c->automations)
                    {
                        if (ai >= dragClipPre["autos"].size()) break;
                        const json& aj = dragClipPre["autos"][ai++];
                        if (aj.contains("keys")) a->keysFromJson(aj["keys"]);
                        for (auto& k : a->keys)  k.time -= d;
                        for (auto& k : a->gkeys) k.time -= d;
                        a->sortKeys();
                    }
                }
                break;
            }
            case Drag::ResizeR:
            {
                Clip* c = seq.findClip(dragItemId);
                if (!c) break;
                double t = snapTime(seq, xToTime(mouse.x), bypassSnap);
                if (!bypassSnap && magnetEnabled)
                {
                    bool sn = false;
                    double m = magnetTime(seq, xToTime(mouse.x), pps, { c->id }, sn);
                    if (sn) { t = m; magnetGuides.push_back(m); }
                }
                // stop flush against the next block on this track
                if (c->layer)
                    for (auto& o : c->layer->clips)
                        if (o->id != c->id && o->start() >= dragOrigA + dragOrigB - 1e-9)
                            t = std::min(t, o->start());
                c->lengthP->setValue((float)std::max(0.05, t - dragOrigA));
                break;
            }
            case Drag::FadeIn:
            case Drag::FadeOut:
            {
                Clip* c = seq.findClip(dragItemId);
                if (!c || c->ctype != Clip::CType::Audio) break;
                if (drag == Drag::FadeIn)
                    c->fadeInP->setValue((float)std::max(0.0, std::min(c->length(), xToTime(mouse.x) - c->start())));
                else
                    c->fadeOutP->setValue((float)std::max(0.0, std::min(c->length(), c->end() - xToTime(mouse.x))));
                break;
            }
            case Drag::AutoKey:
            {
                auto* al = dynamic_cast<AutomationLayer*>(seq.findLayer(dragLayerId));
                const LaneGeom* g = al ? geomOf(al) : nullptr;
                if (!al || !g) break;
                if (AutoKey* k = al->findKey(dragItemId))
                {
                    double t = snapTime(seq, xToTime(mouse.x), bypassSnap);
                    if (!bypassSnap && magnetEnabled)
                    {
                        bool sn = false;
                        double m = magnetTime(seq, xToTime(mouse.x), pps, {}, sn);
                        if (sn) { t = m; magnetGuides.push_back(m); }
                    }
                    k->time = std::max(0.0, t);
                    float mn = al->rangeMinP->floatValue(), mx = al->rangeMaxP->floatValue();
                    float nv = std::max(0.f, std::min(1.f, yToNorm(*g, mouse.y)));
                    k->value = mn + nv * (mx - mn);
                    al->sortKeys();
                }
                break;
            }
            case Drag::BezierA1:
            case Drag::BezierA2:
            {
                auto* al = dynamic_cast<AutomationLayer*>(seq.findLayer(dragLayerId));
                const LaneGeom* g = al ? geomOf(al) : nullptr;
                if (!al || !g) break;
                AutoKey* k = al->findKey(dragItemId);
                if (!k) break;
                AutoKey* nk = nullptr;
                for (size_t i = 0; i + 1 < al->keys.size(); i++)
                    if (al->keys[i].id == k->id) { nk = &al->keys[i + 1]; break; }
                if (!nk) break;
                double segDur = std::max(1e-4, nk->time - k->time);
                float mn = al->rangeMinP->floatValue(), mx = al->rangeMaxP->floatValue();
                float range = (mx - mn) == 0 ? 1.f : (mx - mn);
                float na = (k->value - mn) / range, nb = (nk->value - mn) / range;
                float nMouse = yToNorm(*g, mouse.y);
                if (drag == Drag::BezierA1)
                {
                    k->ep.a1.x = (float)std::max(0.0, std::min(1.0, (xToTime(mouse.x) - k->time) / segDur));
                    k->ep.a1.y = nMouse - na;
                }
                else
                {
                    k->ep.a2.x = (float)std::max(-1.0, std::min(0.0, (xToTime(mouse.x) - nk->time) / segDur));
                    k->ep.a2.y = nMouse - nb;
                }
                break;
            }
            case Drag::EaseHandle:
            {
                auto* al = dynamic_cast<AutomationLayer*>(seq.findLayer(dragLayerId));
                if (!al) break;
                AutoKey* k = al->findKey(dragItemId);
                if (!k) break;
                float dx = mouse.x - dragStartMouse.x;
                float dy = mouse.y - dragStartMouse.y;
                if (k->easing == EasingType::Steps)
                    k->ep.steps = std::max(1, std::min(64, (int)dragOrigC + (int)(dx / 14.f)));
                else
                {
                    k->ep.freq = std::max(0.1f, std::min(50.f, (float)dragOrigA * std::pow(2.f, dx / 70.f)));
                    if (k->easing != EasingType::Elastic)
                        k->ep.amp = std::max(0.f, std::min(2.f, (float)dragOrigB - dy / 90.f));
                }
                break;
            }
            case Drag::KeyBoxMove:
            case Drag::KeyBoxL:
            case Drag::KeyBoxR:
            case Drag::KeyBoxT:
            case Drag::KeyBoxB:
            {
                auto* al = dynamic_cast<AutomationLayer*>(seq.findLayer(dragLayerId));
                const LaneGeom* g = al ? geomOf(al) : nullptr;
                if (!al || !g || keyBoxRefs.empty()) break;
                float mn = al->rangeMinP->floatValue(), mx = al->rangeMaxP->floatValue();
                double spanT = std::max(1e-6, keyBoxT1 - keyBoxT0);
                float spanV = std::max(1e-6f, keyBoxV1 - keyBoxV0);

                if (drag == Drag::KeyBoxMove)
                {
                    double dt = (mouse.x - dragStartMouse.x) / pps;
                    dt = snapTime(seq, keyBoxT0 + dt, bypassSnap) - keyBoxT0;
                    dt = std::max(dt, -keyBoxT0);
                    float dvNorm = yToNorm(*g, mouse.y) - yToNorm(*g, dragStartMouse.y);
                    float dv = dvNorm * (mx - mn);
                    for (auto& r : keyBoxRefs)
                        if (AutoKey* k = al->findKey(r.id))
                        {
                            k->time = r.t + dt;
                            k->value = std::max(std::min(mn, mx), std::min(std::max(mn, mx), r.v + dv));
                        }
                }
                else if (drag == Drag::KeyBoxL || drag == Drag::KeyBoxR)
                {
                    double t = snapTime(seq, xToTime(mouse.x), bypassSnap);
                    double n0 = keyBoxT0, n1 = keyBoxT1;
                    if (drag == Drag::KeyBoxL) n0 = std::min(t, keyBoxT1 - 0.01);
                    else                       n1 = std::max(t, keyBoxT0 + 0.01);
                    n0 = std::max(0.0, n0);
                    for (auto& r : keyBoxRefs)
                        if (AutoKey* k = al->findKey(r.id))
                            k->time = n0 + (r.t - keyBoxT0) / spanT * (n1 - n0);
                }
                else // T / B: scale values
                {
                    float vAtMouse = mn + yToNorm(*g, mouse.y) * (mx - mn);
                    float n0 = keyBoxV0, n1 = keyBoxV1;
                    if (drag == Drag::KeyBoxT) n1 = vAtMouse;
                    else                       n0 = vAtMouse;
                    for (auto& r : keyBoxRefs)
                        if (AutoKey* k = al->findKey(r.id))
                        {
                            float w = (float)((r.v - keyBoxV0) / spanV);
                            float nv = n0 + w * (n1 - n0);
                            k->value = std::max(std::min(mn, mx), std::min(std::max(mn, mx), nv));
                        }
                }
                al->sortKeys();
                break;
            }
            case Drag::GradKey:
            {
                auto* gl = dynamic_cast<GradientLayer*>(seq.findLayer(dragLayerId));
                if (!gl) break;
                if (GradKey* k = gl->findKey(dragItemId))
                {
                    k->time = snapTime(seq, xToTime(mouse.x), bypassSnap);
                    gl->sortKeys();
                }
                break;
            }
            case Drag::TriggerKey:
            {
                auto* tl = dynamic_cast<TriggerLayer*>(seq.findLayer(dragLayerId));
                const LaneGeom* g = tl ? geomOf(tl) : nullptr;
                if (!tl || !g) break;
                if (TimeTrigger* t = tl->findTrigger(dragItemId))
                {
                    double tt = snapTime(seq, xToTime(mouse.x), bypassSnap);
                    if (!bypassSnap && magnetEnabled)
                    {
                        bool sn = false;
                        double m = magnetTime(seq, xToTime(mouse.x), pps, {}, sn);
                        if (sn) { tt = m; magnetGuides.push_back(m); }
                    }
                    t->time = std::max(0.0, tt);
                    float laneH = g->y1 - g->y0;
                    t->flagY = std::max(0.f, std::min(1.f, (mouse.y - g->y0 - 5) / std::max(1.f, laneH - 22)));
                    tl->sortTriggers();
                }
                break;
            }
            case Drag::CAKey:
            case Drag::CABez1:
            case Drag::CABez2:
            case Drag::CAEase:
            case Drag::CAGKey:
            case Drag::PencilClip:
            {
                Clip* c = seq.findClip(dragClipId);
                ClipAutomation* a = c ? c->findAutomation(dragAutoId) : nullptr;
                const LaneGeom* g = (c && c->layer) ? geomOf(c->layer) : nullptr;
                if (!c || !a || !g) break;
                float cy0, cy1;
                clipRectY(*g, *c, cy0, cy1);
                std::vector<AutoRowGeom> rows;
                buildAutoRows(*c, cy0, rows);
                const AutoRowGeom* rg = nullptr;
                for (auto& r : rows) if (r.a == a) rg = &r;
                if (!rg) break;
                const double localMouse =
                    std::max(0.0, std::min(c->length(), xToTime(mouse.x) - c->start()));

                if (drag == Drag::CAKey)
                {
                    if (AutoKey* k = a->findKey(dragItemId))
                    {
                        double tAbs = snapTime(seq, xToTime(mouse.x), bypassSnap);
                        k->time = std::max(0.0, std::min(c->length(), tAbs - c->start()));
                        float nv = caRowYToNorm(*rg, mouse.y);
                        k->value = a->rangeMin + nv * (a->rangeMax - a->rangeMin);
                        a->sortKeys();
                    }
                }
                else if (drag == Drag::CABez1 || drag == Drag::CABez2)
                {
                    AutoKey* k = a->findKey(dragItemId);
                    if (!k) break;
                    AutoKey* nk = nullptr;
                    for (size_t i = 0; i + 1 < a->keys.size(); i++)
                        if (a->keys[i].id == k->id) { nk = &a->keys[i + 1]; break; }
                    if (!nk) break;
                    double segDur = std::max(1e-4, nk->time - k->time);
                    float rr = (a->rangeMax - a->rangeMin) == 0 ? 1.f : (a->rangeMax - a->rangeMin);
                    float na = (k->value - a->rangeMin) / rr, nb = (nk->value - a->rangeMin) / rr;
                    float nMouse = caRowYToNorm(*rg, mouse.y);
                    if (drag == Drag::CABez1)
                    {
                        k->ep.a1.x = (float)std::max(0.0, std::min(1.0, (localMouse - k->time) / segDur));
                        k->ep.a1.y = nMouse - na;
                    }
                    else
                    {
                        k->ep.a2.x = (float)std::max(-1.0, std::min(0.0, (localMouse - nk->time) / segDur));
                        k->ep.a2.y = nMouse - nb;
                    }
                }
                else if (drag == Drag::CAEase)
                {
                    AutoKey* k = a->findKey(dragItemId);
                    if (!k) break;
                    float dx = mouse.x - dragStartMouse.x;
                    float dy = mouse.y - dragStartMouse.y;
                    if (k->easing == EasingType::Steps)
                        k->ep.steps = std::max(1, std::min(64, (int)dragOrigC + (int)(dx / 14.f)));
                    else
                    {
                        k->ep.freq = std::max(0.1f, std::min(50.f, (float)dragOrigA * std::pow(2.f, dx / 70.f)));
                        if (k->easing != EasingType::Elastic)
                            k->ep.amp = std::max(0.f, std::min(2.f, (float)dragOrigB - dy / 90.f));
                    }
                }
                else if (drag == Drag::CAGKey)
                {
                    if (GradKey* k = a->findGradKey(dragItemId))
                    {
                        double tAbs = snapTime(seq, xToTime(mouse.x), bypassSnap);
                        k->time = std::max(0.0, std::min(c->length(), tAbs - c->start()));
                        a->sortKeys();
                    }
                }
                else // PencilClip: collect the freehand stroke
                {
                    float nv = caRowYToNorm(*rg, mouse.y);
                    float v = a->rangeMin + nv * (a->rangeMax - a->rangeMin);
                    // paint-over: drawing backwards replaces what was drawn
                    while (!pencilPts.empty() && pencilPts.back().first >= localMouse)
                        pencilPts.pop_back();
                    pencilPts.push_back({ localMouse, v });
                }
                break;
            }
            case Drag::PencilLane:
            {
                auto* al = dynamic_cast<AutomationLayer*>(seq.findLayer(dragLayerId));
                const LaneGeom* g = al ? geomOf(al) : nullptr;
                if (!al || !g) break;
                double t = std::max(0.0, xToTime(mouse.x));
                float mn = al->rangeMinP->floatValue(), mx = al->rangeMaxP->floatValue();
                float v = mn + std::max(0.f, std::min(1.f, yToNorm(*g, mouse.y))) * (mx - mn);
                while (!pencilPts.empty() && pencilPts.back().first >= t)
                    pencilPts.pop_back();
                pencilPts.push_back({ t, v });
                break;
            }
            case Drag::LayerHeight:
            {
                if (Layer* l = seq.findLayer(dragLayerId))
                    l->uiHeight = std::max(26.f, std::min(400.f, dragOrigF + (mouse.y - dragStartMouse.y)));
                break;
            }
            case Drag::LayerReorder:
            {
                if (!dragMoved) break;
                reorderTarget = (int)seq.layers.size();
                for (auto& g : geoms)
                {
                    if (g.layer == stickyL) continue; // pinned: not a seat
                    if (mouse.y < (g.y0 + g.y1) * 0.5f) { reorderTarget = g.index; break; }
                }
                break;
            }
            case Drag::Rubber:
            {
                ImVec2 rMin(std::min(rubberStart.x, mouse.x), std::min(rubberStart.y, mouse.y));
                ImVec2 rMax(std::max(rubberStart.x, mouse.x), std::max(rubberStart.y, mouse.y));
                // clips are PREselected while the band is dragged (committed on release)
                std::vector<Inspectable*> pre;
                for (auto& g : geoms)
                {
                    if (g.y1 < rMin.y || g.y0 > rMax.y) continue;
                    if (auto* cl = dynamic_cast<ClipLayer*>(g.layer))
                    {
                        for (auto& c : cl->clips)
                        {
                            float x0 = timeToX(c->start()), x1 = timeToX(c->end());
                            if (x1 >= rMin.x && x0 <= rMax.x)
                                pre.push_back(c.get());
                        }
                    }
                    else if (auto* al = dynamic_cast<AutomationLayer*>(g.layer))
                    {
                        float mn = al->rangeMinP->floatValue(), mx = al->rangeMaxP->floatValue();
                        float range = (mx - mn) == 0 ? 1.f : (mx - mn);
                        auto base = rubberBaseKeys.count(al->id) ? rubberBaseKeys[al->id] : std::set<uint64_t>();
                        al->selectedKeys = base;
                        for (auto& k : al->keys)
                        {
                            ImVec2 kp(timeToX(k.time), normToY(g, (k.value - mn) / range));
                            if (inRect(kp, rMin, rMax)) al->selectedKeys.insert(k.id);
                        }
                    }
                    else if (auto* gl = dynamic_cast<GradientLayer*>(g.layer))
                    {
                        auto base = rubberBaseKeys.count(gl->id) ? rubberBaseKeys[gl->id] : std::set<uint64_t>();
                        gl->selectedKeys = base;
                        for (auto& k : gl->keys)
                        {
                            ImVec2 kp(timeToX(k.time), g.y1 - 7.f);
                            if (inRect(kp, rMin, rMax)) gl->selectedKeys.insert(k.id);
                        }
                    }
                    else if (auto* tl = dynamic_cast<TriggerLayer*>(g.layer))
                    {
                        auto base = rubberBaseKeys.count(tl->id) ? rubberBaseKeys[tl->id] : std::set<uint64_t>();
                        tl->selectedKeys = base;
                        float laneH = g.y1 - g.y0;
                        for (auto& t : tl->triggers)
                        {
                            ImVec2 kp(timeToX(t.time), g.y0 + 5 + t.flagY * (laneH - 22));
                            if (inRect(kp, rMin, rMax)) tl->selectedKeys.insert(t.id);
                        }
                    }
                }
                Selection::get().setPreselection(pre);
                break;
            }
            default: break;
            }
        }
        else
        {
            // ======================================================== RELEASE
            switch (drag)
            {
            case Drag::MoveClips:
            {
                if (dragMoved)
                {
                    // released below the last layer: a fresh layer is born
                    // for the dragged clips (single undo step with the move)
                    json newLayerJson;
                    uint64_t newLayerId = 0;
                    if (dragBelowLanes)
                    {
                        auto* nl = static_cast<ClipLayer*>(
                            seq.addLayer(Layer::LType::Clips, "Layer"));
                        if (nl)
                        {
                            newLayerId = nl->id;
                            newLayerJson = nl->save(); // EMPTY snapshot — redo
                                                       // recreates the layer,
                                                       // the move recs carry
                                                       // the clips (saving
                                                       // after the move would
                                                       // duplicate ids)
                            for (auto& r : dragClips)
                            {
                                Clip* c = seq.findClip(r.clip);
                                if (!c) continue;
                                physMoveClip(seq, r.clip, nl->id);
                                // several dragged clips may collide on the
                                // fresh track — seat them flush
                                c->startP->setValue((float)nl->resolveOverlap(
                                    c->start(), c->length(), { c->id }), false);
                            }
                        }
                    }

                    struct MoveRec { uint64_t clip, oldLayer, newLayer; double oldStart, newStart; };
                    std::vector<MoveRec> recs;
                    for (auto& r : dragClips)
                    {
                        ClipLayer* nl = nullptr;
                        Clip* c = seq.findClip(r.clip, &nl);
                        if (!c) continue;
                        recs.push_back({ r.clip, r.layer, nl->id, r.start, c->start() });
                    }
                    for (auto& l : seq.layers)
                        if (auto* cl = dynamic_cast<ClipLayer*>(l.get())) cl->sortClips();
                    UndoManager::get().pushDone("Move Clips",
                        [sp, recs, newLayerJson]
                        {
                            if (!newLayerJson.is_null() && !newLayerJson.empty())
                                sp->addLayerFromJson(newLayerJson, -1);
                            for (auto& r : recs)
                            {
                                physMoveClip(*sp, r.clip, r.newLayer);
                                if (Clip* c = sp->findClip(r.clip)) c->startP->setValue((float)r.newStart);
                            }
                        },
                        [sp, recs, newLayerId]
                        {
                            for (auto& r : recs)
                            {
                                physMoveClip(*sp, r.clip, r.oldLayer);
                                if (Clip* c = sp->findClip(r.clip)) c->startP->setValue((float)r.oldStart);
                            }
                            if (newLayerId) sp->removeLayer(newLayerId); // now empty
                        },
                        { sp });
                }
                dragBelowLanes = false;
                break;
            }
            case Drag::ResizeL:
            case Drag::ResizeR:
            {
                Clip* c = seq.findClip(dragItemId);
                if (c && dragMoved)
                {
                    // nothing of an automation may outlive the block: keys
                    // trimmed off by the resize are dropped on release (the
                    // full-clip undo snapshot still restores them)
                    c->clampAutomations();
                    pushClipEdit(sp, dragItemId, dragClipPre, c->save(), "Resize Clip");
                }
                break;
            }
            case Drag::CAKey:
            case Drag::CABez1:
            case Drag::CABez2:
            case Drag::CAEase:
            case Drag::CAGKey:
            {
                Clip* c = seq.findClip(dragClipId);
                ClipAutomation* a = c ? c->findAutomation(dragAutoId) : nullptr;
                if (a && dragMoved)
                    pushClipAutoKeysEdit(sp, dragClipId, dragAutoId, preEditJson,
                                         a->keysToJson(), "Edit Keys");
                break;
            }
            case Drag::PencilClip:
            {
                Clip* c = seq.findClip(dragClipId);
                ClipAutomation* a = c ? c->findAutomation(dragAutoId) : nullptr;
                if (a)
                {
                    if (pencilPts.size() >= 2)
                        a->applyDrawnPoints(pencilPts, 1 /* RDP */, 0.05f);
                    else if (pencilPts.size() == 1) // a click = one key
                        a->addKey(pencilPts[0].first, pencilPts[0].second);
                    pushClipAutoKeysEdit(sp, dragClipId, dragAutoId, preEditJson,
                                         a->keysToJson(), "Draw Curve");
                }
                pencilPts.clear();
                break;
            }
            case Drag::PencilLane:
            {
                if (auto* al = dynamic_cast<AutomationLayer*>(seq.findLayer(dragLayerId)))
                {
                    if (pencilPts.size() >= 2)
                        al->applyDrawnPoints(pencilPts, 1 /* RDP */, 0.05f);
                    else if (pencilPts.size() == 1)
                        al->addKey(pencilPts[0].first, pencilPts[0].second);
                    pushKeysEdit(sp, al->id, preEditJson, al->keysToJson(), "Draw Curve");
                }
                pencilPts.clear();
                break;
            }
            case Drag::FadeIn:
            case Drag::FadeOut:
            {
                Clip* c = seq.findClip(dragItemId);
                if (c && dragMoved)
                {
                    uint64_t cid = dragItemId;
                    double oI = dragOrigA, oO = dragOrigB;
                    double nI = c->fadeInP->floatValue(), nO = c->fadeOutP->floatValue();
                    UndoManager::get().pushDone("Edit Fades",
                        [sp, cid, nI, nO]
                        {
                            if (Clip* cc = sp->findClip(cid))
                            { cc->fadeInP->setValue((float)nI); cc->fadeOutP->setValue((float)nO); }
                        },
                        [sp, cid, oI, oO]
                        {
                            if (Clip* cc = sp->findClip(cid))
                            { cc->fadeInP->setValue((float)oI); cc->fadeOutP->setValue((float)oO); }
                        },
                        { sp });
                }
                break;
            }
            case Drag::AutoKey:
            case Drag::BezierA1:
            case Drag::BezierA2:
            case Drag::EaseHandle:
            case Drag::KeyBoxMove:
            case Drag::KeyBoxL:
            case Drag::KeyBoxR:
            case Drag::KeyBoxT:
            case Drag::KeyBoxB:
            {
                if (auto* al = dynamic_cast<AutomationLayer*>(seq.findLayer(dragLayerId)))
                    if (dragMoved)
                        pushKeysEdit(sp, al->id, preEditJson, al->keysToJson(), "Edit Keys");
                break;
            }
            case Drag::GradKey:
            {
                if (auto* gl = dynamic_cast<GradientLayer*>(seq.findLayer(dragLayerId)))
                    if (dragMoved)
                        pushKeysEdit(sp, gl->id, preEditJson, gl->keysToJson(), "Move Color Key");
                break;
            }
            case Drag::TriggerKey:
            {
                if (auto* tl = dynamic_cast<TriggerLayer*>(seq.findLayer(dragLayerId)))
                    if (dragMoved)
                        pushKeysEdit(sp, tl->id, preEditJson, tl->keysToJson(), "Move Trigger");
                break;
            }
            case Drag::LayerReorder:
            {
                if (dragMoved && reorderTarget >= 0)
                {
                    Layer* l = seq.findLayer(dragLayerId);
                    int from = l ? seq.layerIndex(l) : -1;
                    int to = reorderTarget;
                    if (to > from) to--;
                    if (from >= 0) moveLayerUndoable(from, std::max(0, std::min((int)seq.layers.size() - 1, to)));
                }
                reorderTarget = -1;
                break;
            }
            case Drag::Rubber:
            {
                if (!dragMoved)
                {
                    Selection::get().clearPreselection();
                    if (hit.kind == Hit::Lane && hit.layer && !rubberAdd)
                        hit.layer->select();
                }
                else if (rubberAdd)
                    Selection::get().commitPreselection();
                else
                {
                    auto& s = Selection::get();
                    s.items = s.preselected;
                    s.preselected.clear();
                    s.touch();
                }
                break;
            }
            default: break;
            }
            drag = Drag::None;
            dragClips.clear();
        }
    }

    // ============================================================ RIGHT CLICK -> POPUPS
    if (canvasHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        ImGui::GetMouseDragDelta(ImGuiMouseButton_Right).x == 0 &&
        ImGui::GetMouseDragDelta(ImGuiMouseButton_Right).y == 0)
    {
        ctxTime = snapTime(seq, xToTime(mouse.x), io.KeyAlt);
        ctxLayerId = hit.layer ? hit.layer->id : 0;
        ctxItemId = 0;
        const LaneGeom* g = hit.layer ? laneAtY(mouse.y) : nullptr;
        ctxValue = g ? yToNorm(*g, mouse.y) : 0.f;

        switch (hit.kind)
        {
        case Hit::ClipBody: case Hit::ClipL: case Hit::ClipR: case Hit::FadeIn: case Hit::FadeOut:
            ctxItemId = hit.clip->id;
            if (!hit.clip->isSelected()) Selection::get().set(hit.clip);
            ImGui::OpenPopup("clip_ctx");
            break;
        case Hit::CAKey:
            ctxItemId = hit.keyId;
            ctxClipId = hit.clip->id;
            ctxAutoId = hit.cauto->id;
            ImGui::OpenPopup("cakey_ctx");
            break;
        case Hit::CAGKey:
            ctxItemId = hit.keyId;
            ctxClipId = hit.clip->id;
            ctxAutoId = hit.cauto->id;
            preEditJson = hit.cauto->keysToJson();
            ImGui::OpenPopup("cagkey_ctx");
            break;
        case Hit::CAHeader: case Hit::CAArm: case Hit::CACurve: case Hit::CAGrad:
            ctxClipId = hit.clip->id;
            ctxAutoId = hit.cauto->id;
            ctxItemId = hit.clip->id;
            ImGui::OpenPopup("carow_ctx");
            break;
        case Hit::AKey:
            ctxItemId = hit.keyId;
            ImGui::OpenPopup("akey_ctx");
            break;
        case Hit::GKey:
            ctxItemId = hit.keyId;
            preEditJson = static_cast<GradientLayer*>(hit.layer)->keysToJson();
            ImGui::OpenPopup("gkey_ctx");
            break;
        case Hit::TKey:
            ctxItemId = hit.keyId;
            ImGui::OpenPopup("tkey_ctx");
            break;
        case Hit::Lane:
            ImGui::OpenPopup("lane_ctx");
            break;
        case Hit::Header:
        case Hit::HeaderGrip:
            hit.layer->select();
            ImGui::OpenPopup("layer_ctx");
            break;
        default:
            ImGui::OpenPopup("empty_ctx");
            break;
        }
    }

    // ============================================================ DRAWING
    // grid/rubber/playhead vertical extent = the scrolling FLOW (the sticky
    // lane sits pinned on top and draws over it)
    float lanesBottom = flowBottom > canvasP0.y + LANE_GAP + stickyBandH
                            ? flowBottom - LANE_GAP
                            : canvasP0.y;

    auto drawLaneChrome = [&](const LaneGeom& g)
    {
        Layer* l = g.layer;
        bool lSel = l->isSelected();
        bool lEnabled = l->enabledP->boolValue();
        ImVec4 lcol = l->colorP->color();

        // header
        ImU32 hbg = lSel ? IM_COL32(48, 45, 40, 255) : IM_COL32(38, 38, 42, 255);
        cdl->AddRectFilled(ImVec2(canvasP0.x, g.y0), ImVec2(cLaneX0 - 2, g.y1), hbg, 4.f);
        cdl->AddRectFilled(ImVec2(canvasP0.x, g.y0), ImVec2(canvasP0.x + 3, g.y1), col32(lcol), 2.f);
        if (lSel)
            cdl->AddRect(ImVec2(canvasP0.x, g.y0), ImVec2(cLaneX0 - 2, g.y1), accentU, 4.f, 0, 1.f);
        cdl->PushClipRect(ImVec2(canvasP0.x, g.y0), ImVec2(cLaneX0 - 6, g.y1), true);
        ImU32 nameCol = lEnabled ? IM_COL32(225, 225, 228, 255) : IM_COL32(130, 130, 135, 255);
        cdl->AddText(ImVec2(canvasP0.x + 10, g.y0 + 5), nameCol, l->niceName.c_str());
        std::string typeLbl = Layer::ltypeName(l->ltype);
        if (l->id == stickyLayerId) typeLbl = "Pinned";
        if (!lEnabled) typeLbl += " (off)";
        if (auto* alr = dynamic_cast<AutomationLayer*>(l))
            if (alr->recArmP->boolValue()) typeLbl += alr->recording ? "  REC" : "  ARM";
        cdl->AddText(ImVec2(canvasP0.x + 10, g.y0 + 5 + ImGui::GetFontSize() + 1),
                     IM_COL32(140, 140, 146, 255), typeLbl.c_str());
        cdl->PopClipRect();
        cdl->AddLine(ImVec2(canvasP0.x + 6, g.y1 - 2.5f), ImVec2(cLaneX0 - 10, g.y1 - 2.5f),
                     IM_COL32(90, 90, 95, 120), 1.f);

        // lane background
        ImU32 lbg = (g.index & 1) ? IM_COL32(30, 30, 33, 255) : IM_COL32(33, 33, 36, 255);
        cdl->AddRectFilled(ImVec2(cLaneX0, g.y0), ImVec2(cLaneX1, g.y1), lbg);
        cdl->AddRectFilled(ImVec2(cLaneX0, g.y0), ImVec2(cLaneX1, g.y1), col32(lcol, 0.05f));
        if (!lEnabled)
            cdl->AddRectFilled(ImVec2(cLaneX0, g.y0), ImVec2(cLaneX1, g.y1), IM_COL32(0, 0, 0, 90));
    };
    for (auto& g : geoms) drawLaneChrome(g);

    // clip drag below the last layer → a fresh layer is born on release
    if (drag == Drag::MoveClips && dragMoved && dragBelowLanes)
    {
        float gy = lanesBottom + LANE_GAP;
        cdl->AddRectFilled(ImVec2(cLaneX0, gy), ImVec2(cLaneX1, gy + 50), IM_COL32(255, 255, 255, 14), 4.f);
        cdl->AddRect(ImVec2(cLaneX0, gy), ImVec2(cLaneX1, gy + 50), col32(accent, 0.5f), 4.f, 0, 1.5f);
        cdl->AddText(ImVec2(cLaneX0 + 8, gy + 4), IM_COL32(200, 200, 205, 220),
                     "New layer — drop here");
    }

    // layer reorder preview line
    if (drag == Drag::LayerReorder && dragMoved && reorderTarget >= 0)
    {
        // reorderTarget is a LAYER index — map to the first FLOW geom at or
        // after it (the pinned lane is out of the flow and never a seat)
        float y = lanesBottom + 1;
        for (auto& g : geoms)
        {
            if (g.layer == stickyL) continue;
            if (g.index >= reorderTarget) { y = g.y0 - 1; break; }
        }
        cdl->AddLine(ImVec2(canvasP0.x, y), ImVec2(cLaneX1, y), accentU, 2.f);
    }

    // vertical grid
    if (!geoms.empty())
    {
        cdl->PushClipRect(ImVec2(cLaneX0, canvasP0.y), ImVec2(cLaneX1, lanesBottom), true);
        double tEnd = xToTime(cLaneX1);
        double tm = std::floor(std::max(0.0, viewStart) / minorStep) * minorStep;
        for (; tm <= tEnd; tm += minorStep)
        {
            float x = timeToX(tm);
            bool isMajor = std::fabs(std::fmod(tm + minorStep * 0.5, majorStep) - minorStep * 0.5) < minorStep * 0.25;
            cdl->AddLine(ImVec2(x, canvasP0.y), ImVec2(x, lanesBottom),
                         IM_COL32(255, 255, 255, isMajor ? 14 : 6), 1.f);
        }
        float xe = timeToX(seq.totalTime());
        if (xe < cLaneX1)
            cdl->AddRectFilled(ImVec2(std::max(xe, cLaneX0), canvasP0.y), ImVec2(cLaneX1, lanesBottom),
                               IM_COL32(0, 0, 0, 70));
        // loop range shading in lanes
        if (seq.loopIn >= 0 && seq.loopOut > seq.loopIn)
        {
            cdl->AddRectFilled(ImVec2(timeToX(seq.loopIn), canvasP0.y), ImVec2(timeToX(seq.loopOut), lanesBottom),
                               col32(accent, 0.045f));
        }
        cdl->PopClipRect();
    }
    else
    {
        const char* hint = "Right-click to add layers, or drop media here";
        ImVec2 ts = ImGui::CalcTextSize(hint);
        cdl->AddText(ImVec2(canvasP0.x + (canvasW - ts.x) * 0.5f, canvasP0.y + 30),
                     IM_COL32(120, 120, 126, 255), hint);
    }

    // layer contents
    double nowT = ImGui::GetTime();
    for (auto& g : geoms)
    {
        // the pinned lane draws LAST (it is the last geom): repaint its
        // opaque backdrop first — flow lanes scroll BENEATH it — and give
        // it a drop shadow so the separation reads
        if (g.layer == stickyL)
        {
            drawLaneChrome(g);
            cdl->AddRectFilledMultiColor(ImVec2(canvasP0.x, g.y1), ImVec2(cLaneX1, g.y1 + 9.f),
                                         IM_COL32(0, 0, 0, 130), IM_COL32(0, 0, 0, 130),
                                         IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
            cdl->AddLine(ImVec2(canvasP0.x, g.y1 + 0.5f), ImVec2(cLaneX1, g.y1 + 0.5f),
                         IM_COL32(70, 70, 76, 200), 1.f);
        }
        cdl->PushClipRect(ImVec2(cLaneX0, g.y0), ImVec2(cLaneX1, g.y1), true);
        bool lEnabled = g.layer->enabledP->boolValue();

        if (auto* cl = dynamic_cast<ClipLayer*>(g.layer))
        {
            for (auto& cu : cl->clips)
            {
                Clip* c = cu.get();
                float x0 = timeToX(c->start());
                float x1 = timeToX(c->end());
                if (x1 < cLaneX0 - 2 || x0 > cLaneX1 + 2) continue;
                float y0, y1;
                clipRectY(g, *c, y0, y1);
                ImVec4 col = c->colorP->color();
                bool sel = c->isSelected();
                bool pre = c->isPreselected();
                bool hov = (hit.clip == c);
                bool cEnabled = c->enabled() && lEnabled;
                bool liveNow = seq.playing && cEnabled &&
                               seq.currentTime >= c->start() && seq.currentTime < c->end();
                ImVec4 fill = sel ? lighten(col, 0.08f) : col;
                const bool blockStyle = c->ctype == Clip::CType::Block && !c->automations.empty();
                if (blockStyle)
                {
                    // effect block: soft body, tinted title strip, inner top
                    // highlight — its automations render as rows below
                    cdl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1),
                                       col32(ImVec4(fill.x * 0.32f, fill.y * 0.32f, fill.z * 0.32f, 1.f),
                                             cEnabled ? 0.98f : 0.5f), 5.f);
                    cdl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, std::min(y1, y0 + CA_TITLE_H)),
                                       col32(fill, cEnabled ? 0.9f : 0.35f), 5.f,
                                       ImDrawFlags_RoundCornersTop);
                    cdl->AddLine(ImVec2(x0 + 3, y0 + 1), ImVec2(x1 - 3, y0 + 1),
                                 col32(lighten(fill, 0.25f), 0.5f), 1.f);
                }
                else
                {
                    cdl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), col32(fill, cEnabled ? 0.95f : 0.38f), 4.f);
                }
                if (liveNow)
                    cdl->AddRect(ImVec2(x0 - 1, y0 - 1), ImVec2(x1 + 1, y1 + 1),
                                 col32(accent, 0.28f + 0.18f * (float)std::sin(nowT * 6.0)),
                                 5.f, 0, 2.f);

                // waveform (with fade envelope + media loop tiling)
                if (c->ctype == Clip::CType::Audio && c->asset)
                {
                    float wy0 = y0 + ImGui::GetFontSize() + 6;
                    float wy1 = y1 - 2;
                    if (wy1 > wy0 + 4)
                    {
                        float mid = (wy0 + wy1) * 0.5f;
                        float half = (wy1 - wy0) * 0.5f;
                        float gain = c->gainP->floatValue();
                        double off = c->offsetP->floatValue();
                        bool loopMedia = c->loopMediaP->boolValue();
                        double assetDur = c->asset->buffer.duration();
                        float px0 = std::max(x0, cLaneX0);
                        float px1 = std::min(x1, cLaneX1);
                        ImU32 wcol = IM_COL32(255, 255, 255, cEnabled ? 110 : 45);
                        for (float px = px0; px < px1; px += 1.f)
                        {
                            double local = (px - x0) / pps;
                            double lt0 = off + local;
                            if (loopMedia && assetDur > 0.01) lt0 = std::fmod(lt0, assetDur);
                            double lt1 = lt0 + 1.0 / pps;
                            float mn, mx;
                            if (c->asset->peaks.query(lt0, lt1, mn, mx))
                            {
                                float env = gain * c->fadeGainAt(local);
                                mn = std::max(-1.f, std::min(1.f, mn * env));
                                mx = std::max(-1.f, std::min(1.f, mx * env));
                                cdl->AddLine(ImVec2(px, mid - mx * half), ImVec2(px, mid - mn * half + 1), wcol, 1.f);
                            }
                        }
                        // loop tiling separators
                        if (loopMedia && assetDur > 0.01)
                        {
                            for (double lt = assetDur - std::fmod(off, assetDur); lt < c->length(); lt += assetDur)
                            {
                                float lx = x0 + (float)(lt * pps);
                                if (lx > px0 && lx < px1)
                                    cdl->AddLine(ImVec2(lx, wy0), ImVec2(lx, wy1), IM_COL32(255, 255, 255, 70), 1.f);
                            }
                        }
                    }

                    // fades
                    float fiW = (float)(c->fadeInP->floatValue() * pps);
                    float foW = (float)(c->fadeOutP->floatValue() * pps);
                    ImU32 fadeCol = IM_COL32(255, 255, 255, 130);
                    if (fiW > 1)
                    {
                        cdl->AddLine(ImVec2(x0, y1 - 1), ImVec2(x0 + fiW, y0 + 9), fadeCol, 1.5f);
                        cdl->AddTriangleFilled(ImVec2(x0, y0 + 3), ImVec2(x0 + fiW, y0 + 3), ImVec2(x0, y1 - 1),
                                               IM_COL32(0, 0, 0, 46));
                    }
                    if (foW > 1)
                    {
                        cdl->AddLine(ImVec2(x1 - foW, y0 + 9), ImVec2(x1, y1 - 1), fadeCol, 1.5f);
                        cdl->AddTriangleFilled(ImVec2(x1 - foW, y0 + 3), ImVec2(x1, y0 + 3), ImVec2(x1, y1 - 1),
                                               IM_COL32(0, 0, 0, 46));
                    }
                    if (sel)
                    {
                        ImVec2 hIn(x0 + fiW, y0 + 9), hOut(x1 - foW, y0 + 9);
                        cdl->AddCircleFilled(hIn, 4.f, IM_COL32(255, 255, 255, 210));
                        cdl->AddCircleFilled(hOut, 4.f, IM_COL32(255, 255, 255, 210));
                        cdl->AddCircle(hIn, 4.f, IM_COL32(0, 0, 0, 180), 0, 1.f);
                        cdl->AddCircle(hOut, 4.f, IM_COL32(0, 0, 0, 180), 0, 1.f);
                    }
                }

                // name
                cdl->PushClipRect(ImVec2(x0 + 2, y0), ImVec2(x1 - 2, y1), true);
                cdl->AddText(ImVec2(x0 + 6, y0 + 3), IM_COL32(10, 10, 12, 220), c->niceName.c_str());
                cdl->AddText(ImVec2(x0 + 5, y0 + 2), IM_COL32(255, 255, 255, cEnabled ? 235 : 130), c->niceName.c_str());
                cdl->PopClipRect();

                // ---- embedded automation rows (the block CONTAINS its
                // automations: compact named headers, expandable editors) ----
                if (blockStyle)
                {
                    std::vector<AutoRowGeom> rows;
                    buildAutoRows(*c, y0, rows);
                    cdl->PushClipRect(ImVec2(x0 + 1, y0), ImVec2(x1 - 1, y1), true);
                    for (auto& rg : rows)
                    {
                        ClipAutomation* a = rg.a;
                        const bool isGrad = a->akind == ClipAutomation::AKind::Gradient;
                        const bool rowHov = hit.cauto == a && hit.clip == c;
                        const float open = a->uiAnim < 0 ? (a->expanded ? 1.f : 0.f) : a->uiAnim;

                        // header strip: expand triangle + name + live chip + rec dot
                        ImU32 hcol = IM_COL32(255, 255, 255, rowHov ? 26 : 14);
                        cdl->AddRectFilled(ImVec2(x0 + 3, rg.hy0 + 1), ImVec2(x1 - 3, rg.hy1 - 1),
                                           hcol, 3.f);
                        cdl->AddRectFilled(ImVec2(x0 + 3, rg.hy0 + 1), ImVec2(x0 + 5.5f, rg.hy1 - 1),
                                           col32(a->color, cEnabled ? 0.9f : 0.4f), 2.f);
                        // triangle rotates smoothly right → down with `open`
                        {
                            ImVec2 tc(x0 + 13.f, (rg.hy0 + rg.hy1) * 0.5f);
                            float r = 3.6f;
                            float ang = open * 1.5707963f; // 0 → 90°
                            auto rot = [&](float px, float py)
                            {
                                float ca = std::cos(ang), sa = std::sin(ang);
                                return ImVec2(tc.x + px * ca - py * sa, tc.y + px * sa + py * ca);
                            };
                            ImVec2 t1 = rot(-r * 0.6f, -r), t2 = rot(-r * 0.6f, r), t3 = rot(r, 0);
                            cdl->AddTriangleFilled(t1, t2, t3, IM_COL32(235, 235, 240, cEnabled ? 220 : 120));
                        }
                        // small row label
                        const float rowFontSz = ImGui::GetFontSize() * 0.86f;
                        cdl->AddText(ImGui::GetFont(), rowFontSz,
                                     ImVec2(x0 + 20, rg.hy0 + 2.f),
                                     IM_COL32(225, 225, 230, cEnabled ? 225 : 120),
                                     a->name.c_str());
                        float nameW = ImGui::GetFont()->CalcTextSizeA(
                            rowFontSz, FLT_MAX, 0.f, a->name.c_str()).x;

                        // live value chip at the right (playhead inside → live)
                        {
                            double lt = seq.currentTime - c->start();
                            bool inside = lt >= 0 && lt <= c->length();
                            char chip[24] = {};
                            if (isGrad)
                            {
                                if (!a->gkeys.empty())
                                {
                                    ImVec4 cc2 = a->colorAt(std::max(0.0, std::min(c->length(), lt)));
                                    cdl->AddRectFilled(ImVec2(x1 - 34, rg.hy0 + 3.5f), ImVec2(x1 - 20, rg.hy1 - 3.5f),
                                                       col32(cc2), 2.f);
                                    cdl->AddRect(ImVec2(x1 - 34, rg.hy0 + 3.5f), ImVec2(x1 - 20, rg.hy1 - 3.5f),
                                                 IM_COL32(0, 0, 0, 120), 2.f);
                                }
                            }
                            else if (!a->keys.empty())
                            {
                                snprintf(chip, sizeof(chip), "%.2f",
                                         a->valueAt(std::max(0.0, std::min(c->length(), lt))));
                                const float chipSz = ImGui::GetFontSize() * 0.8f;
                                float cw = ImGui::GetFont()->CalcTextSizeA(chipSz, FLT_MAX, 0.f, chip).x;
                                cdl->AddText(ImGui::GetFont(), chipSz,
                                             ImVec2(x1 - 22 - cw, rg.hy0 + 2.5f),
                                             inside && seq.playing ? accentU
                                                                   : IM_COL32(160, 160, 168, 200),
                                             chip);
                            }
                        }
                        // record dot
                        {
                            ImVec2 dot(x1 - 10.f, (rg.hy0 + rg.hy1) * 0.5f);
                            bool recNow = a->recording;
                            float pulse = recNow ? (0.6f + 0.4f * (float)std::sin(nowT * 8.0)) : 1.f;
                            ImU32 dcol = a->recArm ? IM_COL32((int)(230 * pulse), 40, 40, 255)
                                                   : IM_COL32(120, 120, 128, rowHov ? 200 : 90);
                            cdl->AddCircleFilled(dot, 3.2f, dcol);
                            if (a->recArm) cdl->AddCircle(dot, 5.f, IM_COL32(230, 60, 60, 150), 0, 1.f);
                        }

                        // collapsed sparkline preview between name and chips
                        if (open < 0.999f && !isGrad && !a->keys.empty())
                        {
                            float sx0 = x0 + 26 + nameW, sx1 = x1 - 40;
                            if (sx1 > sx0 + 24)
                            {
                                float sy0 = rg.hy0 + 3, sy1 = rg.hy1 - 3;
                                const int N = std::min(48, (int)((sx1 - sx0) / 3));
                                ImVec2 prev;
                                for (int i = 0; i <= N; i++)
                                {
                                    double lt = c->length() * i / std::max(1, N);
                                    float nv = a->normValueAt(lt);
                                    ImVec2 p(sx0 + (sx1 - sx0) * i / std::max(1, N),
                                             sy1 - nv * (sy1 - sy0));
                                    if (i) cdl->AddLine(prev, p, col32(a->color, (0.999f - open) * 0.6f), 1.f);
                                    prev = p;
                                }
                            }
                        }
                        else if (open < 0.999f && isGrad && !a->gkeys.empty())
                        {
                            float sx0 = x0 + 26 + nameW, sx1 = x1 - 40;
                            if (sx1 > sx0 + 24)
                                for (float px = sx0; px < sx1; px += 3.f)
                                {
                                    double lt = c->length() * (px - sx0) / std::max(1.f, sx1 - sx0);
                                    cdl->AddRectFilled(ImVec2(px, rg.hy0 + 4), ImVec2(px + 3, rg.hy1 - 4),
                                                       col32(a->colorAt(lt), (0.999f - open) * 0.8f));
                                }
                        }

                        // expanded editor body
                        if (rg.by1 > rg.by0 + 4)
                        {
                            cdl->PushClipRect(ImVec2(std::max(x0 + 3, cLaneX0), rg.by0),
                                              ImVec2(std::min(x1 - 3, cLaneX1), rg.by1), true);
                            cdl->AddRectFilled(ImVec2(x0 + 3, rg.by0), ImVec2(x1 - 3, rg.by1),
                                               IM_COL32(12, 12, 14, 235), 3.f);
                            if (isGrad)
                            {
                                float gy0 = rg.by0 + 3, gy1 = rg.by1 - 3;
                                if (a->gkeys.empty())
                                    cdl->AddText(ImVec2(x0 + 10, rg.by0 + 3), IM_COL32(110, 110, 118, 200),
                                                 "double-click: add color");
                                else
                                {
                                    float xf = x0 + (float)(a->gkeys.front().time * pps);
                                    float xl = x0 + (float)(a->gkeys.back().time * pps);
                                    if (xf > x0 + 3)
                                        cdl->AddRectFilled(ImVec2(x0 + 3, gy0), ImVec2(xf, gy1),
                                                           col32(a->gkeys.front().color));
                                    if (xl < x1 - 3)
                                        cdl->AddRectFilled(ImVec2(xl, gy0), ImVec2(x1 - 3, gy1),
                                                           col32(a->gkeys.back().color));
                                    for (size_t i = 0; i + 1 < a->gkeys.size(); i++)
                                    {
                                        const GradKey& ka = a->gkeys[i];
                                        const GradKey& kb = a->gkeys[i + 1];
                                        float xa = x0 + (float)(ka.time * pps);
                                        float xb = x0 + (float)(kb.time * pps);
                                        if (xb <= xa) continue;
                                        if (ka.hold)
                                            cdl->AddRectFilled(ImVec2(xa, gy0), ImVec2(xb, gy1), col32(ka.color));
                                        else
                                            cdl->AddRectFilledMultiColor(ImVec2(xa, gy0), ImVec2(xb, gy1),
                                                                         col32(ka.color), col32(kb.color),
                                                                         col32(kb.color), col32(ka.color));
                                    }
                                    for (auto& k : a->gkeys)
                                    {
                                        float kx = x0 + (float)(k.time * pps);
                                        bool ksel = a->selectedKeys.count(k.id) != 0;
                                        drawDiamond(cdl, ImVec2(kx, rg.by1 - 6), 4.f, col32(k.color),
                                                    ksel ? accentU : IM_COL32(230, 230, 235, 255));
                                    }
                                }
                            }
                            else
                            {
                                // grid
                                for (float f : { 0.f, 0.5f, 1.f })
                                    cdl->AddLine(ImVec2(x0 + 3, caRowNormToY(rg, f)),
                                                 ImVec2(x1 - 3, caRowNormToY(rg, f)),
                                                 IM_COL32(255, 255, 255, f == 0.5f ? 13 : 7), 1.f);
                                if (a->keys.empty() && !a->recording)
                                    cdl->AddText(ImVec2(x0 + 10, rg.by0 + 4), IM_COL32(110, 110, 118, 200),
                                                 pencilMode ? "click: add key - drag: draw"
                                                            : "double-click: add keys");
                                float rmn = a->rangeMin, rmx = a->rangeMax;
                                float rr = (rmx - rmn) == 0 ? 1.f : (rmx - rmn);
                                ImU32 curveC = col32(lighten(a->color, 0.12f), cEnabled ? 1.f : 0.45f);
                                if (!a->keys.empty())
                                {
                                    // flats before/after + segments + area fill
                                    const AutoKey& kf = a->keys.front();
                                    const AutoKey& kl = a->keys.back();
                                    float yf = caRowNormToY(rg, (kf.value - rmn) / rr);
                                    float yl = caRowNormToY(rg, (kl.value - rmn) / rr);
                                    float xf = x0 + (float)(kf.time * pps);
                                    float xl = x0 + (float)(kl.time * pps);
                                    if (xf > x0 + 3) cdl->AddLine(ImVec2(x0 + 3, yf), ImVec2(xf, yf), curveC, 1.6f);
                                    if (xl < x1 - 3) cdl->AddLine(ImVec2(xl, yl), ImVec2(x1 - 3, yl), curveC, 1.6f);
                                    std::vector<ImVec2> pts;
                                    for (size_t i = 0; i + 1 < a->keys.size(); i++)
                                    {
                                        const AutoKey& ka = a->keys[i];
                                        const AutoKey& kb = a->keys[i + 1];
                                        float xa = x0 + (float)(ka.time * pps);
                                        float xb = x0 + (float)(kb.time * pps);
                                        if (xb < cLaneX0 - 4 || xa > cLaneX1 + 4) continue;
                                        float na = (ka.value - rmn) / rr, nb = (kb.value - rmn) / rr;
                                        if (ka.easing == EasingType::Hold)
                                        {
                                            cdl->AddLine(ImVec2(xa, caRowNormToY(rg, na)), ImVec2(xb, caRowNormToY(rg, na)), curveC, 1.6f);
                                            cdl->AddLine(ImVec2(xb, caRowNormToY(rg, na)), ImVec2(xb, caRowNormToY(rg, nb)), curveC, 1.6f);
                                            continue;
                                        }
                                        int n = (int)std::max(2.f, std::min(120.f, (xb - xa) / 3.f));
                                        if (ka.easing == EasingType::Linear) n = 2;
                                        pts.clear();
                                        for (int s2 = 0; s2 <= n; s2++)
                                        {
                                            float w = (float)s2 / n;
                                            float v = ease(ka.easing, na, nb, w, ka.ep);
                                            pts.push_back(ImVec2(xa + (xb - xa) * w, caRowNormToY(rg, v)));
                                        }
                                        // soft area fill under the segment
                                        for (size_t pi = 0; pi + 1 < pts.size(); pi++)
                                            cdl->AddQuadFilled(pts[pi], pts[pi + 1],
                                                               ImVec2(pts[pi + 1].x, rg.by1 - 2),
                                                               ImVec2(pts[pi].x, rg.by1 - 2),
                                                               col32(a->color, 0.10f));
                                        cdl->AddPolyline(pts.data(), (int)pts.size(), curveC, 0, 1.7f);
                                    }
                                    for (auto& k : a->keys)
                                    {
                                        float kx = x0 + (float)(k.time * pps);
                                        float ky = caRowNormToY(rg, (k.value - rmn) / rr);
                                        bool ksel = a->selectedKeys.count(k.id) != 0;
                                        bool khov = hit.kind == Hit::CAKey && hit.keyId == k.id && hit.cauto == a;
                                        cdl->AddCircleFilled(ImVec2(kx, ky), 3.6f, ksel ? accentU : IM_COL32(22, 22, 25, 255));
                                        cdl->AddCircle(ImVec2(kx, ky), 3.6f, ksel ? IM_COL32_WHITE : curveC, 0, khov ? 2.2f : 1.3f);
                                        if (khov && drag == Drag::None)
                                            ImGui::SetTooltip("%.3fs | %.3f | %s", k.time, k.value, easingName(k.easing));
                                    }
                                    // handles of selected keys
                                    for (size_t i = 0; i + 1 < a->keys.size(); i++)
                                    {
                                        AutoKey& k = a->keys[i];
                                        if (!a->selectedKeys.count(k.id)) continue;
                                        AutoKey& nk = a->keys[i + 1];
                                        double segDur = nk.time - k.time;
                                        float na = (k.value - rmn) / rr, nb = (nk.value - rmn) / rr;
                                        if (k.easing == EasingType::Bezier)
                                        {
                                            ImVec2 kp(x0 + (float)(k.time * pps), caRowNormToY(rg, na));
                                            ImVec2 np(x0 + (float)(nk.time * pps), caRowNormToY(rg, nb));
                                            ImVec2 h1(x0 + (float)((k.time + k.ep.a1.x * segDur) * pps), caRowNormToY(rg, na + k.ep.a1.y));
                                            ImVec2 h2(x0 + (float)((nk.time + k.ep.a2.x * segDur) * pps), caRowNormToY(rg, nb + k.ep.a2.y));
                                            cdl->AddLine(kp, h1, IM_COL32(255, 255, 255, 90), 1.f);
                                            cdl->AddLine(np, h2, IM_COL32(255, 255, 255, 90), 1.f);
                                            cdl->AddRectFilled(ImVec2(h1.x - 2.5f, h1.y - 2.5f), ImVec2(h1.x + 2.5f, h1.y + 2.5f), IM_COL32_WHITE);
                                            cdl->AddRectFilled(ImVec2(h2.x - 2.5f, h2.y - 2.5f), ImVec2(h2.x + 2.5f, h2.y + 2.5f), IM_COL32_WHITE);
                                        }
                                        else if (easingHasHandle(k.easing))
                                        {
                                            double midT = (k.time + nk.time) * 0.5;
                                            float midV = ease(k.easing, na, nb, 0.5f, k.ep);
                                            ImVec2 hm(x0 + (float)(midT * pps), caRowNormToY(rg, midV));
                                            cdl->AddCircleFilled(hm, 3.2f, IM_COL32(255, 255, 255, 220));
                                            cdl->AddCircle(hm, 5.f, IM_COL32(255, 255, 255, 90), 0, 1.f);
                                        }
                                    }
                                }
                                // pencil stroke in progress on this row
                                if (drag == Drag::PencilClip && dragClipId == c->id &&
                                    dragAutoId == a->id && pencilPts.size() >= 2)
                                {
                                    std::vector<ImVec2> sp2;
                                    for (auto& p : pencilPts)
                                        sp2.push_back(ImVec2(x0 + (float)(p.first * pps),
                                                             caRowNormToY(rg, (p.second - rmn) / rr)));
                                    cdl->AddPolyline(sp2.data(), (int)sp2.size(), accentU, 0, 2.f);
                                }
                                // live playhead dot
                                double lt = seq.currentTime - c->start();
                                if (lt >= 0 && lt <= c->length() && !a->keys.empty())
                                {
                                    float phx2 = x0 + (float)(lt * pps);
                                    cdl->AddCircleFilled(ImVec2(phx2, caRowNormToY(rg, a->normValueAt(lt))), 2.6f, accentU);
                                }
                            }
                            cdl->PopClipRect();
                        }
                    }
                    cdl->PopClipRect();
                }

                if (sel) cdl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), accentU, 4.f, 0, 2.f);
                else if (pre) cdl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), col32(accent, 0.65f), 4.f, 0, 1.5f);
                else if (c->isHighlighted()) cdl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(90, 160, 255, 200), 4.f, 0, 2.f);
                else if (hov) cdl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(255, 255, 255, 110), 4.f, 0, 1.f);

                if (hov && (hit.kind == Hit::ClipL || hit.kind == Hit::ClipR))
                {
                    float ex = hit.kind == Hit::ClipL ? x0 + 2 : x1 - 2;
                    cdl->AddLine(ImVec2(ex, y0 + 2), ImVec2(ex, y1 - 2), IM_COL32(255, 255, 255, 200), 2.f);
                }
            }
        }
        else if (auto* al = dynamic_cast<AutomationLayer*>(g.layer))
        {
            for (float f : { 0.f, 0.25f, 0.5f, 0.75f, 1.f })
                cdl->AddLine(ImVec2(cLaneX0, normToY(g, f)), ImVec2(cLaneX1, normToY(g, f)),
                             IM_COL32(255, 255, 255, f == 0.5f ? 14 : 7), 1.f);

            float mn = al->rangeMinP->floatValue(), mx = al->rangeMaxP->floatValue();
            float range = (mx - mn) == 0 ? 1.f : (mx - mn);
            ImU32 curveCol = col32(lighten(al->colorP->color(), 0.15f), lEnabled ? 1.f : 0.4f);

            if (al->keys.empty() && !al->recording)
            {
                cdl->AddText(ImVec2(cLaneX0 + 8, g.y0 + 6), IM_COL32(120, 120, 126, 200),
                             "Double-click to add keys");
            }
            if (!al->keys.empty())
            {
                const AutoKey& kf = al->keys.front();
                const AutoKey& kl = al->keys.back();
                float yf = normToY(g, (kf.value - mn) / range);
                float yl = normToY(g, (kl.value - mn) / range);
                if (timeToX(kf.time) > cLaneX0)
                    cdl->AddLine(ImVec2(cLaneX0, yf), ImVec2(timeToX(kf.time), yf), curveCol, 2.f);
                if (timeToX(kl.time) < cLaneX1)
                    cdl->AddLine(ImVec2(timeToX(kl.time), yl), ImVec2(cLaneX1, yl), curveCol, 2.f);

                std::vector<ImVec2> pts;
                for (size_t i = 0; i + 1 < al->keys.size(); i++)
                {
                    const AutoKey& a = al->keys[i];
                    const AutoKey& b = al->keys[i + 1];
                    float xa = timeToX(a.time), xb = timeToX(b.time);
                    if (xb < cLaneX0 - 4 || xa > cLaneX1 + 4) continue;
                    float na = (a.value - mn) / range, nb = (b.value - mn) / range;
                    int n = (int)std::max(2.f, std::min(160.f, (xb - xa) / 3.f));
                    if (a.easing == EasingType::Linear || a.easing == EasingType::Hold) n = 2;
                    if (a.easing == EasingType::Hold)
                    {
                        cdl->AddLine(ImVec2(xa, normToY(g, na)), ImVec2(xb, normToY(g, na)), curveCol, 2.f);
                        cdl->AddLine(ImVec2(xb, normToY(g, na)), ImVec2(xb, normToY(g, nb)), curveCol, 2.f);
                        continue;
                    }
                    pts.clear();
                    for (int s = 0; s <= n; s++)
                    {
                        float w = (float)s / n;
                        float v = ease(a.easing, na, nb, w, a.ep);
                        pts.push_back(ImVec2(xa + (xb - xa) * w, normToY(g, v)));
                    }
                    cdl->AddPolyline(pts.data(), (int)pts.size(), curveCol, 0, 2.f);
                }

                for (auto& k : al->keys)
                {
                    float x = timeToX(k.time);
                    if (x < cLaneX0 - 8 || x > cLaneX1 + 8) continue;
                    float y = normToY(g, (k.value - mn) / range);
                    bool ksel = al->selectedKeys.count(k.id) != 0;
                    bool khov = (hit.kind == Hit::AKey && hit.keyId == k.id);
                    cdl->AddCircleFilled(ImVec2(x, y), 4.5f, ksel ? accentU : IM_COL32(25, 25, 28, 255));
                    cdl->AddCircle(ImVec2(x, y), 4.5f, ksel ? IM_COL32_WHITE : curveCol, 0, khov ? 2.5f : 1.5f);
                    if (khov && drag == Drag::None)
                        ImGui::SetTooltip("%.3fs  |  %.3f  |  %s", k.time, k.value, easingName(k.easing));
                }

                // bezier + easing-parameter handles for selected keys
                for (size_t i = 0; i + 1 < al->keys.size(); i++)
                {
                    AutoKey& k = al->keys[i];
                    if (!al->selectedKeys.count(k.id)) continue;
                    AutoKey& nk = al->keys[i + 1];
                    double segDur = nk.time - k.time;
                    float na = (k.value - mn) / range, nb = (nk.value - mn) / range;
                    if (k.easing == EasingType::Bezier)
                    {
                        ImVec2 kp(timeToX(k.time), normToY(g, na));
                        ImVec2 np(timeToX(nk.time), normToY(g, nb));
                        ImVec2 h1(timeToX(k.time + k.ep.a1.x * segDur), normToY(g, na + k.ep.a1.y));
                        ImVec2 h2(timeToX(nk.time + k.ep.a2.x * segDur), normToY(g, nb + k.ep.a2.y));
                        cdl->AddLine(kp, h1, IM_COL32(255, 255, 255, 90), 1.f);
                        cdl->AddLine(np, h2, IM_COL32(255, 255, 255, 90), 1.f);
                        cdl->AddRectFilled(ImVec2(h1.x - 3, h1.y - 3), ImVec2(h1.x + 3, h1.y + 3), IM_COL32_WHITE);
                        cdl->AddRectFilled(ImVec2(h2.x - 3, h2.y - 3), ImVec2(h2.x + 3, h2.y + 3), IM_COL32_WHITE);
                    }
                    else if (easingHasHandle(k.easing))
                    {
                        double midT = (k.time + nk.time) * 0.5;
                        float midV = ease(k.easing, na, nb, 0.5f, k.ep);
                        ImVec2 hm(timeToX(midT), normToY(g, midV));
                        cdl->AddCircleFilled(hm, 4.f, IM_COL32(255, 255, 255, 220));
                        cdl->AddCircle(hm, 6.f, IM_COL32(255, 255, 255, 90), 0, 1.f);
                        if (hit.kind == Hit::EaseHandle && hit.keyId == k.id && drag == Drag::None)
                            ImGui::SetTooltip(k.easing == EasingType::Steps
                                              ? "drag right/left: steps (%d)"
                                              : "drag: frequency / amplitude", k.ep.steps);
                    }
                }

                // live value dot at playhead
                float phx = timeToX(seq.currentTime);
                if (phx >= cLaneX0 && phx <= cLaneX1)
                {
                    float y = normToY(g, al->normValueAt(seq.currentTime));
                    cdl->AddCircleFilled(ImVec2(phx, y), 3.f, accentU);
                }
            }

            // recording overlay (live points)
            if (al->recording && al->recPoints.size() >= 2)
            {
                std::vector<ImVec2> rp;
                for (auto& p : al->recPoints)
                {
                    float x = timeToX(p.first);
                    if (x < cLaneX0 - 4 || x > cLaneX1 + 4) continue;
                    rp.push_back(ImVec2(x, normToY(g, (p.second - mn) / range)));
                }
                if (rp.size() >= 2)
                    cdl->AddPolyline(rp.data(), (int)rp.size(), IM_COL32(255, 70, 70, 220), 0, 2.f);
            }

            // pencil stroke in progress on this lane
            if (drag == Drag::PencilLane && dragLayerId == al->id && pencilPts.size() >= 2)
            {
                std::vector<ImVec2> sp2;
                for (auto& p : pencilPts)
                    sp2.push_back(ImVec2(timeToX(p.first), normToY(g, (p.second - mn) / range)));
                cdl->AddPolyline(sp2.data(), (int)sp2.size(), accentU, 0, 2.f);
            }
        }
        else if (auto* gl = dynamic_cast<GradientLayer*>(g.layer))
        {
            float sy0 = g.y0 + 3, sy1 = g.y1 - 13;
            if (gl->keys.empty())
            {
                cdl->AddRectFilled(ImVec2(cLaneX0, sy0), ImVec2(cLaneX1, sy1), IM_COL32(20, 20, 22, 255));
                cdl->AddText(ImVec2(cLaneX0 + 8, sy0 + 2), IM_COL32(120, 120, 126, 200),
                             "Double-click to add color keys");
            }
            else
            {
                float xf = timeToX(gl->keys.front().time);
                float xl = timeToX(gl->keys.back().time);
                if (xf > cLaneX0)
                    cdl->AddRectFilled(ImVec2(cLaneX0, sy0), ImVec2(std::min(xf, cLaneX1), sy1),
                                       col32(gl->keys.front().color));
                if (xl < cLaneX1)
                    cdl->AddRectFilled(ImVec2(std::max(xl, cLaneX0), sy0), ImVec2(cLaneX1, sy1),
                                       col32(gl->keys.back().color));
                for (size_t i = 0; i + 1 < gl->keys.size(); i++)
                {
                    const GradKey& a = gl->keys[i];
                    const GradKey& b = gl->keys[i + 1];
                    float xa = timeToX(a.time), xb = timeToX(b.time);
                    if (xb < cLaneX0 || xa > cLaneX1 || xb <= xa) continue;
                    if (a.hold)
                        cdl->AddRectFilled(ImVec2(xa, sy0), ImVec2(xb, sy1), col32(a.color));
                    else
                        cdl->AddRectFilledMultiColor(ImVec2(xa, sy0), ImVec2(xb, sy1),
                                                     col32(a.color), col32(b.color),
                                                     col32(b.color), col32(a.color));
                }
                for (auto& k : gl->keys)
                {
                    float x = timeToX(k.time);
                    if (x < cLaneX0 - 8 || x > cLaneX1 + 8) continue;
                    bool ksel = gl->selectedKeys.count(k.id) != 0;
                    drawDiamond(cdl, ImVec2(x, g.y1 - 7), 5.f, col32(k.color),
                                ksel ? accentU : IM_COL32(230, 230, 235, 255));
                    if (k.hold)
                        cdl->AddText(ImVec2(x + 6, g.y1 - 13), IM_COL32(230, 230, 235, 170), "H");
                }
            }
        }
        else if (auto* tl = dynamic_cast<TriggerLayer*>(g.layer))
        {
            float laneH = g.y1 - g.y0;
            if (tl->triggers.empty())
                cdl->AddText(ImVec2(cLaneX0 + 8, g.y0 + 6), IM_COL32(120, 120, 126, 200),
                             "Double-click to add triggers");
            for (auto& t : tl->triggers)
            {
                float x = timeToX(t.time);
                if (x < cLaneX0 - 140 || x > cLaneX1 + 20) continue;
                float yFlag = g.y0 + 5 + t.flagY * (laneH - 22);
                bool ksel = tl->selectedKeys.count(t.id) != 0;
                float flash = (float)std::max(0.0, 1.0 - (nowT - t.flashTime) * 2.5);
                ImVec4 base = tl->colorP->color();
                ImVec4 fcol = flash > 0 ? lighten(base, 0.5f * flash) : base;
                ImU32 poleCol = col32(fcol, lEnabled ? 0.9f : 0.4f);

                cdl->AddLine(ImVec2(x, yFlag), ImVec2(x, g.y1 - 3), poleCol, ksel ? 2.5f : 1.5f);
                ImVec2 ts = ImGui::CalcTextSize(t.name.c_str());
                ImVec2 fMin(x, yFlag - 8), fMax(x + ts.x + 12, yFlag + 8);
                cdl->AddRectFilled(fMin, fMax, col32(fcol, lEnabled ? (t.fired ? 0.55f : 0.85f) : 0.35f), 3.f);
                if (ksel) cdl->AddRect(fMin, fMax, accentU, 3.f, 0, 1.5f);
                if (flash > 0) cdl->AddRect(fMin, fMax, IM_COL32_WHITE, 3.f, 0, 2.f * flash);
                cdl->AddText(ImVec2(x + 6, yFlag - ImGui::GetFontSize() * 0.5f),
                             IM_COL32(255, 255, 255, 235), t.name.c_str());
                cdl->AddCircleFilled(ImVec2(x, g.y1 - 4), 2.5f, poleCol);
            }
        }

        cdl->PopClipRect();
    }

    // key transform boxes (drawn over lanes)
    for (auto& kb : keyBoxes)
    {
        ImVec2 bMin(kb.x0, kb.yT - 6), bMax(kb.x1, kb.yB + 6);
        cdl->AddRect(bMin, bMax, col32(accent, 0.8f), 2.f, 0, 1.f);
        float xm = (kb.x0 + kb.x1) * 0.5f, ym = (kb.yT + kb.yB) * 0.5f;
        auto handle = [&](float hx, float hy)
        {
            cdl->AddRectFilled(ImVec2(hx - 3.5f, hy - 3.5f), ImVec2(hx + 3.5f, hy + 3.5f), accentU);
            cdl->AddRect(ImVec2(hx - 3.5f, hy - 3.5f), ImVec2(hx + 3.5f, hy + 3.5f), IM_COL32(0, 0, 0, 160));
        };
        handle(kb.x0, ym);
        handle(kb.x1, ym);
        handle(xm, kb.yT - 8);
        handle(xm, kb.yB + 8);
    }

    // magnet guides
    for (double gt : magnetGuides)
    {
        float x = timeToX(gt);
        cdl->AddLine(ImVec2(x, canvasP0.y), ImVec2(x, std::max(lanesBottom, canvasP0.y + lanesH)),
                     col32(accent, 0.75f), 1.f);
    }

    // rubber band rect
    if (drag == Drag::Rubber && dragMoved)
    {
        ImVec2 rMin(std::min(rubberStart.x, mouse.x), std::min(rubberStart.y, mouse.y));
        ImVec2 rMax(std::max(rubberStart.x, mouse.x), std::max(rubberStart.y, mouse.y));
        cdl->AddRectFilled(rMin, rMax, col32(accent, 0.12f));
        cdl->AddRect(rMin, rMax, col32(accent, 0.8f));
    }

    // playhead line across lanes
    {
        float phx = timeToX(seq.currentTime);
        if (phx >= cLaneX0 - 1 && phx <= cLaneX1 + 1)
            cdl->AddLine(ImVec2(phx, canvasP0.y), ImVec2(phx, std::max(lanesBottom, canvasP0.y + lanesH)),
                         col32(accent, 0.9f), 1.5f);
    }

    // ============================================================ DRAG & DROP TARGET
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(ORGANIC_MEDIA_PAYLOAD,
                ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
        {
            if (pl->DataSize == (int)sizeof(MediaPayload))
            {
                MediaPayload mp;
                memcpy(&mp, pl->Data, sizeof(mp));
                double t = snapTime(seq, xToTime(mouse.x), io.KeyAlt);
                double len = mp.duration > 0 ? mp.duration : 2.0;
                ImVec4 gcol(mp.color[0], mp.color[1], mp.color[2], 0.55f);
                const LaneGeom* g = laneAtY(mouse.y);
                ClipLayer* target = g ? dynamic_cast<ClipLayer*>(g->layer) : nullptr;
                // the pinned audio lane only takes audio media — an effect
                // dropped on it falls through to the new-layer seat below
                if (target && target->id == stickyLayerId && mp.kind != 1)
                {
                    target = nullptr;
                    g = nullptr;
                }

                if (target)
                {
                    // preview the RESOLVED seat — blocks butt flush, never stack
                    const double rt = target->resolveOverlap(t, len);
                    cdl->AddRectFilled(ImVec2(timeToX(rt), g->y0 + 3), ImVec2(timeToX(rt + len), g->y1 - 3),
                                       col32(gcol), 4.f);
                    cdl->AddRect(ImVec2(timeToX(rt), g->y0 + 3), ImVec2(timeToX(rt + len), g->y1 - 3),
                                 accentU, 4.f, 0, 2.f);
                    if (pl->IsDelivery())
                        createClipFromPayload(target, rt, mp);
                }
                else
                {
                    float gy = lanesBottom + LANE_GAP;
                    cdl->AddRectFilled(ImVec2(cLaneX0, gy), ImVec2(cLaneX1, gy + 50), IM_COL32(255, 255, 255, 14), 4.f);
                    cdl->AddRectFilled(ImVec2(timeToX(t), gy + 3), ImVec2(timeToX(t + len), gy + 47), col32(gcol), 4.f);
                    cdl->AddText(ImVec2(cLaneX0 + 8, gy + 4), IM_COL32(200, 200, 205, 200), "New layer");
                    if (pl->IsDelivery())
                    {
                        auto* nl = static_cast<ClipLayer*>(seq.addLayer(Layer::LType::Clips,
                                                           mp.name[0] ? mp.name : "Clips"));
                        Clip* c = nl->addClip(mp.kind == 1 ? Clip::CType::Audio : Clip::CType::Block,
                                              mp.name[0] ? mp.name : "Clip", t, len);
                        ImVec4 col(mp.color[0], mp.color[1], mp.color[2], mp.color[3]);
                        c->colorP->setValue(col, false);
                        c->colorP->defaultValue = col;
                        if (mp.kind == 1 && mp.file[0]) c->setAudioFile(mp.file, mp.duration <= 0);
                        json snap = nl->save();
                        int idx = seq.layerIndex(nl);
                        uint64_t nid = nl->id;
                        UndoManager::get().pushDone("Drop Media",
                            [sp, snap, idx] { sp->addLayerFromJson(snap, idx); },
                            [sp, nid] { sp->removeLayer(nid); },
                            { sp });
                        c->select();
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // ============================================================ POPUPS
    if (ImGui::BeginPopup("clip_ctx"))
    {
        Clip* ctxClip = seq.findClip(ctxItemId);
        if (ImGui::MenuItem("Split Here", nullptr, false,
                            ctxClip && ctxTime > ctxClip->start() + 0.05 && ctxTime < ctxClip->end() - 0.05))
            splitClip(ctxClip, ctxTime);
        if (ImGui::MenuItem("Split At Playhead"))
        {
            for (Clip* c : Selection::get().getAs<Clip>())
                if (c->layer && c->layer->sequence == sp)
                    splitClip(c, seq.currentTime);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Copy", "Ctrl+C")) copySelection();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) duplicateSelection();
        if (ImGui::MenuItem("Delete", "Del")) deleteSelection();
        ImGui::Separator();
        if (ImGui::MenuItem("Select Layer"))
            if (Layer* l = seq.findLayer(ctxLayerId)) l->select();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("akey_ctx"))
    {
        auto* al = dynamic_cast<AutomationLayer*>(seq.findLayer(ctxLayerId));
        AutoKey* k = al ? al->findKey(ctxItemId) : nullptr;
        if (al && k)
        {
            ImGui::TextDisabled("Key @ %.3fs", k->time);
            ImGui::Separator();
            for (int e = 0; e < (int)EasingType::COUNT; e++)
            {
                bool active = (int)k->easing == e;
                if (ImGui::MenuItem(easingName((EasingType)e), nullptr, active))
                {
                    json pre = al->keysToJson();
                    if (al->selectedKeys.count(k->id))
                        for (uint64_t kid : al->selectedKeys)
                        {
                            if (AutoKey* kk = al->findKey(kid)) kk->easing = (EasingType)e;
                        }
                    else k->easing = (EasingType)e;
                    pushKeysEdit(sp, al->id, pre, al->keysToJson(), "Change Easing");
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Copy Keys", "Ctrl+C")) copySelection();
            if (ImGui::MenuItem("Delete Key", "Del"))
            {
                json pre = al->keysToJson();
                if (al->selectedKeys.count(k->id))
                    for (uint64_t kid : std::vector<uint64_t>(al->selectedKeys.begin(), al->selectedKeys.end()))
                        al->removeKey(kid);
                else al->removeKey(k->id);
                pushKeysEdit(sp, al->id, pre, al->keysToJson(), "Delete Key");
            }
        }
        ImGui::EndPopup();
    }

    {
        bool gkeyOpenNow = false;
        if (ImGui::BeginPopup("gkey_ctx"))
        {
            gkeyOpenNow = true;
            auto* gl = dynamic_cast<GradientLayer*>(seq.findLayer(ctxLayerId));
            GradKey* k = gl ? gl->findKey(ctxItemId) : nullptr;
            if (gl && k)
            {
                float col[4] = { k->color.x, k->color.y, k->color.z, k->color.w };
                ImGui::SetNextItemWidth(200);
                if (ImGui::ColorPicker4("##gk", col, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoAlpha))
                    k->color = ImVec4(col[0], col[1], col[2], 1.f);
                bool hold = k->hold;
                if (ImGui::Checkbox("Hold (no interpolation)", &hold)) k->hold = hold;
                ImGui::Separator();
                if (ImGui::MenuItem("Delete Key", "Del"))
                {
                    json pre = gl->keysToJson();
                    gl->removeKey(k->id);
                    pushKeysEdit(sp, gl->id, pre, gl->keysToJson(), "Delete Color Key");
                }
            }
            ImGui::EndPopup();
        }
        if (gkeyWasOpen && !gkeyOpenNow)
        {
            if (auto* gl = dynamic_cast<GradientLayer*>(seq.findLayer(ctxLayerId)))
                pushKeysEdit(sp, gl->id, preEditJson, gl->keysToJson(), "Edit Color Key");
        }
        gkeyWasOpen = gkeyOpenNow;
    }

    if (ImGui::BeginPopup("tkey_ctx"))
    {
        auto* tl = dynamic_cast<TriggerLayer*>(seq.findLayer(ctxLayerId));
        TimeTrigger* t = tl ? tl->findTrigger(ctxItemId) : nullptr;
        if (tl && t)
        {
            char buf[96];
            snprintf(buf, sizeof(buf), "%s", t->name.c_str());
            ImGui::SetNextItemWidth(150);
            if (ImGui::InputText("##tname", buf, sizeof(buf))) t->name = buf;
            if (ImGui::MenuItem("Fire Now"))
            {
                t->flashTime = ImGui::GetTime();
                OLOG(tl->niceName, "Trigger fired manually: " << t->name);
                if (tl->onTriggered) tl->onTriggered(*tl, *t);
            }
            if (ImGui::MenuItem("Delete Trigger", "Del"))
            {
                json pre = tl->keysToJson();
                if (tl->selectedKeys.count(t->id))
                    for (uint64_t kid : std::vector<uint64_t>(tl->selectedKeys.begin(), tl->selectedKeys.end()))
                        tl->removeTrigger(kid);
                else tl->removeTrigger(t->id);
                pushKeysEdit(sp, tl->id, pre, tl->keysToJson(), "Delete Trigger");
            }
        }
        ImGui::EndPopup();
    }

    // ---- embedded clip-automation popups --------------------------------
    if (ImGui::BeginPopup("cakey_ctx"))
    {
        Clip* c = seq.findClip(ctxClipId);
        ClipAutomation* a = c ? c->findAutomation(ctxAutoId) : nullptr;
        AutoKey* k = a ? a->findKey(ctxItemId) : nullptr;
        if (a && k)
        {
            ImGui::TextDisabled("%s @ %.3fs", a->name.c_str(), k->time);
            ImGui::Separator();
            for (int e = 0; e < (int)EasingType::COUNT; e++)
            {
                bool active = (int)k->easing == e;
                if (ImGui::MenuItem(easingName((EasingType)e), nullptr, active))
                {
                    json pre = a->keysToJson();
                    if (a->selectedKeys.count(k->id))
                        for (uint64_t kid : a->selectedKeys)
                        {
                            if (AutoKey* kk = a->findKey(kid)) kk->easing = (EasingType)e;
                        }
                    else k->easing = (EasingType)e;
                    pushClipAutoKeysEdit(sp, ctxClipId, ctxAutoId, pre, a->keysToJson(),
                                         "Change Easing");
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Key", "Del"))
            {
                json pre = a->keysToJson();
                if (a->selectedKeys.count(k->id))
                    for (uint64_t kid : std::vector<uint64_t>(a->selectedKeys.begin(),
                                                              a->selectedKeys.end()))
                        a->removeKey(kid);
                else a->removeKey(k->id);
                pushClipAutoKeysEdit(sp, ctxClipId, ctxAutoId, pre, a->keysToJson(),
                                     "Delete Key");
            }
        }
        ImGui::EndPopup();
    }

    {
        bool cagkeyOpenNow = false;
        if (ImGui::BeginPopup("cagkey_ctx"))
        {
            cagkeyOpenNow = true;
            Clip* c = seq.findClip(ctxClipId);
            ClipAutomation* a = c ? c->findAutomation(ctxAutoId) : nullptr;
            GradKey* k = a ? a->findGradKey(ctxItemId) : nullptr;
            if (a && k)
            {
                float col[4] = { k->color.x, k->color.y, k->color.z, k->color.w };
                ImGui::SetNextItemWidth(200);
                if (ImGui::ColorPicker4("##cagk", col, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoAlpha))
                    k->color = ImVec4(col[0], col[1], col[2], 1.f);
                bool hold = k->hold;
                if (ImGui::Checkbox("Hold (no interpolation)", &hold)) k->hold = hold;
                ImGui::Separator();
                if (ImGui::MenuItem("Delete Key", "Del"))
                {
                    json pre = a->keysToJson();
                    a->removeKey(k->id);
                    pushClipAutoKeysEdit(sp, ctxClipId, ctxAutoId, pre, a->keysToJson(),
                                         "Delete Color Key");
                }
            }
            ImGui::EndPopup();
        }
        if (cagkeyWasOpen && !cagkeyOpenNow)
        {
            Clip* c = seq.findClip(ctxClipId);
            if (ClipAutomation* a = c ? c->findAutomation(ctxAutoId) : nullptr)
                pushClipAutoKeysEdit(sp, ctxClipId, ctxAutoId, preEditJson,
                                     a->keysToJson(), "Edit Color Key");
        }
        cagkeyWasOpen = cagkeyOpenNow;
    }

    if (ImGui::BeginPopup("carow_ctx"))
    {
        Clip* c = seq.findClip(ctxClipId);
        ClipAutomation* a = c ? c->findAutomation(ctxAutoId) : nullptr;
        if (c && a)
        {
            ImGui::TextDisabled("%s — %s", c->niceName.c_str(), a->name.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem(a->expanded ? "Collapse" : "Expand"))
                a->expanded = !a->expanded;
            if (ImGui::MenuItem("Record Arm", nullptr, a->recArm))
            {
                a->recArm = !a->recArm;
                if (!a->recArm && a->recording) a->stopRecordingAndApply();
            }
            ImGui::Separator();
            bool hasKeys = !a->keys.empty() || !a->gkeys.empty();
            if (ImGui::MenuItem("Clear Keys", nullptr, false, hasKeys))
            {
                json pre = a->keysToJson();
                a->keys.clear();
                a->gkeys.clear();
                a->selectedKeys.clear();
                pushClipAutoKeysEdit(sp, ctxClipId, ctxAutoId, pre, a->keysToJson(),
                                     "Clear Keys");
            }
            if (ImGui::MenuItem("Remove Automation"))
            {
                // host-derived rows: let the app clear whatever owns the
                // row (its reconciliation removes it) — else local removal
                if (!(removeAutomationHook && removeAutomationHook(*c, *a)))
                {
                    uint64_t cid = ctxClipId;
                    json data = c->removeAutomation(a->id);
                    if (!data.is_null() && !data.empty())
                    {
                        UndoManager::get().pushDone("Remove Automation",
                            [sp, cid, data]
                            {
                                if (Clip* cc = sp->findClip(cid))
                                    cc->removeAutomation(data.value("id", (uint64_t)0));
                            },
                            [sp, cid, data]
                            {
                                if (Clip* cc = sp->findClip(cid))
                                {
                                    ClipAutomation* na = cc->addAutomation(
                                        ClipAutomation::AKind::Curve,
                                        data.value("name", "Automation"));
                                    na->load(data);
                                }
                            },
                            { sp });
                    }
                }
            }
        }
        ImGui::EndPopup();
    }

    auto layerOpsMenu = [&](Layer* l)
    {
        int idx = seq.layerIndex(l);
        bool en = l->enabledP->boolValue();
        if (ImGui::MenuItem("Enabled", nullptr, en)) l->enabledP->setUndoable(!en);
        if (offerLayerRename && ImGui::MenuItem("Rename..."))
        {
            ctxLayerId = l->id;
            snprintf(renameBuf, sizeof(renameBuf), "%s", l->niceName.c_str());
            wantRenamePopup = true;
        }
        const bool sticky = l->id == stickyLayerId; // pinned: position is fixed,
                                                    // and it stays SINGLE
        if (!sticky)
        {
            if (ImGui::MenuItem("Move Up", nullptr, false, idx > 0)) moveLayerUndoable(idx, idx - 1);
            if (ImGui::MenuItem("Move Down", nullptr, false, idx < (int)seq.layers.size() - 1)) moveLayerUndoable(idx, idx + 1);
            if (ImGui::MenuItem("Duplicate Layer")) duplicateLayerUndoable(l->id);
        }
        if (ImGui::MenuItem("Delete Layer")) removeLayerUndoable(l->id);
    };

    auto addLayerMenu = [&](int insertIdx)
    {
        if (offerClipLayers && ImGui::MenuItem("Clip Layer"))
            addLayerUndoable(Layer::LType::Clips, "Clips", insertIdx);
        if (offerAutomationLayers && ImGui::MenuItem("Automation Layer"))
            addLayerUndoable(Layer::LType::Automation, "Automation", insertIdx);
        if (offerGradientLayers && ImGui::MenuItem("Gradient Layer"))
            addLayerUndoable(Layer::LType::Gradient, "Gradient", insertIdx);
        if (offerTriggerLayers && ImGui::MenuItem("Trigger Layer"))
            addLayerUndoable(Layer::LType::Triggers, "Triggers", insertIdx);
    };

    if (ImGui::BeginPopup("lane_ctx"))
    {
        Layer* l = seq.findLayer(ctxLayerId);
        if (auto* cl = dynamic_cast<ClipLayer*>(l))
        {
            if (ImGui::MenuItem("Add Clip Here"))
            {
                const double len = std::max(1.0, snapStep(seq, pps) * 4.0);
                Clip* c = cl->addClip(Clip::CType::Block, "Clip",
                                      cl->resolveOverlap(ctxTime, len), len);
                pushClipAdded(sp, cl->id, c->save(), "Add Clip");
                c->select();
            }
            if (ImGui::MenuItem("Add Audio Clip Here"))
            {
                Clip* c = cl->addClip(Clip::CType::Audio, "Audio",
                                      cl->resolveOverlap(ctxTime, 4.0), 4.0);
                pushClipAdded(sp, cl->id, c->save(), "Add Audio Clip");
                c->select();
            }
        }
        else if (auto* al = dynamic_cast<AutomationLayer*>(l))
        {
            if (ImGui::MenuItem("Add Key Here"))
            {
                json pre = al->keysToJson();
                float mn = al->rangeMinP->floatValue(), mx = al->rangeMaxP->floatValue();
                AutoKey* k = al->addKey(ctxTime, mn + std::max(0.f, std::min(1.f, ctxValue)) * (mx - mn));
                al->selectedKeys.clear();
                al->selectedKeys.insert(k->id);
                al->select();
                pushKeysEdit(sp, al->id, pre, al->keysToJson(), "Add Key");
            }
            bool armed = al->recArmP->boolValue();
            if (ImGui::MenuItem("Record Arm", nullptr, armed))
                al->recArmP->setUndoable(!armed);
        }
        else if (auto* gl = dynamic_cast<GradientLayer*>(l))
        {
            if (ImGui::MenuItem("Add Color Key Here"))
            {
                json pre = gl->keysToJson();
                GradKey* k = gl->addKey(ctxTime, gl->colorAt(ctxTime));
                gl->selectedKeys.clear();
                gl->selectedKeys.insert(k->id);
                gl->select();
                pushKeysEdit(sp, gl->id, pre, gl->keysToJson(), "Add Color Key");
            }
        }
        else if (auto* tl = dynamic_cast<TriggerLayer*>(l))
        {
            if (ImGui::MenuItem("Add Trigger Here"))
            {
                json pre = tl->keysToJson();
                TimeTrigger* t = tl->addTrigger(ctxTime);
                tl->selectedKeys.clear();
                tl->selectedKeys.insert(t->id);
                tl->select();
                pushKeysEdit(sp, tl->id, pre, tl->keysToJson(), "Add Trigger");
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Paste At Playhead", "Ctrl+V")) pasteClipboard();
        if (l)
        {
            ImGui::Separator();
            if (ImGui::BeginMenu("Layer")) { layerOpsMenu(l); ImGui::EndMenu(); }
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Add Layer")) { addLayerMenu(-1); ImGui::EndMenu(); }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("layer_ctx"))
    {
        if (Layer* l = seq.findLayer(ctxLayerId)) layerOpsMenu(l);
        ImGui::Separator();
        if (ImGui::BeginMenu("Add Layer")) { addLayerMenu(-1); ImGui::EndMenu(); }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("empty_ctx"))
    {
        if (ImGui::BeginMenu("Add Layer")) { addLayerMenu(-1); ImGui::EndMenu(); }
        if (ImGui::MenuItem("Paste At Playhead", "Ctrl+V")) pasteClipboard();
        if (ImGui::MenuItem("Fit View", "F")) fitRequested = true;
        ImGui::EndPopup();
    }

    if (wantRenamePopup)
    {
        ImGui::OpenPopup("layer_rename");
        wantRenamePopup = false;
    }
    if (ImGui::BeginPopup("layer_rename"))
    {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        bool enter = ImGui::InputText("##rename", renameBuf, sizeof(renameBuf),
                                      ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("OK") || enter)
        {
            if (Layer* l = seq.findLayer(ctxLayerId))
            {
                std::string oldName = l->niceName, newName = renameBuf;
                uint64_t lid = l->id;
                if (!newName.empty() && newName != oldName)
                {
                    UndoManager::get().perform("Rename Layer",
                        [sp, lid, newName] { if (Layer* ll = sp->findLayer(lid)) ll->setNiceName(newName); },
                        [sp, lid, oldName] { if (Layer* ll = sp->findLayer(lid)) ll->setNiceName(oldName); },
                        { sp });
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild(); // ##lanes

    // ============================================================ H SCROLLBAR
    {
        ImVec2 hsMin(origin.x, origin.y + RULER_H + lanesH);
        ImGui::SetCursorScreenPos(hsMin);
        ImGui::InvisibleButton("##hscroll", ImVec2(avail.x, HSCROLL_H));
        bool hovered = ImGui::IsItemHovered();
        double viewDur = laneW / pps;
        double total = std::max({ (double)seq.totalTime(), contentEnd, viewStart + viewDur }) + 2.0;
        float trackX0 = laneX0, trackX1 = origin.x + avail.x;
        float trackW = trackX1 - trackX0;
        float thX0 = trackX0 + (float)(viewStart / total * trackW);
        float thW = std::max(24.f, (float)(viewDur / total * trackW));
        dl->AddRectFilled(hsMin, ImVec2(trackX1, hsMin.y + HSCROLL_H), IM_COL32(24, 24, 26, 255));
        dl->AddRectFilled(ImVec2(thX0, hsMin.y + 2), ImVec2(std::min(thX0 + thW, trackX1), hsMin.y + HSCROLL_H - 2),
                          hovered || drag == Drag::HScroll ? IM_COL32(110, 110, 116, 255) : IM_COL32(80, 80, 86, 255), 4.f);
        if (ImGui::IsItemActivated())
        {
            drag = Drag::HScroll;
            dragStartMouse = mouse;
            if (mouse.x < thX0 || mouse.x > thX0 + thW)
                viewStart = std::max(0.0, (mouse.x - trackX0 - thW * 0.5f) / trackW * total);
            dragOrigA = viewStart;
        }
        if (drag == Drag::HScroll)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                viewStart = std::max(0.0, dragOrigA + (mouse.x - dragStartMouse.x) * total / trackW);
            else drag = Drag::None;
        }
    }

    // ============================================================ RULER PLAYHEAD
    {
        float phx = timeToX(seq.currentTime);
        if (phx >= laneX0 - 6 && phx <= origin.x + avail.x + 6)
        {
            dl->AddLine(ImVec2(phx, rulerMin.y + 16), ImVec2(phx, rulerMax.y), accentU, 1.5f);
            ImVec2 tri[3] = { ImVec2(phx - 5, rulerMax.y - 8), ImVec2(phx + 5, rulerMax.y - 8), ImVec2(phx, rulerMax.y) };
            dl->AddConvexPolyFilled(tri, 3, accentU);
        }
    }

    // follow playhead
    if (seq.playing && followPlayhead && drag == Drag::None)
    {
        float phx = timeToX(seq.currentTime);
        if (phx > laneX0 + laneW * 0.92f || phx < laneX0)
            viewStart = std::max(0.0, seq.currentTime - 0.08 * laneW / pps);
    }

    // ============================================================ KEYBOARD
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) seq.togglePlay();
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) seq.setTime(seq.loopIn >= 0 ? seq.loopIn : 0);
        if (ImGui::IsKeyPressed(ImGuiKey_End)) seq.setTime(seq.totalTime());
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) deleteSelection();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelection();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) selectAllClips();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) copySelection();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) pasteClipboard();
        if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.KeyCtrl) fitRequested = true;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) nudgeSelection(-snapStep(seq, pps));
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) nudgeSelection(snapStep(seq, pps));
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
        {
            double t = seq.nextCueTime(seq.currentTime, 1);
            seq.setTime(t >= 0 ? t : seq.totalTime());
        }
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
        {
            double t = seq.nextCueTime(seq.currentTime, -1);
            seq.setTime(t >= 0 ? t : 0);
        }
    }

    ImGui::PopID();
}

} // namespace organic
