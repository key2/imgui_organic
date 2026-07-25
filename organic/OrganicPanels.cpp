#include "OrganicPanels.h"
#include "imgui_stdlib.h"
#include "implot.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace organic
{

// ================================================================ parameter widgets + coalesced undo
// pending edits: group of parameters being continuously edited by one widget
struct PendingEdit { Parameter* param; Value oldValue; };
static std::vector<PendingEdit> s_pending;
static const void* s_pendingTag = nullptr; // identifies the active widget group

static void commitPendingNow()
{
    if (s_pending.empty()) { s_pendingTag = nullptr; return; }
    struct Rec { Parameter* p; Value oldV, newV; };
    std::vector<Rec> recs;
    for (auto& pe : s_pending)
        if (!valueEquals(pe.oldValue, pe.param->value))
            recs.push_back({ pe.param, pe.oldValue, pe.param->value });
    s_pending.clear();
    s_pendingTag = nullptr;
    if (recs.empty()) return;

    std::vector<const void*> owners;
    for (auto& r : recs) owners.push_back(r.p);
    std::string name = recs.size() == 1 ? "Set " + recs[0].p->niceName : "Edit Parameters";
    UndoManager::get().pushDone(name,
        [recs] { for (auto& r : recs) r.p->setValue(r.newV); },
        [recs] { for (auto& r : recs) r.p->setValue(r.oldV); },
        owners);
}

void CommitPendingParamEdits()
{
    if (s_pending.empty()) return;
    if (ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
    commitPendingNow();
}

// apply a continuous edit to a group of parameters (captures old values once)
static void applyPendingEdit(const std::vector<Parameter*>& targets, const void* tag,
                             std::function<void(Parameter&)> apply)
{
    if (s_pendingTag != tag)
    {
        commitPendingNow();
        s_pendingTag = tag;
        for (Parameter* t : targets) s_pending.push_back({ t, t->value });
    }
    for (Parameter* t : targets) apply(*t);
}

// apply an instant edit (checkbox/combo/...) as one undo step
static void applyInstantEdit(const std::vector<Parameter*>& targets,
                             std::function<void(Parameter&)> apply, const std::string& name)
{
    struct Rec { Parameter* p; Value oldV, newV; };
    std::vector<Rec> recs;
    for (Parameter* t : targets)
    {
        Value before = t->value;
        apply(*t);
        if (!valueEquals(before, t->value)) recs.push_back({ t, before, t->value });
    }
    if (recs.empty()) return;
    std::vector<const void*> owners;
    for (auto& r : recs) owners.push_back(r.p);
    UndoManager::get().pushDone(name,
        [recs] { for (auto& r : recs) r.p->setValue(r.newV); },
        [recs] { for (auto& r : recs) r.p->setValue(r.oldV); },
        owners);
}

static bool valuesMixed(const std::vector<Parameter*>& targets)
{
    for (size_t i = 1; i < targets.size(); i++)
        if (!valueEquals(targets[0]->value, targets[i]->value)) return true;
    return false;
}

static bool drawParamWidgetEx(Parameter& p, const std::vector<Parameter*>& targets)
{
    ImGui::PushID(&p);
    bool changed = false;
    const char* label = p.niceName.c_str();
    if (p.readOnly) ImGui::BeginDisabled();

    float w = std::max(120.f, ImGui::GetContentRegionAvail().x * 0.55f);
    char fmt[32];
    const void* tag = &p; // widget identity for pending grouping

    switch (p.type)
    {
    case PType::Trigger:
    {
        if (ImGui::Button(label))
        {
            for (Parameter* t : targets) t->trigger();
            changed = true;
        }
        break;
    }
    case PType::Bool:
    {
        bool v = p.boolValue();
        if (ImGui::Checkbox(label, &v))
        {
            applyInstantEdit(targets, [v](Parameter& t) { t.setValue(v); }, "Set " + p.niceName);
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
            applyPendingEdit(targets, tag, [v](Parameter& t) { t.setValue(v); });
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
            applyPendingEdit(targets, tag, [v](Parameter& t) { t.setValue(v); });
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
            applyPendingEdit(targets, tag, [&v](Parameter& t) { t.setValue(v); });
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
        if (!opts.empty() && ImGui::Combo(label, &v, opts.data(), (int)opts.size()))
        {
            applyInstantEdit(targets, [v](Parameter& t) { t.setValue(v); }, "Set " + p.niceName);
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
            ImVec4 nv(col[0], col[1], col[2], col[3]);
            applyPendingEdit(targets, tag, [nv](Parameter& t) { t.setValue(nv); });
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
            ImVec2 nv(xy[0], xy[1]);
            applyPendingEdit(targets, tag, [nv](Parameter& t) { t.setValue(nv); });
            changed = true;
        }
        break;
    }
    }

    if (p.readOnly) ImGui::EndDisabled();

    if (targets.size() > 1 && valuesMixed(targets))
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(mixed)");
    }

    // tooltip
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    {
        ImGui::BeginTooltip();
        if (!p.description.empty()) ImGui::TextUnformatted(p.description.c_str());
        ImGui::TextDisabled("%s  [%s]%s%s", p.controlAddress().c_str(), ptypeName(p.type),
                            p.isOverriden() ? "  (edited)" : "",
                            targets.size() > 1 ? "  (multi)" : "");
        ImGui::EndTooltip();
    }

    // context menu
    if (ImGui::BeginPopupContextItem("##param_ctx"))
    {
        ImGui::TextDisabled("%s", p.controlAddress().c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("Reset to default", nullptr, false, !p.readOnly))
            applyInstantEdit(targets, [](Parameter& t) { t.setValue(t.defaultValue); }, "Reset " + p.niceName);
        if (ImGui::MenuItem("Copy address"))
            ImGui::SetClipboardText(p.controlAddress().c_str());
        if (ImGui::MenuItem("Copy value"))
            ImGui::SetClipboardText(p.stringValue().c_str());
        bool watchable = p.type == PType::Float || p.type == PType::Int || p.type == PType::Bool;
        if (Detective::main && watchable && ImGui::MenuItem("Watch in Detective"))
            Detective::main->watch(p.controlAddress());
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return changed;
}

bool DrawParamWidget(Parameter& p)
{
    return drawParamWidgetEx(p, { &p });
}

bool DrawParamWidgetMulti(const std::vector<Parameter*>& params)
{
    if (params.empty()) return false;
    return drawParamWidgetEx(*params[0], params);
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

// ================================================================ parameter picker popup
static void paramPickerNode(Container* c, const char* filter, std::string& picked)
{
    std::string filterLo = filter;
    std::transform(filterLo.begin(), filterLo.end(), filterLo.begin(), ::tolower);
    auto matches = [&](const std::string& s)
    {
        if (filterLo.empty()) return true;
        std::string lo = s;
        std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
        return lo.find(filterLo) != std::string::npos;
    };

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (filter[0]) ImGui::SetNextItemOpen(true);
    if (!ImGui::TreeNodeEx((void*)c, flags, "%s", c->niceName.c_str())) return;
    for (auto& p : c->params)
    {
        if (p->type == PType::Trigger) continue;
        if (!matches(p->niceName) && !matches(p->shortName)) continue;
        ImGui::PushID(p.get());
        std::string lbl = "  " + p->niceName;
        if (ImGui::Selectable(lbl.c_str()))
        {
            picked = p->controlAddress();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemTooltip("%s [%s]", p->controlAddress().c_str(), ptypeName(p->type));
        ImGui::PopID();
    }
    for (Container* ch : c->children)
        paramPickerNode(ch, filter, picked);
    ImGui::TreePop();
}

bool ParamPickerPopup(const char* popupId, std::string& address)
{
    bool pickedSomething = false;
    ImGui::SetNextWindowSize(ImVec2(360, 380), ImGuiCond_Appearing);
    if (ImGui::BeginPopup(popupId))
    {
        static char filter[64] = "";
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##pickfilter", "Filter parameters...", filter, sizeof(filter));
        ImGui::Separator();
        ImGui::BeginChild("##picktree", ImVec2(0, 300));
        std::string picked;
        for (Container* root : rootContainers())
            paramPickerNode(root, filter, picked);
        ImGui::EndChild();
        if (!picked.empty())
        {
            address = picked;
            pickedSomething = true;
        }
        ImGui::EndPopup();
    }
    return pickedSomething;
}

// ================================================================ Inspector
void InspectorPanel(bool* open)
{
    if (!ImGui::Begin("Inspector", open))
    {
        ImGui::End();
        return;
    }

    Selection* activeSel = Selection::active();
    auto items = activeSel ? activeSel->items : std::vector<Inspectable*>();

    // per-selection scroll memory
    static std::unordered_map<size_t, float> s_scrollMem;
    static uint32_t s_lastRev = 0;
    static size_t s_lastKey = 0;
    size_t key = 1469598103u;
    for (auto* i : items) key = (key ^ (size_t)i) * 1099511628211ull;
    uint32_t rev = activeSel ? activeSel->revision : 0;
    bool selectionChanged = (rev != s_lastRev) || (key != s_lastKey);

    if (items.empty())
    {
        ImGui::TextDisabled("Nothing selected.");
        ImGui::TextWrapped("Click items in the Timeline, Outliner, Board or Media Pool to inspect and edit them here.");
        s_lastRev = rev;
        s_lastKey = key;
        ImGui::End();
        return;
    }

    ImVec4 accent(1.f, 0.573f, 0.184f, 1.f);

    // ---- true multi-editing when all items share the same type
    bool homogeneous = items.size() > 1;
    for (auto* i : items)
        if (i->inspectableTypeName() != items[0]->inspectableTypeName()) { homogeneous = false; break; }

    if (homogeneous)
    {
        std::vector<Container*> cs;
        for (auto* i : items)
            if (auto* c = dynamic_cast<Container*>(i)) cs.push_back(c);
        if (cs.size() == items.size())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, accent);
            ImGui::SeparatorText((std::to_string(items.size()) + " x " + items[0]->inspectableTypeName()
                                  + "  (multi-edit)").c_str());
            ImGui::PopStyleColor();
            for (auto& p : cs[0]->params)
            {
                if (p->hideInEditor) continue;
                std::vector<Parameter*> targets;
                for (Container* c : cs)
                    if (Parameter* tp = c->getParam(p->shortName))
                        if (tp->type == p->type) targets.push_back(tp);
                if (targets.size() == cs.size())
                    DrawParamWidgetMulti(targets);
            }
            ImGui::Spacing();
            ImGui::TextDisabled("Editing writes to all selected items (one undo step).");
            if (selectionChanged)
            {
                auto it = s_scrollMem.find(key);
                ImGui::SetScrollY(it != s_scrollMem.end() ? it->second : 0.f);
            }
            else s_scrollMem[key] = ImGui::GetScrollY();
            s_lastRev = rev;
            s_lastKey = key;
            ImGui::End();
            return;
        }
    }

    if (items.size() > 1)
        ImGui::TextDisabled("%d items selected", (int)items.size());

    int idx = 0;
    for (Inspectable* it : items)
    {
        ImGui::PushID(idx++);
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

    if (selectionChanged)
    {
        auto it = s_scrollMem.find(key);
        ImGui::SetScrollY(it != s_scrollMem.end() ? it->second : 0.f);
    }
    else s_scrollMem[key] = ImGui::GetScrollY();
    s_lastRev = rev;
    s_lastKey = key;
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

    if (c->isHighlighted())
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.72f, 1.f, 1.f));
    bool nodeOpen = ImGui::TreeNodeEx((void*)c, flags, "%s", c->niceName.c_str());
    if (c->isHighlighted()) ImGui::PopStyleColor();
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
        bool highlighted = m->isHighlighted();
        if (highlighted)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.72f, 1.f, 1.f));
        if (ImGui::Selectable(label.c_str(), m->isSelected()))
            m->select(ImGui::GetIO().KeyCtrl);
        if (highlighted) ImGui::PopStyleColor();

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
    std::vector<Channel> next;
    for (auto& l : seq.layers)
    {
        if (auto* al = dynamic_cast<AutomationLayer*>(l.get()))
        {
            if (!al->enabledP->boolValue()) continue;
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
        ImGui::TextWrapped("Add an Automation layer in the Timeline and its output will be plotted here.");
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

// ================================================================ Detective
Detective* Detective::main = nullptr;

DetectiveWatcher::DetectiveWatcher() : BaseItem("Watcher", "Watcher")
{
    addressP = addString("Address", "", "Control address of the watched parameter");
    windowP  = addFloat("Window", 10.f, 1.f, 120.f, "Time window in seconds");
    windowP->unit = "s";
    colorP->setValue(ImVec4(0.35f, 0.75f, 0.95f, 1.f), false);
    colorP->defaultValue = colorP->value;
    viewSize = ImVec2(220, 90);
}

void DetectiveWatcher::sample(double now)
{
    if (!enabled()) return;
    Parameter* p = resolveParamAddress(addressP->stringValue());
    if (!p) return;
    times.push_back((float)now);
    values.push_back(p->floatValue());
    float window = windowP->floatValue() + 2.f;
    while (!times.empty() && times.front() < now - window)
    {
        times.erase(times.begin());
        values.erase(values.begin());
    }
}

Detective::Detective() : BaseManager("Detective")
{
    addDef("Watcher", "Watcher", [] { return std::make_unique<DetectiveWatcher>(); });
}

void Detective::update(double now)
{
    for (auto& i : items)
        if (auto* w = dynamic_cast<DetectiveWatcher*>(i.get()))
            w->sample(now);
}

DetectiveWatcher* Detective::watch(const std::string& address)
{
    // reuse existing watcher for this address
    for (auto& i : items)
        if (auto* w = dynamic_cast<DetectiveWatcher*>(i.get()))
            if (w->addressP->stringValue() == address) { w->select(); return w; }

    BaseItem* it = undoableAdd("Watcher");
    auto* w = dynamic_cast<DetectiveWatcher*>(it);
    if (w)
    {
        w->addressP->setValue(address, false);
        std::string nice = address;
        size_t slash = nice.find_last_of('/');
        if (slash != std::string::npos && slash + 1 < nice.size())
            nice = nice.substr(slash + 1);
        w->setNiceName(nice);
        OLOG("Detective", "Watching " << address);
    }
    return w;
}

void DetectivePanel(Detective& d, bool* open)
{
    if (!ImGui::Begin("Detective", open))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button("+ Watcher")) d.undoableAdd("Watcher");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    ImGui::SetItemTooltip("Watch any parameter over time.\n"
                          "Tip: right-click any parameter widget -> 'Watch in Detective'.");
    ImGui::Separator();

    if (d.items.empty())
        ImGui::TextDisabled("No watchers. Right-click a parameter and choose 'Watch in Detective'.");

    ImGui::BeginChild("##watchers");
    double now = ImGui::GetTime();
    BaseItem* toRemove = nullptr;

    for (auto& item : d.items)
    {
        auto* w = dynamic_cast<DetectiveWatcher*>(item.get());
        if (!w) continue;
        ImGui::PushID(w);

        bool en = w->enabled();
        if (ImGui::Checkbox("##en", &en)) w->enabledP->setUndoable(en);
        ImGui::SameLine();
        ImGui::ColorButton("##c", w->colorP->color(), ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
        ImGui::SameLine();
        std::string addr = w->addressP->stringValue();
        if (ImGui::Selectable(addr.empty() ? "(no address - click to pick)" : addr.c_str(),
                              w->isSelected(), 0,
                              ImVec2(std::max(60.f, ImGui::GetContentRegionAvail().x - 150), 0)))
        {
            w->select(ImGui::GetIO().KeyCtrl);
            ImGui::OpenPopup("##wpick");
        }
        std::string newAddr = addr;
        if (ParamPickerPopup("##wpick", newAddr) && newAddr != addr)
            w->addressP->setUndoable(newAddr);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        float win = w->windowP->floatValue();
        if (ImGui::DragFloat("##win", &win, 0.1f, 1.f, 120.f, "%.0fs"))
            w->windowP->setValue(win);
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) toRemove = w;

        // plot
        Parameter* p = resolveParamAddress(w->addressP->stringValue());
        if (!p && !addr.empty())
            ImGui::TextDisabled("  (address not found)");
        int n = (int)std::min(w->times.size(), w->values.size());
        std::string plotId = "##plot" + std::to_string((intptr_t)w);
        if (ImPlot::BeginPlot(plotId.c_str(), ImVec2(-1, 86),
                              ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMenus))
        {
            ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_AutoFit);
            ImPlot::SetupAxisLimits(ImAxis_X1, now - w->windowP->floatValue(), now, ImGuiCond_Always);
            if (n >= 2)
            {
                ImPlotSpec spec;
                spec.LineColor = w->colorP->color();
                spec.LineWeight = 2.f;
                ImPlot::PlotLine("##v", w->times.data() + (w->times.size() - n),
                                 w->values.data() + (w->values.size() - n), n, spec);
            }
            ImPlot::EndPlot();
        }
        if (p)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.f, 0.573f, 0.184f, 1.f), "%.3f", p->floatValue());
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    if (toRemove) d.undoableRemove({ toRemove });
    ImGui::EndChild();
    ImGui::End();
}

} // namespace organic
