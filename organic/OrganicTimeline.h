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
    // audio only
    Parameter* fileP   = nullptr;
    Parameter* gainP   = nullptr;
    Parameter* offsetP = nullptr; // seconds into the media

    std::shared_ptr<AudioAsset> asset; // waveform data (audio clips)

    double start()  const { return startP->floatValue(); }
    double length() const { return lengthP->floatValue(); }
    double end()    const { return start() + length(); }

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
    enum class LType { Clips, Automation, Gradient };

    Layer(Sequence* seq, LType type, const std::string& name);

    uint64_t  id = 0;
    Sequence* sequence = nullptr;
    LType     ltype;
    float     uiHeight = 60.f;

    Parameter* colorP = nullptr;

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

    float valueAt(double t) const;                       // real units
    float normValueAt(double t) const;                   // 0..1 for display
    AutoKey* addKey(double t, float v, EasingType e = EasingType::Linear);
    AutoKey* findKey(uint64_t id);
    void removeKey(uint64_t id);
    void sortKeys();

    json keysToJson() const;
    void keysFromJson(const json& j);

    json save() const override;
    void load(const json& j) override;
    void inspectorGui() override;
};

// ---------------------------------------------------------------- GradientLayer
struct GradKey
{
    uint64_t id = 0;
    double   time = 0;
    ImVec4   color = ImVec4(1, 1, 1, 1);
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

// ---------------------------------------------------------------- Sequence
class Sequence : public Container
{
public:
    Sequence(const std::string& name);

    Parameter* lengthP = nullptr;
    Parameter* loopP   = nullptr;
    Parameter* speedP  = nullptr;

    double currentTime = 0;
    bool   playing     = false;

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

    void play()  { playing = true; }
    void pause() { playing = false; }
    void stop()  { playing = false; setTime(0); }
    void togglePlay() { playing = !playing; }
    void setTime(double t);
    void update(double dt);

    std::string inspectableTypeName() const override { return "Sequence"; }
    void inspectorGui() override;

    json save() const override;
    void load(const json& j);
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
