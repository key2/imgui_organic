#include "OrganicManager.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace organic
{

static bool inRectMgr(const ImVec2& p, const ImVec2& mn, const ImVec2& mx)
{
    return p.x >= mn.x && p.x < mx.x && p.y >= mn.y && p.y < mx.y;
}

// ================================================================ BaseItem
BaseItem::BaseItem(const std::string& type, const std::string& nice)
    : Container(nice), typeName(type)
{
    renamable = true;
    enabledP = addBool("Enabled", true, "Enable / bypass this item");
    colorP   = addColor("Color", ImVec4(0.42f, 0.47f, 0.55f, 1.f), "Item color");
}

json BaseItem::save() const
{
    json j = Container::save();
    j["type"] = typeName;
    j["uid"] = uid;
    j["mini"] = miniMode;
    j["locked"] = uiLocked;
    j["viewPos"] = { viewPos.x, viewPos.y };
    j["viewSize"] = { viewSize.x, viewSize.y };
    return j;
}

void BaseItem::load(const json& j)
{
    Container::load(j);
    uid      = j.value("uid", (uint64_t)0);
    miniMode = j.value("mini", false);
    uiLocked = j.value("locked", false);
    if (j.contains("viewPos"))  viewPos  = ImVec2(j["viewPos"][0], j["viewPos"][1]);
    if (j.contains("viewSize")) viewSize = ImVec2(j["viewSize"][0], j["viewSize"][1]);
}

// ================================================================ BaseManager
BaseManager::BaseManager(const std::string& nice, Container* parent)
    : Container(nice, parent)
{
}

Selection& BaseManager::sel() const
{
    return Selection::scope(selectionScopeName);
}

void BaseManager::addDef(const std::string& menuPath, const std::string& type,
                         std::function<std::unique_ptr<BaseItem>()> create)
{
    factory.push_back({ menuPath, type, std::move(create) });
}

std::unique_ptr<BaseItem> BaseManager::createFromType(const std::string& type) const
{
    for (auto& d : factory)
        if (d.type == type)
        {
            auto item = d.create();
            item->typeName = type;
            return item;
        }
    return nullptr;
}

BaseItem* BaseManager::addItem(std::unique_ptr<BaseItem> item, int index)
{
    if (!item) return nullptr;
    if (item->uid == 0) item->uid = nextUid++;
    nextUid = std::max(nextUid, item->uid + 1);
    item->manager = this;
    addChild(item.get());
    BaseItem* raw = item.get();
    if (index < 0 || index > (int)items.size()) index = (int)items.size();
    items.insert(items.begin() + index, std::move(item));
    onItemsChanged();
    return raw;
}

BaseItem* BaseManager::addItemFromJson(const json& j, int index)
{
    std::string type = j.value("type", "");
    auto item = createFromType(type);
    if (!item)
    {
        OLOGE(niceName, "Unknown item type '" << type << "'");
        return nullptr;
    }
    BaseItem* raw = item.get();
    if (index < 0) index = j.value("_index", -1);
    addItem(std::move(item), index);
    raw->load(j);
    if (raw->uid == 0) raw->uid = nextUid++;
    nextUid = std::max(nextUid, raw->uid + 1);
    return raw;
}

json BaseManager::removeItem(uint64_t uid)
{
    for (size_t i = 0; i < items.size(); i++)
    {
        if (items[i]->uid == uid)
        {
            json j = items[i]->save();
            j["_index"] = (int)i;
            removeChild(items[i].get());
            items.erase(items.begin() + i);
            onItemsChanged();
            return j;
        }
    }
    return json();
}

BaseItem* BaseManager::findItem(uint64_t uid) const
{
    for (auto& i : items) if (i->uid == uid) return i.get();
    return nullptr;
}

int BaseManager::indexOf(const BaseItem* item) const
{
    for (size_t i = 0; i < items.size(); i++)
        if (items[i].get() == item) return (int)i;
    return -1;
}

void BaseManager::moveItem(int from, int to)
{
    if (from < 0 || from >= (int)items.size() || to < 0 || to >= (int)items.size() || from == to)
        return;
    auto it = std::move(items[from]);
    items.erase(items.begin() + from);
    items.insert(items.begin() + to, std::move(it));
    onItemsChanged();
}

BaseItem* BaseManager::undoableAdd(const std::string& type, ImVec2 canvasPos, int index)
{
    auto item = createFromType(type);
    if (!item) return nullptr;
    item->viewPos = canvasPos;
    BaseItem* raw = addItem(std::move(item), index);
    json data = raw->save();
    data["_index"] = indexOf(raw);
    uint64_t uid = raw->uid;
    BaseManager* self = this;
    UndoManager::get().pushDone("Add " + type,
        [self, data] { self->addItemFromJson(data); },
        [self, uid]  { self->removeItem(uid); },
        { self });
    sel().set(raw);
    return raw;
}

void BaseManager::undoableRemove(const std::vector<BaseItem*>& toRemove)
{
    struct Rec { json data; };
    std::vector<json> recs;
    for (BaseItem* i : toRemove)
    {
        if (!i || i->manager != this || !i->userCanRemove) continue;
        json j = i->save();
        j["_index"] = indexOf(i);
        recs.push_back(j);
    }
    if (recs.empty()) return;
    // sort by index so undo re-inserts in order
    std::sort(recs.begin(), recs.end(),
              [](const json& a, const json& b) { return a.value("_index", 0) < b.value("_index", 0); });
    for (auto& r : recs) removeItem(r.value("uid", (uint64_t)0));

    BaseManager* self = this;
    UndoManager::get().pushDone("Remove Items",
        [self, recs] { for (auto& r : recs) self->removeItem(r.value("uid", (uint64_t)0)); },
        [self, recs] { for (auto& r : recs) self->addItemFromJson(r, r.value("_index", -1)); },
        { self });
}

void BaseManager::undoableDuplicate(const std::vector<BaseItem*>& toDup)
{
    std::vector<json> recs;
    sel().clear();
    for (BaseItem* i : toDup)
    {
        if (!i || i->manager != this || !i->userCanDuplicate) continue;
        json j = i->save();
        j["uid"] = nextUid++;
        j["viewPos"] = { i->viewPos.x + 24, i->viewPos.y + 24 };
        j["niceName"] = i->niceName + " Copy";
        j.erase("_index");
        BaseItem* ni = addItemFromJson(j);
        if (ni) { sel().add(ni); recs.push_back(ni->save()); }
    }
    if (recs.empty()) return;
    BaseManager* self = this;
    UndoManager::get().pushDone("Duplicate Items",
        [self, recs] { for (auto& r : recs) self->addItemFromJson(r); },
        [self, recs] { for (auto& r : recs) self->removeItem(r.value("uid", (uint64_t)0)); },
        { self });
}

void BaseManager::undoableMove(int from, int to)
{
    if (from == to) return;
    moveItem(from, to);
    BaseManager* self = this;
    UndoManager::get().pushDone("Reorder Items",
        [self, from, to] { self->moveItem(from, to); },
        [self, from, to] { self->moveItem(to, from); },
        { self });
}

void BaseManager::copyToClipboard(const std::vector<BaseItem*>& itemsToCopy) const
{
    json arr = json::array();
    for (BaseItem* i : itemsToCopy)
        if (i && i->manager == this) arr.push_back(i->save());
    if (arr.empty()) return;
    json env = { { "organic", "items" }, { "items", arr } };
    ImGui::SetClipboardText(env.dump().c_str());
    OLOG(niceName, "Copied " << arr.size() << " item(s)");
}

bool BaseManager::pasteFromClipboard(ImVec2 offset)
{
    const char* txt = ImGui::GetClipboardText();
    if (!txt) return false;
    json env;
    try { env = json::parse(txt); }
    catch (...) { return false; }
    if (!env.is_object() || env.value("organic", "") != "items") return false;

    std::vector<json> recs;
    sel().clear();
    for (auto& j : env["items"])
    {
        json jj = j;
        jj["uid"] = 0;
        jj.erase("_index");
        if (jj.contains("viewPos"))
            jj["viewPos"] = { (float)jj["viewPos"][0] + offset.x, (float)jj["viewPos"][1] + offset.y };
        BaseItem* ni = addItemFromJson(jj);
        if (ni) { sel().add(ni); recs.push_back(ni->save()); }
    }
    if (recs.empty()) return false;
    BaseManager* self = this;
    UndoManager::get().pushDone("Paste Items",
        [self, recs] { for (auto& r : recs) self->addItemFromJson(r); },
        [self, recs] { for (auto& r : recs) self->removeItem(r.value("uid", (uint64_t)0)); },
        { self });
    return true;
}

std::vector<BaseItem*> BaseManager::selectedItems() const
{
    std::vector<BaseItem*> out;
    for (auto& i : items)
        if (sel().contains(i.get())) out.push_back(i.get());
    return out;
}

void BaseManager::selectAll()
{
    sel().clear();
    for (auto& i : items) sel().add(i.get());
}

json BaseManager::save() const
{
    json j = Container::save();
    j["nextUid"] = nextUid;
    j["viewOffset"] = { viewOffset.x, viewOffset.y };
    j["viewZoom"] = viewZoom;
    json arr = json::array();
    for (auto& i : items) arr.push_back(i->save());
    j["items"] = arr;
    return j;
}

void BaseManager::load(const json& j)
{
    Container::load(j);
    items.clear();
    if (j.contains("viewOffset")) viewOffset = ImVec2(j["viewOffset"][0], j["viewOffset"][1]);
    if (j.contains("viewZoom"))   viewZoom = j["viewZoom"].get<float>();
    if (j.contains("items"))
        for (auto& ij : j["items"]) addItemFromJson(ij);
    if (j.contains("nextUid")) nextUid = std::max(nextUid, j["nextUid"].get<uint64_t>());
}

// ================================================================ shared UI helpers
// factory menu; returns the chosen type or "" (call inside an open popup/menu)
static std::string factoryMenuItems(BaseManager& m)
{
    std::string picked;
    // group by submenu prefix
    std::vector<std::pair<std::string, std::vector<const BaseManager::Def*>>> groups;
    for (auto& d : m.factory)
    {
        std::string grp, label = d.menuPath;
        size_t slash = d.menuPath.find('/');
        if (slash != std::string::npos)
        {
            grp = d.menuPath.substr(0, slash);
            label = d.menuPath.substr(slash + 1);
        }
        bool found = false;
        for (auto& g : groups)
            if (g.first == grp) { g.second.push_back(&d); found = true; }
        if (!found) groups.push_back({ grp, { &d } });
    }
    auto leafLabel = [](const BaseManager::Def& d) -> std::string
    {
        size_t slash = d.menuPath.find('/');
        return slash == std::string::npos ? d.menuPath : d.menuPath.substr(slash + 1);
    };
    for (auto& g : groups)
    {
        if (g.first.empty())
        {
            for (auto* d : g.second)
                if (ImGui::MenuItem(leafLabel(*d).c_str())) picked = d->type;
        }
        else if (ImGui::BeginMenu(g.first.c_str()))
        {
            for (auto* d : g.second)
                if (ImGui::MenuItem(leafLabel(*d).c_str())) picked = d->type;
            ImGui::EndMenu();
        }
    }
    return picked;
}

// apply a uid ordering (used by list drag-reorder undo)
static void applyOrder(BaseManager& m, const std::vector<uint64_t>& order)
{
    std::vector<std::unique_ptr<BaseItem>> next;
    for (uint64_t uid : order)
    {
        for (auto& i : m.items)
            if (i && i->uid == uid) next.push_back(std::move(i));
    }
    for (auto& i : m.items)
        if (i) next.push_back(std::move(i));
    m.items = std::move(next);
    m.onItemsChanged();
}

static std::vector<uint64_t> currentOrder(BaseManager& m)
{
    std::vector<uint64_t> o;
    for (auto& i : m.items) o.push_back(i->uid);
    return o;
}

// keyboard shortcuts shared by both manager UIs (call when host window focused)
static void managerShortcuts(BaseManager& m)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;

    auto selItems = m.selectedItems();
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))
        m.undoableRemove(selItems);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) m.undoableDuplicate(selItems);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) m.selectAll();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) m.copyToClipboard(selItems);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) m.pasteFromClipboard();

    // select previous / next, move before / after
    if (!m.items.empty() &&
        (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow)))
    {
        int dir = ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? -1 : 1;
        int idx = selItems.empty() ? (dir > 0 ? -1 : (int)m.items.size())
                                   : m.indexOf(dir > 0 ? selItems.back() : selItems.front());
        if (io.KeyCtrl)
        {
            if (selItems.size() == 1)
            {
                int ni = idx + dir;
                if (ni >= 0 && ni < (int)m.items.size()) m.undoableMove(idx, ni);
            }
        }
        else
        {
            int ni = std::max(0, std::min((int)m.items.size() - 1, idx + dir));
            m.sel().set(m.items[ni].get());
        }
    }
}

