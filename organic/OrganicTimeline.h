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
class Clip;

// ---------------------------------------------------------------- keys
struct AutoKey
{
    uint64_t   id = 0;
    double     time  = 0;
    float      value = 0;   // real units, within [rangeMin, rangeMax]
    EasingType easing = EasingType::Linear;
    EaseParams ep;
};

struct GradKey
{
    uint64_t id = 0;
    double   time = 0;
    ImVec4   color = ImVec4(1, 1, 1, 1);
    bool     hold = false; // NONE interpolation: keep this color until the next key
};

// shared key-curve math (AutomationLayer lanes AND embedded clip automations)
float  autoKeysValueAt(const std::vector<AutoKey>& keys, float mn, float mx, double t);
// shape-preserving key insertion (bezier segments split via de Casteljau);
// newId mints ids (sequence-scoped). Returns the inserted key's id.
uint64_t autoKeysInsertAt(std::vector<AutoKey>& keys, float mn, float mx, double t,
                          const std::function<uint64_t()>& newId);
// replace every key inside [t0, t1] with keys built from a drawn/recorded
// point cloud (pencil mode + recorders). method: 0 = decimated points,
// 1 = RDP linear, 2 = bezier fit. Values are clamped into [mn, mx].
void autoKeysReplaceRange(std::vector<AutoKey>& keys, float mn, float mx,
                          const std::vector<std::pair<double, float>>& pts,
                          int method, float tol,
                          const std::function<uint64_t()>& newId);
ImVec4 gradKeysColorAt(const std::vector<GradKey>& keys, double t);

json autoKeysToJson(const std::vector<AutoKey>& keys);
void autoKeysFromJson(std::vector<AutoKey>& keys, const json& arr,
                      const std::function<uint64_t()>& newId, uint64_t* maxId);
json gradKeysToJson(const std::vector<GradKey>& keys);
void gradKeysFromJson(std::vector<GradKey>& keys, const json& arr,
                      const std::function<uint64_t()>& newId, uint64_t* maxId);

// ---------------------------------------------------------------- ClipAutomation
// One automation embedded INSIDE an effect clip (Timeline v2): the block
// carries its automations as internal rows instead of separate sequence
// lanes. Key times are CLIP-LOCAL seconds (0 .. clip length), so the data
// travels with the clip by construction and can never reach beyond the
// block. A row is either a value CURVE (scalar) or a color GRADIENT.
class ClipAutomation
{
public:
    enum class AKind { Curve, Gradient };

    ClipAutomation(Clip* owner, AKind kind, const std::string& name);

    uint64_t    id = 0;
    Clip*       clip = nullptr;
    AKind       akind = AKind::Curve;
    std::string name;            // row label (left side of the block)
    std::string target;          // host binding tag (e.g. "<nodeId>:<param>")
    ImVec4      color = ImVec4(0.9f, 0.57f, 0.18f, 1.f); // row tint

    // curve rows
    std::vector<AutoKey> keys;   // clip-local time, sorted
    float rangeMin = 0.f, rangeMax = 1.f;
    // gradient rows
    std::vector<GradKey> gkeys;  // clip-local time, sorted

    std::set<uint64_t> selectedKeys;

    // UI state — expanded persists ("collapsed row stays very compact"),
    // uiAnim is the smooth open/close animation (runtime only)
    bool  expanded = false;
    float uiAnim = -1.f;         // 0..1, seeded from `expanded` on first draw

    // recording (host-fed: the app samples its value source every frame)
    bool recArm = false;         // runtime only — never persisted (a loaded
                                 // show must not record by surprise)
    bool recording = false;
    std::vector<std::pair<double, float>> recPoints; // clip-local time
    // gradient latch recorder state (host-driven)
    float  latch[3] = { -9, 0, 0 };
    double latchStart = -1;

    // evaluation (localT = seconds since clip start)
    float  valueAt(double localT) const;
    float  normValueAt(double localT) const;
    ImVec4 colorAt(double localT) const;

    AutoKey* addKey(double t, float v, EasingType e = EasingType::Linear);
    AutoKey* insertKeyAt(double t);          // preserves curve shape
    AutoKey* findKey(uint64_t id);
    GradKey* addGradKey(double t, ImVec4 c);
    GradKey* findGradKey(uint64_t id);
    void removeKey(uint64_t id);             // curve or gradient key
    void sortKeys();
    // drop keys outside 0..clip length (after resize gestures)
    void clampToClip();
    // the row carries a curve / gradient (any key of either kind)
    bool hasKeys() const { return !keys.empty() || !gkeys.empty(); }
    // wipe the row's content so it can be redrawn: every key (both kinds),
    // the key selection and a pending recording take go. Everything that
    // makes the row what it is stays — identity, name, target, range, arm
    // state and expansion — so the host sees the same row, now empty.
    // Returns whether a key was removed (the undo-worthy part).
    bool clearKeys();

