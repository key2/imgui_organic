// OrganicAudio.h - minimal audio file support for waveform display.
// WAV load/save (PCM 8/16/24/32 + float32), procedural demo sounds, peak pyramids.
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace organic
{

struct AudioBuffer
{
    int sampleRate = 44100;
    int channels   = 1;
    std::vector<float> samples; // interleaved

    int    frames()   const { return channels > 0 ? (int)samples.size() / channels : 0; }
    double duration() const { return sampleRate > 0 ? (double)frames() / sampleRate : 0.0; }
    float  sampleMono(int frame) const;
};

// min/max peaks per fixed-size bin (mono mix), for fast waveform drawing
struct Peaks
{
    int samplesPerBin = 512;
    int sampleRate    = 44100;
    std::vector<float> mins, maxs;

    void build(const AudioBuffer& buf, int spb = 512);
    // aggregated min/max over time range [t0,t1] (seconds in the media)
    bool query(double t0, double t1, float& mn, float& mx) const;
};

bool loadWav(const std::string& path, AudioBuffer& out, std::string* err = nullptr);
bool saveWavPcm16(const std::string& path, const AudioBuffer& buf);

AudioBuffer makeTone (float seconds, float freq);
AudioBuffer makeBeat (float seconds, float bpm);
AudioBuffer makeSweep(float seconds, float f0, float f1);

struct AudioAsset
{
    std::string path;
    AudioBuffer buffer;
    Peaks       peaks;
};

class AudioCache
{
public:
    static AudioCache& get();
    std::shared_ptr<AudioAsset> load(const std::string& path); // nullptr on failure

private:
    std::unordered_map<std::string, std::weak_ptr<AudioAsset>> cache;
};

} // namespace organic
