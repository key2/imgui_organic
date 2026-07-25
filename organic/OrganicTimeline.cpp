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
    enabledP = addBool("Enabled", true, "Disabled clips are dimmed and muted");
    startP  = addFloatUnbounded("Start", 0.f, "Start time in seconds");
    startP->unit = "s"; startP->dragSpeed = 0.05f;
    lengthP = addFloatUnbounded("Length", 4.f, "Duration in seconds");
    lengthP->unit = "s"; lengthP->dragSpeed = 0.05f;
    colorP  = addColor("Color", ImVec4(0.30f, 0.55f, 0.85f, 1.f), "Clip color");

    if (ctype == CType::Audio)
    {
        fileP   = addString("File", "", "Audio file path (.wav)");
        gainP   = addFloat("Gain", 1.f, 0.f, 4.f, "Playback / waveform gain");
        offsetP = addFloatUnbounded("Offset", 0.f, "Offset into the media, in seconds");
        offsetP->unit = "s"; offsetP->dragSpeed = 0.05f;
        fadeInP  = addFloat("Fade In", 0.f, 0.f, 30.f, "Fade-in duration (seconds)");
        fadeInP->unit = "s";
        fadeOutP = addFloat("Fade Out", 0.f, 0.f, 30.f, "Fade-out duration (seconds)");
        fadeOutP->unit = "s";
        loopMediaP = addBool("Loop Media", false, "Tile the audio file to fill the clip");
        colorP->setValue(ImVec4(0.25f, 0.65f, 0.45f, 1.f));
        colorP->defaultValue = colorP->value;
    }
}

float Clip::fadeGainAt(double localT) const
{
    float g = 1.f;
    if (fadeInP)
    {
        float fi = fadeInP->floatValue();
        if (fi > 0.001f && localT < fi) g *= (float)(localT / fi);
    }
    if (fadeOutP)
    {
        float fo = fadeOutP->floatValue();
        double len = length();
        if (fo > 0.001f && localT > len - fo) g *= (float)((len - localT) / fo);
    }
    return std::max(0.f, std::min(1.f, g));
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
    enabledP = addBool("Enabled", true, "Disabled layers are dimmed, muted and do not fire");
    colorP = addColor("Color", ImVec4(0.55f, 0.55f, 0.6f, 1.f), "Layer tint");
    switch (t)
    {
    case LType::Clips:      uiHeight = 64.f; break;
    case LType::Automation: uiHeight = 90.f; break;
    case LType::Gradient:   uiHeight = 34.f; break;
    case LType::Triggers:   uiHeight = 48.f; break;
    }
}

