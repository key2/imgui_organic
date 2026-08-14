// OrganicAudio.h - audio file support for timeline playback + waveforms.
// Decoding: ma_decoder (miniaudio — native WAV/FLAC/MP3) with the classic
// hand-rolled WAV reader as a safety net, plus a HOST-INJECTABLE fallback
// hook for everything else (AIFF/ALAC/AAC/…): the embedding app decides
// how exotic containers decode (e.g. piping through the ffmpeg CLI) —
// the library itself never spawns processes. Save stays WAV (PCM16).
#pragma once

#include <functional>
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
// ma_decoder path: WAV/FLAC/MP3 natively (f32, source rate/channels kept)
bool decodeAudio(const std::string& path, AudioBuffer& out, std::string* err = nullptr);

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

    // Host-injected decoder for formats ma_decoder does not cover
    // (AIFF/ALAC/AAC/OGG/…). load() tries: ma_decoder → the classic WAV
    // reader → this hook. Return true and fill `out` (any rate/channel
    // layout — playback and peaks are rate-agnostic); optionally set *err.
    std::function<bool(const std::string& path, AudioBuffer& out,
                       std::string* err)> decodeFallback;

private:
    std::unordered_map<std::string, std::weak_ptr<AudioAsset>> cache;
};

} // namespace organic