// ================================================================ ManagerListUI
struct ListDragState
{
    BaseManager* mgr = nullptr;
    uint64_t uid = 0;
    std::vector<uint64_t> orderAtStart;
};
static ListDragState s_listDrag;

void ManagerListUI(BaseManager& m)
{
    ImGui::PushID(&m);
    Selection& sel = m.sel();

    // ---- toolbar
    if (m.userCanAdd && !m.factory.empty())
    {
        if (ImGui::Button("+ Add")) ImGui::OpenPopup("##list_add");
        if (ImGui::BeginPopup("##list_add"))
        {
            std::string t = factoryMenuItems(m);
            if (!t.empty()) m.undoableAdd(t);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
    }
    static std::unordered_map<const void*, std::string> s_filters;
    std::string& filter = s_filters[&m];
    ImGui::SetNextItemWidth(std::max(60.f, ImGui::GetContentRegionAvail().x - 90));
    ImGui::InputTextWithHint("##search", "Search...", &filter);
    ImGui::SameLine();
    ImGui::TextDisabled("%d", (int)m.items.size());
    ImGui::Separator();

    // ---- rows
    ImGui::BeginChild("##listrows");
    std::string filterLo = filter;
    std::transform(filterLo.begin(), filterLo.end(), filterLo.begin(), ::tolower);

    BaseItem* clickedItem = nullptr;
    bool clickedCtrl = false, clickedShift = false;

    for (size_t idx = 0; idx < m.items.size(); idx++)
    {
        BaseItem* item = m.items[idx].get();
        if (!filterLo.empty())
        {
            std::string n = item->niceName;
            std::transform(n.begin(), n.end(), n.begin(), ::tolower);
            if (n.find(filterLo) == std::string::npos) continue;
        }
        ImGui::PushID((int)(intptr_t)item->uid);

        // enable toggle
        bool en = item->enabled();
        if (ImGui::Checkbox("##en", &en)) item->enabledP->setUndoable(en);
        ImGui::SameLine();

        // color swatch -> picker popup
        if (ImGui::ColorButton("##col", item->colorP->color(),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(16, 16)))
            ImGui::OpenPopup("##colpick");
        if (ImGui::BeginPopup("##colpick"))
        {
            DrawParamWidget(*item->colorP);
            ImGui::EndPopup();
        }
        ImGui::SameLine();

        // mini-mode toggle
        if (ImGui::SmallButton(item->miniMode ? ">" : "v")) item->miniMode = !item->miniMode;
        ImGui::SetItemTooltip(item->miniMode ? "Expand" : "Collapse (mini mode)");
        ImGui::SameLine();

        // selectable name (with drag-to-reorder)
        bool isSel = sel.contains(item) || sel.preselContains(item);
        float removeW = 26.f;
        ImVec4 nameCol = item->enabled() ? ImVec4(0.92f, 0.92f, 0.94f, 1.f)
                                         : ImVec4(0.55f, 0.55f, 0.58f, 1.f);
        ImGui::PushStyleColor(ImGuiCol_Text, nameCol);
        std::string label = item->niceName + "##sel";
        if (ImGui::Selectable(label.c_str(), isSel,
                              ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(ImGui::GetContentRegionAvail().x - removeW, 0)))
        {
            clickedItem = item;
            clickedCtrl = ImGui::GetIO().KeyCtrl;
            clickedShift = ImGui::GetIO().KeyShift;
        }
        ImGui::PopStyleColor();
        if (item->isHighlighted())
        {
            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                IM_COL32(90, 160, 255, 160), 3.f);
        }

        // drag to reorder
        if (ImGui::IsItemActivated())
            s_listDrag = { &m, item->uid, currentOrder(m) };
        if (ImGui::IsItemActive() && !ImGui::IsItemHovered() && s_listDrag.mgr == &m)
        {
            ImVec2 mp = ImGui::GetMousePos();
            int dir = mp.y < ImGui::GetItemRectMin().y ? -1
                    : (mp.y > ImGui::GetItemRectMax().y ? 1 : 0);
            if (dir != 0)
            {
                int ni = (int)idx + dir;
                if (ni >= 0 && ni < (int)m.items.size())
                    m.moveItem((int)idx, ni); // raw move; undo pushed on release
            }
        }

        // context menu
        if (ImGui::BeginPopupContextItem("##item_ctx"))
        {
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) m.undoableDuplicate({ item });
            if (ImGui::MenuItem("Remove", "Del", false, item->userCanRemove)) m.undoableRemove({ item });
            ImGui::Separator();
            if (ImGui::MenuItem("Copy", "Ctrl+C")) m.copyToClipboard(m.selectedItems());
            if (ImGui::MenuItem("Paste", "Ctrl+V")) m.pasteFromClipboard();
            ImGui::EndPopup();
        }

        // remove button
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - removeW + 6);
        if (item->userCanRemove && ImGui::SmallButton("x"))
            m.undoableRemove({ item });

        // body
        if (!item->miniMode)
        {
            ImGui::Indent(24);
            item->inspectorGui();
            ImGui::Unindent(24);
        }
        ImGui::Separator();
        ImGui::PopID();
    }

    // click selection (after loop so shift-range sees stable order)
    if (clickedItem)
    {
        if (clickedShift && !sel.items.empty())
        {
            int a = -1, b = m.indexOf(clickedItem);
            for (auto& i : m.items)
                if (sel.contains(i.get())) { a = m.indexOf(i.get()); break; }
            if (a >= 0 && b >= 0)
            {
                if (a > b) std::swap(a, b);
                if (!clickedCtrl) sel.clear();
                for (int i = a; i <= b; i++) sel.add(m.items[i].get());
            }
            else clickedItem->select(clickedCtrl);
        }
        else if (clickedCtrl) sel.toggle(clickedItem);
        else sel.set(clickedItem);
    }

    // finish drag-reorder -> one undo step
    if (s_listDrag.mgr == &m && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        auto now = currentOrder(m);
        if (now != s_listDrag.orderAtStart)
        {
            BaseManager* self = &m;
            auto oldO = s_listDrag.orderAtStart, newO = now;
            UndoManager::get().pushDone("Reorder Items",
                [self, newO] { applyOrder(*self, newO); },
                [self, oldO] { applyOrder(*self, oldO); },
                { self });
        }
        s_listDrag = {};
    }

    ImGui::EndChild();
    managerShortcuts(m);
    ImGui::PopID();
}

