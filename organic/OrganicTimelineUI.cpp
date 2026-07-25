#include "OrganicTimelineUI.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace organic
{

// ---------------------------------------------------------------- constants
static const float HEADER_W  = 170.f;
static const float RULER_H   = 26.f;
static const float HSCROLL_H = 14.f;
static const float LANE_GAP  = 2.f;
static const float VAL_MARGIN = 5.f;   // vertical margin inside automation lanes

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
    enum Kind { None, Corner, Header, HeaderGrip, Lane, ClipBody, ClipL, ClipR, AKey, BezA1, BezA2, GKey };
    Kind   kind = None;
    Layer* layer = nullptr;
    int    laneIdx = -1;
    Clip*  clip = nullptr;
    uint64_t keyId = 0;
};

// value <-> y helpers for automation lanes
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

// push an undo entry for a clip that was just created (already exists)
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

// push an undo entry for key edits on an automation/gradient layer (pre/post snapshots)
static void pushKeysEdit(Sequence* seq, uint64_t layerId, const json& pre, const json& post, const std::string& name)
{
    if (pre == post) return;
    auto apply = [seq, layerId](const json& data)
    {
        Layer* l = seq->findLayer(layerId);
        if (auto* al = dynamic_cast<AutomationLayer*>(l)) al->keysFromJson(data);
        else if (auto* gl = dynamic_cast<GradientLayer*>(l)) gl->keysFromJson(data);
    };
    UndoManager::get().pushDone(name,
        [apply, post] { apply(post); },
        [apply, pre]  { apply(pre); },
        { seq });
}

// ---------------------------------------------------------------- TimelineUI
TimelineUI::TimelineUI() {}

double TimelineUI::snapStep(double pps) const
{
    if (snapChoice > 0)
    {
        static const double fixed[] = { 1.0, 0.5, 0.25, 0.1, 0.05, 0.01 };
        int i = snapChoice - 1;
        if (i >= 0 && i < 6) return fixed[i];
    }
    // adaptive: ruler major step / 5
    double major = RULER_STEPS[IM_ARRAYSIZE(RULER_STEPS) - 1];
    for (double s : RULER_STEPS)
        if (s * pps >= 80.0) { major = s; break; }
    return major / 5.0;
}

double TimelineUI::snapTime(const Sequence& seq, double t, bool bypass) const
{
    if (!snapEnabled || bypass) return std::max(0.0, t);
    double s = snapStep(seq.pixelsPerSecond);
    return std::max(0.0, std::round(t / s) * s);
}