const char* Layer::ltypeName(LType t)
{
    switch (t)
    {
    case LType::Clips:      return "Clips";
    case LType::Automation: return "Automation";
    case LType::Gradient:   return "Gradient";
    case LType::Triggers:   return "Triggers";
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
    rangeRemapP = addEnum("Range Remap", { "Absolute", "Proportional" }, 0,
                          "When the range changes: keep key values or rescale them proportionally");
    recArmP = addBool("Record Arm", false, "Record the source parameter into the curve while playing");
    recSourceP = addString("Record Source", "", "Address of the parameter to record (e.g. /sequences/demo/energy/rangeMax)");
    recSimplifyP = addEnum("Record Simplify", { "Points", "Linear (RDP)", "Bezier Fit" }, 2,
                           "How recorded points are turned into keys");
    recTolP = addFloat("Record Tolerance", 0.1f, 0.f, 1.f, "Simplification tolerance");
    outputP = addFloatUnbounded("Output", 0.f, "Live curve output at the playhead (read-only)");
    outputP->readOnly = true;
    colorP->setValue(ImVec4(0.9f, 0.57f, 0.18f, 1.f));
    colorP->defaultValue = colorP->value;
    prevMin = rangeMinP->floatValue();
    prevMax = rangeMaxP->floatValue();
}

void AutomationLayer::onParamChanged(Parameter* p)
{
    if (p == rangeMinP || p == rangeMaxP)
    {
        float nmn = rangeMinP->floatValue(), nmx = rangeMaxP->floatValue();
        if (rangeRemapP->intValue() == 1 && prevMax != prevMin && nmx != nmn)
        {
            for (auto& k : keys)
            {
                float nv = (k.value - prevMin) / (prevMax - prevMin);
                k.value = nmn + nv * (nmx - nmn);
            }
        }
        prevMin = nmn;
        prevMax = nmx;
    }
}

AutoKey* AutomationLayer::insertKeyAt(double t)
{
    if (keys.size() < 2 || t <= keys.front().time || t >= keys.back().time)
        return addKey(t, valueAt(t));

    // find segment
    int seg = -1;
    for (size_t i = 0; i + 1 < keys.size(); i++)
        if (t >= keys[i].time && t <= keys[i + 1].time) { seg = (int)i; break; }
    if (seg < 0) return addKey(t, valueAt(t));

    AutoKey a = keys[seg];
    AutoKey b = keys[seg + 1];
    float mn = rangeMinP->floatValue(), mx = rangeMaxP->floatValue();
    float range = (mx - mn) == 0 ? 1.f : (mx - mn);

    if (a.easing == EasingType::Bezier)
    {
        // split preserving shape, in (time, normValue) space
        double segDur = std::max(1e-6, b.time - a.time);
        float na = (a.value - mn) / range, nb = (b.value - mn) / range;
        ImVec2 p0((float)a.time, na);
        ImVec2 c1((float)(a.time + a.ep.a1.x * segDur), na + a.ep.a1.y);
        ImVec2 c2((float)(b.time + a.ep.a2.x * segDur), nb + a.ep.a2.y);
        ImVec2 p3((float)b.time, nb);

        // solve bezier parameter for x = t (x is monotonic since anchors are clamped)
        float lo = 0.f, hi = 1.f, bt = 0.5f;
        for (int i = 0; i < 28; i++)
        {
            bt = 0.5f * (lo + hi);
            float it = 1.f - bt;
            float x = it * it * it * p0.x + 3 * it * it * bt * c1.x + 3 * it * bt * bt * c2.x + bt * bt * bt * p3.x;
            if (x < t) lo = bt; else hi = bt;
        }
        ImVec2 l1, l2, mid, r1, r2;
        splitCubic(p0, c1, c2, p3, bt, l1, l2, mid, r1, r2);

        AutoKey nk;
        nk.id = sequence->newId();
        nk.time = mid.x;
        nk.value = mn + mid.y * range;
        nk.easing = EasingType::Bezier;
        double d1 = std::max(1e-6, (double)mid.x - a.time);
        double d2 = std::max(1e-6, (double)b.time - mid.x);
        AutoKey& ka = keys[seg];
        ka.ep.a1 = ImVec2((float)((l1.x - a.time) / d1), l1.y - na);
        ka.ep.a2 = ImVec2((float)((l2.x - mid.x) / d1), l2.y - mid.y);
        nk.ep.a1 = ImVec2((float)((r1.x - mid.x) / d2), r1.y - mid.y);
        nk.ep.a2 = ImVec2((float)((r2.x - b.time) / d2), r2.y - nb);
        keys.insert(keys.begin() + seg + 1, nk);
        sortKeys();
        return findKey(nk.id);
    }

    // other easings: keep the segment type, value from the curve
    AutoKey nk;
    nk.id = sequence->newId();
    nk.time = t;
    nk.value = valueAt(t);
    nk.easing = a.easing;
    nk.ep = a.ep;
    keys.insert(keys.begin() + seg + 1, nk);
    sortKeys();
    return findKey(nk.id);
}

void AutomationLayer::updateRecording(double t)
{
    if (!recArmP->boolValue())
    {
        if (recording) stopRecordingAndApply();
        return;
    }
    Parameter* src = resolveParamAddress(recSourceP->stringValue());
    if (!src) return;
    if (!recording)
    {
        recording = true;
        recPoints.clear();
        OLOG(niceName, "Recording from " << src->controlAddress());
    }
    if (!recPoints.empty() && t < recPoints.back().first - 1e-6)
    {
        stopRecordingAndApply(); // transport wrapped
        return;
    }
    float mn = rangeMinP->floatValue(), mx = rangeMaxP->floatValue();
    float v = src->floatValue();
    if (src->hasRange && src->maxF > src->minF)
    {
        float nv = (v - src->minF) / (src->maxF - src->minF);
        v = mn + nv * (mx - mn);
    }
    v = std::max(std::min(mn, mx), std::min(std::max(mn, mx), v));
    if (recPoints.empty() || t - recPoints.back().first >= 0.004)
        recPoints.push_back({ t, v });
}

void AutomationLayer::stopRecordingAndApply()
{
    recording = false;
    recArmP->setValue(false, false);
    if (recPoints.size() < 2) { recPoints.clear(); return; }

    json pre = keysToJson();
    double t0 = recPoints.front().first;
    double t1 = recPoints.back().first;
    keys.erase(std::remove_if(keys.begin(), keys.end(),
               [t0, t1](const AutoKey& k) { return k.time >= t0 && k.time <= t1; }), keys.end());

    float mn = rangeMinP->floatValue(), mx = rangeMaxP->floatValue();
    float range = (mx - mn) == 0 ? 1.f : (mx - mn);
    double dur = std::max(1e-6, t1 - t0);

    // normalized point cloud (x: 0..1 over recorded span, y: 0..1 over range)
    std::vector<ImVec2> pts;
    pts.reserve(recPoints.size());
    for (auto& rp : recPoints)
        pts.push_back(ImVec2((float)((rp.first - t0) / dur), (rp.second - mn) / range));

    int method = recSimplifyP->intValue();
    float tol = recTolP->floatValue();

    if (method == 0) // raw points, decimated
    {
        int step = std::max(1, (int)(recPoints.size() / std::max(2.0, dur * 20.0)));
        for (size_t i = 0; i < recPoints.size(); i += step)
            addKey(recPoints[i].first, recPoints[i].second, EasingType::Linear);
        addKey(t1, recPoints.back().second, EasingType::Linear);
    }
    else if (method == 1) // RDP
    {
        std::vector<int> keep;
        simplifyRDP(pts, std::max(0.0015f, tol * 0.06f), keep);
        for (int idx : keep)
            addKey(recPoints[idx].first, recPoints[idx].second, EasingType::Linear);
    }
    else // bezier fit
    {
        std::vector<FittedCubic> cubics;
        fitCubicBeziers(pts, std::max(0.002f, tol * 0.08f), cubics);
        for (size_t i = 0; i < cubics.size(); i++)
        {
            const FittedCubic& b = cubics[i];
            double kt = t0 + b.p0.x * dur;
            AutoKey* k = addKey(kt, mn + b.p0.y * range, EasingType::Bezier);
            float dx = std::max(1e-6f, b.p3.x - b.p0.x);
            k->ep.a1 = ImVec2(std::max(0.f, std::min(1.f, (b.c1.x - b.p0.x) / dx)), b.c1.y - b.p0.y);
            k->ep.a2 = ImVec2(std::max(-1.f, std::min(0.f, (b.c2.x - b.p3.x) / dx)), b.c2.y - b.p3.y);
        }
        addKey(t1, mn + cubics.back().p3.y * range, EasingType::Linear);
    }
    sortKeys();
    recPoints.clear();

    json post = keysToJson();
    AutomationLayer* self = this;
    UndoManager::get().pushDone("Record Automation",
        [self, post] { self->keysFromJson(post); },
        [self, pre]  { self->keysFromJson(pre); },
        { this });
    OLOG(niceName, "Recorded " << keys.size() << " key(s)");
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
    if (ImGui::Button("Pick record source..."))
        ImGui::OpenPopup("##recsrc_pick");
    std::string addr = recSourceP->stringValue();
    if (ParamPickerPopup("##recsrc_pick", addr))
        recSourceP->setUndoable(addr);
    if (recording)
        ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f), "RECORDING (%d points)", (int)recPoints.size());
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
            if (a.hold) return a.color; // NONE interpolation
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
        arr.push_back({ { "id", k.id }, { "t", k.time }, { "hold", k.hold },
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
        k.hold = kj.value("hold", false);
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
        bool hold = k->hold;
        if (ImGui::Checkbox("Hold (no interpolation)", &hold)) { k->hold = hold; edited = true; }
        ImGui::PopID();
    }

    if (edited) sortKeys();
    trackKeysEdit(this, preFrame, edited, anyActive, "Edit Color Keys");
}

