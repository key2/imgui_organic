#include "OrganicAudio.h"
#include "OrganicCore.h"
#include "miniaudio.h" // declarations only — the impl lives in OrganicAudioEngine.cpp
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <random>

namespace organic
{

float AudioBuffer::sampleMono(int frame) const
{
    if (frame < 0 || frame >= frames()) return 0.f;
    float s = 0.f;
    for (int c = 0; c < channels; c++) s += samples[(size_t)frame * channels + c];
    return s / (float)channels;
}

// ---------------------------------------------------------------- Peaks
void Peaks::build(const AudioBuffer& buf, int spb)
{
    samplesPerBin = std::max(16, spb);
    sampleRate    = buf.sampleRate;
    int nFrames   = buf.frames();
    int nBins     = (nFrames + samplesPerBin - 1) / samplesPerBin;
    mins.assign(nBins, 0.f);
    maxs.assign(nBins, 0.f);
    for (int b = 0; b < nBins; b++)
    {
        float mn = 1e9f, mx = -1e9f;
        int start = b * samplesPerBin;
        int end   = std::min(nFrames, start + samplesPerBin);
        for (int f = start; f < end; f++)
        {
            float s = buf.sampleMono(f);
            mn = std::min(mn, s);
            mx = std::max(mx, s);
        }
        if (start >= end) { mn = mx = 0.f; }
        mins[b] = mn; maxs[b] = mx;
    }
}

bool Peaks::query(double t0, double t1, float& mn, float& mx) const
{
    if (mins.empty() || sampleRate <= 0) return false;
    double binDur = (double)samplesPerBin / sampleRate;
    int b0 = (int)std::floor(t0 / binDur);
    int b1 = (int)std::floor(t1 / binDur);
    b0 = std::max(0, std::min((int)mins.size() - 1, b0));
    b1 = std::max(0, std::min((int)mins.size() - 1, b1));
    if (b1 < b0) std::swap(b0, b1);
    mn = 1e9f; mx = -1e9f;
    for (int b = b0; b <= b1; b++)
    {
        mn = std::min(mn, mins[b]);
        mx = std::max(mx, maxs[b]);
    }
    return mn <= mx;
}

// ---------------------------------------------------------------- WAV IO
static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool loadWav(const std::string& path, AudioBuffer& out, std::string* err)
{
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return fail("cannot open file");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 44) { fclose(f); return fail("file too small"); }
    std::vector<uint8_t> data((size_t)size);
    if (fread(data.data(), 1, (size_t)size, f) != (size_t)size) { fclose(f); return fail("read error"); }
    fclose(f);

    if (memcmp(data.data(), "RIFF", 4) != 0 || memcmp(data.data() + 8, "WAVE", 4) != 0)
        return fail("not a RIFF/WAVE file");

    int fmt = 0, channels = 0, rate = 0, bits = 0;
    const uint8_t* dataChunk = nullptr;
    uint32_t dataSize = 0;

    size_t pos = 12;
    while (pos + 8 <= data.size())
    {
        const uint8_t* ck = data.data() + pos;
        uint32_t ckSize = rd32(ck + 4);
        const uint8_t* body = ck + 8;
        if (pos + 8 + ckSize > data.size()) ckSize = (uint32_t)(data.size() - pos - 8);

        if (memcmp(ck, "fmt ", 4) == 0 && ckSize >= 16)
        {
            fmt      = rd16(body);
            channels = rd16(body + 2);
            rate     = (int)rd32(body + 4);
            bits     = rd16(body + 14);
            if (fmt == 0xFFFE && ckSize >= 40) fmt = rd16(body + 24); // extensible: subformat
        }
        else if (memcmp(ck, "data", 4) == 0)
        {
            dataChunk = body;
            dataSize  = ckSize;
        }
        pos += 8 + ckSize + (ckSize & 1);
    }

    if (!dataChunk || channels <= 0 || rate <= 0) return fail("missing fmt/data chunk");

    out.sampleRate = rate;
    out.channels   = channels;
    out.samples.clear();

