// OrganicManager.h - generic manager / item framework.
// The ImGui equivalent of organicui's BaseItem / BaseManager / Factory plus the
// two standard manager UIs: a list view (search, reorder, per-item headers) and
// an infinite 2D canvas view (pan/zoom, snap guides, align tools, minimap).
#pragma once

#include "OrganicCore.h"

namespace organic
{

// ---------------------------------------------------------------- BaseItem
class BaseManager;

class BaseItem : public Container
{
public:
    BaseItem(const std::string& typeName, const std::string& niceName);

    std::string typeName;          // factory type id
    uint64_t    uid = 0;           // unique inside the owning manager
    BaseManager* manager = nullptr;

    Parameter* enabledP = nullptr; // bypass toggle shown in UIs
    Parameter* colorP   = nullptr; // item color

    bool   miniMode  = false;      // collapsed in list view
    bool   uiLocked  = false;      // cannot be moved/resized in canvas view
    ImVec2 viewPos   = ImVec2(0, 0);    // canvas placement (world units)
    ImVec2 viewSize  = ImVec2(160, 70); // canvas size

    bool userCanRemove = true;
    bool userCanDuplicate = true;

    bool enabled() const { return enabledP->boolValue(); }

    // extra content drawn inside canvas cards (default: nothing)
    virtual void canvasGui() {}

    std::string inspectableTypeName() const override { return typeName; }

    json save() const override;
    void load(const json& j) override;
};

// ---------------------------------------------------------------- BaseManager
class BaseManager : public Container
{
public:
    explicit BaseManager(const std::string& niceName, Container* parent = nullptr);

    struct Def
    {
        std::string menuPath; // "Basics/Note" -> submenu Basics, item Note
        std::string type;
        std::function<std::unique_ptr<BaseItem>()> create;
    };

    std::vector<std::unique_ptr<BaseItem>> items;
    std::vector<Def> factory;
    bool userCanAdd = true;
    std::string selectionScopeName; // "" = main selection

    // canvas view state (persisted)
    ImVec2 viewOffset = ImVec2(0, 0);
    float  viewZoom   = 1.f;

    uint64_t nextUid = 1;

    Selection& sel() const;

    void addDef(const std::string& menuPath, const std::string& type,
                std::function<std::unique_ptr<BaseItem>()> create);
    std::unique_ptr<BaseItem> createFromType(const std::string& type) const;

    BaseItem* addItem(std::unique_ptr<BaseItem> item, int index = -1);
    BaseItem* addItemFromJson(const json& j, int index = -1);
    json      removeItem(uint64_t uid);            // returns data (with "_index")
    BaseItem* findItem(uint64_t uid) const;
    int       indexOf(const BaseItem* item) const;
    void      moveItem(int from, int to);

    // undoable operations (single undo steps, usable from any UI)
    BaseItem* undoableAdd(const std::string& type, ImVec2 canvasPos = ImVec2(0, 0),
                          int index = -1);
    void undoableRemove(const std::vector<BaseItem*>& toRemove);
    void undoableDuplicate(const std::vector<BaseItem*>& toDup);
    void undoableMove(int from, int to);

    // clipboard (system clipboard, JSON envelope; paste works across managers
    // sharing the same item types)
    void copyToClipboard(const std::vector<BaseItem*>& itemsToCopy) const;
    bool pasteFromClipboard(ImVec2 canvasPosOffset = ImVec2(30, 30));

    std::vector<BaseItem*> selectedItems() const;
    void selectAll();

    virtual void onItemsChanged() {}

    json save() const override;
    void load(const json& j) override;
};

// ---------------------------------------------------------------- manager UIs
// Draw a list-style manager editor inside the current window:
// toolbar ("+" factory menu, search box), rows with enable toggle / color
// swatch / rename / mini-mode / remove, drag-to-reorder, keyboard shortcuts
// (Up/Down select, Ctrl+Up/Down move, Del, Ctrl+D, Ctrl+C/V, Ctrl+A).
void ManagerListUI(BaseManager& m);

// Draw an infinite 2D canvas editor inside the current window:
// pan (middle/alt-drag), zoom at mouse (Ctrl+wheel), grid, movable/resizable
// item cards, snap-to-item guides, rubber-band (preselect) selection,
// align/distribute toolbar, minimap, factory context menu, full undo.
void ManagerCanvasUI(BaseManager& m);

} // namespace organic
