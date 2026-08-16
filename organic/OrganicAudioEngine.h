// OrganicAudioEngine.h - audio playback for timeline sequences (miniaudio).
// Mixes the audio clips of a sequence at the transport position: gain,
// media offset, media looping, playback speed (also reversed for ping-pong).
// The engine follows the UI transport clock with light drift correction.
#pragma once

#include "OrganicAudio.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace organic
{

class Sequence;

class AudioEngine
{
public:
    static AudioEngine& get();

    bool init();       // safe to call more than once
    void shutdown();
    bool ok() const { return deviceOk; }

    std::string deviceName;
    int         deviceSampleRate = 0;

    void setMasterVolume(float v) { masterVolume.store(v); }
    float getMasterVolume() const { return masterVolume.load(); }
    void setMuted(bool m) { muted.store(m); }
    bool isMuted() const { return muted.load(); }

    // call once per frame with the sequence to render (nullptr = silence)
    void syncFromSequence(Sequence* seq);

    // audio thread entry (internal)
    void render(float* out, unsigned int frames, int channels, int sampleRate);

private:
    AudioEngine() = default;
    // join the device worker BEFORE members destruct: the singleton dies in
    // exit handlers, and a PulseAudio/ALSA callback mid-render() would race
    // the destruction of `snap` (asset shared_ptr double-free — found by
    // ASAN via Light Show Studio's open+audio+exit smoke).
    ~AudioEngine() { shutdown(); }

    struct ClipSnap
    {
        std::shared_ptr<AudioAsset> asset;
        double start = 0, length = 0, offset = 0;
        float  gain = 1.f;
        bool   loopMedia = false;
    };
    struct Snap
    {
        std::vector<ClipSnap> clips;
        bool   playing = false;
        double speed = 1.0;
        int    direction = 1;
    };

    std::mutex snapMutex;
    Snap snap;

    std::atomic<double> uiTime{ 0.0 };     // transport time from the UI thread
    std::atomic<uint64_t> uiSyncGen{ 0 };  // bumped per syncFromSequence: the
                                           // mixer consumes each transport
                                           // snapshot at most ONCE — a frozen
                                           // clock (stalled UI loop: occluded/
                                           // minimized window, hidden panel, a
                                           // blocked vsync swap) must never
                                           // pull audio backwards repeatedly
    double audioTime = 0.0;                // audio-thread local clock
    uint64_t lastSyncGen = 0;              // audio-thread: last consumed gen
    std::atomic<float> masterVolume{ 0.8f };
    std::atomic<bool>  muted{ false };

    bool deviceOk = false;
    bool inited = false;
    void* device = nullptr; // ma_device*
};

} // namespace organic