    int bytesPer = bits / 8;
    if (bytesPer <= 0) return fail("bad bit depth");
    size_t count = dataSize / bytesPer;
    out.samples.reserve(count);

    if (fmt == 1) // PCM
    {
        for (size_t i = 0; i < count; i++)
        {
            const uint8_t* s = dataChunk + i * bytesPer;
            float v = 0.f;
            switch (bits)
            {
            case 8:  v = ((int)s[0] - 128) / 128.f; break;
            case 16: v = (int16_t)rd16(s) / 32768.f; break;
            case 24:
            {
                int32_t x = s[0] | (s[1] << 8) | (s[2] << 16);
                if (x & 0x800000) x |= 0xFF000000;
                v = x / 8388608.f;
                break;
            }
            case 32: v = (int32_t)rd32(s) / 2147483648.f; break;
            default: return fail("unsupported PCM bit depth");
            }
            out.samples.push_back(v);
        }
    }
    else if (fmt == 3 && bits == 32) // IEEE float
    {
        for (size_t i = 0; i < count; i++)
        {
            float v;
            memcpy(&v, dataChunk + i * 4, 4);
            out.samples.push_back(v);
        }
    }
    else return fail("unsupported WAV format");

    return true;
}

bool saveWavPcm16(const std::string& path, const AudioBuffer& buf)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;

    uint32_t dataSize = (uint32_t)buf.samples.size() * 2;
    uint32_t riffSize = 36 + dataSize;
    uint16_t ch  = (uint16_t)buf.channels;
    uint32_t sr  = (uint32_t)buf.sampleRate;
    uint32_t br  = sr * ch * 2;
    uint16_t ba  = ch * 2;
    uint16_t bits = 16, fmt = 1;

    fwrite("RIFF", 1, 4, f); fwrite(&riffSize, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t fmtSize = 16;
    fwrite(&fmtSize, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataSize, 4, 1, f);

    for (float s : buf.samples)
    {
        int v = (int)std::lround(std::min(1.f, std::max(-1.f, s)) * 32767.f);
        int16_t s16 = (int16_t)v;
        fwrite(&s16, 2, 1, f);
    }
    fclose(f);
    return true;
}

// ---------------------------------------------------------------- generators
AudioBuffer makeTone(float seconds, float freq)
{
    AudioBuffer b;
    int n = (int)(seconds * b.sampleRate);
    b.samples.resize(n);
    for (int i = 0; i < n; i++)
    {
        float t = i / (float)b.sampleRate;
        float env = std::min(1.f, t * 20.f) * std::exp(-t * 1.2f);
        float v = std::sin(6.2831853f * freq * t) * 0.6f
                + std::sin(6.2831853f * freq * 2.f * t) * 0.15f;
        b.samples[i] = v * env * 0.8f;
    }
    return b;
}

AudioBuffer makeBeat(float seconds, float bpm)
{
    AudioBuffer b;
    int n = (int)(seconds * b.sampleRate);
    b.samples.assign(n, 0.f);
    float beatDur = 60.f / bpm;
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> noise(-1.f, 1.f);

    for (int i = 0; i < n; i++)
    {
        float t = i / (float)b.sampleRate;
        float tb = std::fmod(t, beatDur);
        int   beat = (int)(t / beatDur);

        // kick on every beat: sine sweep 120->40 Hz
        float kick = 0.f;
        if (tb < 0.25f)
        {
            float f = 120.f - 320.f * tb;
            kick = std::sin(6.2831853f * std::max(35.f, f) * tb) * std::exp(-tb * 18.f);
        }
        // hat on off-beats
        float hat = 0.f;
        float th = std::fmod(t + beatDur * 0.5f, beatDur);
        if (th < 0.08f) hat = noise(rng) * std::exp(-th * 90.f) * 0.35f;
        // snare every 2nd beat
        float snare = 0.f;
        if ((beat % 2) == 1 && tb < 0.15f)
            snare = (noise(rng) * 0.7f + std::sin(6.2831853f * 190.f * tb) * 0.4f) * std::exp(-tb * 25.f) * 0.6f;

        b.samples[i] = std::max(-1.f, std::min(1.f, kick * 0.95f + hat + snare));
    }
    return b;
}

