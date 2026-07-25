// OrganicEasing.h - easing functions for automation curves.
// Mirrors juce_organicui's Easing types: Linear, Hold, Bezier, Sine, Elastic, Bounce, Steps, Noise.
#pragma once

#include "imgui.h"
#include "json.hpp"

namespace organic
{

enum class EasingType : int
{
    Linear = 0, Bezier, Hold, Sine, Elastic, Bounce, Steps, Noise,
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

} // namespace organic