// ================================================================ TriggerLayer
TriggerLayer::TriggerLayer(Sequence* s, const std::string& name)
    : Layer(s, LType::Triggers, name)
{
    colorP->setValue(ImVec4(0.85f, 0.35f, 0.35f, 1.f));
    colorP->defaultValue = colorP->value;
}

TimeTrigger* TriggerLayer::addTrigger(double t, const std::string& n)
{
    TimeTrigger tt;
    tt.id = sequence->newId();
    tt.time = std::max(0.0, t);
    tt.name = n;
    triggers.push_back(tt);
    sortTriggers();
    return findTrigger(tt.id);
}

TimeTrigger* TriggerLayer::findTrigger(uint64_t tid)
{
    for (auto& t : triggers) if (t.id == tid) return &t;
    return nullptr;
}

void TriggerLayer::removeTrigger(uint64_t tid)
{
    triggers.erase(std::remove_if(triggers.begin(), triggers.end(),
                   [tid](const TimeTrigger& t) { return t.id == tid; }), triggers.end());
    selectedKeys.erase(tid);
}

void TriggerLayer::sortTriggers()
{
    std::stable_sort(triggers.begin(), triggers.end(),
                     [](const TimeTrigger& a, const TimeTrigger& b) { return a.time < b.time; });
}

