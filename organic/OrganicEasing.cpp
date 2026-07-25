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
    case EasingType::Noise:  return "Noise";
    case EasingType::Perlin: return "Perlin";
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

    case EasingType::Perlin:
    {
        // gradient noise (1D perlin): smoother than value noise
        float base = lerpf(from, to, w);
        float env  = std::sin(3.14159265f * w);
        float x = w * std::max(0.5f, p.freq) * 4.f;
        int i = (int)std::floor(x);
        float f = x - i;
        float g0 = hash01(i * 73 + p.seed * 131) * 2.f - 1.f;
        float g1 = hash01((i + 1) * 73 + p.seed * 131) * 2.f - 1.f;
        float u = f * f * f * (f * (f * 6.f - 15.f) + 10.f); // quintic fade
        float n = lerpf(g0 * f, g1 * (f - 1.f), u) * 2.f;
        return base + n * p.amp * env;
    }

    default:
        return lerpf(from, to, w);
    }
}

// ---------------------------------------------------------------- curve utilities
static ImVec2 vlerp(const ImVec2& a, const ImVec2& b, float t)
{
    return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

void splitCubic(ImVec2 p0, ImVec2 c1, ImVec2 c2, ImVec2 p3, float t,
                ImVec2& l1, ImVec2& l2, ImVec2& mid, ImVec2& r1, ImVec2& r2)
{
    ImVec2 q0 = vlerp(p0, c1, t);
    ImVec2 q1 = vlerp(c1, c2, t);
    ImVec2 q2 = vlerp(c2, p3, t);
    ImVec2 s0 = vlerp(q0, q1, t);
    ImVec2 s1 = vlerp(q1, q2, t);
    mid = vlerp(s0, s1, t);
    l1 = q0; l2 = s0;
    r1 = s1; r2 = q2;
}

// ---------- RDP
static float perpDist(const ImVec2& p, const ImVec2& a, const ImVec2& b)
{
    float dx = b.x - a.x, dy = b.y - a.y;
    float len2 = dx * dx + dy * dy;
    if (len2 < 1e-12f)
    {
        float ex = p.x - a.x, ey = p.y - a.y;
        return std::sqrt(ex * ex + ey * ey);
    }
    float t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
    t = std::max(0.f, std::min(1.f, t));
    float px = a.x + dx * t, py = a.y + dy * t;
    float ex = p.x - px, ey = p.y - py;
    return std::sqrt(ex * ex + ey * ey);
}

static void rdpRec(const std::vector<ImVec2>& pts, int i0, int i1, float eps,
                   std::vector<int>& keep)
{
    if (i1 <= i0 + 1) return;
    float dmax = -1.f;
    int imax = -1;
    for (int i = i0 + 1; i < i1; i++)
    {
        float d = perpDist(pts[i], pts[i0], pts[i1]);
        if (d > dmax) { dmax = d; imax = i; }
    }
    if (dmax > eps && imax > 0)
    {
        rdpRec(pts, i0, imax, eps, keep);
        keep.push_back(imax);
        rdpRec(pts, imax, i1, eps, keep);
    }
}

void simplifyRDP(const std::vector<ImVec2>& points, float epsilon, std::vector<int>& keep)
{
    keep.clear();
    int n = (int)points.size();
    if (n == 0) return;
    keep.push_back(0);
    if (n > 2) rdpRec(points, 0, n - 1, epsilon, keep);
    if (n > 1) keep.push_back(n - 1);
    std::sort(keep.begin(), keep.end());
    keep.erase(std::unique(keep.begin(), keep.end()), keep.end());
}

// ---------- cubic bezier fitting (Schneider, Graphics Gems "FitCurve")
static ImVec2 bezPoint(const FittedCubic& b, float t)
{
    float it = 1.f - t;
    float b0 = it * it * it, b1 = 3 * it * it * t, b2 = 3 * it * t * t, b3 = t * t * t;
    return ImVec2(b.p0.x * b0 + b.c1.x * b1 + b.c2.x * b2 + b.p3.x * b3,
                  b.p0.y * b0 + b.c1.y * b1 + b.c2.y * b2 + b.p3.y * b3);
}

static ImVec2 vsub(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x - b.x, a.y - b.y); }
static ImVec2 vadd(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }
static ImVec2 vmul(const ImVec2& a, float s) { return ImVec2(a.x * s, a.y * s); }
static float  vdot(const ImVec2& a, const ImVec2& b) { return a.x * b.x + a.y * b.y; }
static float  vlen(const ImVec2& a) { return std::sqrt(vdot(a, a)); }
static ImVec2 vnorm(const ImVec2& a)
{
    float l = vlen(a);
    return l > 1e-9f ? vmul(a, 1.f / l) : ImVec2(0, 0);
}

