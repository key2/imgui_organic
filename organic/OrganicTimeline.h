// OrganicTimeline.h - timeline data model.
// Mirrors benkuper's juce_timeline concepts on top of the organic core:
// Sequence -> Layers (Clips / Automation / Gradient) -> Clips / Keys.
#pragma once

#include "OrganicCore.h"
#include "OrganicEasing.h"
#include "OrganicAudio.h"
#include <set>

namespace organic
{

class Sequence;
class ClipLayer;

// ---------------------------------------------------------------- Clip
class Clip : public Container
{
public:
    enum class CType { Block, Audio };

    Clip(ClipLayer* layer, CType type, const std::string& name);

    uint64_t   id = 0;
    ClipLayer* layer = nullptr;
    CType      ctype = CType::Block;

    Parameter* startP  = nullptr; // seconds
    Parameter* lengthP = nullptr; // seconds
    Parameter* colorP  = nullptr;
    Parameter* enabledP = nullptr; // bypass (dimmed + muted)
    // audio only
    Parameter* fileP    = nullptr;
    Parameter* gainP    = nullptr;
    Parameter* offsetP  = nullptr; // seconds into the media
    Parameter* fadeInP  = nullptr; // seconds
    Parameter* fadeOutP = nullptr; // seconds
    Parameter* loopMediaP = nullptr; // tile the media to fill the clip

    std::shared_ptr<AudioAsset> asset; // waveform data (audio clips)

    double start()  const { return startP->floatValue(); }
    double length() const { return lengthP->floatValue(); }
    double end()    const { return start() + length(); }
    bool   enabled() const { return enabledP->boolValue(); }
    float  fadeGainAt(double localT) const; // fade envelope (1 when no fades)

    void setAudioFile(const std::string& path, bool adjustLength = false);

    std::string inspectableTypeName() const override { return ctype == CType::Audio ? "Audio Clip" : "Clip"; }
    void inspectorGui() override;
    void onParamChanged(Parameter* p) override;

    json save() const override;
    void load(const json& j) override;
};

// ---------------------------------------------------------------- Layer base
class Layer : public Container
{
public:
    enum class LType { Clips, Automation, Gradient, Triggers };

    Layer(Sequence* seq, LType type, const std::string& name);

    uint64_t  id = 0;
    Sequence* sequence = nullptr;
    LType     ltype;
    float     uiHeight = 60.f;

    Parameter* colorP = nullptr;
    Parameter* enabledP = nullptr; // mute / bypass the whole layer

    static const char* ltypeName(LType t);
    std::string inspectableTypeName() const override { return std::string(ltypeName(ltype)) + " Layer"; }

    json save() const override;
    void load(const json& j) override;
};

// ---------------------------------------------------------------- ClipLayer
class ClipLayer : public Layer
{
public:
    ClipLayer(Sequence* seq, const std::string& name);

    std::vector<std::unique_ptr<Clip>> clips;

    Clip* addClip(Clip::CType type, const std::string& name, double t, double len);
    Clip* addClipFromJson(const json& j);         // restores id too
    json  removeClip(uint64_t id);                // returns saved data
    Clip* findClip(uint64_t id) const;
    void  sortClips();

    json save() const override;
    void load(const json& j) override;
    void inspectorGui() override;
};

// ---------------------------------------------------------------- AutomationLayer
struct AutoKey
{
    uint64_t   id = 0;
    double     time  = 0;
    float      value = 0;   // real units, within [rangeMin, rangeMax]
    EasingType easing = EasingType::Linear;
    EaseParams ep;
};

class AutomationLayer : public Layer
{
public:
    AutomationLayer(Sequence* seq, const std::string& name);

    std::vector<AutoKey> keys; // sorted by time
    std::set<uint64_t>   selectedKeys;

    Parameter* rangeMinP = nullptr;
    Parameter* rangeMaxP = nullptr;
    Parameter* rangeRemapP = nullptr;  // on range change: Absolute / Proportional
    Parameter* outputP = nullptr;      // live output value (read-only, watchable/recordable)