    // pencil mode / scalar recording: replace the swept range with keys
    // simplified from a point cloud (clip-local times)
    void applyDrawnPoints(const std::vector<std::pair<double, float>>& pts,
                          int method, float tol);

    // host-fed scalar recorder (mirrors AutomationLayer's recorder)
    void updateRecording(double localT, float value);
    void stopRecordingAndApply();

    json keysToJson() const;
    void keysFromJson(const json& arr);

    json save() const;
    void load(const json& j);

    uint64_t newId() const; // sequence-scoped id mint (via the owning clip)
};

// ---------------------------------------------------------------- AudioAnalysisView
// Machine-listened facts about an audio clip's media, drawn as foldable
// structure rows under the waveform and as the tracked beat grid: beats +
// downbeats (beat tracker) and labeled section maps (an EDM-structure
// model and a general song-form model — hosts may MERGE the two into one
// map riding `edm` with combined labels like "drop-chorus", leaving `song`
// empty and setting `edmTitle`). HOST-FED: the embedding app owns
// the data (and its persistence) and refreshes this view whenever its
// store changes — organic never serializes the data itself, only the two
// fold flags on the clip. All times are MEDIA-LOCAL seconds (0 = start of
// the audio file); the UI maps them through the clip's start/offset.
struct AudioAnalysisView
{
    struct Section { double t0 = 0, t1 = 0; std::string label; };
    struct Beat    { double time = 0; bool downbeat = false; };

    double bpm = 0;                 // 0 = unknown
    int    beatsPerBar = 0;         // dominant meter (0 = unknown)
    std::vector<Beat>    beats;     // sorted by time
    std::vector<Section> edm;       // EDM head: intro/buildup/drop/breakdown/…
    std::vector<Section> song;      // song head: verse/chorus/bridge/inst/…
    std::string status;             // "" or a progress/error line to display
    int revision = 0;               // host bumps on change (cache invalidation)

    // ---- classic-DSP extras (host-fed like everything above; all may be
    // empty). Onsets/curves/boundaries draw under the waveform; the scalar
    // facts render as clip-header chips.
    std::vector<double> onsets;     // transient times (media s) — tick row
    std::vector<double> boundaries; // unlabeled section-change candidates
    struct Curve
    {
        std::vector<float> v;       // 0..1 samples
        double rateHz = 0;
        bool empty() const { return v.empty() || rateHz <= 0; }
        float at(double t) const    // nearest sample, 0 outside
        {
            if (empty() || t < 0) return 0.f;
            size_t i = (size_t)(t * rateHz);
            return i < v.size() ? v[i] : 0.f;
        }
    };
    Curve loud, low, mid, high, novelty;
    bool edmIsDsp = false;          // edm row carries dsp FALLBACK sections
                                    // (no labeled structure available)
    // Row-0 title override ("" = "EDM"). Hosts that MERGE their structure
    // maps into one list (combined labels like "drop-chorus" riding `edm`,
    // `song` left empty) set the honest name here — e.g. "SECTIONS".
    std::string edmTitle;
    // chips (0 / empty = unknown)
    std::string key;                // "F minor"
    float keyStrength = 0;
    bool  keyAgree = false;         // both profile families agree
    float tuningHz = 0;
    float lufs = 0, rangeLu = 0, truePeakDb = 0, trackGainDb = 0;
    float dspBpm = 0, dspBpmConf = 0, dspBpmStability = 0;
    bool  bpmMismatch = false;      // dsp estimate disagrees with the grid
    float danceability = 0;
    float qcClipPct = 0; int qcClipRuns = 0;
    float qcHumDb = -120, qcHumHz = 0; int qcGaps = 0;

    bool hasCurves() const
    {
        return !loud.empty() || !low.empty() || !mid.empty() || !high.empty() ||
               !novelty.empty();
    }
    bool hasQcIssue() const
    {
        return qcClipPct > 0.1f || qcHumDb > 10.f || qcGaps > 0;
    }
    bool hasData() const { return !beats.empty() || !edm.empty() || !song.empty() ||
                                  hasCurves() || !onsets.empty(); }
    bool hasRows() const { return !edm.empty() || !song.empty() || !status.empty(); }
    // index of the section containing t (media seconds), -1 when none
    static int sectionAt(const std::vector<Section>& v, double t);
};

// ---------------------------------------------------------------- Clip
class Clip : public Container
{
public:
    enum class CType { Block, Audio };

    Clip(ClipLayer* layer, CType type, const std::string& name);

