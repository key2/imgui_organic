#include "OrganicPanels.h"
#include "imgui_stdlib.h"
#include "implot.h"
#include <algorithm>
#include <cstring>

namespace organic
{

// ================================================================ parameter widget + undo coalescing
static Parameter* s_pendingParam = nullptr;
static Value      s_pendingOld;

static void commitPendingIfOther(Parameter* p)
{
    if (s_pendingParam && s_pendingParam != p)
    {
        s_pendingParam->recordEdit(s_pendingOld, s_pendingParam->value);
        s_pendingParam = nullptr;
    }
}

void CommitPendingParamEdits()
{
    if (!s_pendingParam) return;
    if (ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
    s_pendingParam->recordEdit(s_pendingOld, s_pendingParam->value);
    s_pendingParam = nullptr;
}

static void beginPending(Parameter& p, const Value& before)
{
    if (s_pendingParam != &p)
    {
        commitPendingIfOther(&p);
        s_pendingParam = &p;
        s_pendingOld = before;
    }
}

bool DrawParamWidget(Parameter& p)
{
    ImGui::PushID(&p);
    bool changed = false;
    const char* label = p.niceName.c_str();
    if (p.readOnly) ImGui::BeginDisabled();

    float w = std::max(120.f, ImGui::GetContentRegionAvail().x * 0.55f);
    char fmt[32];

    switch (p.type)
    {
    case PType::Trigger:
    {
        if (ImGui::Button(label)) { p.trigger(); changed = true; }
        break;
    }
    case PType::Bool:
    {
        bool v = p.boolValue();
        if (ImGui::Checkbox(label, &v))
        {
            Value before = p.value;
            p.setValue(v);
            p.recordEdit(before, p.value);
            changed = true;
        }
        break;
    }
    case PType::Int:
    {
        int v = p.intValue();
        ImGui::SetNextItemWidth(w);
        bool ch = p.hasRange ? ImGui::SliderInt(label, &v, (int)p.minF, (int)p.maxF)
                             : ImGui::DragInt(label, &v, 0.2f);
        if (ch)
        {
            Value before = p.value;
            p.setValue(v);
            beginPending(p, before);
            changed = true;
        }
        break;
    }
    case PType::Float:
    {
        float v = p.floatValue();
        snprintf(fmt, sizeof(fmt), p.unit[0] ? "%%.3f %s" : "%%.3f", p.unit);
        ImGui::SetNextItemWidth(w);
        bool ch = p.hasRange ? ImGui::SliderFloat(label, &v, p.minF, p.maxF, fmt)
                             : ImGui::DragFloat(label, &v, p.dragSpeed, 0.f, 0.f, fmt);
        if (ch)
        {
            Value before = p.value;
            p.setValue(v);
            beginPending(p, before);
            changed = true;
        }
        break;
    }
    case PType::String:
    {
        std::string v = p.stringValue();
        ImGui::SetNextItemWidth(w);
        if (ImGui::InputText(label, &v))
        {
            Value before = p.value;
            p.setValue(v);
            beginPending(p, before);
            changed = true;
        }
        break;
    }
    case PType::Enum:
    {
        int v = p.intValue();
        std::vector<const char*> opts;
        for (auto& o : p.enumOptions) opts.push_back(o.c_str());
        ImGui::SetNextItemWidth(w);
        if (ImGui::Combo(label, &v, opts.data(), (int)opts.size()))
        {
            Value before = p.value;
            p.setValue(v);
            p.recordEdit(before, p.value);
            changed = true;
        }
        break;
    }
    case PType::Color:
    {
        ImVec4 c = p.color();
        float col[4] = { c.x, c.y, c.z, c.w };
        ImGui::SetNextItemWidth(w);
        if (ImGui::ColorEdit4(label, col))
        {
            Value before = p.value;
            p.setValue(ImVec4(col[0], col[1], col[2], col[3]));
            beginPending(p, before);
            changed = true;
        }
        break;
    }
    case PType::Point2D:
    {
        ImVec2 v = p.point();
        float xy[2] = { v.x, v.y };
        ImGui::SetNextItemWidth(w);
        if (ImGui::DragFloat2(label, xy, p.dragSpeed))
        {
            Value before = p.value;
            p.setValue(ImVec2(xy[0], xy[1]));
            beginPending(p, before);
            changed = true;
        }
        break;
    }
    }

    if (p.readOnly) ImGui::EndDisabled();

    // tooltip
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    {
        ImGui::BeginTooltip();
        if (!p.description.empty()) ImGui::TextUnformatted(p.description.c_str());
        ImGui::TextDisabled("%s  [%s]%s", p.controlAddress().c_str(), ptypeName(p.type),
                            p.isOverriden() ? "  (edited)" : "");
        ImGui::EndTooltip();
    }

    // context menu
    if (ImGui::BeginPopupContextItem("##param_ctx"))
    {
        ImGui::TextDisabled("%s", p.controlAddress().c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("Reset to default", nullptr, false, p.isOverriden() && !p.readOnly))
            p.resetToDefault();
        if (ImGui::MenuItem("Copy address"))
            ImGui::SetClipboardText(p.controlAddress().c_str());
        if (ImGui::MenuItem("Copy value"))
            ImGui::SetClipboardText(p.stringValue().c_str());
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return changed;
}

bool UndoableInputText(const char* label, std::string& str, const void* owner,
                       std::function<void(const std::string&, const std::string&)> apply)
{
    static const void* s_owner = nullptr;
    static std::string s_old;

    std::string tmp = str;
    bool ch = ImGui::InputText(label, &tmp);
    if (ImGui::IsItemActivated()) { s_owner = owner; s_old = str; }
    if (ch) { str = tmp; if (apply) apply(s_old, str); }
    if (ImGui::IsItemDeactivatedAfterEdit() && s_owner == owner)
    {
        std::string oldV = s_old, newV = str;
        if (oldV != newV && apply)
        {
            UndoManager::get().pushDone("Rename",
                [apply, oldV, newV] { apply(oldV, newV); },
                [apply, newV, oldV] { apply(newV, oldV); },
                { owner });
        }
        s_owner = nullptr;
    }
    return ch;
}

// ================================================================ Inspector
void InspectorPanel(bool* open)
{
    if (!ImGui::Begin("Inspector", open))
    {
        ImGui::End();
        return;
    }

    auto items = Selection::get().items; // copy: gui may alter selection
    if (items.empty())
    {
        ImGui::TextDisabled("Nothing selected.");
        ImGui::TextWrapped("Click items in the Timeline, Outliner or Media Pool to inspect and edit them here.");
        ImGui::End();
        return;
    }

    if (items.size() > 1)
        ImGui::TextDisabled("%d items selected", (int)items.size());

    int idx = 0;
    for (Inspectable* it : items)
    {
        ImGui::PushID(idx++);
        ImVec4 accent(1.f, 0.573f, 0.184f, 1.f);
        ImGui::PushStyleColor(ImGuiCol_Text, accent);
        ImGui::SeparatorText(it->inspectableTypeName().c_str());
        ImGui::PopStyleColor();

        if (auto* c = dynamic_cast<Container*>(it))
        {
            if (c->renamable)
            {
                UndoableInputText("Name", c->niceName, c,
                    [c](const std::string&, const std::string& to) { c->setNiceName(to); });
            }
        }
        it->inspectorGui();
        ImGui::Spacing();
        ImGui::PopID();
        if (idx > 20) { ImGui::TextDisabled("..."); break; }
    }
    ImGui::End();
}

// ================================================================ Outliner
static bool outlinerMatch(Container* c, const char* filter)
{
    if (!filter[0]) return true;
    std::string n = c->niceName;
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    std::string f = filter;
    std::transform(f.begin(), f.end(), f.begin(), ::tolower);
    if (n.find(f) != std::string::npos) return true;
    for (auto& p : c->params)
    {
        std::string pn = p->niceName;
        std::transform(pn.begin(), pn.end(), pn.begin(), ::tolower);
        if (pn.find(f) != std::string::npos) return true;
    }
    for (auto* ch : c->children)
        if (outlinerMatch(ch, filter)) return true;
    return false;
}

static void outlinerNode(Container* c, const char* filter, bool showValues)
{
    if (c->hideInOutliner) return;
    if (!outlinerMatch(c, filter)) return;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (c->isSelected()) flags |= ImGuiTreeNodeFlags_Selected;
    bool hasKids = !c->children.empty() || (showValues && !c->params.empty());
    if (!hasKids) flags |= ImGuiTreeNodeFlags_Leaf;
    if (filter[0]) ImGui::SetNextItemOpen(true);

    bool nodeOpen = ImGui::TreeNodeEx((void*)c, flags, "%s", c->niceName.c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
        c->select(ImGui::GetIO().KeyCtrl);

    if (nodeOpen)
    {
        for (auto* ch : c->children)
            outlinerNode(ch, filter, showValues);
        if (showValues)
        {
            for (auto& p : c->params)
            {
                if (p->type == PType::Trigger) continue;
                ImGui::Indent(20);
                ImGui::TextDisabled("%s: %s%s", p->niceName.c_str(), p->stringValue().c_str(),
                                    p->unit[0] ? p->unit : "");
                ImGui::Unindent(20);
            }
        }
        ImGui::TreePop();
    }
}

void OutlinerPanel(bool* open)
{
    if (!ImGui::Begin("Outliner", open))
    {
        ImGui::End();
        return;
    }
    static char filter[64] = "";
    static bool showValues = false;
    ImGui::SetNextItemWidth(std::max(80.f, ImGui::GetContentRegionAvail().x - 70));
    ImGui::InputTextWithHint("##filter", "Filter...", filter, sizeof(filter));
    ImGui::SameLine();
    ImGui::Checkbox("Values", &showValues);
    ImGui::Separator();

    ImGui::BeginChild("##tree");
    for (Container* root : rootContainers())
        outlinerNode(root, filter, showValues);
    ImGui::EndChild();
    ImGui::End();
}

// ================================================================ Logger
void LoggerPanel(bool* open)
{
    if (!ImGui::Begin("Logger", open))
    {
        ImGui::End();
        return;
    }
    static bool showInfo = true, showWarn = true, showErr = true, autoScroll = true;
    static uint32_t lastRev = 0;

    Logger& log = Logger::get();

    ImGui::Checkbox("Info", &showInfo); ImGui::SameLine();
    ImGui::Checkbox("Warnings", &showWarn); ImGui::SameLine();
    ImGui::Checkbox("Errors", &showErr); ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &autoScroll); ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) log.clear();
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy"))
    {
        std::string all;
        for (auto& e : log.entries)
            all += "[" + e.source + "] " + e.message + "\n";
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::Separator();

    ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (auto& e : log.entries)
    {
        if (e.level == LogLevel::Info && !showInfo) continue;
        if (e.level == LogLevel::Warning && !showWarn) continue;
        if (e.level == LogLevel::Error && !showErr) continue;
        ImVec4 col = e.level == LogLevel::Error   ? ImVec4(0.95f, 0.4f, 0.35f, 1.f)
                   : e.level == LogLevel::Warning ? ImVec4(1.f, 0.75f, 0.3f, 1.f)
                                                  : ImVec4(0.78f, 0.78f, 0.8f, 1.f);
        ImGui::TextDisabled("%7.2f", e.time);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.8f, 1.f), "[%s]", e.source.c_str());
        ImGui::SameLine();
        ImGui::TextColored(col, "%s", e.message.c_str());
    }
    if (autoScroll && lastRev != log.revision && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40)
        ImGui::SetScrollHereY(1.f);
    lastRev = log.revision;
    ImGui::EndChild();
    ImGui::End();
}

// ================================================================ Media Pool
void MediaPoolPanel(MediaPool& pool, bool* open)
{
    if (!ImGui::Begin("Media Pool", open))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button("+ Block"))
    {
        MediaItem* m = pool.addMedia("Block", 0);
        m->select();
        OLOG("Media", "Added block media");
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Audio"))
    {
        MediaItem* m = pool.addMedia("Audio", 1);
        m->select();
        OLOG("Media", "Added audio media; set its File path in the Inspector");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    ImGui::SetItemTooltip("Drag items below onto the Timeline.\n"
                          "Drop on a clip lane to add a clip there,\n"
                          "or below the layers to create a new layer.");
    ImGui::Separator();

    MediaItem* toDelete = nullptr;
    for (auto& m : pool.items)
    {
        ImGui::PushID(m.get());
        ImVec4 c = m->colorP->color();
        ImGui::ColorButton("##c", c, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                           ImVec2(14, 14));
        ImGui::SameLine();

        std::string label = m->niceName + (m->kind == 1 ? "  [wav]" : "  [block]");
        if (ImGui::Selectable(label.c_str(), m->isSelected()))
            m->select(ImGui::GetIO().KeyCtrl);

        if (ImGui::BeginDragDropSource())
        {
            MediaPayload p = m->makePayload();
            ImGui::SetDragDropPayload(ORGANIC_MEDIA_PAYLOAD, &p, sizeof(p));
            ImGui::ColorButton("##cc", c, ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
            ImGui::SameLine();
            ImGui::TextUnformatted(m->niceName.c_str());
            ImGui::TextDisabled(m->kind == 1 ? "audio media" : "block media");
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem("##media_ctx"))
        {
            if (ImGui::MenuItem("Remove")) toDelete = m.get();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (toDelete) pool.removeMedia(toDelete);

    ImGui::End();
}

// ================================================================ Scope
void ScopeBuffers::push(Sequence& seq, double now)
{
    // sync channels with automation layers
    std::vector<Channel> next;
    for (auto& l : seq.layers)
    {
        if (auto* al = dynamic_cast<AutomationLayer*>(l.get()))
        {
            Channel* prev = nullptr;
            for (auto& ch : channels) if (ch.layerId == al->id) prev = &ch;
            Channel ch;
            if (prev) ch = *prev;
            ch.layerId = al->id;
            ch.name = al->niceName;
            ch.color = al->colorP->color();
            ch.values.push_back(al->valueAt(seq.currentTime));
            if ((int)ch.values.size() > maxPoints)
                ch.values.erase(ch.values.begin(), ch.values.begin() + (ch.values.size() - maxPoints));
            next.push_back(std::move(ch));
        }
    }
    channels = std::move(next);

    times.push_back((float)now);
    if ((int)times.size() > maxPoints)
        times.erase(times.begin(), times.begin() + (times.size() - maxPoints));

    // keep channel buffers aligned with the time buffer
    for (auto& ch : channels)
        while (ch.values.size() < times.size())
            ch.values.insert(ch.values.begin(), ch.values.empty() ? 0.f : ch.values.front());
}

void ScopePanel(ScopeBuffers& buffers, bool* open)
{
    if (!ImGui::Begin("Scope", open))
    {
        ImGui::End();
        return;
    }
    if (buffers.channels.empty())
    {
        ImGui::TextDisabled("No automation layers to watch.");
        ImGui::TextWrapped("Add an Automation layer in the Timeline and its output will be plotted here (organicui's 'Detective').");
        ImGui::End();
        return;
    }

    double now = buffers.times.empty() ? 0.0 : buffers.times.back();
    if (ImPlot::BeginPlot("##scope", ImVec2(-1, -1), ImPlotFlags_NoTitle))
    {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, now - buffers.timeWindow, now, ImGuiCond_Always);
        for (auto& ch : buffers.channels)
        {
            int n = (int)std::min(buffers.times.size(), ch.values.size());
            if (n < 2) continue;
            const float* xs = buffers.times.data() + (buffers.times.size() - n);
            const float* ys = ch.values.data() + (ch.values.size() - n);
            ImPlotSpec spec;
            spec.LineColor = ch.color;
            spec.LineWeight = 2.f;
            ImPlot::PlotLine(ch.name.c_str(), xs, ys, n, spec);
        }
        ImPlot::EndPlot();
    }
    ImGui::End();
}

} // namespace organic
