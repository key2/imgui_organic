#include "OrganicEasing.h"
#include <cmath>
#include <algorithm>

namespace organic
{

const char* easingName(EasingType t)
{
    switch (t)
    {
    case EasingType::Linear:  return "Linear";
    case EasingType::Bezier:  return "Bezier";
    case EasingType::Hold:    return "Hold";
    case EasingType::Sine:    return "Sine";
    case EasingType::Elastic: return "Elastic";
    case EasingType::Bounce:  return "Bounce";
    case EasingType::Steps:   return "Steps";
    case EasingType::Noise:   return "Noise";
    default: return "?";
    }
}

nlohmann::json EaseParams::toJson() const
{
    return { { "a1", { a1.x, a1.y } }, { "a2", { a2.x, a2.y } },
             { "freq", freq }, { "amp", amp }, { "steps", steps }, { "seed", seed } };
}

void EaseParams::fromJson(const nlohmann::json& j)
{
    try
    {
        if (j.contains("a1")) { a1.x = j["a1"][0]; a1.y = j["a1"][1]; }
        if (j.contains("a2")) { a2.x = j["a2"][0]; a2.y = j["a2"][1]; }
        if (j.contains("freq"))  freq  = j["freq"];
        if (j.contains("amp"))   amp   = j["amp"];
        if (j.contains("steps")) steps = j["steps"];
        if (j.contains("seed"))  seed  = j["seed"];
    }
    catch (...) {}
}

// ---------------------------------------------------------------- internals
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

static float bezierAxis(float p0, float p1, float p2, float p3, float t)
{
    float it = 1.f - t;
    return it * it * it * p0 + 3 * it * it * t * p1 + 3 * it * t * t * p2 + t * t * t * p3;
}

// solve t so that x(t) == x, x monotonic-ish in [0,1]; bisection is robust
static float bezierSolveT(float cx1, float cx2, float x)
{
    float lo = 0.f, hi = 1.f, t = x;
    for (int i = 0; i < 24; i++)
    {
        float xt = bezierAxis(0.f, cx1, cx2, 1.f, t);
        if (std::fabs(xt - x) < 1e-5f) break;
        if (xt < x) lo = t; else hi = t;
        t = 0.5f * (lo + hi);
    }
    return t;
}

static float easeOutBounce(float w)
{
    const float n1 = 7.5625f, d1 = 2.75f;
    if (w < 1.f / d1)        return n1 * w * w;
    else if (w < 2.f / d1)   { w -= 1.5f / d1;  return n1 * w * w + 0.75f; }
    else if (w < 2.5f / d1)  { w -= 2.25f / d1; return n1 * w * w + 0.9375f; }
    else                     { w -= 2.625f / d1; return n1 * w * w + 0.984375f; }
}

static float hash01(int n)
{
    n = (n << 13) ^ n;
    n = n * (n * n * 15731 + 789221) + 1376312589;
    return ((n & 0x7fffffff) / 1073741824.0f) * 0.5f; // 0..1
}

static float valueNoise(float x, int seed)
{
    int i = (int)std::floor(x);
    float f = x - i;
    float a = hash01(i * 57 + seed * 131);
    float b = hash01((i + 1) * 57 + seed * 131);
    float u = f * f * (3.f - 2.f * f);
    return lerpf(a, b, u);
}

// ---------------------------------------------------------------- ease
float ease(EasingType type, float from, float to, float w, const EaseParams& p)
{
    w = std::min(1.f, std::max(0.f, w));
    switch (type)
    {
    case EasingType::Linear:
        return lerpf(from, to, w);

    case EasingType::Hold:
        return w >= 1.f ? to : from;

    case EasingType::Bezier:
    {
        float cx1 = std::min(1.f, std::max(0.f, p.a1.x));
        float cx2 = std::min(1.f, std::max(0.f, 1.f + p.a2.x));
        float t = bezierSolveT(cx1, cx2, w);
        return bezierAxis(from, from + p.a1.y, to + p.a2.y, to, t);
    }

    case EasingType::Sine:
    {
        float base = lerpf(from, to, w);
        float env  = std::sin(3.14159265f * w); // endpoint-safe envelope
        return base + std::sin(w * 6.2831853f * p.freq) * p.amp * env;
    }

    case EasingType::Elastic:
    {
        if (w <= 0.f) return from;
        if (w >= 1.f) return to;
        const float c4 = 6.2831853f / std::max(0.3f, 3.f / std::max(0.5f, p.freq));
        float v = std::pow(2.f, -10.f * w) * std::sin((w * 10.f - 0.75f) * c4) + 1.f;
        return from + (to - from) * v;
    }

    case EasingType::Bounce:
        return from + (to - from) * easeOutBounce(w);

    case EasingType::Steps:
    {
        int n = std::max(1, p.steps);
        float q = std::floor(w * n) / (float)n;
        if (w >= 1.f) q = 1.f;
        return lerpf(from, to, q);
    }

    case EasingType::Noise:
    {
        float base = lerpf(from, to, w);
        float env  = std::sin(3.14159265f * w);
        float n = (valueNoise(w * std::max(0.5f, p.freq) * 4.f, p.seed) * 2.f - 1.f);
        return base + n * p.amp * env;
    }

    default:
        return lerpf(from, to, w);
    }
}

} // namespace organic
