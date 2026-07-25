#include "OrganicAudioEngine.h"
#include "OrganicTimeline.h"
#include "OrganicCore.h"

#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <cmath>
#include <cstring>

namespace organic
{

AudioEngine& AudioEngine::get()
{
    static AudioEngine e;
    return e;
}

static void maDataCallback(ma_device* dev, void* output, const void*, ma_uint32 frameCount)
{
    auto* engine = (AudioEngine*)dev->pUserData;
    engine->render((float*)output, frameCount, (int)dev->playback.channels,
                   (int)dev->sampleRate);
}

bool AudioEngine::init()
{
    if (inited) return deviceOk;
    inited = true;

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = 0; // native
    cfg.dataCallback = maDataCallback;
    cfg.pUserData = this;

    ma_device* dev = new ma_device();
    if (ma_device_init(nullptr, &cfg, dev) != MA_SUCCESS)
    {
        delete dev;
        OLOGW("Audio", "No audio playback device available (running silent)");
        return false;
    }
    if (ma_device_start(dev) != MA_SUCCESS)
    {
        ma_device_uninit(dev);
        delete dev;
        OLOGW("Audio", "Failed to start audio device (running silent)");
        return false;
    }
    device = dev;
    deviceOk = true;
    deviceName = dev->playback.name;
    deviceSampleRate = (int)dev->sampleRate;
    OLOG("Audio", "Playback device: " << deviceName << " @ " << deviceSampleRate << " Hz");
    return true;
}

void AudioEngine::shutdown()
{
    if (device)
    {
        ma_device* dev = (ma_device*)device;
        ma_device_uninit(dev);
        delete dev;
        device = nullptr;
    }
    deviceOk = false;
    inited = false;
}

void AudioEngine::syncFromSequence(Sequence* seq)
{
    Snap next;
    if (seq)
    {
        next.playing = seq->playing;
        next.speed = seq->speedP->floatValue();
        next.direction = seq->direction;
        uiTime.store(seq->currentTime);
        for (auto& l : seq->layers)
        {
            auto* cl = dynamic_cast<ClipLayer*>(l.get());
            if (!cl || !cl->enabledP->boolValue()) continue;
            for (auto& c : cl->clips)
            {
                if (c->ctype != Clip::CType::Audio || !c->asset) continue;
                if (!c->enabledP || !c->enabledP->boolValue()) continue;
                ClipSnap s;
                s.asset = c->asset;
                s.start = c->start();
                s.length = c->length();
                s.offset = c->offsetP->floatValue();
                s.gain = c->gainP->floatValue();
                s.fadeIn = c->fadeInP ? c->fadeInP->floatValue() : 0.f;
                s.fadeOut = c->fadeOutP ? c->fadeOutP->floatValue() : 0.f;
                s.loopMedia = c->loopMediaP && c->loopMediaP->boolValue();
                next.clips.push_back(std::move(s));
            }
        }
    }
    std::lock_guard<std::mutex> lock(snapMutex);
    snap = std::move(next);
}

static float sampleAsset(const AudioAsset& a, double mediaT, int channel)
{
    if (mediaT < 0) return 0.f;
    double fpos = mediaT * a.buffer.sampleRate;
    int f0 = (int)fpos;
    if (f0 >= a.buffer.frames() - 1) return 0.f;
    float frac = (float)(fpos - f0);
    int ch = a.buffer.channels;
    int c = channel < ch ? channel : 0;
    float s0 = a.buffer.samples[(size_t)f0 * ch + c];
    float s1 = a.buffer.samples[(size_t)(f0 + 1) * ch + c];
    return s0 + (s1 - s0) * frac;
}

void AudioEngine::render(float* out, unsigned int frames, int channels, int sampleRate)
{
    memset(out, 0, sizeof(float) * frames * channels);

    Snap localSnap;
    {
        std::lock_guard<std::mutex> lock(snapMutex);
        localSnap = snap;
    }

    double ui = uiTime.load();
    if (!localSnap.playing || muted.load())
    {
        audioTime = ui;
        return;
    }

    // drift correction against the UI clock
    double drift = audioTime - ui;
    double rateAdjust = 1.0;
    if (std::fabs(drift) > 0.09) audioTime = ui;        // hard resync (seek/loop)
    else rateAdjust = 1.0 - drift * 0.1;                // gentle pull

    double dtPerFrame = (localSnap.speed * localSnap.direction * rateAdjust) / sampleRate;
    float master = masterVolume.load();

    for (unsigned int f = 0; f < frames; f++)
    {
        double t = audioTime;
        float mixL = 0.f, mixR = 0.f;
        for (auto& c : localSnap.clips)
        {
            double local = t - c.start;
            if (local < 0 || local >= c.length) continue;
            double mediaT = c.offset + local;
            if (c.loopMedia && c.asset->buffer.duration() > 0.01)
                mediaT = std::fmod(mediaT, c.asset->buffer.duration());
            float env = c.gain;
            if (c.fadeIn > 0.001f && local < c.fadeIn)
                env *= (float)(local / c.fadeIn);
            if (c.fadeOut > 0.001f && local > c.length - c.fadeOut)
                env *= (float)((c.length - local) / c.fadeOut);
            mixL += sampleAsset(*c.asset, mediaT, 0) * env;
            mixR += sampleAsset(*c.asset, mediaT, 1) * env;
        }
        mixL = std::max(-1.f, std::min(1.f, mixL * master));
        mixR = std::max(-1.f, std::min(1.f, mixR * master));
        if (channels >= 2)
        {
            out[f * channels] = mixL;
            out[f * channels + 1] = mixR;
        }
        else out[f] = 0.5f * (mixL + mixR);
        audioTime += dtPerFrame;
    }
}

} // namespace organic