    // live recorder (organicui's AutomationRecorder)
    Parameter* recArmP      = nullptr; // arm recording (records while playing)
    Parameter* recSourceP   = nullptr; // source parameter address
    Parameter* recSimplifyP = nullptr; // Points / Linear (RDP) / Bezier fit
    Parameter* recTolP      = nullptr; // simplification tolerance
    bool recording = false;
    std::vector<std::pair<double, float>> recPoints;

    float valueAt(double t) const;                       // real units
    float normValueAt(double t) const;                   // 0..1 for display
    AutoKey* addKey(double t, float v, EasingType e = EasingType::Linear);
    AutoKey* insertKeyAt(double t);                      // preserves curve shape (bezier split)
    AutoKey* findKey(uint64_t id);
    void removeKey(uint64_t id);
    void sortKeys();

    void updateRecording(double t);      // called by Sequence::update while playing
    void stopRecordingAndApply();        // simplify + replace keys in range (undoable)

    json keysToJson() const;
    void keysFromJson(const json& j);

    json save() const override;
    void load(const json& j) override;
    void inspectorGui() override;
    void onParamChanged(Parameter* p) override;

private:
    float prevMin = 0.f, prevMax = 1.f; // for proportional range remap
};

// ---------------------------------------------------------------- GradientLayer
struct GradKey
{
    uint64_t id = 0;
    double   time = 0;
    ImVec4   color = ImVec4(1, 1, 1, 1);
    bool     hold = false; // NONE interpolation: keep this color until the next key
};

class GradientLayer : public Layer
{
public:
    GradientLayer(Sequence* seq, const std::string& name);

    std::vector<GradKey> keys; // sorted
    std::set<uint64_t>   selectedKeys;

    ImVec4 colorAt(double t) const;
    GradKey* addKey(double t, ImVec4 c);
    GradKey* findKey(uint64_t id);
    void removeKey(uint64_t id);
    void sortKeys();

    json keysToJson() const;
    void keysFromJson(const json& j);

    json save() const override;
    void load(const json& j) override;
    void inspectorGui() override;
};

// ---------------------------------------------------------------- TriggerLayer
struct TimeTrigger
{
    uint64_t    id = 0;
    double      time = 0;
    float       flagY = 0.3f;  // vertical placement of the flag (0..1)
    std::string name = "Trigger";
    bool        fired = false;
    double      flashTime = -100; // for UI feedback when fired
};

class TriggerLayer : public Layer
{
public:
    TriggerLayer(Sequence* seq, const std::string& name);

    std::vector<TimeTrigger> triggers; // sorted
    std::set<uint64_t>       selectedKeys;

    // hook your engine here; triggers also always log when fired
    std::function<void(TriggerLayer&, TimeTrigger&)> onTriggered;

    TimeTrigger* addTrigger(double t, const std::string& name = "Trigger");
    TimeTrigger* findTrigger(uint64_t id);
    void removeTrigger(uint64_t id);
    void sortTriggers();

    void resetFiredStates(double playheadTime); // fired = (time <= playhead)
    void fireCrossings(double t0, double t1, double now); // fires in (t0, t1]

    json keysToJson() const;
    void keysFromJson(const json& j);
    json save() const override;
    void load(const json& j) override;
    void inspectorGui() override;
};

// ---------------------------------------------------------------- Sequence
struct TimeCue
{
    uint64_t    id = 0;
    double      time = 0;
    std::string name = "Cue";
};

class Sequence : public Container
{
public:
    Sequence(const std::string& name);

    uint64_t managerUid = 0; // assigned by SequenceManager (undoable tab ops)

    Parameter* lengthP   = nullptr;
    Parameter* playModeP = nullptr; // Once / Loop / Ping-Pong
    Parameter* speedP    = nullptr;
    Parameter* bpmP      = nullptr; // musical grid
    Parameter* beatsPerBarP = nullptr;
    Parameter* lengthModeP  = nullptr; // on length change: Keep / Stretch / Stick To End

