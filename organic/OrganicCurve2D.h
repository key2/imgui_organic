// OrganicCurve2D.h - 2D spatial curves (organicui's Curve2D):
// a polyline/bezier path in 2D, arc-length parameterized so a normalized
// position 0..1 travels along it at even speed. Comes with a pan/zoom editor.
#pragma once

#include "OrganicCore.h"
#include "OrganicEasing.h"
#include <set>

namespace organic
{

struct Curve2DKey
{
    uint64_t id = 0;
    ImVec2   pos = ImVec2(0, 0);
    bool     bezier = false;              // linear or cubic segment to the next key
    ImVec2   a1 = ImVec2(0.3f, 0.f);      // out anchor (relative to pos)
    ImVec2   a2 = ImVec2(-0.3f, 0.f);     // in anchor of the segment end (relative to next pos)
};

class Curve2D : public Container
{
public:
    explicit Curve2D(const std::string& name);

    std::vector<Curve2DKey> keys;
    std::set<uint64_t>      selectedKeys;
    uint64_t nextId = 1;

    float length = 0.f; // total arc length (world units)

    Curve2DKey* addKey(ImVec2 pos, int index = -1);
    Curve2DKey* findKey(uint64_t id);
    void removeKey(uint64_t id);
    int  keyIndex(uint64_t id) const;

    // insert a key on the nearest segment at the closest curve point
    Curve2DKey* insertKeyAt(ImVec2 nearPos);

    void   rebuild();                      // recompute arc-length tables
    ImVec2 pointOnSegment(int seg, float t) const;
    ImVec2 valueAtLength(float l) const;
    ImVec2 valueAtNorm(float u) const { return valueAtLength(u * length); }

    json keysToJson() const;
    void keysFromJson(const json& j);
    json save() const override;
    void load(const json& j) override;

    std::string inspectableTypeName() const override { return "Curve 2D"; }
    void inspectorGui() override;

private:
    // per segment: sampled cumulative lengths (N samples)
    std::vector<std::vector<float>> segTables;
    std::vector<float> segStartLen;
};

// Editor panel body (call inside a window). normPos: 0..1 position indicator
// drawn as a travelling dot (pass < 0 to hide).
void Curve2DEditor(Curve2D& c, float normPos = -1.f);

} // namespace organic