// ================================================================ ManagerCanvasUI
struct CanvasState
{
    enum class Drag { None, Move, Resize, Rubber, Pan };
    Drag drag = Drag::None;
    BaseManager* mgr = nullptr;
    uint64_t itemUid = 0;
    ImVec2 startMouse, panStart;
    struct Ref { uint64_t uid; ImVec2 pos; };
    std::vector<Ref> moveRefs;
    ImVec2 resizeOrig;
    ImVec2 rubberStart;
    bool rubberAdd = false;
    bool moved = false;
    ImVec2 ctxWorld;
};
static CanvasState s_cv;

static float snapAxis(float v, const std::vector<float>& cands, float threshold, bool& snapped, float& guide)
{
    float best = v, bestD = threshold;
    snapped = false;
    for (float c : cands)
    {
        float d = std::fabs(v - c);
        if (d < bestD) { bestD = d; best = c; snapped = true; guide = c; }
    }
    return best;
}

void ManagerCanvasUI(BaseManager& m)
{
    ImGui::PushID(&m);
    Selection& sel = m.sel();
    ImGuiIO& io = ImGui::GetIO();
    auto selItems = m.selectedItems();

    // ---- toolbar: add + align/distribute + frame
    if (m.userCanAdd && !m.factory.empty())
    {
        if (ImGui::Button("+ Add")) ImGui::OpenPopup("##cv_addbtn");
        if (ImGui::BeginPopup("##cv_addbtn"))
        {
            std::string t = factoryMenuItems(m);
            if (!t.empty())
                m.undoableAdd(t, ImVec2(m.viewOffset.x, m.viewOffset.y));
            ImGui::EndPopup();
        }
        ImGui::SameLine();
    }

    bool canAlign = selItems.size() >= 2;
    auto alignOp = [&](const char* name, std::function<void(std::vector<BaseItem*>&)> fn)
    {
        ImGui::SameLine();
        ImGui::BeginDisabled(!canAlign);
        if (ImGui::SmallButton(name))
        {
            struct R { uint64_t uid; ImVec2 o, n; };
            std::vector<R> recs;
            for (auto* i : selItems) recs.push_back({ i->uid, i->viewPos, i->viewPos });
            fn(selItems);
            bool changed = false;
            for (auto& r : recs)
                if (BaseItem* i = m.findItem(r.uid))
                {
                    r.n = i->viewPos;
                    if (r.n.x != r.o.x || r.n.y != r.o.y) changed = true;
                }
            if (changed)
            {
                BaseManager* self = &m;
                UndoManager::get().pushDone("Align Items",
                    [self, recs] { for (auto& r : recs) if (BaseItem* i = self->findItem(r.uid)) i->viewPos = r.n; },
                    [self, recs] { for (auto& r : recs) if (BaseItem* i = self->findItem(r.uid)) i->viewPos = r.o; },
                    { self });
            }
        }
        ImGui::EndDisabled();
    };

    ImGui::SameLine();
    ImGui::TextDisabled("Align:");
    alignOp("L", [](std::vector<BaseItem*>& its)
    {
        float x = 1e9f; for (auto* i : its) x = std::min(x, i->viewPos.x);
        for (auto* i : its) i->viewPos.x = x;
    });
    alignOp("C", [](std::vector<BaseItem*>& its)
    {
        float mn = 1e9f, mx = -1e9f;
        for (auto* i : its) { mn = std::min(mn, i->viewPos.x); mx = std::max(mx, i->viewPos.x + i->viewSize.x); }
        float c = (mn + mx) * 0.5f;
        for (auto* i : its) i->viewPos.x = c - i->viewSize.x * 0.5f;
    });
    alignOp("R", [](std::vector<BaseItem*>& its)
    {
        float x = -1e9f; for (auto* i : its) x = std::max(x, i->viewPos.x + i->viewSize.x);
        for (auto* i : its) i->viewPos.x = x - i->viewSize.x;
    });
    alignOp("T", [](std::vector<BaseItem*>& its)
    {
        float y = 1e9f; for (auto* i : its) y = std::min(y, i->viewPos.y);
        for (auto* i : its) i->viewPos.y = y;
    });
    alignOp("M", [](std::vector<BaseItem*>& its)
    {
        float mn = 1e9f, mx = -1e9f;
        for (auto* i : its) { mn = std::min(mn, i->viewPos.y); mx = std::max(mx, i->viewPos.y + i->viewSize.y); }
        float c = (mn + mx) * 0.5f;
        for (auto* i : its) i->viewPos.y = c - i->viewSize.y * 0.5f;
    });
    alignOp("B", [](std::vector<BaseItem*>& its)
    {
        float y = -1e9f; for (auto* i : its) y = std::max(y, i->viewPos.y + i->viewSize.y);
        for (auto* i : its) i->viewPos.y = y - i->viewSize.y;
    });
    alignOp("H", [](std::vector<BaseItem*>& its)
    {
        std::sort(its.begin(), its.end(), [](BaseItem* a, BaseItem* b) { return a->viewPos.x < b->viewPos.x; });
        if (its.size() < 3) return;
        float x0 = its.front()->viewPos.x;
        float x1 = its.back()->viewPos.x;
        for (size_t i = 0; i < its.size(); i++)
            its[i]->viewPos.x = x0 + (x1 - x0) * (float)i / (float)(its.size() - 1);
    });
    alignOp("V", [](std::vector<BaseItem*>& its)
    {
        std::sort(its.begin(), its.end(), [](BaseItem* a, BaseItem* b) { return a->viewPos.y < b->viewPos.y; });
        if (its.size() < 3) return;
        float y0 = its.front()->viewPos.y;
        float y1 = its.back()->viewPos.y;
        for (size_t i = 0; i < its.size(); i++)
            its[i]->viewPos.y = y0 + (y1 - y0) * (float)i / (float)(its.size() - 1);
    });
    ImGui::SameLine();
    ImGui::TextDisabled("| zoom %.0f%%", m.viewZoom * 100.f);

    // ---- canvas
    ImGui::BeginChild("##canvas", ImVec2(0, 0), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 cp0 = ImGui::GetCursorScreenPos();
    ImVec2 csz = ImGui::GetContentRegionAvail();
    if (csz.x < 40 || csz.y < 40) { ImGui::EndChild(); ImGui::PopID(); return; }
    ImVec2 cCenter(cp0.x + csz.x * 0.5f, cp0.y + csz.y * 0.5f);

    float& zoom = m.viewZoom;
    ImVec2& off = m.viewOffset; // world coords at canvas center
    auto W2S = [&](ImVec2 w) { return ImVec2(cCenter.x + (w.x - off.x) * zoom, cCenter.y + (w.y - off.y) * zoom); };
    auto S2W = [&](ImVec2 s) { return ImVec2((s.x - cCenter.x) / zoom + off.x, (s.y - cCenter.y) / zoom + off.y); };

    ImGui::InvisibleButton("##cvbtn", csz,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    ImVec2 mouse = io.MousePos;

    // minimap geometry (interactions checked before canvas ops)
    float mmW = 170, mmH = 110;
    ImVec2 mmMin(cp0.x + csz.x - mmW - 10, cp0.y + csz.y - mmH - 10);
    ImVec2 mmMax(mmMin.x + mmW, mmMin.y + mmH);
    bool inMinimap = hovered && inRectMgr(mouse, mmMin, mmMax);

    // world bounds of all items
    ImVec2 wbMin(1e9f, 1e9f), wbMax(-1e9f, -1e9f);
    for (auto& i : m.items)
    {
        wbMin.x = std::min(wbMin.x, i->viewPos.x); wbMin.y = std::min(wbMin.y, i->viewPos.y);
        wbMax.x = std::max(wbMax.x, i->viewPos.x + i->viewSize.x);
        wbMax.y = std::max(wbMax.y, i->viewPos.y + i->viewSize.y);
    }
    bool hasItems = wbMin.x < wbMax.x;

    // ---- hit test (topmost item last in draw order = last in list)
    BaseItem* hitItem = nullptr;
    bool hitResize = false;
    bool hitTitle = false;
    if (hovered && !inMinimap)
    {
        for (int i = (int)m.items.size() - 1; i >= 0; i--)
        {
            BaseItem* it = m.items[i].get();
            ImVec2 p0 = W2S(it->viewPos);
            ImVec2 p1 = W2S(ImVec2(it->viewPos.x + it->viewSize.x, it->viewPos.y + it->viewSize.y));
            if (mouse.x >= p0.x && mouse.x < p1.x && mouse.y >= p0.y && mouse.y < p1.y)
            {
                hitItem = it;
                hitResize = (mouse.x > p1.x - 12 && mouse.y > p1.y - 12) && !it->uiLocked;
                // drag from the title bar (body may host interactive widgets)
                hitTitle = mouse.y < p0.y + std::min(24.f * zoom, p1.y - p0.y);
                break;
            }
        }
    }
    if (hitResize || s_cv.drag == CanvasState::Drag::Resize)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);

    // ---- wheel
    if (hovered && io.MouseWheel != 0)
    {
        if (io.KeyCtrl)
        {
            ImVec2 wAtMouse = S2W(mouse);
            zoom = std::max(0.2f, std::min(3.f, zoom * std::pow(1.15f, io.MouseWheel)));
            ImVec2 sAfter = W2S(wAtMouse);
            off.x += (sAfter.x - mouse.x) / zoom;
            off.y += (sAfter.y - mouse.y) / zoom;
        }
        else if (io.KeyShift) off.x -= io.MouseWheel * 40.f / zoom;
        else                  off.y -= io.MouseWheel * 40.f / zoom;
    }

    // ---- press
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        s_cv.mgr = &m;
        s_cv.startMouse = mouse;
        s_cv.moved = false;
        if (inMinimap && hasItems)
        {
            s_cv.drag = CanvasState::Drag::Pan; // minimap jump handled below
        }
        else if (io.KeyAlt || (!hitItem && io.KeyShift))
        {
            s_cv.drag = CanvasState::Drag::Pan;
            s_cv.panStart = off;
        }
        else if (hitItem && hitResize)
        {
            if (!sel.contains(hitItem)) sel.set(hitItem);
            s_cv.drag = CanvasState::Drag::Resize;
            s_cv.itemUid = hitItem->uid;
            s_cv.resizeOrig = hitItem->viewSize;
        }
        else if (hitItem)
        {
            bool was = sel.contains(hitItem);
            if (io.KeyCtrl) sel.toggle(hitItem);
            else if (!was) sel.set(hitItem);
            // move only from the title bar so widgets inside the card stay usable
            if (hitTitle && sel.contains(hitItem) && !hitItem->uiLocked)
            {
                s_cv.drag = CanvasState::Drag::Move;
                s_cv.itemUid = hitItem->uid;
                s_cv.moveRefs.clear();
                for (auto* i : m.selectedItems())
                    if (!i->uiLocked) s_cv.moveRefs.push_back({ i->uid, i->viewPos });
            }
        }
        else
        {
            s_cv.drag = CanvasState::Drag::Rubber;
            s_cv.rubberStart = mouse;
            s_cv.rubberAdd = io.KeyCtrl;
        }
        if (hitItem && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            s_cv.drag = CanvasState::Drag::None;
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
    {
        s_cv.mgr = &m;
        s_cv.drag = CanvasState::Drag::Pan;
        s_cv.startMouse = mouse;
        s_cv.panStart = off;
    }

    // snap guides collected during move
    std::vector<float> guideX, guideY;

    // ---- drag update / release
    if (s_cv.mgr == &m && s_cv.drag != CanvasState::Drag::None)
    {
        bool lDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        bool mDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        if (std::fabs(mouse.x - s_cv.startMouse.x) + std::fabs(mouse.y - s_cv.startMouse.y) > 3.f)
            s_cv.moved = true;

        switch (s_cv.drag)
        {
        case CanvasState::Drag::Pan:
        {
            if (inRectMgr(s_cv.startMouse, mmMin, mmMax) && hasItems && lDown)
            {
                // minimap navigate: map mouse to world
                float sx = (wbMax.x - wbMin.x) / std::max(1.f, mmW - 8);
                float sy = (wbMax.y - wbMin.y) / std::max(1.f, mmH - 8);
                float s = std::max(sx, sy);
                ImVec2 c((wbMin.x + wbMax.x) * 0.5f, (wbMin.y + wbMax.y) * 0.5f);
                ImVec2 mmC((mmMin.x + mmMax.x) * 0.5f, (mmMin.y + mmMax.y) * 0.5f);
                off = ImVec2(c.x + (mouse.x - mmC.x) * s, c.y + (mouse.y - mmC.y) * s);
            }
            else if (lDown || mDown)
            {
                off.x = s_cv.panStart.x - (mouse.x - s_cv.startMouse.x) / zoom;
                off.y = s_cv.panStart.y - (mouse.y - s_cv.startMouse.y) / zoom;
            }
            else s_cv.drag = CanvasState::Drag::None;
            break;
        }
        case CanvasState::Drag::Move:
        {
            if (lDown)
            {
                ImVec2 delta((mouse.x - s_cv.startMouse.x) / zoom, (mouse.y - s_cv.startMouse.y) / zoom);
                // group bbox at original positions
                ImVec2 bbMin(1e9f, 1e9f), bbMax(-1e9f, -1e9f);
                for (auto& r : s_cv.moveRefs)
                {
                    BaseItem* i = m.findItem(r.uid);
                    if (!i) continue;
                    bbMin.x = std::min(bbMin.x, r.pos.x); bbMin.y = std::min(bbMin.y, r.pos.y);
                    bbMax.x = std::max(bbMax.x, r.pos.x + i->viewSize.x);
                    bbMax.y = std::max(bbMax.y, r.pos.y + i->viewSize.y);
                }
                bbMin.x += delta.x; bbMin.y += delta.y;
                bbMax.x += delta.x; bbMax.y += delta.y;
                // snap: candidates from unselected items (edges + centers)
                if (!io.KeyAlt)
                {
                    std::vector<float> cx, cy;
                    for (auto& i : m.items)
                    {
                        bool moving = false;
                        for (auto& r : s_cv.moveRefs) if (r.uid == i->uid) moving = true;
                        if (moving) continue;
                        cx.push_back(i->viewPos.x);
                        cx.push_back(i->viewPos.x + i->viewSize.x);
                        cx.push_back(i->viewPos.x + i->viewSize.x * 0.5f);
                        cy.push_back(i->viewPos.y);
                        cy.push_back(i->viewPos.y + i->viewSize.y);
                        cy.push_back(i->viewPos.y + i->viewSize.y * 0.5f);
                    }
                    float th = 7.f / zoom, g;
                    bool s1, s2, s3;
                    float bx = snapAxis(bbMin.x, cx, th, s1, g);
                    if (s1) { delta.x += bx - bbMin.x; guideX.push_back(g); }
                    else
                    {
                        float bx2 = snapAxis(bbMax.x, cx, th, s2, g);
                        if (s2) { delta.x += bx2 - bbMax.x; guideX.push_back(g); }
                        else
                        {
                            float bc = snapAxis((bbMin.x + bbMax.x) * 0.5f, cx, th, s3, g);
                            if (s3) { delta.x += bc - (bbMin.x + bbMax.x) * 0.5f; guideX.push_back(g); }
                        }
                    }
                    float by = snapAxis(bbMin.y, cy, th, s1, g);
                    if (s1) { delta.y += by - bbMin.y; guideY.push_back(g); }
                    else
                    {
                        float by2 = snapAxis(bbMax.y, cy, th, s2, g);
                        if (s2) { delta.y += by2 - bbMax.y; guideY.push_back(g); }
                        else
                        {
                            float bc = snapAxis((bbMin.y + bbMax.y) * 0.5f, cy, th, s3, g);
                            if (s3) { delta.y += bc - (bbMin.y + bbMax.y) * 0.5f; guideY.push_back(g); }
                        }
                    }
                }
                for (auto& r : s_cv.moveRefs)
                    if (BaseItem* i = m.findItem(r.uid))
                        i->viewPos = ImVec2(r.pos.x + delta.x, r.pos.y + delta.y);
            }
            else
            {
                if (s_cv.moved)
                {
                    struct R { uint64_t uid; ImVec2 o, n; };
                    std::vector<R> recs;
                    for (auto& r : s_cv.moveRefs)
                        if (BaseItem* i = m.findItem(r.uid))
                            recs.push_back({ r.uid, r.pos, i->viewPos });
                    BaseManager* self = &m;
                    UndoManager::get().pushDone("Move Items",
                        [self, recs] { for (auto& r : recs) if (BaseItem* i = self->findItem(r.uid)) i->viewPos = r.n; },
                        [self, recs] { for (auto& r : recs) if (BaseItem* i = self->findItem(r.uid)) i->viewPos = r.o; },
                        { self });
                }
                s_cv.drag = CanvasState::Drag::None;
            }
            break;
        }
        case CanvasState::Drag::Resize:
        {
            BaseItem* i = m.findItem(s_cv.itemUid);
            if (lDown && i)
            {
                ImVec2 d((mouse.x - s_cv.startMouse.x) / zoom, (mouse.y - s_cv.startMouse.y) / zoom);
                i->viewSize = ImVec2(std::max(70.f, s_cv.resizeOrig.x + d.x),
                                     std::max(40.f, s_cv.resizeOrig.y + d.y));
            }
            else
            {
                if (i && s_cv.moved)
                {
                    uint64_t uid = s_cv.itemUid;
                    ImVec2 o = s_cv.resizeOrig, n = i->viewSize;
                    BaseManager* self = &m;
                    UndoManager::get().pushDone("Resize Item",
                        [self, uid, n] { if (BaseItem* it = self->findItem(uid)) it->viewSize = n; },
                        [self, uid, o] { if (BaseItem* it = self->findItem(uid)) it->viewSize = o; },
                        { self });
                }
                s_cv.drag = CanvasState::Drag::None;
            }
            break;
        }
        case CanvasState::Drag::Rubber:
        {
            if (lDown)
            {
                ImVec2 rMin(std::min(s_cv.rubberStart.x, mouse.x), std::min(s_cv.rubberStart.y, mouse.y));
                ImVec2 rMax(std::max(s_cv.rubberStart.x, mouse.x), std::max(s_cv.rubberStart.y, mouse.y));
                std::vector<Inspectable*> pre;
                for (auto& i : m.items)
                {
                    ImVec2 p0 = W2S(i->viewPos);
                    ImVec2 p1 = W2S(ImVec2(i->viewPos.x + i->viewSize.x, i->viewPos.y + i->viewSize.y));
                    if (p1.x >= rMin.x && p0.x <= rMax.x && p1.y >= rMin.y && p0.y <= rMax.y)
                        pre.push_back(i.get());
                }
                sel.setPreselection(pre);
            }
            else
            {
                if (!s_cv.moved)
                {
                    sel.clearPreselection();
                    if (!s_cv.rubberAdd) sel.clear();
                }
                else if (s_cv.rubberAdd) sel.commitPreselection();
                else
                {
                    sel.items = sel.preselected;
                    sel.preselected.clear();
                    sel.touch();
                }
                s_cv.drag = CanvasState::Drag::None;
            }
            break;
        }
        default: break;
        }
    }

    // ---- right click menus
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !s_cv.moved)
    {
        s_cv.ctxWorld = S2W(mouse);
        if (hitItem)
        {
            if (!sel.contains(hitItem)) sel.set(hitItem);
            s_cv.itemUid = hitItem->uid;
            ImGui::OpenPopup("##cv_item_ctx");
        }
        else ImGui::OpenPopup("##cv_ctx");
    }

    // ---- draw
    dl->PushClipRect(cp0, ImVec2(cp0.x + csz.x, cp0.y + csz.y), true);
    dl->AddRectFilled(cp0, ImVec2(cp0.x + csz.x, cp0.y + csz.y), IM_COL32(24, 24, 27, 255));

    // grid
    {
        float step = 40.f * zoom;
        while (step < 14.f) step *= 2.f;
        ImVec2 w0 = S2W(cp0);
        float gx = std::floor(w0.x / (step / zoom)) * (step / zoom);
        float gy = std::floor(w0.y / (step / zoom)) * (step / zoom);
        for (float x = gx; W2S(ImVec2(x, 0)).x < cp0.x + csz.x; x += step / zoom)
            dl->AddLine(ImVec2(W2S(ImVec2(x, 0)).x, cp0.y), ImVec2(W2S(ImVec2(x, 0)).x, cp0.y + csz.y),
                        IM_COL32(255, 255, 255, 8));
        for (float y = gy; W2S(ImVec2(0, y)).y < cp0.y + csz.y; y += step / zoom)
            dl->AddLine(ImVec2(cp0.x, W2S(ImVec2(0, y)).y), ImVec2(cp0.x + csz.x, W2S(ImVec2(0, y)).y),
                        IM_COL32(255, 255, 255, 8));
        // origin
        ImVec2 o = W2S(ImVec2(0, 0));
        dl->AddLine(ImVec2(o.x, cp0.y), ImVec2(o.x, cp0.y + csz.y), IM_COL32(255, 255, 255, 22));
        dl->AddLine(ImVec2(cp0.x, o.y), ImVec2(cp0.x + csz.x, o.y), IM_COL32(255, 255, 255, 22));
    }

    ImVec4 accent(1.f, 0.573f, 0.184f, 1.f);
    ImU32 accentU = ImGui::ColorConvertFloat4ToU32(accent);

    // items (draw + inline widgets)
    for (auto& iu : m.items)
    {
        BaseItem* i = iu.get();
        ImVec2 p0 = W2S(i->viewPos);
        ImVec2 p1 = W2S(ImVec2(i->viewPos.x + i->viewSize.x, i->viewPos.y + i->viewSize.y));
        if (p1.x < cp0.x - 4 || p0.x > cp0.x + csz.x + 4 || p1.y < cp0.y - 4 || p0.y > cp0.y + csz.y + 4)
            continue;

        bool isSel = sel.contains(i);
        bool isPre = sel.preselContains(i);
        bool isHil = i->isHighlighted();
        ImVec4 col = i->colorP->color();
        if (!i->enabled()) col.w *= 0.45f;

        dl->AddRectFilled(ImVec2(p0.x + 3, p0.y + 3), ImVec2(p1.x + 3, p1.y + 3), IM_COL32(0, 0, 0, 90), 6.f);
        dl->AddRectFilled(p0, p1, ImGui::ColorConvertFloat4ToU32(ImVec4(col.x * 0.35f, col.y * 0.35f, col.z * 0.35f, 0.97f)), 6.f);
        float titleH = std::min(24.f * zoom, p1.y - p0.y);
        dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + titleH),
                          ImGui::ColorConvertFloat4ToU32(ImVec4(col.x * 0.6f, col.y * 0.6f, col.z * 0.6f, col.w)), 6.f,
                          ImDrawFlags_RoundCornersTop);
        // enable dot
        dl->AddCircleFilled(ImVec2(p0.x + 9 * zoom, p0.y + titleH * 0.5f), 3.5f * zoom,
                            i->enabled() ? IM_COL32(120, 255, 140, 230) : IM_COL32(120, 120, 125, 200));
        dl->PushClipRect(ImVec2(p0.x + 14 * zoom, p0.y), ImVec2(p1.x - 4, p0.y + titleH), true);
        dl->AddText(ImVec2(p0.x + 17 * zoom, p0.y + (titleH - ImGui::GetFontSize()) * 0.5f),
                    IM_COL32(240, 240, 245, 255), i->niceName.c_str());
        dl->PopClipRect();

        if (i->uiLocked)
            dl->AddText(ImVec2(p1.x - 16, p0.y + 3), IM_COL32(255, 255, 255, 140), "L");

        if (isSel)      dl->AddRect(p0, p1, accentU, 6.f, 0, 2.f);
        else if (isPre) dl->AddRect(p0, p1, ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 0.6f)), 6.f, 0, 1.5f);
        else if (isHil) dl->AddRect(p0, p1, IM_COL32(90, 160, 255, 200), 6.f, 0, 2.f);
        else if (hitItem == i) dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 80), 6.f, 0, 1.f);

        // resize grip
        if (!i->uiLocked)
        {
            dl->AddTriangleFilled(ImVec2(p1.x - 3, p1.y - 12), ImVec2(p1.x - 3, p1.y - 3),
                                  ImVec2(p1.x - 12, p1.y - 3), IM_COL32(255, 255, 255, 60));
        }

        // inline content (real widgets) under the title bar, zoom > 0.6 only
        float bodyY = p0.y + titleH + 4;
        if (zoom > 0.6f && p1.y - bodyY > 18 && p1.x - p0.x > 60)
        {
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 8, bodyY));
            ImGui::PushID((int)(intptr_t)i->uid);
            ImGui::BeginGroup();
            ImGui::PushClipRect(ImVec2(p0.x + 2, bodyY), ImVec2(p1.x - 2, p1.y - 2), true);
            ImGui::PushItemWidth(std::max(50.f, (p1.x - p0.x) - 60));
            i->canvasGui();
            ImGui::PopItemWidth();
            ImGui::PopClipRect();
            ImGui::EndGroup();
            ImGui::PopID();
        }
    }

    // snap guides
    for (float gx : guideX)
    {
        float x = W2S(ImVec2(gx, 0)).x;
        dl->AddLine(ImVec2(x, cp0.y), ImVec2(x, cp0.y + csz.y), accentU, 1.f);
    }
    for (float gy : guideY)
    {
        float y = W2S(ImVec2(0, gy)).y;
        dl->AddLine(ImVec2(cp0.x, y), ImVec2(cp0.x + csz.x, y), accentU, 1.f);
    }

    // rubber
    if (s_cv.mgr == &m && s_cv.drag == CanvasState::Drag::Rubber && s_cv.moved)
    {
        ImVec2 rMin(std::min(s_cv.rubberStart.x, mouse.x), std::min(s_cv.rubberStart.y, mouse.y));
        ImVec2 rMax(std::max(s_cv.rubberStart.x, mouse.x), std::max(s_cv.rubberStart.y, mouse.y));
        dl->AddRectFilled(rMin, rMax, ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 0.12f)));
        dl->AddRect(rMin, rMax, accentU);
    }

    // minimap
    if (hasItems)
    {
        dl->AddRectFilled(mmMin, mmMax, IM_COL32(15, 15, 17, 220), 4.f);
        dl->AddRect(mmMin, mmMax, IM_COL32(255, 255, 255, 40), 4.f);
        float sx = (wbMax.x - wbMin.x) / std::max(1.f, mmW - 8);
        float sy = (wbMax.y - wbMin.y) / std::max(1.f, mmH - 8);
        float s = std::max({ sx, sy, 0.001f });
        ImVec2 c((wbMin.x + wbMax.x) * 0.5f, (wbMin.y + wbMax.y) * 0.5f);
        ImVec2 mmC((mmMin.x + mmMax.x) * 0.5f, (mmMin.y + mmMax.y) * 0.5f);
        auto W2M = [&](ImVec2 w) { return ImVec2(mmC.x + (w.x - c.x) / s, mmC.y + (w.y - c.y) / s); };
        for (auto& i : m.items)
        {
            ImVec2 a = W2M(i->viewPos);
            ImVec2 b = W2M(ImVec2(i->viewPos.x + i->viewSize.x, i->viewPos.y + i->viewSize.y));
            dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(i->colorP->color()), 2.f);
            if (sel.contains(i.get())) dl->AddRect(a, b, accentU, 2.f);
        }
        ImVec2 va = W2M(S2W(cp0));
        ImVec2 vb = W2M(S2W(ImVec2(cp0.x + csz.x, cp0.y + csz.y)));
        dl->AddRect(va, vb, IM_COL32(255, 255, 255, 130), 2.f);
    }

    dl->PopClipRect();

    // ---- popups
    if (ImGui::BeginPopup("##cv_ctx"))
    {
        if (m.userCanAdd && !m.factory.empty())
        {
            ImGui::TextDisabled("Add");
            std::string t = factoryMenuItems(m);
            if (!t.empty()) m.undoableAdd(t, s_cv.ctxWorld);
            ImGui::Separator();
        }
        if (ImGui::MenuItem("Paste", "Ctrl+V")) m.pasteFromClipboard();
        if (ImGui::MenuItem("Frame All", "F") && hasItems)
        {
            off = ImVec2((wbMin.x + wbMax.x) * 0.5f, (wbMin.y + wbMax.y) * 0.5f);
            zoom = std::max(0.2f, std::min(3.f, 0.9f * std::min(csz.x / std::max(1.f, wbMax.x - wbMin.x),
                                                                csz.y / std::max(1.f, wbMax.y - wbMin.y))));
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##cv_item_ctx"))
    {
        BaseItem* it = m.findItem(s_cv.itemUid);
        if (it)
        {
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) m.undoableDuplicate(m.selectedItems());
            if (ImGui::MenuItem("Delete", "Del", false, it->userCanRemove)) m.undoableRemove(m.selectedItems());
            if (ImGui::MenuItem(it->uiLocked ? "Unlock" : "Lock")) it->uiLocked = !it->uiLocked;
            ImGui::Separator();
            if (ImGui::MenuItem("Copy", "Ctrl+C")) m.copyToClipboard(m.selectedItems());
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();

    // frame shortcut + manager shortcuts
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_F, false) && hasItems)
        {
            auto its = m.selectedItems();
            ImVec2 mn = wbMin, mx = wbMax;
            if (!its.empty())
            {
                mn = ImVec2(1e9f, 1e9f); mx = ImVec2(-1e9f, -1e9f);
                for (auto* i : its)
                {
                    mn.x = std::min(mn.x, i->viewPos.x); mn.y = std::min(mn.y, i->viewPos.y);
                    mx.x = std::max(mx.x, i->viewPos.x + i->viewSize.x);
                    mx.y = std::max(mx.y, i->viewPos.y + i->viewSize.y);
                }
            }
            off = ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
            zoom = std::max(0.2f, std::min(3.f, 0.9f * std::min(csz.x / std::max(1.f, mx.x - mn.x),
                                                                csz.y / std::max(1.f, mx.y - mn.y))));
        }
    }
    managerShortcuts(m);
    ImGui::PopID();
}

} // namespace organic