AudioBuffer makeSweep(float seconds, float f0, float f1)
{
    AudioBuffer b;
    int n = (int)(seconds * b.sampleRate);
    b.samples.resize(n);
    double phase = 0;
    for (int i = 0; i < n; i++)
    {
        float t = i / (float)b.sampleRate;
        float w = t / seconds;
        float f = f0 * std::pow(f1 / f0, w);
        phase += 6.2831853 * f / b.sampleRate;
        float env = std::sin(3.14159265f * w); // fade in/out
        b.samples[i] = (float)std::sin(phase) * env * 0.7f;
    }
    return b;
}

// ---------------------------------------------------------------- cache
AudioCache& AudioCache::get() { static AudioCache c; return c; }

// ma_decoder: WAV/FLAC/MP3 natively (dr_libs embedded in miniaudio),
// decoded to f32 with the SOURCE rate/channels kept — playback and peaks
// are rate-agnostic, so nothing downstream cares what the file was.
bool decodeAudio(const std::string& path, AudioBuffer& out, std::string* err)
{
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0); // 0 = keep source
    ma_decoder dec;
    ma_result r = ma_decoder_init_file(path.c_str(), &cfg, &dec);
    if (r != MA_SUCCESS)
    {
        if (err) *err = std::string("ma_decoder: ") + ma_result_description(r);
        return false;
    }
    out.channels   = (int)dec.outputChannels;
    out.sampleRate = (int)dec.outputSampleRate;
    out.samples.clear();
    ma_uint64 totalFrames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&dec, &totalFrames) == MA_SUCCESS &&
        totalFrames > 0)
        out.samples.reserve((size_t)totalFrames * (size_t)out.channels);
    std::vector<float> chunk((size_t)4096 * (size_t)std::max(1, out.channels));
    for (;;)
    {
        ma_uint64 got = 0;
        r = ma_decoder_read_pcm_frames(&dec, chunk.data(), 4096, &got);
        if (got > 0)
            out.samples.insert(out.samples.end(), chunk.begin(),
                               chunk.begin() + (size_t)got * (size_t)out.channels);
        if (r != MA_SUCCESS || got < 4096) break; // MA_AT_END included
    }
    ma_decoder_uninit(&dec);
    if (out.samples.empty())
    {
        if (err) *err = "ma_decoder: no frames decoded";
        return false;
    }
    return true;
}

std::shared_ptr<AudioAsset> AudioCache::load(const std::string& path)
{
    auto it = cache.find(path);
    if (it != cache.end())
        if (auto sp = it->second.lock()) return sp;

    auto asset = std::make_shared<AudioAsset>();
    asset->path = path;
    // decode ladder: ma_decoder (wav/flac/mp3) → classic WAV reader (odd
    // RIFFs ma rejects) → host fallback hook (AIFF/ALAC/AAC/… — e.g. the
    // ffmpeg CLI in Light Show Studio)
    std::string err1, err2, err3;
    const char* via = "ma_decoder";
    bool ok = decodeAudio(path, asset->buffer, &err1);
    if (!ok)
    {
        ok = loadWav(path, asset->buffer, &err2);
        via = "wav";
    }
    if (!ok && decodeFallback)
    {
        ok = decodeFallback(path, asset->buffer, &err3);
        via = "host fallback";
    }
    if (!ok)
    {
        OLOGE("Audio", "Failed to load '" << path << "': " << err1
              << (err2.empty() ? "" : " | ") << err2
              << (err3.empty() ? "" : " | ") << err3);
        return nullptr;
    }
    asset->peaks.build(asset->buffer);
    cache[path] = asset;
    OLOG("Audio", "Loaded '" << path << "' (" << asset->buffer.duration() << "s, "
         << asset->buffer.channels << " ch, " << asset->buffer.sampleRate << " Hz, "
         << via << ")");
    return asset;
}

} // namespace organic