static void fitCubicRec(const std::vector<ImVec2>& pts, int first, int last,
                        ImVec2 tHat1, ImVec2 tHat2, float error,
                        std::vector<FittedCubic>& out, int depth)
{
    int nPts = last - first + 1;

    // two points: heuristic straight-ish segment
    if (nPts == 2)
    {
        float dist = vlen(vsub(pts[last], pts[first])) / 3.f;
        FittedCubic b;
        b.p0 = pts[first];
        b.p3 = pts[last];
        b.c1 = vadd(b.p0, vmul(tHat1, dist));
        b.c2 = vadd(b.p3, vmul(tHat2, dist));
        out.push_back(b);
        return;
    }

    // chord-length parameterization
    std::vector<float> u(nPts);
    u[0] = 0;
    for (int i = 1; i < nPts; i++)
        u[i] = u[i - 1] + vlen(vsub(pts[first + i], pts[first + i - 1]));
    float total = u[nPts - 1] > 1e-9f ? u[nPts - 1] : 1.f;
    for (auto& uu : u) uu /= total;

    auto generateBezier = [&](const std::vector<float>& uPrime) -> FittedCubic
    {
        FittedCubic b;
        b.p0 = pts[first];
        b.p3 = pts[last];
        double C[2][2] = { { 0, 0 }, { 0, 0 } };
        double X[2] = { 0, 0 };
        for (int i = 0; i < nPts; i++)
        {
            float t = uPrime[i], it = 1.f - t;
            ImVec2 A0 = vmul(tHat1, 3.f * it * it * t);
            ImVec2 A1 = vmul(tHat2, 3.f * it * t * t);
            C[0][0] += vdot(A0, A0);
            C[0][1] += vdot(A0, A1);
            C[1][1] += vdot(A1, A1);
            float b0 = it * it * it, b1 = 3 * it * it * t, b2 = 3 * it * t * t, b3 = t * t * t;
            ImVec2 tmp = vsub(pts[first + i],
                              vadd(vmul(b.p0, b0 + b1), vmul(b.p3, b2 + b3)));
            X[0] += vdot(A0, tmp);
            X[1] += vdot(A1, tmp);
        }
        C[1][0] = C[0][1];
        double detC0C1 = C[0][0] * C[1][1] - C[1][0] * C[0][1];
        double detC0X  = C[0][0] * X[1] - C[1][0] * X[0];
        double detXC1  = X[0] * C[1][1] - X[1] * C[0][1];
        double alphaL = detC0C1 == 0 ? 0 : detXC1 / detC0C1;
        double alphaR = detC0C1 == 0 ? 0 : detC0X / detC0C1;
        float segLen = vlen(vsub(b.p3, b.p0));
        double eps = 1e-6 * segLen;
        if (alphaL < eps || alphaR < eps)
        {
            float dist = segLen / 3.f;
            alphaL = alphaR = dist;
        }
        b.c1 = vadd(b.p0, vmul(tHat1, (float)alphaL));
        b.c2 = vadd(b.p3, vmul(tHat2, (float)alphaR));
        return b;
    };

    auto maxErrorOf = [&](const FittedCubic& b, const std::vector<float>& uPrime, int& splitPoint) -> float
    {
        float maxDist = 0;
        splitPoint = (first + last) / 2;
        for (int i = 1; i < nPts - 1; i++)
        {
            ImVec2 p = bezPoint(b, uPrime[i]);
            ImVec2 d = vsub(p, pts[first + i]);
            float dist = vdot(d, d);
            if (dist > maxDist)
            {
                maxDist = dist;
                splitPoint = first + i;
            }
        }
        return maxDist;
    };

    // Newton-Raphson reparameterization
    auto reparameterize = [&](const FittedCubic& b, std::vector<float>& uPrime)
    {
        for (int i = 0; i < nPts; i++)
        {
            float t = uPrime[i];
            ImVec2 q = bezPoint(b, t);
            // derivatives
            float it = 1.f - t;
            ImVec2 d1 = vadd(vadd(vmul(vsub(b.c1, b.p0), 3 * it * it),
                                  vmul(vsub(b.c2, b.c1), 6 * it * t)),
                             vmul(vsub(b.p3, b.c2), 3 * t * t));
            ImVec2 d2 = vadd(vmul(vadd(vsub(b.c2, vmul(b.c1, 2.f)), b.p0), 6 * it),
                             vmul(vadd(vsub(b.p3, vmul(b.c2, 2.f)), b.c1), 6 * t));
            ImVec2 diff = vsub(q, pts[first + i]);
            float num = vdot(diff, d1);
            float den = vdot(d1, d1) + vdot(diff, d2);
            if (std::fabs(den) > 1e-9f)
                uPrime[i] = std::max(0.f, std::min(1.f, t - num / den));
        }
    };

    std::vector<float> uPrime = u;
    FittedCubic bez = generateBezier(uPrime);
    int splitPoint;
    float maxErr = maxErrorOf(bez, uPrime, splitPoint);
    if (maxErr < error * error)
    {
        out.push_back(bez);
        return;
    }

    // try reparameterizing a few times
    if (maxErr < error * error * 16.f)
    {
        for (int it = 0; it < 4; it++)
        {
            reparameterize(bez, uPrime);
            bez = generateBezier(uPrime);
            maxErr = maxErrorOf(bez, uPrime, splitPoint);
            if (maxErr < error * error)
            {
                out.push_back(bez);
                return;
            }
        }
    }

    if (depth > 24) { out.push_back(bez); return; } // safety

    // split at max error point and recurse with G1 continuity
    ImVec2 tHatCenter = vnorm(vsub(pts[splitPoint - 1], pts[splitPoint + 1]));
    fitCubicRec(pts, first, splitPoint, tHat1, tHatCenter, error, out, depth + 1);
    fitCubicRec(pts, splitPoint, last, vmul(tHatCenter, -1.f), tHat2, error, out, depth + 1);
}

void fitCubicBeziers(const std::vector<ImVec2>& points, float maxError,
                     std::vector<FittedCubic>& out)
{
    out.clear();
    int n = (int)points.size();
    if (n < 2) return;
    if (n == 2)
    {
        FittedCubic b;
        b.p0 = points[0]; b.p3 = points[1];
        b.c1 = vlerp(b.p0, b.p3, 1.f / 3.f);
        b.c2 = vlerp(b.p0, b.p3, 2.f / 3.f);
        out.push_back(b);
        return;
    }
    ImVec2 tHat1 = vnorm(vsub(points[1], points[0]));
    ImVec2 tHat2 = vnorm(vsub(points[n - 2], points[n - 1]));
    fitCubicRec(points, 0, n - 1, tHat1, tHat2, std::max(1e-6f, maxError), out, 0);
}

} // namespace organic