void TimelineUI::toolbar(Sequence& seq)
{
    ImVec4 accent = ImVec4(1.f, 0.573f, 0.184f, 1.f);

    if (ImGui::Button("|<")) seq.setTime(0);
    ImGui::SetItemTooltip("Go to start (Home)");
    ImGui::SameLine();

    bool playing = seq.playing;
    if (playing) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.45f, 0.22f, 1.f));
    if (ImGui::Button(playing ? "Pause" : "Play ")) seq.togglePlay();
    if (playing) ImGui::PopStyleColor();
    ImGui::SetItemTooltip("Play / Pause (Space)");
    ImGui::SameLine();

    if (ImGui::Button("Stop")) seq.stop();
    ImGui::SameLine();

    bool loop = seq.loopP->boolValue();
    if (ImGui::Checkbox("Loop", &loop)) seq.loopP->setUndoable(loop);
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::Text("%s", formatTime(seq.currentTime).c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("/ %s", formatTime(seq.totalTime()).c_str());

    ImGui::SameLine(0, 18);
    ImGui::Checkbox("Snap", &snapEnabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    const char* snapNames[] = { "Adaptive", "1 s", "1/2 s", "1/4 s", "1/10 s", "1/20 s", "1/100 s" };
    ImGui::Combo("##snapstep", &snapChoice, snapNames, IM_ARRAYSIZE(snapNames));
    ImGui::SetItemTooltip("Snap grid (hold Alt to bypass while dragging)");

    ImGui::SameLine();
    ImGui::Checkbox("Follow", &followPlayhead);
    ImGui::SetItemTooltip("Keep the playhead visible while playing");

    ImGui::SameLine();
    if (ImGui::Button("Fit")) { /* handled below via flag */ fitRequested = true; }
    ImGui::SetItemTooltip("Fit content in view (F)");

    ImGui::SameLine();
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
        if (ImGui::MenuItem("Clip Layer"))       addL(Layer::LType::Clips, "Clips");
        if (ImGui::MenuItem("Automation Layer")) addL(Layer::LType::Automation, "Automation");
        if (ImGui::MenuItem("Gradient Layer"))   addL(Layer::LType::Gradient, "Gradient");
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Sequence")) seq.select();
    ImGui::SetItemTooltip("Edit sequence settings in the Inspector");
}

void TimelineUI::gui(Sequence& seq, bool* open, const char* windowName)
{
    ImGui::SetNextWindowSize(ImVec2(1000, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(windowName, open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::End();
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    Sequence* sp = &seq;

    toolbar(seq);

    double& viewStart = seq.viewStart;
    double& pps       = seq.pixelsPerSecond;
    pps = std::max(2.0, std::min(4000.0, pps));

    ImVec2 avail  = ImGui::GetContentRegionAvail();
    if (avail.x < 80 || avail.y < 70) { ImGui::End(); return; }
    ImVec2 origin = ImGui::GetCursorScreenPos();

    float laneX0 = origin.x + HEADER_W;
    float laneW  = avail.x - HEADER_W;
    auto timeToX = [&](double t) { return laneX0 + (float)((t - viewStart) * pps); };
    auto xToTime = [&](float x)  { return viewStart + (x - laneX0) / pps; };

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
    }

    if (fitRequested)
    {
        fitRequested = false;
        viewStart = 0;
        pps = std::max(2.0, std::min(4000.0, laneW / std::max(1.0, contentEnd * 1.02)));
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ============================================================ RULER
    ImVec2 rulerMin(origin.x, origin.y);
    ImVec2 rulerMax(origin.x + avail.x, origin.y + RULER_H);
    dl->AddRectFilled(rulerMin, rulerMax, IM_COL32(28, 28, 30, 255));
    dl->AddRectFilled(rulerMin, ImVec2(laneX0, rulerMax.y), IM_COL32(24, 24, 26, 255));

    // sequence name in the corner
    dl->PushClipRect(rulerMin, ImVec2(laneX0 - 4, rulerMax.y), true);
    dl->AddText(ImVec2(rulerMin.x + 6, rulerMin.y + 5), IM_COL32(170, 170, 175, 255), seq.niceName.c_str());
    dl->PopClipRect();

    // adaptive steps
    double majorStep = RULER_STEPS[IM_ARRAYSIZE(RULER_STEPS) - 1];
    for (double s : RULER_STEPS)
        if (s * pps >= 80.0) { majorStep = s; break; }
    double minorStep = majorStep / 5.0;

    dl->PushClipRect(ImVec2(laneX0, rulerMin.y), rulerMax, true);
    {
        double t0 = std::max(0.0, viewStart);
        double tEnd = xToTime(origin.x + avail.x);
        double tm = std::floor(t0 / minorStep) * minorStep;
        for (; tm <= tEnd; tm += minorStep)
        {
            float x = timeToX(tm);
            bool isMajor = std::fabs(std::fmod(tm + minorStep * 0.5, majorStep) - minorStep * 0.5) < minorStep * 0.25;
            float h = isMajor ? 12.f : 6.f;
            dl->AddLine(ImVec2(x, rulerMax.y - h), ImVec2(x, rulerMax.y), IM_COL32(140, 140, 145, isMajor ? 200 : 110), 1.f);
            if (isMajor)
            {
                std::string lbl = formatTime(tm, majorStep < 1.0);
                dl->AddText(ImVec2(x + 3, rulerMin.y + 2), IM_COL32(150, 150, 155, 220), lbl.c_str());
            }
        }
        // sequence end shading
        float xe = timeToX(seq.totalTime());
        if (xe < rulerMax.x)
            dl->AddRectFilled(ImVec2(std::max(xe, laneX0), rulerMin.y), rulerMax, IM_COL32(0, 0, 0, 90));
    }
    dl->PopClipRect();

    // ruler interaction (scrub)
    ImGui::SetCursorScreenPos(rulerMin);
    ImGui::InvisibleButton("##ruler", ImVec2(avail.x, RULER_H));
    bool rulerHovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActivated())
    {
        if (io.MousePos.x < laneX0) seq.select();
        else drag = Drag::Scrub;
    }
    if (drag == Drag::Scrub)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            seq.setTime(std::max(0.0, xToTime(io.MousePos.x)));
        else drag = Drag::None;
    }

    // ============================================================ LANES CHILD
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + RULER_H));
    float lanesH = avail.y - RULER_H - HSCROLL_H;
    ImGui::BeginChild("##lanes", ImVec2(avail.x, lanesH), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollWithMouse); // wheel handled manually (zoom/pan/scroll)
    ImDrawList* cdl = ImGui::GetWindowDrawList();

    float contentH = LANE_GAP;
    for (auto& l : seq.layers) contentH += l->uiHeight + LANE_GAP;
    contentH += 70; // empty drop zone at the bottom

    float canvasW = avail.x;
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::InvisibleButton("##canvas", ImVec2(canvasW, std::max(contentH, lanesH)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    bool canvasHovered = ImGui::IsItemHovered();
    ImVec2 canvasP0 = ImGui::GetItemRectMin();
    ImVec2 mouse = io.MousePos;

    float cLaneX0 = canvasP0.x + HEADER_W;
    float cLaneX1 = canvasP0.x + canvasW;

    // geometry
    std::vector<LaneGeom> geoms;
    {
        float y = canvasP0.y + LANE_GAP;
        int idx = 0;
        for (auto& l : seq.layers)
        {
            LaneGeom g; g.layer = l.get(); g.index = idx++;
            g.y0 = y; g.y1 = y + l->uiHeight;
            geoms.push_back(g);
            y = g.y1 + LANE_GAP;
        }
    }
    auto laneAtY = [&](float y) -> const LaneGeom*
    {
        for (auto& g : geoms) if (y >= g.y0 && y < g.y1 + LANE_GAP) return &g;
        return nullptr;
    };

    // ============================================================ HIT TESTING
    Hit hit;
    if (canvasHovered)
    {
        const LaneGeom* g = laneAtY(mouse.y);
        if (g)
        {
            hit.layer = g->layer;
            hit.laneIdx = g->index;
            if (mouse.x < cLaneX0)
            {
                hit.kind = (mouse.y > g->y1 - 5.f) ? Hit::HeaderGrip : Hit::Header;
            }
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
                        if (mouse.x >= x0 && mouse.x < x1 && mouse.y >= g->y0 && mouse.y < g->y1)
                        {
                            float edge = std::min(7.f, (x1 - x0) * 0.25f);
                            if (mouse.x < x0 + edge)       hit.kind = Hit::ClipL;
                            else if (mouse.x > x1 - edge)  hit.kind = Hit::ClipR;
                            else                           hit.kind = Hit::ClipBody;
                            hit.clip = c;
                            break;
                        }
                    }
                }
                else if (auto* al = dynamic_cast<AutomationLayer*>(g->layer))
                {
                    float mn = al->rangeMinP->floatValue(), mx = al->rangeMaxP->floatValue();
                    float range = (mx - mn) == 0 ? 1.f : (mx - mn);
                    // bezier handles of selected keys have priority
                    for (size_t i = 0; i < al->keys.size(); i++)
                    {
                        AutoKey& k = al->keys[i];
                        if (!al->selectedKeys.count(k.id) || k.easing != EasingType::Bezier) continue;
                        if (i + 1 >= al->keys.size()) continue;
                        AutoKey& nk = al->keys[i + 1];
                        double segDur = nk.time - k.time;
                        float na = (k.value - mn) / range, nb = (nk.value - mn) / range;
                        ImVec2 h1(timeToX(k.time + k.ep.a1.x * segDur), normToY(*g, na + k.ep.a1.y));
                        ImVec2 h2(timeToX(nk.time + k.ep.a2.x * segDur), normToY(*g, nb + k.ep.a2.y));
                        if (dist2(mouse, h1) < 49) { hit.kind = Hit::BezA1; hit.keyId = k.id; break; }
                        if (dist2(mouse, h2) < 49) { hit.kind = Hit::BezA2; hit.keyId = k.id; break; }
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
            }
        }
    }

    // cursor feedback
    if (hit.kind == Hit::ClipL || hit.kind == Hit::ClipR || drag == Drag::ResizeL || drag == Drag::ResizeR)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (hit.kind == Hit::HeaderGrip || drag == Drag::LayerHeight)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

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
        case Hit::Header:
        {
            hit.layer->select(io.KeyCtrl);
            if (dbl)
            {
                ctxLayerId = hit.layer->id;
                snprintf(renameBuf, sizeof(renameBuf), "%s", hit.layer->niceName.c_str());
                wantRenamePopup = true;
            }
            break;
        }
        case Hit::HeaderGrip:
            drag = Drag::LayerHeight;
            dragLayerId = hit.layer->id;
            dragOrigF = hit.layer->uiHeight;
            break;

        case Hit::ClipBody:
        {
            if (dbl) { hit.clip->select(); break; }
            bool wasSelected = hit.clip->isSelected();
            if (io.KeyCtrl) { Selection::get().toggle(hit.clip); }
            else if (!wasSelected) { Selection::get().set(hit.clip); }
            if (!hit.clip->isSelected()) break; // ctrl-deselected: no drag

            drag = Drag::MoveClips;
            dragItemId = hit.clip->id;
            dragLayerId = hit.layer->id;
            dragGrabDT = xToTime(mouse.x) - hit.clip->start();
            dragClips.clear();
            for (Clip* c : Selection::get().getAs<Clip>())
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
        case Hit::Lane:
        {
            double t = snapTime(seq, xToTime(mouse.x), bypassSnap);
            if (dbl && hit.layer)
            {
                if (auto* cl = dynamic_cast<ClipLayer*>(hit.layer))
                {
                    double len = std::max(snapStep(pps) * 4.0, 1.0);
                    Clip* c = cl->addClip(Clip::CType::Block, "Clip", t, len);
                    pushClipAdded(sp, cl->id, c->save(), "Add Clip");
                    c->select();
                }
                else if (auto* al = dynamic_cast<AutomationLayer*>(hit.layer))
                {
                    json pre = al->keysToJson();
                    float mn = al->rangeMinP->floatValue(), mx = al->rangeMaxP->floatValue();
                    float v = mn + yToNorm(*laneAtY(mouse.y), mouse.y) * (mx - mn);
                    AutoKey* k = al->addKey(t, v);
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
            }
            if (!rubberAdd)
            {
                Selection::get().clear();
                for (auto& l : seq.layers)
                {
                    if (auto* al = dynamic_cast<AutomationLayer*>(l.get())) al->selectedKeys.clear();
                    else if (auto* gl = dynamic_cast<GradientLayer*>(l.get())) gl->selectedKeys.clear();
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
    if (drag != Drag::None && drag != Drag::Scrub && drag != Drag::PanH && drag != Drag::HScroll)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (std::fabs(mouse.x - dragStartMouse.x) + std::fabs(mouse.y - dragStartMouse.y) > 3.f)
                dragMoved = true;

            // horizontal autoscroll while dragging near edges
            if (drag != Drag::Rubber && drag != Drag::LayerHeight && dragMoved)
            {
                if (mouse.x > cLaneX1 - 15) viewStart += (mouse.x - (cLaneX1 - 15)) * 0.4 / pps * (io.DeltaTime * 60.0);
                if (mouse.x < cLaneX0 + 15) viewStart = std::max(0.0, viewStart - ((cLaneX0 + 15) - mouse.x) * 0.4 / pps * (io.DeltaTime * 60.0));
            }

            switch (drag)
            {
            case Drag::MoveClips:
            {
                if (!dragMoved) break;
                // horizontal move
                ClipRef* grabRef = nullptr;
                for (auto& r : dragClips) if (r.clip == dragItemId) grabRef = &r;
                if (!grabRef) break;
                double target = snapTime(seq, xToTime(mouse.x) - dragGrabDT, bypassSnap);
                double delta = target - grabRef->start;
                for (auto& r : dragClips)
                    delta = std::max(delta, -r.start); // keep everything >= 0
                for (auto& r : dragClips)
                    if (Clip* c = seq.findClip(r.clip))
                        c->startP->setValue((float)(r.start + delta));

                // vertical move between clip layers
                const LaneGeom* tg = laneAtY(mouse.y);
                if (tg && dynamic_cast<ClipLayer*>(tg->layer))
                {
                    Clip* grabbed = seq.findClip(dragItemId);
                    int curIdx = grabbed ? seq.layerIndex(grabbed->layer) : -1;
                    int lDelta = tg->index - curIdx;
                    if (lDelta != 0 && grabbed)
                    {
                        // check all selected clips can shift by lDelta onto clip layers
                        bool ok = true;
                        for (auto& r : dragClips)
                        {
                            Clip* c = seq.findClip(r.clip);
                            if (!c) { ok = false; break; }
                            int ni = seq.layerIndex(c->layer) + lDelta;
                            if (ni < 0 || ni >= (int)seq.layers.size() ||
                                !dynamic_cast<ClipLayer*>(seq.layers[ni].get())) { ok = false; break; }
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
                break;
            }
            case Drag::ResizeL:
            {
                Clip* c = seq.findClip(dragItemId);
                if (!c) break;
                double t = snapTime(seq, xToTime(mouse.x), bypassSnap);
                t = std::min(t, dragOrigA + dragOrigB - 0.05);
                t = std::max(0.0, t);
                double d = t - dragOrigA;
                c->startP->setValue((float)t);
                c->lengthP->setValue((float)(dragOrigB - d));
                if (c->ctype == Clip::CType::Audio)
                    c->offsetP->setValue((float)std::max(0.0, dragOrigC + d));
                break;
            }
            case Drag::ResizeR:
            {
                Clip* c = seq.findClip(dragItemId);
                if (!c) break;
                double t = snapTime(seq, xToTime(mouse.x), bypassSnap);
                c->lengthP->setValue((float)std::max(0.05, t - dragOrigA));
                break;
            }
            case Drag::AutoKey:
            {
                auto* al = dynamic_cast<AutomationLayer*>(seq.findLayer(dragLayerId));
                const LaneGeom* g = nullptr;
                for (auto& gg : geoms) if (gg.layer == al) g = &gg;
                if (!al || !g) break;
                if (AutoKey* k = al->findKey(dragItemId))
                {
                    k->time = snapTime(seq, xToTime(mouse.x), bypassSnap);
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
                const LaneGeom* g = nullptr;
                for (auto& gg : geoms) if (gg.layer == al) g = &gg;
                if (!al || !g) break;
                AutoKey* k = al->findKey(dragItemId);
                if (!k) break;
                // find next key
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
            case Drag::LayerHeight:
            {
                if (Layer* l = seq.findLayer(dragLayerId))
                    l->uiHeight = std::max(26.f, std::min(400.f, dragOrigF + (mouse.y - dragStartMouse.y)));
                break;
            }
            case Drag::Rubber:
            {
                ImVec2 rMin(std::min(rubberStart.x, mouse.x), std::min(rubberStart.y, mouse.y));
                ImVec2 rMax(std::max(rubberStart.x, mouse.x), std::max(rubberStart.y, mouse.y));
                // rebuild selection = base + hits
                auto& sel = Selection::get();
                sel.items = rubberBaseSel;
                sel.revision++;
                for (auto& g : geoms)
                {
                    if (g.y1 < rMin.y || g.y0 > rMax.y) continue;
                    if (auto* cl = dynamic_cast<ClipLayer*>(g.layer))
                    {
                        for (auto& c : cl->clips)
                        {
                            float x0 = timeToX(c->start()), x1 = timeToX(c->end());
                            if (x1 >= rMin.x && x0 <= rMax.x && g.y1 >= rMin.y && g.y0 <= rMax.y)
                                if (std::find(sel.items.begin(), sel.items.end(), c.get()) == sel.items.end())
                                    sel.items.push_back(c.get());
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
                }
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
                        [sp, recs]
                        {
                            for (auto& r : recs)
                            {
                                physMoveClip(*sp, r.clip, r.newLayer);
                                if (Clip* c = sp->findClip(r.clip)) c->startP->setValue((float)r.newStart);
                            }
                        },
                        [sp, recs]
                        {
                            for (auto& r : recs)
                            {
                                physMoveClip(*sp, r.clip, r.oldLayer);
                                if (Clip* c = sp->findClip(r.clip)) c->startP->setValue((float)r.oldStart);
                            }
                        },
                        { sp });
                }
                break;
            }
            case Drag::ResizeL:
            case Drag::ResizeR:
            {
                Clip* c = seq.findClip(dragItemId);
                if (c && dragMoved)
                {
                    uint64_t cid = dragItemId;
                    double oS = dragOrigA, oL = dragOrigB, oO = dragOrigC;
                    double nS = c->start(), nL = c->length();
                    double nO = c->ctype == Clip::CType::Audio ? c->offsetP->floatValue() : 0.0;
                    UndoManager::get().pushDone("Resize Clip",
                        [sp, cid, nS, nL, nO]
                        {
                            if (Clip* cc = sp->findClip(cid))
                            {
                                cc->startP->setValue((float)nS);
                                cc->lengthP->setValue((float)nL);
                                if (cc->ctype == Clip::CType::Audio) cc->offsetP->setValue((float)nO);
                            }
                        },
                        [sp, cid, oS, oL, oO]
                        {
                            if (Clip* cc = sp->findClip(cid))
                            {
                                cc->startP->setValue((float)oS);
                                cc->lengthP->setValue((float)oL);
                                if (cc->ctype == Clip::CType::Audio) cc->offsetP->setValue((float)oO);
                            }
                        },
                        { sp });
                }
                break;
            }
            case Drag::AutoKey:
            case Drag::BezierA1:
            case Drag::BezierA2:
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
            case Drag::Rubber:
            {
                if (!dragMoved && hit.kind == Hit::Lane && hit.layer && !rubberAdd)
                    hit.layer->select();
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
        case Hit::ClipBody: case Hit::ClipL: case Hit::ClipR:
            ctxItemId = hit.clip->id;
            if (!hit.clip->isSelected()) Selection::get().set(hit.clip);
            ImGui::OpenPopup("clip_ctx");
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

    // ============================================================ OPERATION HELPERS
    auto deleteSelection = [&]()
    {
        struct ClipRec { uint64_t layer; json data; };
        std::vector<ClipRec> clipRecs;
        for (Clip* c : Selection::get().getAs<Clip>())
            clipRecs.push_back({ c->layer->id, c->save() });

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
        }
        for (auto& r : clipRecs)
            if (auto* cl = dynamic_cast<ClipLayer*>(seq.findLayer(r.layer)))
                cl->removeClip(r.data.value("id", (uint64_t)0));

        if (clipRecs.empty() && keyRecs.empty()) return;
        UndoManager::get().pushDone("Delete",
            [sp, clipRecs, keyRecs]
            {
                for (auto& r : clipRecs)
                    if (auto* cl = dynamic_cast<ClipLayer*>(sp->findLayer(r.layer)))
                        cl->removeClip(r.data.value("id", (uint64_t)0));
                for (auto& r : keyRecs)
                {
                    Layer* l = sp->findLayer(r.layer);
                    if (auto* al = dynamic_cast<AutomationLayer*>(l)) al->keysFromJson(r.post);
                    else if (auto* gl = dynamic_cast<GradientLayer*>(l)) gl->keysFromJson(r.post);
                }
            },
            [sp, clipRecs, keyRecs]
            {
                for (auto& r : clipRecs)
                    if (auto* cl = dynamic_cast<ClipLayer*>(sp->findLayer(r.layer)))
                        cl->addClipFromJson(r.data);
                for (auto& r : keyRecs)
                {
                    Layer* l = sp->findLayer(r.layer);
                    if (auto* al = dynamic_cast<AutomationLayer*>(l)) al->keysFromJson(r.pre);
                    else if (auto* gl = dynamic_cast<GradientLayer*>(l)) gl->keysFromJson(r.pre);
                }
            },
            { sp });
    };

    auto duplicateSelection = [&]()
    {
        auto clips = Selection::get().getAs<Clip>();
        if (clips.empty()) return;
        struct Rec { uint64_t layer; json data; };
        std::vector<Rec> recs;
        Selection::get().clear();
        for (Clip* c : clips)
        {
            json j = c->save();
            j["id"] = seq.newId();
            j["params"]["start"] = (float)c->end();
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
            double n = std::max(0.0, c->start() + dt);
            rs.push_back({ c->id, c->start(), n });
            c->startP->setValue((float)n);
        }
        UndoManager::get().pushDone("Nudge",
            [sp, rs] { for (auto& r : rs) if (Clip* c = sp->findClip(r.id)) c->startP->setValue((float)r.n); },
            [sp, rs] { for (auto& r : rs) if (Clip* c = sp->findClip(r.id)) c->startP->setValue((float)r.o); },
            { sp });
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
        Clip* c = cl->addClip(mp.kind == 1 ? Clip::CType::Audio : Clip::CType::Block,
                              mp.name[0] ? mp.name : "Clip", t,
                              mp.duration > 0 ? mp.duration : 2.0);
        ImVec4 col(mp.color[0], mp.color[1], mp.color[2], mp.color[3]);
        c->colorP->setValue(col, false);
        c->colorP->defaultValue = col;
        if (mp.kind == 1 && mp.file[0]) c->setAudioFile(mp.file, mp.duration <= 0);
        pushClipAdded(sp, cl->id, c->save(), "Drop Media");
        c->select();
        return c;
    };

    // ============================================================ DRAWING
    ImVec4 accent(1.f, 0.573f, 0.184f, 1.f);
    ImU32 accentU = col32(accent);
    float lanesBottom = geoms.empty() ? canvasP0.y : geoms.back().y1;

    // lane + header backgrounds
    for (auto& g : geoms)
    {
        Layer* l = g.layer;
        bool lSel = l->isSelected();
        ImVec4 lcol = l->colorP->color();

        // header
        ImU32 hbg = lSel ? IM_COL32(48, 45, 40, 255) : IM_COL32(38, 38, 42, 255);
        cdl->AddRectFilled(ImVec2(canvasP0.x, g.y0), ImVec2(cLaneX0 - 2, g.y1), hbg, 4.f);
        cdl->AddRectFilled(ImVec2(canvasP0.x, g.y0), ImVec2(canvasP0.x + 3, g.y1), col32(lcol), 2.f);
        if (lSel)
            cdl->AddRect(ImVec2(canvasP0.x, g.y0), ImVec2(cLaneX0 - 2, g.y1), accentU, 4.f, 0, 1.f);
        cdl->PushClipRect(ImVec2(canvasP0.x, g.y0), ImVec2(cLaneX0 - 6, g.y1), true);
        cdl->AddText(ImVec2(canvasP0.x + 10, g.y0 + 5), IM_COL32(225, 225, 228, 255), l->niceName.c_str());
        cdl->AddText(ImVec2(canvasP0.x + 10, g.y0 + 5 + ImGui::GetFontSize() + 1),
                     IM_COL32(140, 140, 146, 255), Layer::ltypeName(l->ltype));
        cdl->PopClipRect();
        cdl->AddLine(ImVec2(canvasP0.x + 6, g.y1 - 2.5f), ImVec2(cLaneX0 - 10, g.y1 - 2.5f),
                     IM_COL32(90, 90, 95, 120), 1.f);

        // lane background
        ImU32 lbg = (g.index & 1) ? IM_COL32(30, 30, 33, 255) : IM_COL32(33, 33, 36, 255);
        cdl->AddRectFilled(ImVec2(cLaneX0, g.y0), ImVec2(cLaneX1, g.y1), lbg);
        cdl->AddRectFilled(ImVec2(cLaneX0, g.y0), ImVec2(cLaneX1, g.y1), col32(lcol, 0.05f));
    }

    // vertical grid over lanes
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
        // beyond sequence end
        float xe = timeToX(seq.totalTime());
        if (xe < cLaneX1)
            cdl->AddRectFilled(ImVec2(std::max(xe, cLaneX0), canvasP0.y), ImVec2(cLaneX1, lanesBottom),
                               IM_COL32(0, 0, 0, 70));
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
    for (auto& g : geoms)
    {
        cdl->PushClipRect(ImVec2(cLaneX0, g.y0), ImVec2(cLaneX1, g.y1), true);

        if (auto* cl = dynamic_cast<ClipLayer*>(g.layer))
        {
            for (auto& cu : cl->clips)
            {
                Clip* c = cu.get();
                float x0 = timeToX(c->start());
                float x1 = timeToX(c->end());
                if (x1 < cLaneX0 - 2 || x0 > cLaneX1 + 2) continue;
                float y0 = g.y0 + 3, y1 = g.y1 - 3;
                ImVec4 col = c->colorP->color();
                bool sel = c->isSelected();
                bool hov = (hit.clip == c);
                ImVec4 fill = sel ? lighten(col, 0.08f) : col;
                cdl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), col32(fill, 0.95f), 4.f);

                // waveform
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
                        float px0 = std::max(x0, cLaneX0);
                        float px1 = std::min(x1, cLaneX1);
                        ImU32 wcol = IM_COL32(255, 255, 255, 110);
                        for (float px = px0; px < px1; px += 1.f)
                        {
                            double lt0 = (px - x0) / pps + off;
                            double lt1 = lt0 + 1.0 / pps;
                            float mn, mx;
                            if (c->asset->peaks.query(lt0, lt1, mn, mx))
                            {
                                mn = std::max(-1.f, std::min(1.f, mn * gain));
                                mx = std::max(-1.f, std::min(1.f, mx * gain));
                                cdl->AddLine(ImVec2(px, mid - mx * half), ImVec2(px, mid - mn * half + 1), wcol, 1.f);
                            }
                        }
                    }
                }

                // name
                cdl->PushClipRect(ImVec2(x0 + 2, y0), ImVec2(x1 - 2, y1), true);
                cdl->AddText(ImVec2(x0 + 6, y0 + 3), IM_COL32(10, 10, 12, 220), c->niceName.c_str());
                cdl->AddText(ImVec2(x0 + 5, y0 + 2), IM_COL32(255, 255, 255, 235), c->niceName.c_str());
                cdl->PopClipRect();

                // borders
                if (sel) cdl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), accentU, 4.f, 0, 2.f);
                else if (hov) cdl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(255, 255, 255, 110), 4.f, 0, 1.f);

                // edge accents
                if (hov && (hit.kind == Hit::ClipL || hit.kind == Hit::ClipR))
                {
                    float ex = hit.kind == Hit::ClipL ? x0 + 2 : x1 - 2;
                    cdl->AddLine(ImVec2(ex, y0 + 2), ImVec2(ex, y1 - 2), IM_COL32(255, 255, 255, 200), 2.f);
                }
            }
        }
        else if (auto* al = dynamic_cast<AutomationLayer*>(g.layer))
        {
            // horizontal guides
            for (float f : { 0.f, 0.25f, 0.5f, 0.75f, 1.f })
                cdl->AddLine(ImVec2(cLaneX0, normToY(g, f)), ImVec2(cLaneX1, normToY(g, f)),
                             IM_COL32(255, 255, 255, f == 0.5f ? 14 : 7), 1.f);

            float mn = al->rangeMinP->floatValue(), mx = al->rangeMaxP->floatValue();
            float range = (mx - mn) == 0 ? 1.f : (mx - mn);
            ImU32 curveCol = col32(lighten(al->colorP->color(), 0.15f));

            if (al->keys.empty())
            {
                cdl->AddText(ImVec2(cLaneX0 + 8, g.y0 + 6), IM_COL32(120, 120, 126, 200),
                             "Double-click to add keys");
            }
            else
            {
                // flat before first / after last
                const AutoKey& kf = al->keys.front();
                const AutoKey& kl = al->keys.back();
                float yf = normToY(g, (kf.value - mn) / range);
                float yl = normToY(g, (kl.value - mn) / range);
                if (timeToX(kf.time) > cLaneX0)
                    cdl->AddLine(ImVec2(cLaneX0, yf), ImVec2(timeToX(kf.time), yf), curveCol, 2.f);
                if (timeToX(kl.time) < cLaneX1)
                    cdl->AddLine(ImVec2(timeToX(kl.time), yl), ImVec2(cLaneX1, yl), curveCol, 2.f);

                // segments
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

                // keys
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
                        ImGui::SetTooltip("%.3fs  |  %.3f", k.time, k.value);
                }

                // bezier handles for selected bezier keys
                for (size_t i = 0; i + 1 < al->keys.size(); i++)
                {
                    AutoKey& k = al->keys[i];
                    if (!al->selectedKeys.count(k.id) || k.easing != EasingType::Bezier) continue;
                    AutoKey& nk = al->keys[i + 1];
                    double segDur = nk.time - k.time;
                    float na = (k.value - mn) / range, nb = (nk.value - mn) / range;
                    ImVec2 kp(timeToX(k.time), normToY(g, na));
                    ImVec2 np(timeToX(nk.time), normToY(g, nb));
                    ImVec2 h1(timeToX(k.time + k.ep.a1.x * segDur), normToY(g, na + k.ep.a1.y));
                    ImVec2 h2(timeToX(nk.time + k.ep.a2.x * segDur), normToY(g, nb + k.ep.a2.y));
                    cdl->AddLine(kp, h1, IM_COL32(255, 255, 255, 90), 1.f);
                    cdl->AddLine(np, h2, IM_COL32(255, 255, 255, 90), 1.f);
                    cdl->AddRectFilled(ImVec2(h1.x - 3, h1.y - 3), ImVec2(h1.x + 3, h1.y + 3), IM_COL32_WHITE);
                    cdl->AddRectFilled(ImVec2(h2.x - 3, h2.y - 3), ImVec2(h2.x + 3, h2.y + 3), IM_COL32_WHITE);
                }

                // live value dot at playhead
                float phx = timeToX(seq.currentTime);
                if (phx >= cLaneX0 && phx <= cLaneX1)
                {
                    float y = normToY(g, al->normValueAt(seq.currentTime));
                    cdl->AddCircleFilled(ImVec2(phx, y), 3.f, accentU);
                }
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
                    cdl->AddRectFilledMultiColor(ImVec2(xa, sy0), ImVec2(xb, sy1),
                                                 col32(a.color), col32(b.color),
                                                 col32(b.color), col32(a.color));
                }
                // markers
                for (auto& k : gl->keys)
                {
                    float x = timeToX(k.time);
                    if (x < cLaneX0 - 8 || x > cLaneX1 + 8) continue;
                    bool ksel = gl->selectedKeys.count(k.id) != 0;
                    drawDiamond(cdl, ImVec2(x, g.y1 - 7), 5.f, col32(k.color),
                                ksel ? accentU : IM_COL32(230, 230, 235, 255));
                }
            }
        }

        cdl->PopClipRect();
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

                if (target)
                {
                    cdl->AddRectFilled(ImVec2(timeToX(t), g->y0 + 3), ImVec2(timeToX(t + len), g->y1 - 3),
                                       col32(gcol), 4.f);
                    cdl->AddRect(ImVec2(timeToX(t), g->y0 + 3), ImVec2(timeToX(t + len), g->y1 - 3),
                                 accentU, 4.f, 0, 2.f);
                    if (pl->IsDelivery())
                        createClipFromPayload(target, t, mp);
                }
                else
                {
                    float gy = geoms.empty() ? canvasP0.y + LANE_GAP : geoms.back().y1 + LANE_GAP;
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
                    // apply to all selected keys (or just this one)
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
            // popup just closed -> commit color edits as one undo step
            if (auto* gl = dynamic_cast<GradientLayer*>(seq.findLayer(ctxLayerId)))
                pushKeysEdit(sp, gl->id, preEditJson, gl->keysToJson(), "Edit Color Key");
        }
        gkeyWasOpen = gkeyOpenNow;
    }

    auto layerOpsMenu = [&](Layer* l)
    {
        int idx = seq.layerIndex(l);
        if (ImGui::MenuItem("Rename..."))
        {
            ctxLayerId = l->id;
            snprintf(renameBuf, sizeof(renameBuf), "%s", l->niceName.c_str());
            wantRenamePopup = true;
        }
        if (ImGui::MenuItem("Move Up", nullptr, false, idx > 0)) moveLayerUndoable(idx, idx - 1);
        if (ImGui::MenuItem("Move Down", nullptr, false, idx < (int)seq.layers.size() - 1)) moveLayerUndoable(idx, idx + 1);
        if (ImGui::MenuItem("Duplicate Layer")) duplicateLayerUndoable(l->id);
        if (ImGui::MenuItem("Delete Layer")) removeLayerUndoable(l->id);
    };

    auto addLayerMenu = [&](int insertIdx)
    {
        if (ImGui::MenuItem("Clip Layer")) addLayerUndoable(Layer::LType::Clips, "Clips", insertIdx);
        if (ImGui::MenuItem("Automation Layer")) addLayerUndoable(Layer::LType::Automation, "Automation", insertIdx);
        if (ImGui::MenuItem("Gradient Layer")) addLayerUndoable(Layer::LType::Gradient, "Gradient", insertIdx);
    };

    if (ImGui::BeginPopup("lane_ctx"))
    {
        Layer* l = seq.findLayer(ctxLayerId);
        if (auto* cl = dynamic_cast<ClipLayer*>(l))
        {
            if (ImGui::MenuItem("Add Clip Here"))
            {
                Clip* c = cl->addClip(Clip::CType::Block, "Clip", ctxTime, std::max(1.0, snapStep(pps) * 4.0));
                pushClipAdded(sp, cl->id, c->save(), "Add Clip");
                c->select();
            }
            if (ImGui::MenuItem("Add Audio Clip Here"))
            {
                Clip* c = cl->addClip(Clip::CType::Audio, "Audio", ctxTime, 4.0);
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
            if (mouse.x < thX0 || mouse.x > thX0 + thW) // jump
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

    // ============================================================ RULER PLAYHEAD (over everything in ruler)
    {
        float phx = timeToX(seq.currentTime);
        if (phx >= laneX0 - 6 && phx <= origin.x + avail.x + 6)
        {
            ImU32 acc = col32(ImVec4(1.f, 0.573f, 0.184f, 1.f));
            dl->AddLine(ImVec2(phx, rulerMin.y + 14), ImVec2(phx, rulerMax.y), acc, 1.5f);
            ImVec2 tri[3] = { ImVec2(phx - 5, rulerMax.y - 8), ImVec2(phx + 5, rulerMax.y - 8), ImVec2(phx, rulerMax.y) };
            dl->AddConvexPolyFilled(tri, 3, acc);
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
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) seq.setTime(0);
        if (ImGui::IsKeyPressed(ImGuiKey_End)) seq.setTime(seq.totalTime());
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) deleteSelection();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelection();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) selectAllClips();
        if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.KeyCtrl) fitRequested = true;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) nudgeSelection(-snapStep(pps));
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) nudgeSelection(snapStep(pps));
    }

    ImGui::End();
}

} // namespace organic