void TriggerLayer::resetFiredStates(double playheadTime)
{
    for (auto& t : triggers) t.fired = t.time <= playheadTime;
}

void TriggerLayer::fireCrossings(double t0, double t1, double now)
{
    if (!enabledP->boolValue()) return;
    for (auto& t : triggers)
    {
        bool crossed = t0 <= t1 ? (t.time > t0 && t.time <= t1)
                                : (t.time < t0 && t.time >= t1); // reverse direction
        if (crossed && !t.fired)
        {
            t.fired = true;
            t.flashTime = now;
            OLOG(niceName, "Trigger fired: " << t.name << " @ " << formatTime(t.time));
            if (onTriggered) onTriggered(*this, t);
        }
    }
}

json TriggerLayer::keysToJson() const
{
    json arr = json::array();
    for (auto& t : triggers)
        arr.push_back({ { "id", t.id }, { "t", t.time }, { "y", t.flagY }, { "name", t.name } });
    return arr;
}

void TriggerLayer::keysFromJson(const json& arr)
{
    triggers.clear();
    selectedKeys.clear();
    if (!arr.is_array()) return;
    for (auto& tj : arr)
    {
        TimeTrigger t;
        t.id = tj.value("id", (uint64_t)0);
        t.time = tj.value("t", 0.0);
        t.flagY = tj.value("y", 0.3f);
        t.name = tj.value("name", "Trigger");
        if (t.id == 0) t.id = sequence->newId();
        sequence->nextId = std::max(sequence->nextId, t.id + 1);
        triggers.push_back(t);
    }
    sortTriggers();
}

json TriggerLayer::save() const
{
    json j = Layer::save();
    j["keys"] = keysToJson();
    return j;
}

void TriggerLayer::load(const json& j)
{
    Layer::load(j);
    if (j.contains("keys")) keysFromJson(j["keys"]);
}

void TriggerLayer::inspectorGui()
{
    Container::inspectorGui();
    ImGui::TextDisabled("%d trigger(s), %d selected", (int)triggers.size(), (int)selectedKeys.size());
    ImGui::TextDisabled("Hook TriggerLayer::onTriggered to react from code.");

    json preFrame = keysToJson();
    bool edited = false, anyActive = false;
    for (uint64_t tid : std::vector<uint64_t>(selectedKeys.begin(), selectedKeys.end()))
    {
        TimeTrigger* t = findTrigger(tid);
        if (!t) continue;
        ImGui::PushID((int)(intptr_t)tid);
        ImGui::SeparatorText("Trigger");
        char buf[128];
        snprintf(buf, sizeof(buf), "%s", t->name.c_str());
        if (ImGui::InputText("Name", buf, sizeof(buf))) { t->name = buf; edited = true; }
        anyActive |= ImGui::IsItemActive();
        float tt = (float)t->time;
        if (ImGui::DragFloat("Time", &tt, 0.01f, 0.f, 1e9f, "%.3f s")) { t->time = tt; edited = true; }
        anyActive |= ImGui::IsItemActive();
        ImGui::PopID();
    }
    if (edited) sortTriggers();
    trackKeysEdit(this, preFrame, edited, anyActive, "Edit Triggers");
}

// ================================================================ Sequence
Sequence::Sequence(const std::string& name)
    : Container(name)
{
    renamable = true;
    lengthP = addFloat("Length", 16.f, 1.f, 3600.f, "Sequence duration in seconds");
    lengthP->unit = "s";
    playModeP = addEnum("Play Mode", { "Once", "Loop", "Ping-Pong" }, 1, "What happens at the end (or loop range end)");
    speedP  = addFloat("Speed", 1.f, 0.05f, 4.f, "Playback speed multiplier");
    bpmP    = addFloat("BPM", 120.f, 20.f, 300.f, "Tempo for the musical grid (Beats ruler mode)");
    beatsPerBarP = addInt("Beats Per Bar", 4, 1, 12, "Bar length for the musical grid");
    lengthModeP  = addEnum("On Length Change", { "Keep Times", "Stretch", "Stick To End" }, 0,
                           "How existing content reacts when Length is edited");
    prevLength = lengthP->floatValue();
}

