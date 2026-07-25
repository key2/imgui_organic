// OrganicEasing.h - easing functions for automation curves.
// Mirrors juce_organicui's Easing types: Linear, Hold, Bezier, Sine, Elastic, Bounce, Steps, Noise.
#pragma once

#include <vector>
#include "imgui.h"
#include "json.hpp"

namespace organic
{

enum class EasingType : int
{
    Linear = 0, Bezier, Hold, Sine, Elastic, Bounce, Steps, Noise, Perlin,
    COUNT
};

const char* easingName(EasingType t);

struct EaseParams
{
    // Bezier anchors, relative to segment: x = fraction of segment duration,
    // y = offset in *normalized* value units. a1 is relative to the start key,
    // a2 relative to the end key (usually negative x).
    ImVec2 a1 = ImVec2(0.35f, 0.0f);
    ImVec2 a2 = ImVec2(-0.35f, 0.0f);
    float  freq  = 3.0f;  // Sine / Elastic / Noise
    float  amp   = 0.5f;  // Sine / Noise amplitude (normalized)
    int    steps = 5;     // Steps
    int    seed  = 0;     // Noise

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);
};

// Interpolate between 'from' and 'to' with weight w in [0,1].
// Values are in normalized units so amplitude parameters behave predictably.
float ease(EasingType type, float from, float to, float w, const EaseParams& p);

// ---------------------------------------------------------------- curve utilities

// Split a cubic bezier (p0,c1,c2,p3) at parameter t (de Casteljau).
// Outputs the control points of the two halves:
//   left  = p0, l1, l2, mid
//   right = mid, r1, r2, p3
void splitCubic(ImVec2 p0, ImVec2 c1, ImVec2 c2, ImVec2 p3, float t,
                ImVec2& l1, ImVec2& l2, ImVec2& mid, ImVec2& r1, ImVec2& r2);

// Ramer-Douglas-Peucker polyline simplification (returns indices to keep, sorted).
// epsilon is the max perpendicular distance in the same units as the points.
void simplifyRDP(const std::vector<ImVec2>& points, float epsilon,
                 std::vector<int>& keepIndices);

// Least-squares cubic bezier fitting (Graphics Gems "FitCurve" style, as used
// by organicui's recorder). Fits (x=time, y=value) points with G1 continuity.
struct FittedCubic
{
    ImVec2 p0, c1, c2, p3;
};
void fitCubicBeziers(const std::vector<ImVec2>& points, float maxError,
                     std::vector<FittedCubic>& out);

} // namespace organic
