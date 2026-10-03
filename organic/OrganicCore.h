// OrganicCore.h - data model core of the "organic" ImGui framework.
// Inspired by juce_organicui (Controllable / Parameter / ControllableContainer /
// Inspectable / SelectionManager / UndoMaster / Logger) re-imagined for Dear ImGui.
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <variant>
#include <sstream>
#include <cstdint>
#include "imgui.h"
#include "json.hpp"

namespace organic
{
using json = nlohmann::json;

// ---------------------------------------------------------------- helpers
std::string shortNameOf(const std::string& niceName); // "My Param" -> "myParam"
std::string formatTime(double seconds, bool withMs = true);

// ---------------------------------------------------------------- Logger
enum class LogLevel { Info, Warning, Error };

struct LogEntry
{
    LogLevel    level;
    std::string source;
    std::string message;
    double      time; // seconds since app start
};

class Logger
{
public:
    static Logger& get();
    void log(LogLevel lvl, const std::string& source, const std::string& msg);
    void clear();

    std::vector<LogEntry> entries;
    uint32_t revision = 0; // bumped on every log, lets UIs know something changed
};

#define ORGANIC_LOG_STREAM(lvl, source, expr)                                   \
    do { std::ostringstream _oss_; _oss_ << expr;                               \
         organic::Logger::get().log(lvl, source, _oss_.str()); } while (0)

#define OLOG(source, expr)  ORGANIC_LOG_STREAM(organic::LogLevel::Info, source, expr)
#define OLOGW(source, expr) ORGANIC_LOG_STREAM(organic::LogLevel::Warning, source, expr)
#define OLOGE(source, expr) ORGANIC_LOG_STREAM(organic::LogLevel::Error, source, expr)

// ---------------------------------------------------------------- Undo (organicui's UndoMaster)
struct UndoAction
{
    std::string               name;
    std::function<void()>     redoFn;
    std::function<void()>     undoFn;
    std::vector<const void*>  owners; // objects referenced by the lambdas; purged when they die
};

class UndoManager
{
public:
    static UndoManager& get();

    // Executes doFn, then records the action.
    void perform(const std::string& name, std::function<void()> doFn,
                 std::function<void()> undoFn, std::vector<const void*> owners = {});
    // Records an action whose "do" part already happened (e.g. end of a mouse drag).
    void pushDone(const std::string& name, std::function<void()> redoFn,
                  std::function<void()> undoFn, std::vector<const void*> owners = {});

    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
    std::string undoName() const { return canUndo() ? undoStack.back().name : ""; }
    std::string redoName() const { return canRedo() ? redoStack.back().name : ""; }

    void undo();
    void redo();
    void clear();
    void purgeOwner(const void* owner); // drop actions referencing a dying object

    int maxActions = 250;
    std::vector<UndoAction> undoStack, redoStack;
};

// ---------------------------------------------------------------- Value / Parameter
enum class PType { Trigger, Bool, Int, Float, String, Enum, Color, Point2D };
const char* ptypeName(PType t);

using Value = std::variant<std::monostate, bool, int, float, std::string, ImVec2, ImVec4>;

bool  valueEquals(const Value& a, const Value& b);
json  valueToJson(const Value& v);
Value valueFromJson(const json& j, PType type);

class Container;

class Parameter
{
public:
    Parameter(Container* parent, PType type, const std::string& niceName, Value def);
    ~Parameter();

    Container*  parent = nullptr;
    PType       type;
    std::string niceName, shortName, description;

    // Optional localized display overrides. These never affect shortName / serialization (the JSON
    // key stays derived from the English niceName), so translating the UI does not break save files.
    // Empty means "fall back to the English niceName / enumOptions / description".
    std::string displayName;
    std::string displayDescription;
    std::vector<std::string> enumLabels;
    const std::string& label()     const { return displayName.empty() ? niceName : displayName; }
    const std::string& tooltip()   const { return displayDescription.empty() ? description : displayDescription; }
    const std::string& enumLabel(int i) const
    {
        if (i >= 0 && i < (int)enumLabels.size() && !enumLabels[i].empty()) return enumLabels[i];
        return enumOptions[i];
    }

    Value value, defaultValue;

    bool  hasRange = false;
    float minF = 0.f, maxF = 1.f;      // used by Int / Float (int cast)
    std::vector<std::string> enumOptions;

    bool readOnly     = false;
    bool hideInEditor = false;
    float dragSpeed   = 0.01f;         // for unbounded drags
    const char* unit  = "";            // display suffix, e.g. "s"

    uint32_t revision = 0;             // bumped on every change

    std::function<void(Parameter&)> onChange; // also fired on trigger()

    // typed getters
    float       floatValue()  const;
    int         intValue()    const;
    bool        boolValue()   const;
    std::string stringValue() const;
    ImVec2      point()       const;
    ImVec4      color()       const;

    void setValue(const Value& v, bool notify = true);
    void setUndoable(const Value& newValue);                 // executes + records
    void recordEdit(const Value& oldValue, const Value& newValue); // already applied, record only
    void resetToDefault(bool undoable = true);
    bool isOverriden() const { return !valueEquals(value, defaultValue); }
    void trigger();                                          // Trigger type only

    Value clamped(const Value& v) const;
    std::string controlAddress() const;

