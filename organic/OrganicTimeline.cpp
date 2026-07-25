#include "OrganicTimeline.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace organic
{

// ================================================================ Clip
Clip::Clip(ClipLayer* l, CType t, const std::string& name)
    : Container(name, l), layer(l), ctype(t)
{
    renamable = true;
    startP  = addFloatUnbounded("Start", 0.f, "Start time in seconds");
    startP->unit = "s"; startP->dragSpeed = 0.05f;
    lengthP = addFloatUnbounded("Length", 4.f, "Duration in seconds");
    lengthP->unit = "s"; lengthP->dragSpeed = 0.05f;
    colorP  = addColor("Color", ImVec4(0.30f, 0.55f, 0.85f, 1.f), "Clip color");

    if (ctype == CType::Audio)
    {
        fileP   = addString("File", "", "Audio file path (.wav)");
        gainP   = addFloat("Gain", 1.f, 0.f, 4.f, "Waveform display gain");
        offsetP = addFloatUnbounded("Offset", 0.f, "Offset into the media, in seconds");
        offsetP->unit = "s"; offsetP->dragSpeed = 0.05f;
        colorP->setValue(ImVec4(0.25f, 0.65f, 0.45f, 1.f));
        colorP->defaultValue = colorP->value;
    }
}

void Clip::setAudioFile(const std::string& path, bool adjustLength)
{
    if (ctype != CType::Audio) return;
    fileP->setValue(path, false);
    asset = AudioCache::get().load(path);
    if (asset && adjustLength)
        lengthP->setValue((float)asset->buffer.duration());
}

void Clip::onParamChanged(Parameter* p)
{
    if (p == startP && p->floatValue() < 0) p->setValue(0.f, false);
    if (p == lengthP && p->floatValue() < 0.05f) p->setValue(0.05f, false);
    if (p == offsetP && p->floatValue() < 0) p->setValue(0.f, false);
    if (p == fileP)
    {
        std::string f = fileP->stringValue();
        if (f.empty()) asset = nullptr;
        else if (std::filesystem::exists(f)) asset = AudioCache::get().load(f);
    }
}

void Clip::inspectorGui()
{
    Container::inspectorGui();
    if (ctype == CType::Audio)
    {
        if (asset)
            ImGui::TextDisabled("%s | %.2fs | %d ch | %d Hz", asset->path.c_str(),
                                asset->buffer.duration(), asset->buffer.channels,
                                asset->buffer.sampleRate);
        else
            ImGui::TextDisabled("No audio loaded");
    }
}

json Clip::save() const
{
    json j = Container::save();
    j["id"]    = id;
    j["ctype"] = ctype == CType::Audio ? "audio" : "block";
    return j;
}

void Clip::load(const json& j)
{
    Container::load(j);
    if (j.contains("id")) id = j["id"].get<uint64_t>();
    if (ctype == CType::Audio && fileP && !fileP->stringValue().empty())
        asset = AudioCache::get().load(fileP->stringValue());
}

// ================================================================ Layer
Layer::Layer(Sequence* s, LType t, const std::string& name)
    : Container(name, s), sequence(s), ltype(t)
{
    renamable = true;
    colorP = addColor("Color", ImVec4(0.55f, 0.55f, 0.6f, 1.f), "Layer tint");
    switch (t)
    {
    case LType::Clips:      uiHeight = 64.f; break;
    case LType::Automation: uiHeight = 90.f; break;
    case LType::Gradient:   uiHeight = 34.f; break;
    }
}

const char* Layer::ltypeName(LType t)
{
    switch (t)
    {
    case LType::Clips:      return "Clips";
    case LType::Automation: return "Automation";
    case LType::Gradient:   return "Gradient";
    }
    return "?";
}

json Layer::save() const
{
    json j = Container::save();
    j["id"]     = id;
    j["ltype"]  = (int)ltype;
    j["height"] = uiHeight;
    return j;
}

void Layer::load(const json& j)
{
    Container::load(j);
    if (j.contains("id"))     id = j["id"].get<uint64_t>();
    if (j.contains("height")) uiHeight = j["height"].get<float>();
}

// ================================================================ ClipLayer
ClipLayer::ClipLayer(Sequence* s, const std::string& name)
    : Layer(s, LType::Clips, name)
{
}

Clip* ClipLayer::addClip(Clip::CType type, const std::string& name, double t, double len)
{
    auto c = std::make_unique<Clip>(this, type, name);
    c->id = sequence->newId();
    c->startP->setValue((float)t, false);
    c->lengthP->setValue((float)len, false);
    Clip* raw = c.get();
    clips.push_back(std::move(c));
    sortClips();
    return raw;
}

Clip* ClipLayer::addClipFromJson(const json& j)
{
    Clip::CType t = (j.value("ctype", "block") == std::string("audio")) ? Clip::CType::Audio : Clip::CType::Block;
    auto c = std::make_unique<Clip>(this, t, j.value("niceName", "Clip"));
    Clip* raw = c.get();
    clips.push_back(std::move(c));
    raw->load(j);
    if (raw->id == 0) raw->id = sequence->newId();
    sequence->nextId = std::max(sequence->nextId, raw->id + 1);
    sortClips();
    return raw;
}

json ClipLayer::removeClip(uint64_t cid)
{
    for (size_t i = 0; i < clips.size(); i++)
    {
        if (clips[i]->id == cid)
        {
            json j = clips[i]->save();
            clips.erase(clips.begin() + i);
            return j;
        }
    }
    return json();
}

Clip* ClipLayer::findClip(uint64_t cid) const
{
    for (auto& c : clips) if (c->id == cid) return c.get();
    return nullptr;
}

void ClipLayer::sortClips()
{
    std::stable_sort(clips.begin(), clips.end(),
                     [](const std::unique_ptr<Clip>& a, const std::unique_ptr<Clip>& b)
                     { return a->start() < b->start(); });
}

json ClipLayer::save() const
{
    json j = Layer::save();
    json arr = json::array();
    for (auto& c : clips) arr.push_back(c->save());
    j["clips"] = arr;
    return j;
}

void ClipLayer::load(const json& j)
{
    Layer::load(j);
    clips.clear();
    if (j.contains("clips"))
        for (auto& cj : j["clips"]) addClipFromJson(cj);
}

void ClipLayer::inspectorGui()
{
    Container::inspectorGui();
    ImGui::TextDisabled("%d clip(s)", (int)clips.size());
}

// ================================================================ AutomationLayer
AutomationLayer::AutomationLayer(Sequence* s, const std::string& name)
    : Layer(s, LType::Automation, name)
{
    rangeMinP = addFloatUnbounded("Range Min", 0.f, "Lowest output value");
    rangeMaxP = addFloatUnbounded("Range Max", 1.f, "Highest output value");
    colorP->setValue(ImVec4(0.9f, 0.57f, 0.18f, 1.f));
    colorP->defaultValue = colorP->value;
}

float AutomationLayer::valueAt(double t) const
{
    float mn = rangeMinP->floatValue(), mx = rangeMaxP->floatValue();
    if (keys.empty()) return mn;
    if (t <= keys.front().time) return keys.front().value;
    if (t >= keys.back().time) return keys.back().value;
    for (size_t i = 0; i + 1 < keys.size(); i++)
    {
        const AutoKey& a = keys[i];
        const AutoKey& b = keys[i + 1];
        if (t >= a.time && t <= b.time)
        {
            double span = b.time - a.time;
            float w = span > 0 ? (float)((t - a.time) / span) : 1.f;
            float range = (mx - mn) == 0 ? 1.f : (mx - mn);
            // normalize for amplitude-based easings, then map back
            float na = (a.value - mn) / range;
            float nb = (b.value - mn) / range;
            float nv = ease(a.easing, na, nb, w, a.ep);
            return mn + nv * range;
        }
    }
    return keys.back().value;
}

float AutomationLayer::normValueAt(double t) const
{
    float mn = rangeMinP->floatValue(), mx = rangeMaxP->floatValue();
    float range = (mx - mn) == 0 ? 1.f : (mx - mn);
    return (valueAt(t) - mn) / range;
}

AutoKey* AutomationLayer::addKey(double t, float v, EasingType e)
{
    AutoKey k;
    k.id = sequence->newId();
    k.time = std::max(0.0, t);
    k.value = v;
    k.easing = e;
    keys.push_back(k);
    sortKeys();
    return findKey(k.id);
}

AutoKey* AutomationLayer::findKey(uint64_t kid)
{
    for (auto& k : keys) if (k.id == kid) return &k;
    return nullptr;
}

void AutomationLayer::removeKey(uint64_t kid)
{
    keys.erase(std::remove_if(keys.begin(), keys.end(),
               [kid](const AutoKey& k) { return k.id == kid; }), keys.end());
    selectedKeys.erase(kid);
}

void AutomationLayer::sortKeys()
{
    std::stable_sort(keys.begin(), keys.end(),
                     [](const AutoKey& a, const AutoKey& b) { return a.time < b.time; });
}

json AutomationLayer::keysToJson() const
{
    json arr = json::array();
    for (auto& k : keys)
        arr.push_back({ { "id", k.id }, { "t", k.time }, { "v", k.value },
                        { "e", (int)k.easing }, { "ep", k.ep.toJson() } });
    return arr;
}

void AutomationLayer::keysFromJson(const json& arr)
{
    keys.clear();
    selectedKeys.clear();
    if (!arr.is_array()) return;
    for (auto& kj : arr)
    {
        AutoKey k;
        k.id     = kj.value("id", (uint64_t)0);
        k.time   = kj.value("t", 0.0);
        k.value  = kj.value("v", 0.f);
        k.easing = (EasingType)kj.value("e", 0);
        if (kj.contains("ep")) k.ep.fromJson(kj["ep"]);
        if (k.id == 0) k.id = sequence->newId();
        sequence->nextId = std::max(sequence->nextId, k.id + 1);
        keys.push_back(k);
    }
    sortKeys();
}

json AutomationLayer::save() const
{
    json j = Layer::save();
    j["keys"] = keysToJson();
    return j;
}

void AutomationLayer::load(const json& j)
{
    Layer::load(j);
    if (j.contains("keys")) keysFromJson(j["keys"]);
}

// coarse undo tracker for inspector key edits: snapshot before first edit,
// commit as a single undo step once the user is idle again
namespace
{
    struct KeysEditTracker
    {
        void* layer = nullptr;
        json  pre;
    };
    static KeysEditTracker s_keysEdit;

    template <typename LayerT>
    void trackKeysEdit(LayerT* self, const json& preFrame, bool edited, bool anyActive, const char* label)
    {
        if (edited && s_keysEdit.layer != self)
        {
            s_keysEdit.layer = self;
            s_keysEdit.pre = preFrame; // state captured before this frame's edits
        }
        if (s_keysEdit.layer == self && !edited && !anyActive &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            json post = self->keysToJson();
            if (post != s_keysEdit.pre)
            {
                json pre = s_keysEdit.pre;
                UndoManager::get().pushDone(label,
                    [self, post] { self->keysFromJson(post); },
                    [self, pre]  { self->keysFromJson(pre); },
                    { self });
            }
            s_keysEdit.layer = nullptr;
        }
    }
}

void AutomationLayer::inspectorGui()
{
    Container::inspectorGui();
    ImGui::TextDisabled("%d key(s), %d selected", (int)keys.size(), (int)selectedKeys.size());

    json preFrame = keysToJson();
    bool edited = false;
    bool anyActive = false;

    for (uint64_t kid : std::vector<uint64_t>(selectedKeys.begin(), selectedKeys.end()))
    {
        AutoKey* k = findKey(kid);
        if (!k) continue;
        ImGui::PushID((int)(intptr_t)kid);
        ImGui::SeparatorText("Key");
        float t = (float)k->time, v = k->value;
        if (ImGui::DragFloat("Time", &t, 0.01f, 0.f, 1e9f, "%.3f s"))  { k->time = t; edited = true; }
        anyActive |= ImGui::IsItemActive();
        if (ImGui::DragFloat("Value", &v, 0.01f)) { k->value = v; edited = true; }
        anyActive |= ImGui::IsItemActive();

        int e = (int)k->easing;
        const char* names[] = { "Linear", "Bezier", "Hold", "Sine", "Elastic", "Bounce", "Steps", "Noise" };
        if (ImGui::Combo("Easing", &e, names, IM_ARRAYSIZE(names))) { k->easing = (EasingType)e; edited = true; }

        switch (k->easing)
        {
        case EasingType::Sine:
        case EasingType::Noise:
            edited |= ImGui::DragFloat("Frequency", &k->ep.freq, 0.05f, 0.1f, 50.f);
            anyActive |= ImGui::IsItemActive();
            edited |= ImGui::DragFloat("Amplitude", &k->ep.amp, 0.01f, 0.f, 2.f);
            anyActive |= ImGui::IsItemActive();
            break;
        case EasingType::Elastic:
            edited |= ImGui::DragFloat("Frequency", &k->ep.freq, 0.05f, 0.1f, 50.f);
            anyActive |= ImGui::IsItemActive();
            break;
        case EasingType::Steps:
            edited |= ImGui::DragInt("Steps", &k->ep.steps, 0.1f, 1, 64);
            anyActive |= ImGui::IsItemActive();
            break;
        default: break;
        }
        ImGui::PopID();
    }

    if (edited) sortKeys();
    trackKeysEdit(this, preFrame, edited, anyActive, "Edit Keys");
}

// ================================================================ GradientLayer
GradientLayer::GradientLayer(Sequence* s, const std::string& name)
    : Layer(s, LType::Gradient, name)
{
    colorP->setValue(ImVec4(0.6f, 0.4f, 0.8f, 1.f));
    colorP->defaultValue = colorP->value;
}

ImVec4 GradientLayer::colorAt(double t) const
{
    if (keys.empty()) return ImVec4(0, 0, 0, 1);
    if (t <= keys.front().time) return keys.front().color;
    if (t >= keys.back().time) return keys.back().color;
    for (size_t i = 0; i + 1 < keys.size(); i++)
    {
        const GradKey& a = keys[i];
        const GradKey& b = keys[i + 1];
        if (t >= a.time && t <= b.time)
        {
            double span = b.time - a.time;
            float w = span > 0 ? (float)((t - a.time) / span) : 1.f;
            return ImVec4(a.color.x + (b.color.x - a.color.x) * w,
                          a.color.y + (b.color.y - a.color.y) * w,
                          a.color.z + (b.color.z - a.color.z) * w,
                          a.color.w + (b.color.w - a.color.w) * w);
        }
    }
    return keys.back().color;
}

GradKey* GradientLayer::addKey(double t, ImVec4 c)
{
    GradKey k;
    k.id = sequence->newId();
    k.time = std::max(0.0, t);
    k.color = c;
    keys.push_back(k);
    sortKeys();
    return findKey(k.id);
}

GradKey* GradientLayer::findKey(uint64_t kid)
{
    for (auto& k : keys) if (k.id == kid) return &k;
    return nullptr;
}

void GradientLayer::removeKey(uint64_t kid)
{
    keys.erase(std::remove_if(keys.begin(), keys.end(),
               [kid](const GradKey& k) { return k.id == kid; }), keys.end());
    selectedKeys.erase(kid);
}

void GradientLayer::sortKeys()
{
    std::stable_sort(keys.begin(), keys.end(),
                     [](const GradKey& a, const GradKey& b) { return a.time < b.time; });
}

json GradientLayer::keysToJson() const
{
    json arr = json::array();
    for (auto& k : keys)
        arr.push_back({ { "id", k.id }, { "t", k.time },
                        { "c", { k.color.x, k.color.y, k.color.z, k.color.w } } });
    return arr;
}

void GradientLayer::keysFromJson(const json& arr)
{
    keys.clear();
    selectedKeys.clear();
    if (!arr.is_array()) return;
    for (auto& kj : arr)
    {
        GradKey k;
        k.id = kj.value("id", (uint64_t)0);
        k.time = kj.value("t", 0.0);
        if (kj.contains("c"))
            k.color = ImVec4(kj["c"][0], kj["c"][1], kj["c"][2], kj["c"][3]);
        if (k.id == 0) k.id = sequence->newId();
        sequence->nextId = std::max(sequence->nextId, k.id + 1);
        keys.push_back(k);
    }
    sortKeys();
}

json GradientLayer::save() const
{
    json j = Layer::save();
    j["keys"] = keysToJson();
    return j;
}

void GradientLayer::load(const json& j)
{
    Layer::load(j);
    if (j.contains("keys")) keysFromJson(j["keys"]);
}

void GradientLayer::inspectorGui()
{
    Container::inspectorGui();
    ImGui::TextDisabled("%d key(s), %d selected", (int)keys.size(), (int)selectedKeys.size());

    json preFrame = keysToJson();
    bool edited = false;
    bool anyActive = false;

    for (uint64_t kid : std::vector<uint64_t>(selectedKeys.begin(), selectedKeys.end()))
    {
        GradKey* k = findKey(kid);
        if (!k) continue;
        ImGui::PushID((int)(intptr_t)kid);
        ImGui::SeparatorText("Color Key");
        float t = (float)k->time;
        if (ImGui::DragFloat("Time", &t, 0.01f, 0.f, 1e9f, "%.3f s")) { k->time = t; edited = true; }
        anyActive |= ImGui::IsItemActive();
        float col[4] = { k->color.x, k->color.y, k->color.z, k->color.w };
        if (ImGui::ColorEdit4("Color", col)) { k->color = ImVec4(col[0], col[1], col[2], col[3]); edited = true; }
        anyActive |= ImGui::IsItemActive();
        ImGui::PopID();
    }

    if (edited) sortKeys();
    trackKeysEdit(this, preFrame, edited, anyActive, "Edit Color Keys");
}

// ================================================================ Sequence
Sequence::Sequence(const std::string& name)
    : Container(name)
{
    renamable = true;
    lengthP = addFloat("Length", 16.f, 1.f, 3600.f, "Sequence duration in seconds");
    lengthP->unit = "s";
    loopP   = addBool("Loop", true, "Loop playback at the end");
    speedP  = addFloat("Speed", 1.f, 0.05f, 4.f, "Playback speed multiplier");
}

Layer* Sequence::addLayer(Layer::LType t, const std::string& name, int index)
{
    std::unique_ptr<Layer> l;
    switch (t)
    {
    case Layer::LType::Clips:      l = std::make_unique<ClipLayer>(this, name); break;
    case Layer::LType::Automation: l = std::make_unique<AutomationLayer>(this, name); break;
    case Layer::LType::Gradient:   l = std::make_unique<GradientLayer>(this, name); break;
    }
    l->id = newId();
    Layer* raw = l.get();
    if (index < 0 || index > (int)layers.size()) index = (int)layers.size();
    layers.insert(layers.begin() + index, std::move(l));
    return raw;
}

Layer* Sequence::addLayerFromJson(const json& j, int index)
{
    Layer::LType t = (Layer::LType)j.value("ltype", 0);
    Layer* l = addLayer(t, j.value("niceName", "Layer"), index);
    uint64_t keepId = l->id;
    l->load(j);
    if (l->id == 0) l->id = keepId;
    nextId = std::max(nextId, l->id + 1);
    return l;
}

json Sequence::removeLayer(uint64_t lid)
{
    for (size_t i = 0; i < layers.size(); i++)
    {
        if (layers[i]->id == lid)
        {
            json j = layers[i]->save();
            j["_index"] = (int)i;
            layers.erase(layers.begin() + i);
            return j;
        }
    }
    return json();
}

Layer* Sequence::findLayer(uint64_t lid) const
{
    for (auto& l : layers) if (l->id == lid) return l.get();
    return nullptr;
}

int Sequence::layerIndex(const Layer* l) const
{
    for (size_t i = 0; i < layers.size(); i++)
        if (layers[i].get() == l) return (int)i;
    return -1;
}

void Sequence::moveLayer(int from, int to)
{
    if (from < 0 || from >= (int)layers.size() || to < 0 || to >= (int)layers.size() || from == to)
        return;
    auto l = std::move(layers[from]);
    layers.erase(layers.begin() + from);
    layers.insert(layers.begin() + to, std::move(l));
}

Clip* Sequence::findClip(uint64_t cid, ClipLayer** outLayer) const
{
    for (auto& l : layers)
    {
        if (auto* cl = dynamic_cast<ClipLayer*>(l.get()))
        {
            if (Clip* c = cl->findClip(cid))
            {
                if (outLayer) *outLayer = cl;
                return c;
            }
        }
    }
    return nullptr;
}

void Sequence::setTime(double t)
{
    currentTime = std::max(0.0, std::min((double)totalTime(), t));
}

void Sequence::update(double dt)
{
    if (!playing) return;
    currentTime += dt * speedP->floatValue();
    double len = totalTime();
    if (currentTime >= len)
    {
        if (loopP->boolValue()) currentTime = std::fmod(currentTime, len);
        else { currentTime = len; playing = false; }
    }
}

void Sequence::inspectorGui()
{
    Container::inspectorGui();
    ImGui::Separator();
    ImGui::Text("Time: %s / %s", formatTime(currentTime).c_str(), formatTime(totalTime()).c_str());
    ImGui::TextDisabled("%d layer(s)", (int)layers.size());
}

json Sequence::save() const
{
    json j = Container::save();
    j["nextId"] = nextId;
    j["viewStart"] = viewStart;
    j["pps"] = pixelsPerSecond;
    json arr = json::array();
    for (auto& l : layers) arr.push_back(l->save());
    j["layers"] = arr;
    return j;
}

void Sequence::load(const json& j)
{
    layers.clear();
    Container::load(j);
    if (j.contains("viewStart")) viewStart = j["viewStart"].get<double>();
    if (j.contains("pps"))       pixelsPerSecond = j["pps"].get<double>();
    if (j.contains("layers"))
        for (auto& lj : j["layers"]) addLayerFromJson(lj);
    if (j.contains("nextId")) nextId = std::max(nextId, j["nextId"].get<uint64_t>());
    currentTime = 0;
    playing = false;
}

// ================================================================ Media
MediaItem::MediaItem(Container* parent, const std::string& name, int k)
    : Container(name, parent), kind(k)
{
    renamable = true;
    colorP = addColor("Color", k == 1 ? ImVec4(0.25f, 0.65f, 0.45f, 1.f)
                                      : ImVec4(0.30f, 0.55f, 0.85f, 1.f), "Clip color when dropped");
    durP = addFloat("Default Length", 4.f, 0.1f, 600.f, "Length used when dropped (blocks)");
    if (kind == 1)
        fileP = addString("File", "", "Audio file path");
}

MediaPayload MediaItem::makePayload() const
{
    MediaPayload p;
    snprintf(p.name, sizeof(p.name), "%s", niceName.c_str());
    if (fileP) snprintf(p.file, sizeof(p.file), "%s", fileP->stringValue().c_str());
    ImVec4 c = colorP->color();
    p.color[0] = c.x; p.color[1] = c.y; p.color[2] = c.z; p.color[3] = c.w;
    p.kind = kind;
    p.duration = durP->floatValue();
    if (kind == 1 && fileP && !fileP->stringValue().empty())
    {
        if (auto asset = AudioCache::get().load(fileP->stringValue()))
            p.duration = (float)asset->buffer.duration();
    }
    return p;
}

json MediaItem::save() const
{
    json j = Container::save();
    j["kind"] = kind;
    return j;
}

void MediaItem::load(const json& j) { Container::load(j); }

MediaPool::MediaPool() : Container("Media Pool") {}

MediaItem* MediaPool::addMedia(const std::string& name, int kind)
{
    items.push_back(std::make_unique<MediaItem>(this, name, kind));
    return items.back().get();
}

void MediaPool::removeMedia(MediaItem* item)
{
    items.erase(std::remove_if(items.begin(), items.end(),
                [item](const std::unique_ptr<MediaItem>& m) { return m.get() == item; }),
                items.end());
}

json MediaPool::save() const
{
    json j = Container::save();
    json arr = json::array();
    for (auto& m : items) arr.push_back(m->save());
    j["items"] = arr;
    return j;
}

void MediaPool::load(const json& j)
{
    Container::load(j);
    items.clear();
    if (j.contains("items"))
    {
        for (auto& mj : j["items"])
        {
            MediaItem* m = addMedia(mj.value("niceName", "Media"), mj.value("kind", 0));
            m->load(mj);
        }
    }
}

} // namespace organic