    uint64_t   id = 0;
    ClipLayer* layer = nullptr;
    CType      ctype = CType::Block;
    // Opaque host resource URI, separate from the displayed name. Travels
    // with save/load, clipboard copies, splits and undo snapshots.
    std::string hostBinding;

    Parameter* startP  = nullptr; // seconds
    Parameter* lengthP = nullptr; // seconds
    Parameter* colorP  = nullptr;
    Parameter* enabledP = nullptr; // bypass (dimmed + muted)
    // audio only
    Parameter* fileP    = nullptr;
    Parameter* gainP    = nullptr;
    Parameter* offsetP  = nullptr; // seconds into the media
    Parameter* loopMediaP = nullptr; // tile the media to fill the clip

    std::shared_ptr<AudioAsset> asset; // waveform data (audio clips)

    // audio structure analysis (audio clips): host-fed data + foldable
    // rows under the waveform — row 0 = the labeled structure map (EDM, or
    // the host's merged EDM×SONG map), row 1 = the song-form map (empty
    // when merged into row 0), row 2 = the DSP curve strips
    // (loud/bands/novelty). Fold flags persist with the clip; anim is runtime.
    AudioAnalysisView analysis;
    bool  structExpanded[3] = { false, false, false };
    float structAnim[3] = { -1.f, -1.f, -1.f };

    // embedded automations (Block clips): the block contains its automation
    // rows — as many internal rows as the effect has automated parameters
    std::vector<std::unique_ptr<ClipAutomation>> automations;

    double start()  const { return startP->floatValue(); }
    double length() const { return lengthP->floatValue(); }
    double end()    const { return start() + length(); }
    bool   enabled() const { return enabledP->boolValue(); }

    ClipAutomation* addAutomation(ClipAutomation::AKind kind,
                                  const std::string& name,
                                  const std::string& target = "");
    ClipAutomation* findAutomation(uint64_t id) const;
    ClipAutomation* findAutomationByTarget(const std::string& target) const;
    json removeAutomation(uint64_t id); // returns saved data
    void clampAutomations();            // keys outside 0..length are dropped
    // any row of the block carries a key
    bool hasAutomationKeys() const;
    // clear the block's automation as a whole — every row's keys go
    // (ClipAutomation::clearKeys), the rows themselves stay: the block
    // keeps its automation layout and can be redrawn row by row. Returns
    // the number of rows that lost keys.
    size_t clearAutomationKeys();

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
    // Restore IDs for undo/load; copies mint new block, automation and key IDs.
    Clip* addClipFromJson(const json& j, bool newIds = false);
    json  removeClip(uint64_t id);                // returns saved data
    Clip* findClip(uint64_t id) const;
    void  sortClips();

    // Timeline v2 rule: two blocks on one track must never overlap in time
    // (conflicting effects/automation values). spanFree tests a candidate
    // span; resolveOverlap returns the nearest legal start for a clip of
    // `len` wanting to sit at `t` (flush against neighbours when needed).
    bool   spanFree(double t0, double t1,
                    const std::vector<uint64_t>& ignore = {}) const;
    double resolveOverlap(double t, double len,
                          const std::vector<uint64_t>& ignore = {}) const;

    json save() const override;
    void load(const json& j) override;
    void inspectorGui() override;
};

// ---------------------------------------------------------------- AutomationLayer
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

    // pencil mode: replace the swept range with keys simplified from the
    // drawn point cloud (same machinery as the recorder, one gesture)
    void applyDrawnPoints(const std::vector<std::pair<double, float>>& pts,
                          int method, float tol);

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
    // Runtime-only event counters: a same-time seek or stop/play between
    // host updates must not disappear into an unchanged transport snapshot.
    uint64_t transportRevision = 0;
    uint64_t stopRevision = 0;

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

    // Key selection lives beside the clip Selection, in per-row sets
    // (embedded automations of every block) and per-layer sets (classic
    // automation / gradient / trigger lanes). The editor keeps ONE selection
    // at a time: a plain click on anything else drops every key selection,
    // so a later Delete can only reach what the user last picked.
    bool anyKeySelected() const;     // any key selected in any row / lane
    void clearKeySelections();       // drop them all (rows and lanes)

    // the audio clip whose host-fed analysis drives the tracked beat grid
    // and the toolbar BPM chip: the first audio clip carrying data (layer
    // order — the pinned audio lane wins in practice). null when none.
    Clip* analysisAudioClip() const;

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

    void play();
    void pause();
    void stop();
    void togglePlay() { if (playing) pause(); else play(); }
    // Clock-follow corrections are not explicit seeks; all UI/API seeks use
    // the default, including seeks to the current time.
    void setTime(double t, bool isSeek = true);
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
