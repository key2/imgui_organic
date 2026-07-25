#include "OrganicCore.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace organic
{

// ---------------------------------------------------------------- helpers
std::string shortNameOf(const std::string& niceName)
{
    std::string out;
    bool upNext = false;
    for (char c : niceName)
    {
        if (std::isalnum((unsigned char)c))
        {
            if (out.empty()) out.push_back((char)std::tolower((unsigned char)c));
            else out.push_back(upNext ? (char)std::toupper((unsigned char)c) : c);
            upNext = false;
        }
        else upNext = true;
    }
    if (out.empty()) out = "param";
    return out;
}

std::string formatTime(double seconds, bool withMs)
{
    if (seconds < 0) seconds = 0;
    int total = (int)seconds;
    int mins = total / 60;
    int secs = total % 60;
    int ms   = (int)std::round((seconds - total) * 1000.0);
    if (ms >= 1000) { ms = 0; secs++; if (secs >= 60) { secs = 0; mins++; } }
    char buf[64];
    if (withMs) snprintf(buf, sizeof(buf), "%d:%02d.%03d", mins, secs, ms);
    else        snprintf(buf, sizeof(buf), "%d:%02d", mins, secs);
    return buf;
}

// ---------------------------------------------------------------- Logger
Logger& Logger::get() { static Logger l; return l; }

void Logger::log(LogLevel lvl, const std::string& source, const std::string& msg)
{
    entries.push_back({ lvl, source, msg, ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0 });
    if (entries.size() > 5000) entries.erase(entries.begin(), entries.begin() + 1000);
    revision++;
}

void Logger::clear() { entries.clear(); revision++; }

// ---------------------------------------------------------------- UndoManager
UndoManager& UndoManager::get() { static UndoManager u; return u; }

void UndoManager::perform(const std::string& name, std::function<void()> doFn,
                          std::function<void()> undoFn, std::vector<const void*> owners)
{
    if (doFn) doFn();
    pushDone(name, std::move(doFn), std::move(undoFn), std::move(owners));
}

void UndoManager::pushDone(const std::string& name, std::function<void()> redoFn,
                           std::function<void()> undoFn, std::vector<const void*> owners)
{
    undoStack.push_back({ name, std::move(redoFn), std::move(undoFn), std::move(owners) });
    redoStack.clear();
    if ((int)undoStack.size() > maxActions)
        undoStack.erase(undoStack.begin(), undoStack.begin() + (undoStack.size() - maxActions));
}

void UndoManager::undo()
{
    if (undoStack.empty()) return;
    UndoAction a = std::move(undoStack.back());
    undoStack.pop_back();
    if (a.undoFn) a.undoFn();
    OLOG("Undo", "Undo: " << a.name);
    redoStack.push_back(std::move(a));
}

void UndoManager::redo()
{
    if (redoStack.empty()) return;
    UndoAction a = std::move(redoStack.back());
    redoStack.pop_back();
    if (a.redoFn) a.redoFn();
    OLOG("Undo", "Redo: " << a.name);
    undoStack.push_back(std::move(a));
}

void UndoManager::clear() { undoStack.clear(); redoStack.clear(); }

void UndoManager::purgeOwner(const void* owner)
{
    auto match = [owner](const UndoAction& a)
    {
        return std::find(a.owners.begin(), a.owners.end(), owner) != a.owners.end();
    };
    undoStack.erase(std::remove_if(undoStack.begin(), undoStack.end(), match), undoStack.end());
    redoStack.erase(std::remove_if(redoStack.begin(), redoStack.end(), match), redoStack.end());
}

// ---------------------------------------------------------------- Value helpers
const char* ptypeName(PType t)
{
    switch (t)
    {
    case PType::Trigger: return "Trigger";
    case PType::Bool:    return "Bool";
    case PType::Int:     return "Int";
    case PType::Float:   return "Float";
    case PType::String:  return "String";
    case PType::Enum:    return "Enum";
    case PType::Color:   return "Color";
    case PType::Point2D: return "Point2D";
    }
    return "?";
}

bool valueEquals(const Value& a, const Value& b)
{
    if (a.index() != b.index()) return false;
    switch (a.index())
    {
    case 0: return true;
    case 1: return std::get<bool>(a) == std::get<bool>(b);
    case 2: return std::get<int>(a) == std::get<int>(b);
    case 3: return std::get<float>(a) == std::get<float>(b);
    case 4: return std::get<std::string>(a) == std::get<std::string>(b);
    case 5: { auto& x = std::get<ImVec2>(a); auto& y = std::get<ImVec2>(b); return x.x == y.x && x.y == y.y; }
    case 6: { auto& x = std::get<ImVec4>(a); auto& y = std::get<ImVec4>(b); return x.x == y.x && x.y == y.y && x.z == y.z && x.w == y.w; }
    }
    return false;
}

json valueToJson(const Value& v)
{
    switch (v.index())
    {
    case 1: return std::get<bool>(v);
    case 2: return std::get<int>(v);
    case 3: return std::get<float>(v);
    case 4: return std::get<std::string>(v);
    case 5: { auto& p = std::get<ImVec2>(v); return json::array({ p.x, p.y }); }
    case 6: { auto& c = std::get<ImVec4>(v); return json::array({ c.x, c.y, c.z, c.w }); }
    default: return nullptr;
    }
}

Value valueFromJson(const json& j, PType type)
{
    try
    {
        switch (type)
        {
        case PType::Trigger: return std::monostate{};
        case PType::Bool:    return j.get<bool>();
        case PType::Int:
        case PType::Enum:    return j.get<int>();
        case PType::Float:   return j.get<float>();
        case PType::String:  return j.get<std::string>();
        case PType::Point2D: return ImVec2(j.at(0).get<float>(), j.at(1).get<float>());
        case PType::Color:   return ImVec4(j.at(0).get<float>(), j.at(1).get<float>(),
                                           j.at(2).get<float>(), j.at(3).get<float>());
        }
    }
    catch (...) {}
    return std::monostate{};
}

// ---------------------------------------------------------------- Parameter
Parameter::Parameter(Container* p, PType t, const std::string& nice, Value def)
    : parent(p), type(t), niceName(nice), shortName(shortNameOf(nice)),
      value(def), defaultValue(def)
{
}

Parameter::~Parameter()
{
    UndoManager::get().purgeOwner(this);
}

float Parameter::floatValue() const
{
    if (auto* f = std::get_if<float>(&value)) return *f;
    if (auto* i = std::get_if<int>(&value)) return (float)*i;
    if (auto* b = std::get_if<bool>(&value)) return *b ? 1.f : 0.f;
    return 0.f;
}

int Parameter::intValue() const
{
    if (auto* i = std::get_if<int>(&value)) return *i;
    if (auto* f = std::get_if<float>(&value)) return (int)*f;
    if (auto* b = std::get_if<bool>(&value)) return *b ? 1 : 0;
    return 0;
}

bool Parameter::boolValue() const
{
    if (auto* b = std::get_if<bool>(&value)) return *b;
    return intValue() != 0;
}

std::string Parameter::stringValue() const
{
    if (auto* s = std::get_if<std::string>(&value)) return *s;
    if (type == PType::Enum)
    {
        int i = intValue();
        if (i >= 0 && i < (int)enumOptions.size()) return enumOptions[i];
    }
    if (auto* f = std::get_if<float>(&value)) { char b[32]; snprintf(b, 32, "%.3f", *f); return b; }
    if (auto* i = std::get_if<int>(&value)) return std::to_string(*i);
    if (auto* bo = std::get_if<bool>(&value)) return *bo ? "true" : "false";
    return "";
}

ImVec2 Parameter::point() const
{
    if (auto* p = std::get_if<ImVec2>(&value)) return *p;
    return ImVec2(0, 0);
}

ImVec4 Parameter::color() const
{
    if (auto* c = std::get_if<ImVec4>(&value)) return *c;
    return ImVec4(1, 1, 1, 1);
}

Value Parameter::clamped(const Value& v) const
{
    if (!hasRange) return v;
    if (auto* f = std::get_if<float>(&v)) return std::min(maxF, std::max(minF, *f));
    if (auto* i = std::get_if<int>(&v))   return std::min((int)maxF, std::max((int)minF, *i));
    return v;
}

void Parameter::setValue(const Value& v, bool notify)
{
    Value cv = clamped(v);
    if (valueEquals(cv, value) && type != PType::Trigger) return;
    value = cv;
    revision++;
    if (notify)
    {
        if (onChange) onChange(*this);
        if (parent) parent->notifyParamChanged(this);
    }
}

void Parameter::setUndoable(const Value& newValue)
{
    Value oldV = value;
    Value newV = clamped(newValue);
    if (valueEquals(oldV, newV)) return;
    Parameter* self = this;
    UndoManager::get().perform(
        "Set " + niceName,
        [self, newV] { self->setValue(newV); },
        [self, oldV] { self->setValue(oldV); },
        { this });
}

void Parameter::recordEdit(const Value& oldValue, const Value& newValue)
{
    if (valueEquals(oldValue, newValue)) return;
    Parameter* self = this;
    UndoManager::get().pushDone(
        "Set " + niceName,
        [self, newValue] { self->setValue(newValue); },
        [self, oldValue] { self->setValue(oldValue); },
        { this });
}

void Parameter::resetToDefault(bool undoable)
{
    if (undoable) setUndoable(defaultValue);
    else setValue(defaultValue);
}

void Parameter::trigger()
{
    if (type != PType::Trigger) return;
    revision++;
    if (onChange) onChange(*this);
    if (parent) parent->notifyParamChanged(this);
}

std::string Parameter::controlAddress() const
{
    return (parent ? parent->controlAddress() : "") + "/" + shortName;
}

// ---------------------------------------------------------------- Inspectable / Selection
Inspectable::~Inspectable()
{
    Selection::get().remove(this);
    UndoManager::get().purgeOwner(this);
}

bool Inspectable::isSelected() const { return Selection::get().contains(this); }
void Inspectable::select(bool add)
{
    if (add) Selection::get().toggle(this);
    else Selection::get().set(this);
}

Selection& Selection::get() { static Selection s; return s; }

void Selection::set(Inspectable* i)
{
    items.clear();
    if (i) items.push_back(i);
    revision++;
}
void Selection::add(Inspectable* i)
{
    if (i && !contains(i)) { items.push_back(i); revision++; }
}
void Selection::toggle(Inspectable* i)
{
    if (!i) return;
    auto it = std::find(items.begin(), items.end(), i);
    if (it != items.end()) items.erase(it); else items.push_back(i);
    revision++;
}
void Selection::remove(Inspectable* i)
{
    auto it = std::find(items.begin(), items.end(), i);
    if (it != items.end()) { items.erase(it); revision++; }
}
void Selection::clear()
{
    if (!items.empty()) { items.clear(); revision++; }
}
bool Selection::contains(const Inspectable* i) const
{
    return std::find(items.begin(), items.end(), i) != items.end();
}

// ---------------------------------------------------------------- Container
Container::Container(const std::string& nice, Container* p)
    : niceName(nice), shortName(shortNameOf(nice)), parent(p)
{
    if (parent) parent->addChild(this);
}

Container::~Container()
{
    if (parent) parent->removeChild(this);
    // children unregister themselves when destroyed; clear weak links
    for (auto* c : children) c->parent = nullptr;
    unregisterRoot(this);
}

void Container::setNiceName(const std::string& n)
{
    niceName = n;
    shortName = shortNameOf(n);
}

static Parameter* addParamTo(Container* c, PType t, const std::string& nice, Value def, const std::string& desc)
{
    auto p = std::make_unique<Parameter>(c, t, nice, def);
    p->description = desc;
    Parameter* raw = p.get();
    c->params.push_back(std::move(p));
    return raw;
}

Parameter* Container::addTrigger(const std::string& nice, const std::string& desc)
{ return addParamTo(this, PType::Trigger, nice, std::monostate{}, desc); }

Parameter* Container::addBool(const std::string& nice, bool def, const std::string& desc)
{ return addParamTo(this, PType::Bool, nice, def, desc); }

Parameter* Container::addInt(const std::string& nice, int def, int mn, int mx, const std::string& desc)
{
    auto* p = addParamTo(this, PType::Int, nice, def, desc);
    p->hasRange = true; p->minF = (float)mn; p->maxF = (float)mx;
    return p;
}

Parameter* Container::addFloat(const std::string& nice, float def, float mn, float mx, const std::string& desc)
{
    auto* p = addParamTo(this, PType::Float, nice, def, desc);
    p->hasRange = true; p->minF = mn; p->maxF = mx;
    return p;
}

Parameter* Container::addFloatUnbounded(const std::string& nice, float def, const std::string& desc)
{ return addParamTo(this, PType::Float, nice, def, desc); }

Parameter* Container::addString(const std::string& nice, const std::string& def, const std::string& desc)
{ return addParamTo(this, PType::String, nice, def, desc); }

Parameter* Container::addEnum(const std::string& nice, const std::vector<std::string>& options, int defIndex, const std::string& desc)
{
    auto* p = addParamTo(this, PType::Enum, nice, defIndex, desc);
    p->enumOptions = options;
    return p;
}

Parameter* Container::addColor(const std::string& nice, ImVec4 def, const std::string& desc)
{ return addParamTo(this, PType::Color, nice, def, desc); }

Parameter* Container::addPoint2D(const std::string& nice, ImVec2 def, const std::string& desc)
{ return addParamTo(this, PType::Point2D, nice, def, desc); }

Parameter* Container::getParam(const std::string& sn) const
{
    for (auto& p : params) if (p->shortName == sn) return p.get();
    return nullptr;
}

void Container::addChild(Container* c)
{
    if (std::find(children.begin(), children.end(), c) == children.end())
        children.push_back(c);
    c->parent = this;
}

void Container::removeChild(Container* c)
{
    children.erase(std::remove(children.begin(), children.end(), c), children.end());
}

std::string Container::controlAddress() const
{
    return (parent ? parent->controlAddress() : "") + "/" + shortName;
}

void Container::notifyParamChanged(Parameter* p)
{
    onParamChanged(p);
    Container* anc = parent;
    Container* src = this;
    while (anc)
    {
        anc->onChildParamChanged(src, p);
        anc = anc->parent;
    }
}

json Container::save() const
{
    json j;
    j["niceName"] = niceName;
    json jp = json::object();
    for (auto& p : params)
        if (p->type != PType::Trigger)
            jp[p->shortName] = p->toJson();
    j["params"] = jp;
    return j;
}

void Container::load(const json& j)
{
    if (j.contains("niceName")) setNiceName(j["niceName"].get<std::string>());
    if (j.contains("params"))
    {
        for (auto& [k, v] : j["params"].items())
            if (Parameter* p = getParam(k)) p->fromJson(v);
    }
}

void Container::inspectorGui()
{
    for (auto& p : params)
        if (!p->hideInEditor)
            DrawParamWidget(*p);
}

// ---------------------------------------------------------------- roots
std::vector<Container*>& rootContainers()
{
    static std::vector<Container*> roots;
    return roots;
}

void registerRoot(Container* c)
{
    auto& r = rootContainers();
    if (std::find(r.begin(), r.end(), c) == r.end()) r.push_back(c);
}

void unregisterRoot(Container* c)
{
    auto& r = rootContainers();
    r.erase(std::remove(r.begin(), r.end(), c), r.end());
}

} // namespace organic