Layer* Sequence::addLayer(Layer::LType t, const std::string& name, int index)
{
    std::unique_ptr<Layer> l;
    switch (t)
    {
    case Layer::LType::Clips:      l = std::make_unique<ClipLayer>(this, name); break;
    case Layer::LType::Automation: l = std::make_unique<AutomationLayer>(this, name); break;
    case Layer::LType::Gradient:   l = std::make_unique<GradientLayer>(this, name); break;
    case Layer::LType::Triggers:   l = std::make_unique<TriggerLayer>(this, name); break;
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

void Sequence::stop()
{
    playing = false;
    direction = 1;
    setTime(0);
    for (auto& l : layers)
        if (auto* al = dynamic_cast<AutomationLayer*>(l.get()))
            if (al->recording) al->stopRecordingAndApply();
}

void Sequence::setTime(double t)
{
    currentTime = std::max(0.0, std::min((double)totalTime(), t));
    for (auto& l : layers)
        if (auto* tl = dynamic_cast<TriggerLayer*>(l.get()))
            tl->resetFiredStates(currentTime);
}

void Sequence::update(double dt)
{
    // live outputs (also while paused/scrubbing)
    for (auto& l : layers)
        if (auto* al = dynamic_cast<AutomationLayer*>(l.get()))
            al->outputP->setValue(al->valueAt(currentTime), false);

    if (!playing)
    {
        for (auto& l : layers)
            if (auto* al = dynamic_cast<AutomationLayer*>(l.get()))
                if (al->recording) al->stopRecordingAndApply();
        return;
    }

    double prev = currentTime;
    currentTime += dt * speedP->floatValue() * direction;

    double lo = 0, hi = totalTime();
    if (loopIn >= 0 && loopOut > loopIn)
    {
        lo = loopIn;
        hi = std::min(loopOut, (double)totalTime());
    }
    int mode = playModeP->intValue();
    bool wrapped = false;

    if (direction > 0 && currentTime >= hi)
    {
        switch (mode)
        {
        case 0: currentTime = hi; playing = false; break;                 // once
        case 1:                                                            // loop
            currentTime = lo + std::fmod(currentTime - lo, std::max(0.001, hi - lo));
            wrapped = true;
            break;
        case 2:                                                            // ping-pong
            currentTime = hi - (currentTime - hi);
            direction = -1;
            break;
        }
    }
    else if (direction < 0 && currentTime <= lo)
    {
        currentTime = lo + (lo - currentTime);
        direction = 1;
    }

    // trigger crossings
    double now = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    for (auto& l : layers)
    {
        auto* tl = dynamic_cast<TriggerLayer*>(l.get());
        if (!tl) continue;
        if (!wrapped) tl->fireCrossings(prev, currentTime, now);
        else
        {
            tl->fireCrossings(prev, hi, now);
            tl->resetFiredStates(lo);
            tl->fireCrossings(lo, currentTime, now);
        }
    }

    // automation recording
    for (auto& l : layers)
        if (auto* al = dynamic_cast<AutomationLayer*>(l.get()))
            al->updateRecording(currentTime);
}

// ---------------------------------------------------------------- cues
TimeCue* Sequence::addCue(double t, const std::string& name)
{
    TimeCue c;
    c.id = newId();
    c.time = std::max(0.0, t);
    c.name = name;
    cues.push_back(c);
    sortCues();
    return findCue(c.id);
}

TimeCue* Sequence::findCue(uint64_t id)
{
    for (auto& c : cues) if (c.id == id) return &c;
    return nullptr;
}

void Sequence::removeCue(uint64_t id)
{
    cues.erase(std::remove_if(cues.begin(), cues.end(),
               [id](const TimeCue& c) { return c.id == id; }), cues.end());
}

void Sequence::sortCues()
{
    std::stable_sort(cues.begin(), cues.end(),
                     [](const TimeCue& a, const TimeCue& b) { return a.time < b.time; });
}

double Sequence::nextCueTime(double t, int dir) const
{
    double best = -1;
    for (auto& c : cues)
    {
        if (dir > 0 && c.time > t + 1e-4 && (best < 0 || c.time < best)) best = c.time;
        if (dir < 0 && c.time < t - 1e-4 && (best < 0 || c.time > best)) best = c.time;
    }
    return best;
}

json Sequence::cuesToJson() const
{
    json arr = json::array();
    for (auto& c : cues)
        arr.push_back({ { "id", c.id }, { "t", c.time }, { "name", c.name } });
    return arr;
}

void Sequence::cuesFromJson(const json& arr)
{
    cues.clear();
    if (!arr.is_array()) return;
    for (auto& cj : arr)
    {
        TimeCue c;
        c.id = cj.value("id", (uint64_t)0);
        c.time = cj.value("t", 0.0);
        c.name = cj.value("name", "Cue");
        if (c.id == 0) c.id = nextId++;
        nextId = std::max(nextId, c.id + 1);
        cues.push_back(c);
    }
    sortCues();
}

// ---------------------------------------------------------------- ripple edits
json Sequence::contentToJson() const
{
    json j;
    json arr = json::array();
    for (auto& l : layers) arr.push_back(l->save());
    j["layers"] = arr;
    j["cues"] = cuesToJson();
    j["loopIn"] = loopIn;
    j["loopOut"] = loopOut;
    j["length"] = lengthP->floatValue();
    return j;
}

void Sequence::contentFromJson(const json& j)
{
    layers.clear();
    if (j.contains("layers"))
        for (auto& lj : j["layers"]) addLayerFromJson(lj);
    if (j.contains("cues")) cuesFromJson(j["cues"]);
    loopIn = j.value("loopIn", -1.0);
    loopOut = j.value("loopOut", -1.0);
    if (j.contains("length"))
    {
        prevLength = j["length"].get<float>(); // avoid length-mode transform
        lengthP->setValue(j["length"].get<float>(), false);
    }
}

void Sequence::insertTime(double at, double dt)
{
    if (dt <= 0) return;
    for (auto& l : layers)
    {
        if (auto* cl = dynamic_cast<ClipLayer*>(l.get()))
        {
            for (auto& c : cl->clips)
                if (c->start() >= at) c->startP->setValue((float)(c->start() + dt), false);
            cl->sortClips();
        }
        else if (auto* al = dynamic_cast<AutomationLayer*>(l.get()))
        {
            for (auto& k : al->keys) if (k.time >= at) k.time += dt;
            al->sortKeys();
        }
        else if (auto* gl = dynamic_cast<GradientLayer*>(l.get()))
        {
            for (auto& k : gl->keys) if (k.time >= at) k.time += dt;
            gl->sortKeys();
        }
        else if (auto* tl = dynamic_cast<TriggerLayer*>(l.get()))
        {
            for (auto& t : tl->triggers) if (t.time >= at) t.time += dt;
            tl->sortTriggers();
        }
    }
    for (auto& c : cues) if (c.time >= at) c.time += dt;
    sortCues();
    if (loopIn >= at) loopIn += dt;
    if (loopOut >= at) loopOut += dt;
    prevLength = lengthP->floatValue() + (float)dt; // bypass length-mode transform
    lengthP->setValue((float)(lengthP->floatValue() + dt));
}

void Sequence::removeTimespan(double t0, double t1)
{
    if (t1 <= t0) return;
    double d = t1 - t0;
    for (auto& l : layers)
    {
        if (auto* cl = dynamic_cast<ClipLayer*>(l.get()))
        {
            for (int i = (int)cl->clips.size() - 1; i >= 0; i--)
            {
                Clip* c = cl->clips[i].get();
                double s = c->start(), e = c->end();
                if (s >= t0 && e <= t1) { cl->clips.erase(cl->clips.begin() + i); continue; }
                if (s >= t1) c->startP->setValue((float)(s - d), false);
                else if (s < t0 && e > t1)      // straddles the whole span
                    c->lengthP->setValue((float)(c->length() - d), false);
                else if (s < t0 && e > t0)      // tail inside the span
                    c->lengthP->setValue((float)(t0 - s), false);
                else if (s >= t0 && e > t1)     // head inside the span
                {
                    double cut = t1 - s;
                    c->startP->setValue((float)t0, false);
                    c->lengthP->setValue((float)(c->length() - cut), false);
                    if (c->ctype == Clip::CType::Audio)
                        c->offsetP->setValue((float)(c->offsetP->floatValue() + cut), false);
                }
            }
            cl->sortClips();
        }
        else if (auto* al = dynamic_cast<AutomationLayer*>(l.get()))
        {
            al->keys.erase(std::remove_if(al->keys.begin(), al->keys.end(),
                          [t0, t1](const AutoKey& k) { return k.time >= t0 && k.time <= t1; }),
                          al->keys.end());
            for (auto& k : al->keys) if (k.time > t1) k.time -= d;
            al->sortKeys();
        }
        else if (auto* gl = dynamic_cast<GradientLayer*>(l.get()))
        {
            gl->keys.erase(std::remove_if(gl->keys.begin(), gl->keys.end(),
                          [t0, t1](const GradKey& k) { return k.time >= t0 && k.time <= t1; }),
                          gl->keys.end());
            for (auto& k : gl->keys) if (k.time > t1) k.time -= d;
            gl->sortKeys();
        }
        else if (auto* tl = dynamic_cast<TriggerLayer*>(l.get()))
        {
            tl->triggers.erase(std::remove_if(tl->triggers.begin(), tl->triggers.end(),
                              [t0, t1](const TimeTrigger& t) { return t.time >= t0 && t.time <= t1; }),
                              tl->triggers.end());
            for (auto& t : tl->triggers) if (t.time > t1) t.time -= d;
            tl->sortTriggers();
        }
    }
    cues.erase(std::remove_if(cues.begin(), cues.end(),
               [t0, t1](const TimeCue& c) { return c.time >= t0 && c.time <= t1; }), cues.end());
    for (auto& c : cues) if (c.time > t1) c.time -= d;
    sortCues();
    if (loopIn > t1) loopIn -= d; else if (loopIn > t0) loopIn = t0;
    if (loopOut > t1) loopOut -= d; else if (loopOut > t0) loopOut = t0;
    if (loopOut - loopIn < 0.01) { loopIn = loopOut = -1; }
    prevLength = std::max(1.f, lengthP->floatValue() - (float)d);
    lengthP->setValue(std::max(1.f, lengthP->floatValue() - (float)d));
    setTime(std::min(currentTime, (double)totalTime()));
}

void Sequence::onParamChanged(Parameter* p)
{
    if (p == lengthP)
    {
        float newLen = lengthP->floatValue();
        float oldLen = (float)prevLength;
        int mode = lengthModeP ? lengthModeP->intValue() : 0;
        if (mode == 1 && oldLen > 0.01f && newLen != oldLen) // stretch
        {
            double f = newLen / oldLen;
            for (auto& l : layers)
            {
                if (auto* cl = dynamic_cast<ClipLayer*>(l.get()))
                    for (auto& c : cl->clips)
                    {
                        c->startP->setValue((float)(c->start() * f), false);
                        c->lengthP->setValue((float)(c->length() * f), false);
                    }
                else if (auto* al = dynamic_cast<AutomationLayer*>(l.get()))
                    for (auto& k : al->keys) k.time *= f;
                else if (auto* gl = dynamic_cast<GradientLayer*>(l.get()))
                    for (auto& k : gl->keys) k.time *= f;
                else if (auto* tl = dynamic_cast<TriggerLayer*>(l.get()))
                    for (auto& t : tl->triggers) t.time *= f;
            }
            for (auto& c : cues) c.time *= f;
            if (loopIn >= 0) { loopIn *= f; loopOut *= f; }
        }
        else if (mode == 2 && newLen != oldLen) // stick to end
        {
            double delta = newLen - oldLen;
            for (auto& l : layers)
            {
                if (auto* cl = dynamic_cast<ClipLayer*>(l.get()))
                    for (auto& c : cl->clips)
                        c->startP->setValue((float)std::max(0.0, c->start() + delta), false);
                else if (auto* al = dynamic_cast<AutomationLayer*>(l.get()))
                    for (auto& k : al->keys) k.time = std::max(0.0, k.time + delta);
                else if (auto* gl = dynamic_cast<GradientLayer*>(l.get()))
                    for (auto& k : gl->keys) k.time = std::max(0.0, k.time + delta);
                else if (auto* tl = dynamic_cast<TriggerLayer*>(l.get()))
                    for (auto& t : tl->triggers) t.time = std::max(0.0, t.time + delta);
            }
            for (auto& c : cues) c.time = std::max(0.0, c.time + delta);
        }
        prevLength = newLen;
    }
}

void Sequence::inspectorGui()
{
    Container::inspectorGui();
    ImGui::Separator();
    ImGui::Text("Time: %s / %s", formatTime(currentTime).c_str(), formatTime(totalTime()).c_str());
    ImGui::TextDisabled("%d layer(s), %d cue(s)", (int)layers.size(), (int)cues.size());
    if (loopIn >= 0)
        ImGui::TextDisabled("Loop range: %s -> %s", formatTime(loopIn).c_str(), formatTime(loopOut).c_str());
}

json Sequence::save() const
{
    json j = Container::save();
    j["nextId"] = nextId;
    j["viewStart"] = viewStart;
    j["pps"] = pixelsPerSecond;
    j["cues"] = cuesToJson();
    j["loopIn"] = loopIn;
    j["loopOut"] = loopOut;
    json arr = json::array();
    for (auto& l : layers) arr.push_back(l->save());
    j["layers"] = arr;
    return j;
}

void Sequence::load(const json& j)
{
    layers.clear();
    cues.clear();
    Container::load(j);
    prevLength = lengthP->floatValue();
    if (j.contains("viewStart")) viewStart = j["viewStart"].get<double>();
    if (j.contains("pps"))       pixelsPerSecond = j["pps"].get<double>();
    if (j.contains("layers"))
        for (auto& lj : j["layers"]) addLayerFromJson(lj);
    if (j.contains("cues")) cuesFromJson(j["cues"]);
    loopIn = j.value("loopIn", -1.0);
    loopOut = j.value("loopOut", -1.0);
    if (j.contains("nextId")) nextId = std::max(nextId, j["nextId"].get<uint64_t>());
    currentTime = 0;
    playing = false;
    direction = 1;
}

// ================================================================ SequenceManager
SequenceManager::SequenceManager() : Container("Sequences")
{
}

Sequence* SequenceManager::current() const
{
    if (sequences.empty()) return nullptr;
    int i = std::max(0, std::min((int)sequences.size() - 1, currentIndex));
    return sequences[i].get();
}

Sequence* SequenceManager::addSequence(const std::string& name, int index)
{
    auto s = std::make_unique<Sequence>(name);
    Sequence* raw = s.get();
    raw->managerUid = nextUid++;
    addChild(raw);
    if (index < 0 || index > (int)sequences.size()) index = (int)sequences.size();
    sequences.insert(sequences.begin() + index, std::move(s));
    return raw;
}

Sequence* SequenceManager::addSequenceFromJson(const json& j, int index)
{
    if (index < 0) index = j.value("_index", -1);
    Sequence* s = addSequence(j.value("niceName", "Sequence"), index);
    uint64_t keep = s->managerUid;
    s->load(j);
    s->managerUid = j.value("managerUid", keep);
    nextUid = std::max(nextUid, s->managerUid + 1);
    return s;
}

Sequence* SequenceManager::findByUid(uint64_t uid) const
{
    for (auto& s : sequences) if (s->managerUid == uid) return s.get();
    return nullptr;
}

json SequenceManager::removeSequence(Sequence* s)
{
    for (size_t i = 0; i < sequences.size(); i++)
    {
        if (sequences[i].get() == s)
        {
            json j = s->save();
            j["_index"] = (int)i;
            j["managerUid"] = s->managerUid;
            removeChild(s);
            sequences.erase(sequences.begin() + i);
            if (currentIndex >= (int)sequences.size())
                currentIndex = std::max(0, (int)sequences.size() - 1);
            return j;
        }
    }
    return json();
}

int SequenceManager::indexOf(const Sequence* s) const
{
    for (size_t i = 0; i < sequences.size(); i++)
        if (sequences[i].get() == s) return (int)i;
    return -1;
}

void SequenceManager::update(double dt)
{
    for (auto& s : sequences) s->update(dt);
}

json SequenceManager::save() const
{
    json j = Container::save();
    j["currentIndex"] = currentIndex;
    j["nextUid"] = nextUid;
    json arr = json::array();
    for (auto& s : sequences)
    {
        json sj = s->save();
        sj["managerUid"] = s->managerUid;
        arr.push_back(sj);
    }
    j["sequences"] = arr;
    return j;
}

void SequenceManager::load(const json& j)
{
    Container::load(j);
    sequences.clear();
    if (j.contains("sequences"))
        for (auto& sj : j["sequences"]) addSequenceFromJson(sj);
    currentIndex = j.value("currentIndex", 0);
    if (j.contains("nextUid")) nextUid = std::max(nextUid, j["nextUid"].get<uint64_t>());
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