    json toJson() const { return valueToJson(value); }
    void fromJson(const json& j) { setValue(valueFromJson(j, type)); }
};

// ---------------------------------------------------------------- Inspectable / Selection
class Inspectable
{
public:
    virtual ~Inspectable();
    virtual std::string inspectableTypeName() const { return "Object"; }
    virtual std::string inspectableLabel()    const { return "Object"; }
    virtual void        inspectorGui() {}

    bool isSelected() const;     // selected in any selection scope
    bool isPreselected() const;  // inside a rubber band that is still being dragged
    bool isHighlighted() const;  // a linked inspectable is selected
    void select(bool addToSelection = false); // selects in the main scope

    std::vector<Inspectable*> linkedInspectables; // managed via linkInspectables()
};

// Link two inspectables: selecting one highlights the other (organicui's
// linked inspectables). Links are removed automatically on destruction.
void linkInspectables(Inspectable* a, Inspectable* b);
void unlinkInspectables(Inspectable* a, Inspectable* b);

// A selection scope. organicui has several selection managers (main + per
// panel); here: Selection::get() is the main scope, Selection::scope("name")
// creates/returns named scopes, Selection::active() is the scope that last
// changed (the Inspector follows it).
class Selection
{
public:
    static Selection& get();                        // main scope
    static Selection& scope(const std::string& n);  // named scope registry
    static Selection* active();                     // last scope that changed
    static std::vector<Selection*>& allScopes();

    std::string name = "main";

    void set(Inspectable* i);
    void add(Inspectable* i);
    void toggle(Inspectable* i);
    void remove(Inspectable* i);
    void clear();
    bool contains(const Inspectable* i) const;

    // preselection (rubber band in progress)
    void setPreselection(const std::vector<Inspectable*>& items);
    void clearPreselection();
    void commitPreselection(); // preselected -> selected (added)
    bool preselContains(const Inspectable* i) const;

    template <typename T> std::vector<T*> getAs() const
    {
        std::vector<T*> out;
        for (auto* i : items) if (auto* t = dynamic_cast<T*>(i)) out.push_back(t);
        return out;
    }

    std::vector<Inspectable*> items;
    std::vector<Inspectable*> preselected;
    uint32_t revision = 0;

    void touch(); // bump revision + become the active scope
};

// ---------------------------------------------------------------- Container
class Container : public Inspectable
{
public:
    explicit Container(const std::string& niceName, Container* parent = nullptr);
    ~Container() override;

    std::string niceName, shortName;
    Container*  parent = nullptr;
    std::vector<Container*> children;                 // weak links (ownership is elsewhere)
    std::vector<std::unique_ptr<Parameter>> params;   // owned

    bool renamable    = false;
    bool hideInOutliner = false;

    void setNiceName(const std::string& n);

    // factory helpers (mirrors organicui's addFloatParameter etc.)
    Parameter* addTrigger(const std::string& nice, const std::string& desc = "");
    Parameter* addBool  (const std::string& nice, bool def, const std::string& desc = "");
    Parameter* addInt   (const std::string& nice, int def, int mn, int mx, const std::string& desc = "");
    Parameter* addFloat (const std::string& nice, float def, float mn, float mx, const std::string& desc = "");
    Parameter* addFloatUnbounded(const std::string& nice, float def, const std::string& desc = "");
    Parameter* addString(const std::string& nice, const std::string& def, const std::string& desc = "");
    Parameter* addEnum  (const std::string& nice, const std::vector<std::string>& options, int defIndex, const std::string& desc = "");
    Parameter* addColor (const std::string& nice, ImVec4 def, const std::string& desc = "");
    Parameter* addPoint2D(const std::string& nice, ImVec2 def, const std::string& desc = "");

    Parameter* getParam(const std::string& shortName) const;

    void addChild(Container* c);
    void removeChild(Container* c);

    std::string controlAddress() const; // "/parent/child"

    // change notification: bubbles up like organicui's controllableFeedbackUpdate
    virtual void onParamChanged(Parameter* p) { (void)p; }
    virtual void onChildParamChanged(Container* c, Parameter* p) { (void)c; (void)p; }
    void notifyParamChanged(Parameter* p);

    // serialization: params only; subclasses extend for structure
    virtual json save() const;
    virtual void load(const json& j);

    // Inspectable
    std::string inspectableTypeName() const override { return "Container"; }
    std::string inspectableLabel()    const override { return niceName; }
    void        inspectorGui()        override;
};

// ---------------------------------------------------------------- root registry (Outliner roots)
std::vector<Container*>& rootContainers();
void registerRoot(Container* c);
void unregisterRoot(Container* c);

// Resolve a control address ("/seq/layer/param") to a Parameter / Container,
// searching from the registered roots. Returns nullptr when not found.
Parameter* resolveParamAddress(const std::string& address);
Container* resolveContainerAddress(const std::string& address);

// ---------------------------------------------------------------- generic parameter widget (defined in OrganicPanels.cpp)
// Draws the right ImGui widget for the parameter, with undo/redo integration.
bool DrawParamWidget(Parameter& p);
// One widget editing several parameters at once (multi-editing).
// Uses the first parameter for display; applies changes to all of them.
bool DrawParamWidgetMulti(const std::vector<Parameter*>& params);
// InputText bound to a std::string with undo integration (used for renames).
bool UndoableInputText(const char* label, std::string& str, const void* owner,
                       std::function<void(const std::string&, const std::string&)> apply);
// Popup tree to pick any parameter in the app; returns true when a pick was
// made and writes its control address. Call ImGui::OpenPopup(popupId) first.
bool ParamPickerPopup(const char* popupId, std::string& address);

} // namespace organic
