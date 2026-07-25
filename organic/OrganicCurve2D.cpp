#include "OrganicCurve2D.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace organic
{

static const int SEG_SAMPLES = 33;

Curve2D::Curve2D(const std::string& name) : Container(name)
{
    renamable = true;
}

Curve2DKey* Curve2D::addKey(ImVec2 pos, int index)
{
    Curve2DKey k;
    k.id = nextId++;
    k.pos = pos;
    if (index < 0 || index > (int)keys.size()) index = (int)keys.size();
    keys.insert(keys.begin() + index, k);
    rebuild();
    return findKey(k.id);
}

Curve2DKey* Curve2D::findKey(uint64_t id)
{
    for (auto& k : keys) if (k.id == id) return &k;
    return nullptr;
}

int Curve2D::keyIndex(uint64_t id) const
{
    for (size_t i = 0; i < keys.size(); i++) if (keys[i].id == id) return (int)i;
    return -1;
}

void Curve2D::removeKey(uint64_t id)
{
    keys.erase(std::remove_if(keys.begin(), keys.end(),
               [id](const Curve2DKey& k) { return k.id == id; }), keys.end());
    selectedKeys.erase(id);
    rebuild();
}

ImVec2 Curve2D::pointOnSegment(int seg, float t) const
{
    const Curve2DKey& a = keys[seg];
    const Curve2DKey& b = keys[seg + 1];
    if (!a.bezier)
        return ImVec2(a.pos.x + (b.pos.x - a.pos.x) * t, a.pos.y + (b.pos.y - a.pos.y) * t);
    ImVec2 c1(a.pos.x + a.a1.x, a.pos.y + a.a1.y);
    ImVec2 c2(b.pos.x + a.a2.x, b.pos.y + a.a2.y);
    float it = 1.f - t;
    float b0 = it * it * it, b1 = 3 * it * it * t, b2 = 3 * it * t * t, b3 = t * t * t;
    return ImVec2(a.pos.x * b0 + c1.x * b1 + c2.x * b2 + b.pos.x * b3,
                  a.pos.y * b0 + c1.y * b1 + c2.y * b2 + b.pos.y * b3);
}

void Curve2D::rebuild()
{
    segTables.clear();
    segStartLen.clear();
    length = 0.f;
    if (keys.size() < 2) return;
    for (size_t s = 0; s + 1 < keys.size(); s++)
    {
        std::vector<float> tab(SEG_SAMPLES, 0.f);
        ImVec2 prev = pointOnSegment((int)s, 0.f);
        for (int i = 1; i < SEG_SAMPLES; i++)
        {
            ImVec2 p = pointOnSegment((int)s, (float)i / (SEG_SAMPLES - 1));
            float dx = p.x - prev.x, dy = p.y - prev.y;
            tab[i] = tab[i - 1] + std::sqrt(dx * dx + dy * dy);
            prev = p;
        }
        segStartLen.push_back(length);
        length += tab.back();
        segTables.push_back(std::move(tab));
    }
}

ImVec2 Curve2D::valueAtLength(float l) const
{
    if (keys.empty()) return ImVec2(0, 0);
    if (keys.size() == 1 || segTables.empty()) return keys.front().pos;
    l = std::max(0.f, std::min(length, l));
    // find segment
    int seg = (int)segTables.size() - 1;
    for (size_t s = 0; s < segTables.size(); s++)
    {
        if (l <= segStartLen[s] + segTables[s].back() + 1e-6f) { seg = (int)s; break; }
    }
    float local = l - segStartLen[seg];
    const auto& tab = segTables[seg];
    // binary search in cumulative table
    int lo = 0, hi = SEG_SAMPLES - 1;
    while (lo + 1 < hi)
    {
        int mid = (lo + hi) / 2;
        if (tab[mid] < local) lo = mid; else hi = mid;
    }
    float span = tab[hi] - tab[lo];
    float f = span > 1e-9f ? (local - tab[lo]) / span : 0.f;
    float t = ((float)lo + f) / (SEG_SAMPLES - 1);
    return pointOnSegment(seg, t);
}

Curve2DKey* Curve2D::insertKeyAt(ImVec2 nearPos)
{
    if (keys.size() < 2) return addKey(nearPos);
    // find closest sampled point over all segments
    int bestSeg = 0;
    float bestT = 0.5f, bestD = 1e18f;
    for (size_t s = 0; s + 1 < keys.size(); s++)
    {
        for (int i = 0; i <= 32; i++)
        {
            float t = i / 32.f;
            ImVec2 p = pointOnSegment((int)s, t);
            float dx = p.x - nearPos.x, dy = p.y - nearPos.y;
            float d = dx * dx + dy * dy;
            if (d < bestD) { bestD = d; bestSeg = (int)s; bestT = t; }
        }
    }
    Curve2DKey& a = keys[bestSeg];
    Curve2DKey& b = keys[bestSeg + 1];
    Curve2DKey nk;
    nk.id = nextId++;
    if (a.bezier)
    {
        // split preserving shape
        ImVec2 c1(a.pos.x + a.a1.x, a.pos.y + a.a1.y);
        ImVec2 c2(b.pos.x + a.a2.x, b.pos.y + a.a2.y);
        ImVec2 l1, l2, mid, r1, r2;
        splitCubic(a.pos, c1, c2, b.pos, bestT, l1, l2, mid, r1, r2);
        nk.pos = mid;
        nk.bezier = true;
        // segment a->nk: a.a1 (out of a) + a.a2 (into nk); segment nk->b: nk.a1 + nk.a2
        a.a1  = ImVec2(l1.x - a.pos.x, l1.y - a.pos.y);
        a.a2  = ImVec2(l2.x - mid.x, l2.y - mid.y);
        nk.a1 = ImVec2(r1.x - mid.x, r1.y - mid.y);
        nk.a2 = ImVec2(r2.x - b.pos.x, r2.y - b.pos.y);
    }
    else nk.pos = pointOnSegment(bestSeg, bestT);
    keys.insert(keys.begin() + bestSeg + 1, nk);
    rebuild();
    return findKey(nk.id);
}

json Curve2D::keysToJson() const
{
    json arr = json::array();
    for (auto& k : keys)
        arr.push_back({ { "id", k.id }, { "p", { k.pos.x, k.pos.y } }, { "bez", k.bezier },
                        { "a1", { k.a1.x, k.a1.y } }, { "a2", { k.a2.x, k.a2.y } } });
    return arr;
}

void Curve2D::keysFromJson(const json& arr)
{
    keys.clear();
    selectedKeys.clear();
    if (arr.is_array())
    {
        for (auto& kj : arr)
        {
            Curve2DKey k;
            k.id = kj.value("id", (uint64_t)0);
            if (kj.contains("p")) k.pos = ImVec2(kj["p"][0], kj["p"][1]);
            k.bezier = kj.value("bez", false);
            if (kj.contains("a1")) k.a1 = ImVec2(kj["a1"][0], kj["a1"][1]);
            if (kj.contains("a2")) k.a2 = ImVec2(kj["a2"][0], kj["a2"][1]);
            if (k.id == 0) k.id = nextId++;
            nextId = std::max(nextId, k.id + 1);
            keys.push_back(k);
        }
    }
    rebuild();
}

json Curve2D::save() const
{
    json j = Container::save();
    j["nextId"] = nextId;
    j["keys"] = keysToJson();
    return j;
}

void Curve2D::load(const json& j)
{
    Container::load(j);
    if (j.contains("keys")) keysFromJson(j["keys"]);
    if (j.contains("nextId")) nextId = std::max(nextId, j["nextId"].get<uint64_t>());
}

void Curve2D::inspectorGui()
{
    Container::inspectorGui();
    ImGui::TextDisabled("%d key(s), length %.2f", (int)keys.size(), length);
    for (uint64_t kid : std::vector<uint64_t>(selectedKeys.begin(), selectedKeys.end()))
    {
        Curve2DKey* k = findKey(kid);
        if (!k) continue;
        ImGui::PushID((int)(intptr_t)kid);
        ImGui::SeparatorText("Key");
        float p[2] = { k->pos.x, k->pos.y };
        if (ImGui::DragFloat2("Position", p, 0.01f)) { k->pos = ImVec2(p[0], p[1]); rebuild(); }
        bool bez = k->bezier;
        if (ImGui::Checkbox("Bezier segment", &bez)) { k->bezier = bez; rebuild(); }
        ImGui::PopID();
    }
}

// ================================================================ editor
struct C2DState
{
    enum class Drag { None, Key, A1, A2, Pan, Rubber };
    Drag drag = Drag::None;
    Curve2D* curve = nullptr;
    uint64_t keyId = 0;
    ImVec2 startMouse, panStart, rubberStart;
    json preEdit;
    bool moved = false;
    bool rubberAdd = false;
    // view (per curve, static map would be nicer; single instance is fine for the demo)
    ImVec2 center = ImVec2(0.5f, 0.5f);
    float  zoom = 260.f; // pixels per world unit
    bool   viewInit = false;
};
static C2DState s_c2d;

static void c2dPushEdit(Curve2D* c, const json& pre, const char* name)
{
    json post = c->keysToJson();
    if (post == pre) return;
    UndoManager::get().pushDone(name,
        [c, post] { c->keysFromJson(post); },
        [c, pre]  { c->keysFromJson(pre); },
        { c });
}

void Curve2DEditor(Curve2D& c, float normPos)
{
    ImGui::PushID(&c);
    ImGuiIO& io = ImGui::GetIO();

    ImGui::TextDisabled("Double-click: add/insert key | drag keys & handles | right-click key: options | Alt/middle drag: pan | Ctrl+wheel: zoom");

    ImGui::BeginChild("##c2d", ImVec2(0, 0), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 cp0 = ImGui::GetCursorScreenPos();
    ImVec2 csz = ImGui::GetContentRegionAvail();
    if (csz.x < 60 || csz.y < 60) { ImGui::EndChild(); ImGui::PopID(); return; }
    ImVec2 cc(cp0.x + csz.x * 0.5f, cp0.y + csz.y * 0.5f);

    if (!s_c2d.viewInit) { s_c2d.viewInit = true; }
    float& zoom = s_c2d.zoom;
    ImVec2& center = s_c2d.center;
    auto W2S = [&](ImVec2 w) { return ImVec2(cc.x + (w.x - center.x) * zoom, cc.y - (w.y - center.y) * zoom); };
    auto S2W = [&](ImVec2 s) { return ImVec2((s.x - cc.x) / zoom + center.x, -(s.y - cc.y) / zoom + center.y); };

    ImGui::InvisibleButton("##c2dbtn", csz,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    ImVec2 mouse = io.MousePos;

    // hit test
    uint64_t hitKey = 0;
    int hitAnchor = 0; // 1 = a1, 2 = a2 (of segment starting at selected key)
    if (hovered)
    {
        for (size_t i = 0; i < c.keys.size(); i++)
        {
            auto& k = c.keys[i];
            if (c.selectedKeys.count(k.id) && k.bezier && i + 1 < c.keys.size())
            {
                ImVec2 h1 = W2S(ImVec2(k.pos.x + k.a1.x, k.pos.y + k.a1.y));
                ImVec2 h2 = W2S(ImVec2(c.keys[i + 1].pos.x + k.a2.x, c.keys[i + 1].pos.y + k.a2.y));
                float d1 = (mouse.x - h1.x) * (mouse.x - h1.x) + (mouse.y - h1.y) * (mouse.y - h1.y);
                float d2 = (mouse.x - h2.x) * (mouse.x - h2.x) + (mouse.y - h2.y) * (mouse.y - h2.y);
                if (d1 < 49) { hitKey = k.id; hitAnchor = 1; break; }
                if (d2 < 49) { hitKey = k.id; hitAnchor = 2; break; }
            }
        }
        if (!hitKey)
        {
            for (auto& k : c.keys)
            {
                ImVec2 p = W2S(k.pos);
                float d = (mouse.x - p.x) * (mouse.x - p.x) + (mouse.y - p.y) * (mouse.y - p.y);
                if (d < 56) { hitKey = k.id; break; }
            }
        }
    }

    // wheel
    if (hovered && io.MouseWheel != 0)
    {
        if (io.KeyCtrl)
        {
            ImVec2 w = S2W(mouse);
            zoom = std::max(30.f, std::min(3000.f, zoom * std::pow(1.15f, io.MouseWheel)));
            ImVec2 sAfter = W2S(w);
            center.x += (sAfter.x - mouse.x) / zoom;
            center.y -= (sAfter.y - mouse.y) / zoom;
        }
        else if (io.KeyShift) center.x -= io.MouseWheel * 30.f / zoom;
        else                  center.y += io.MouseWheel * 30.f / zoom;
    }

    // press
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        s_c2d.curve = &c;
        s_c2d.startMouse = mouse;
        s_c2d.moved = false;
        bool dbl = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

        if (dbl)
        {
            json pre = c.keysToJson();
            Curve2DKey* k = c.keys.size() < 2 ? c.addKey(S2W(mouse)) : c.insertKeyAt(S2W(mouse));
            c.selectedKeys.clear();
            c.selectedKeys.insert(k->id);
            c.select();
            c2dPushEdit(&c, pre, "Add Curve Key");
            s_c2d.drag = C2DState::Drag::None;
        }
        else if (io.KeyAlt)
        {
            s_c2d.drag = C2DState::Drag::Pan;
            s_c2d.panStart = center;
        }
        else if (hitKey && hitAnchor)
        {
            s_c2d.drag = hitAnchor == 1 ? C2DState::Drag::A1 : C2DState::Drag::A2;
            s_c2d.keyId = hitKey;
            s_c2d.preEdit = c.keysToJson();
        }
        else if (hitKey)
        {
            if (io.KeyCtrl)
            {
                if (c.selectedKeys.count(hitKey)) c.selectedKeys.erase(hitKey);
                else c.selectedKeys.insert(hitKey);
            }
            else if (!c.selectedKeys.count(hitKey))
            {
                c.selectedKeys.clear();
                c.selectedKeys.insert(hitKey);
            }
            c.select();
            if (c.selectedKeys.count(hitKey))
            {
                s_c2d.drag = C2DState::Drag::Key;
                s_c2d.keyId = hitKey;
                s_c2d.preEdit = c.keysToJson();
            }
        }
        else
        {
            s_c2d.drag = C2DState::Drag::Rubber;
            s_c2d.rubberStart = mouse;
            s_c2d.rubberAdd = io.KeyCtrl;
            if (!io.KeyCtrl) c.selectedKeys.clear();
        }
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
    {
        s_c2d.curve = &c;
        s_c2d.drag = C2DState::Drag::Pan;
        s_c2d.startMouse = mouse;
        s_c2d.panStart = center;
    }

    // drag / release
    if (s_c2d.curve == &c && s_c2d.drag != C2DState::Drag::None)
    {
        bool lDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        bool mDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        if (std::fabs(mouse.x - s_c2d.startMouse.x) + std::fabs(mouse.y - s_c2d.startMouse.y) > 3.f)
            s_c2d.moved = true;

        switch (s_c2d.drag)
        {
        case C2DState::Drag::Pan:
            if (lDown || mDown)
            {
                center.x = s_c2d.panStart.x - (mouse.x - s_c2d.startMouse.x) / zoom;
                center.y = s_c2d.panStart.y + (mouse.y - s_c2d.startMouse.y) / zoom;
            }
            else s_c2d.drag = C2DState::Drag::None;
            break;
        case C2DState::Drag::Key:
            if (lDown)
            {
                // move all selected keys by mouse delta
                static ImVec2 lastMouse;
                ImVec2 dw((mouse.x - s_c2d.startMouse.x) / zoom, -(mouse.y - s_c2d.startMouse.y) / zoom);
                // recompute from preEdit positions to avoid drift
                json pre = s_c2d.preEdit;
                for (auto& kj : pre)
                {
                    uint64_t id = kj.value("id", (uint64_t)0);
                    if (!c.selectedKeys.count(id)) continue;
                    if (Curve2DKey* k = c.findKey(id))
                        k->pos = ImVec2((float)kj["p"][0] + dw.x, (float)kj["p"][1] + dw.y);
                }
                c.rebuild();
                (void)lastMouse;
            }
            else
            {
                if (s_c2d.moved) c2dPushEdit(&c, s_c2d.preEdit, "Move Curve Keys");
                s_c2d.drag = C2DState::Drag::None;
            }
            break;
        case C2DState::Drag::A1:
        case C2DState::Drag::A2:
        {
            int idx = c.keyIndex(s_c2d.keyId);
            if (lDown && idx >= 0 && idx + 1 < (int)c.keys.size())
            {
                Curve2DKey& k = c.keys[idx];
                Curve2DKey& nk = c.keys[idx + 1];
                ImVec2 w = S2W(mouse);
                if (s_c2d.drag == C2DState::Drag::A1)
                    k.a1 = ImVec2(w.x - k.pos.x, w.y - k.pos.y);
                else
                    k.a2 = ImVec2(w.x - nk.pos.x, w.y - nk.pos.y);
                c.rebuild();
            }
            else
            {
                if (s_c2d.moved) c2dPushEdit(&c, s_c2d.preEdit, "Edit Curve Handles");
                s_c2d.drag = C2DState::Drag::None;
            }
            break;
        }
        case C2DState::Drag::Rubber:
            if (lDown)
            {
                ImVec2 rMin(std::min(s_c2d.rubberStart.x, mouse.x), std::min(s_c2d.rubberStart.y, mouse.y));
                ImVec2 rMax(std::max(s_c2d.rubberStart.x, mouse.x), std::max(s_c2d.rubberStart.y, mouse.y));
                if (!s_c2d.rubberAdd) c.selectedKeys.clear();
                for (auto& k : c.keys)
                {
                    ImVec2 p = W2S(k.pos);
                    if (p.x >= rMin.x && p.x <= rMax.x && p.y >= rMin.y && p.y <= rMax.y)
                        c.selectedKeys.insert(k.id);
                }
            }
            else s_c2d.drag = C2DState::Drag::None;
            break;
        default: break;
        }
    }

    // right click on key
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        if (hitKey)
        {
            s_c2d.keyId = hitKey;
            if (!c.selectedKeys.count(hitKey))
            {
                c.selectedKeys.clear();
                c.selectedKeys.insert(hitKey);
            }
            ImGui::OpenPopup("##c2dkey");
        }
    }

    // ---- draw
    dl->PushClipRect(cp0, ImVec2(cp0.x + csz.x, cp0.y + csz.y), true);
    dl->AddRectFilled(cp0, ImVec2(cp0.x + csz.x, cp0.y + csz.y), IM_COL32(24, 24, 27, 255));

    // grid (0.1 world units)
    {
        float stepW = 0.1f;
        while (stepW * zoom < 16.f) stepW *= 2.f;
        ImVec2 wTL = S2W(cp0);
        ImVec2 wBR = S2W(ImVec2(cp0.x + csz.x, cp0.y + csz.y));
        float x0 = std::floor(wTL.x / stepW) * stepW;
        for (float x = x0; x <= wBR.x; x += stepW)
        {
            float sx = W2S(ImVec2(x, 0)).x;
            bool axis = std::fabs(x) < stepW * 0.25f;
            dl->AddLine(ImVec2(sx, cp0.y), ImVec2(sx, cp0.y + csz.y), IM_COL32(255, 255, 255, axis ? 30 : 9));
        }
        float y0 = std::floor(wBR.y / stepW) * stepW;
        for (float y = y0; y <= wTL.y; y += stepW)
        {
            float sy = W2S(ImVec2(0, y)).y;
            bool axis = std::fabs(y) < stepW * 0.25f;
            dl->AddLine(ImVec2(cp0.x, sy), ImVec2(cp0.x + csz.x, sy), IM_COL32(255, 255, 255, axis ? 30 : 9));
        }
        // unit square 0..1 hint
        ImVec2 u0 = W2S(ImVec2(0, 1)), u1 = W2S(ImVec2(1, 0));
        dl->AddRect(u0, u1, IM_COL32(255, 255, 255, 26));
    }

    ImVec4 accent(1.f, 0.573f, 0.184f, 1.f);
    ImU32 accentU = ImGui::ColorConvertFloat4ToU32(accent);

    // curve
    if (c.keys.size() >= 2)
    {
        for (size_t s = 0; s + 1 < c.keys.size(); s++)
        {
            int n = c.keys[s].bezier ? 40 : 1;
            ImVec2 prev = W2S(c.pointOnSegment((int)s, 0));
            for (int i = 1; i <= n; i++)
            {
                ImVec2 p = W2S(c.pointOnSegment((int)s, (float)i / n));
                dl->AddLine(prev, p, IM_COL32(120, 200, 255, 220), 2.f);
                prev = p;
            }
        }
    }

    // handles for selected bezier keys
    for (size_t i = 0; i + 1 < c.keys.size(); i++)
    {
        auto& k = c.keys[i];
        if (!c.selectedKeys.count(k.id) || !k.bezier) continue;
        auto& nk = c.keys[i + 1];
        ImVec2 kp = W2S(k.pos), np = W2S(nk.pos);
        ImVec2 h1 = W2S(ImVec2(k.pos.x + k.a1.x, k.pos.y + k.a1.y));
        ImVec2 h2 = W2S(ImVec2(nk.pos.x + k.a2.x, nk.pos.y + k.a2.y));
        dl->AddLine(kp, h1, IM_COL32(255, 255, 255, 90));
        dl->AddLine(np, h2, IM_COL32(255, 255, 255, 90));
        dl->AddRectFilled(ImVec2(h1.x - 3, h1.y - 3), ImVec2(h1.x + 3, h1.y + 3), IM_COL32_WHITE);
        dl->AddRectFilled(ImVec2(h2.x - 3, h2.y - 3), ImVec2(h2.x + 3, h2.y + 3), IM_COL32_WHITE);
    }

    // keys
    for (size_t i = 0; i < c.keys.size(); i++)
    {
        auto& k = c.keys[i];
        ImVec2 p = W2S(k.pos);
        bool ksel = c.selectedKeys.count(k.id) != 0;
        dl->AddCircleFilled(p, 5.f, ksel ? accentU : IM_COL32(30, 30, 34, 255));
        dl->AddCircle(p, 5.f, ksel ? IM_COL32_WHITE : IM_COL32(120, 200, 255, 255), 0, 1.8f);
        if (i == 0)
            dl->AddText(ImVec2(p.x + 7, p.y - 16), IM_COL32(150, 150, 155, 200), "start");
    }

    // travelling dot
    if (normPos >= 0.f && c.keys.size() >= 2)
    {
        ImVec2 p = W2S(c.valueAtNorm(std::max(0.f, std::min(1.f, normPos))));
        dl->AddCircleFilled(p, 6.f, accentU);
        dl->AddCircle(p, 9.f, ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 0.5f)), 0, 2.f);
    }

    // rubber
    if (s_c2d.curve == &c && s_c2d.drag == C2DState::Drag::Rubber && s_c2d.moved)
    {
        ImVec2 rMin(std::min(s_c2d.rubberStart.x, mouse.x), std::min(s_c2d.rubberStart.y, mouse.y));
        ImVec2 rMax(std::max(s_c2d.rubberStart.x, mouse.x), std::max(s_c2d.rubberStart.y, mouse.y));
        dl->AddRectFilled(rMin, rMax, ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 0.12f)));
        dl->AddRect(rMin, rMax, accentU);
    }
    dl->PopClipRect();

    // key context menu
    if (ImGui::BeginPopup("##c2dkey"))
    {
        Curve2DKey* k = c.findKey(s_c2d.keyId);
        if (k)
        {
            bool bez = k->bezier;
            if (ImGui::MenuItem("Bezier segment", nullptr, bez))
            {
                json pre = c.keysToJson();
                k->bezier = !bez;
                c.rebuild();
                c2dPushEdit(&c, pre, "Toggle Bezier");
            }
            if (ImGui::MenuItem("Delete Key", "Del"))
            {
                json pre = c.keysToJson();
                for (uint64_t kid : std::vector<uint64_t>(c.selectedKeys.begin(), c.selectedKeys.end()))
                    c.removeKey(kid);
                c2dPushEdit(&c, pre, "Delete Curve Keys");
            }
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();

    // shortcuts
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput)
    {
        if ((ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) &&
            !c.selectedKeys.empty())
        {
            json pre = c.keysToJson();
            for (uint64_t kid : std::vector<uint64_t>(c.selectedKeys.begin(), c.selectedKeys.end()))
                c.removeKey(kid);
            c2dPushEdit(&c, pre, "Delete Curve Keys");
        }
    }
    ImGui::PopID();
}

} // namespace organic