    double currentTime = 0;
    bool   playing     = false;
    int    direction   = 1;   // ping-pong direction

    // loop range (in/out points); loopIn < 0 = whole sequence
    double loopIn = -1, loopOut = -1;

    std::vector<TimeCue> cues; // sorted

    // persisted view state
    double viewStart = 0;
    double pixelsPerSecond = 80;

    std::vector<std::unique_ptr<Layer>> layers;
    uint64_t nextId = 1;

    uint64_t newId() { return nextId++; }
    double   totalTime() const { return lengthP->floatValue(); }

    Layer* addLayer(Layer::LType t, const std::string& name, int index = -1);
    Layer* addLayerFromJson(const json& j, int index = -1);
    json   removeLayer(uint64_t id); // returns saved data (with index)
    Layer* findLayer(uint64_t id) const;
    int    layerIndex(const Layer* l) const;
    void   moveLayer(int from, int to);

    Clip* findClip(uint64_t id, ClipLayer** outLayer = nullptr) const;

    // cues
    TimeCue* addCue(double t, const std::string& name = "Cue");
    TimeCue* findCue(uint64_t id);
    void removeCue(uint64_t id);
    void sortCues();
    double nextCueTime(double t, int dir) const; // < 0 when none
    json cuesToJson() const;
    void cuesFromJson(const json& j);

    // ripple editing (all layers + cues + loop range), undo via content snapshots
    void insertTime(double at, double dt);
    void removeTimespan(double t0, double t1);
    json contentToJson() const;
    void contentFromJson(const json& j);

    void play()  { playing = true; }
    void pause() { playing = false; }
    void stop();
    void togglePlay() { playing = !playing; }
    void setTime(double t);
    void update(double dt);

    std::string inspectableTypeName() const override { return "Sequence"; }
    void inspectorGui() override;
    void onParamChanged(Parameter* p) override;

    json save() const override;
    void load(const json& j);

private:
    double prevLength = 16;
};

// ---------------------------------------------------------------- SequenceManager
class SequenceManager : public Container
{
public:
    SequenceManager();

    std::vector<std::unique_ptr<Sequence>> sequences;
    int currentIndex = 0;
    uint64_t nextUid = 1;

    Sequence* current() const;
    Sequence* addSequence(const std::string& name, int index = -1);
    Sequence* addSequenceFromJson(const json& j, int index = -1);
    json      removeSequence(Sequence* s); // returns data with "_index"
    Sequence* findByUid(uint64_t uid) const;
    int       indexOf(const Sequence* s) const;

    void update(double dt); // updates all sequences

    json save() const override;
    void load(const json& j) override;
};

// ---------------------------------------------------------------- Media pool
struct MediaPayload // POD carried through ImGui drag & drop
{
    char  name[64]  = {};
    char  file[512] = {};
    float color[4]  = { 0.5f, 0.5f, 0.5f, 1.f };
    int   kind      = 0; // 0 = block, 1 = audio
    float duration  = 4.f;
};
#define ORGANIC_MEDIA_PAYLOAD "ORGANIC_MEDIA"

class MediaItem : public Container
{
public:
    MediaItem(Container* parent, const std::string& name, int kind /*0 block, 1 audio*/);

    int kind = 0;
    Parameter* colorP = nullptr;
    Parameter* fileP  = nullptr; // audio only
    Parameter* durP   = nullptr; // block default duration

    MediaPayload makePayload() const;

    std::string inspectableTypeName() const override { return kind == 1 ? "Audio Media" : "Block Media"; }
    json save() const override;
    void load(const json& j) override;
};

class MediaPool : public Container
{
public:
    MediaPool();
    std::vector<std::unique_ptr<MediaItem>> items;

    MediaItem* addMedia(const std::string& name, int kind);
    void removeMedia(MediaItem* item);

    json save() const override;
    void load(const json& j) override;
};

} // namespace organic
