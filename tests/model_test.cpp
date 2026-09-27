// Headless model-level tests for the organic library (no GUI loop; an ImGui
// context exists for clipboard/time helpers).
#include "Organic.h"
#include "imgui.h"
#include <cstdio>
#include <cmath>

using namespace organic;

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

static void testCoreParams()
{
    Container c("Test Container");
    Parameter* f = c.addFloat("My Float", 0.5f, 0.f, 1.f);
    CHECK(f->shortName == "myFloat");
    CHECK(f->controlAddress() == "/testContainer/myFloat");

    f->setUndoable(0.8f);
    CHECK(std::fabs(f->floatValue() - 0.8f) < 1e-6);
    UndoManager::get().undo();
    CHECK(std::fabs(f->floatValue() - 0.5f) < 1e-6);
    UndoManager::get().redo();
    CHECK(std::fabs(f->floatValue() - 0.8f) < 1e-6);

    json j = c.save();
    Container c2("Test Container");
    c2.addFloat("My Float", 0.5f, 0.f, 1.f);
    c2.load(j);
    CHECK(std::fabs(c2.getParam("myFloat")->floatValue() - 0.8f) < 1e-6);

    // address resolution through roots
    registerRoot(&c);
    CHECK(resolveParamAddress("/testContainer/myFloat") == f);
    CHECK(resolveContainerAddress("/testContainer") == &c);
    unregisterRoot(&c);
    UndoManager::get().clear();
}

static void testSelectionScopes()
{
    Container a("A"), b("B");
    Selection::get().set(&a);
    CHECK(a.isSelected());
    Selection& other = Selection::scope("panel2");
    other.set(&b);
    CHECK(b.isSelected());
    CHECK(Selection::active() == &other);
    // preselection
    other.setPreselection({ &a });
    CHECK(a.isPreselected());
    other.commitPreselection();
    CHECK(other.contains(&a) && other.contains(&b));
    // links
    linkInspectables(&a, &b);
    Selection::get().clear();
    other.clear();
    Selection::get().set(&b);
    CHECK(a.isHighlighted());
    Selection::get().clear();
}

static void testSequenceStructure()
{
    Sequence seq("Seq");
    auto* la = static_cast<ClipLayer*>(seq.addLayer(Layer::LType::Clips, "A"));
    seq.addLayer(Layer::LType::Clips, "B");
    Clip* clip = la->addClip(Clip::CType::Block, "C1", 1.0, 2.0);
    uint64_t cid = clip->id;
    CHECK(seq.findClip(cid) == clip);

    json snap = seq.save();
    Sequence seq2("Seq2");
    seq2.load(snap);
    CHECK(seq2.layers.size() == 2);
    CHECK(seq2.findClip(cid) != nullptr);
}

static void testAutomation()
{
    Sequence seq("Seq");
    auto* al = static_cast<AutomationLayer*>(seq.addLayer(Layer::LType::Automation, "Auto"));
    al->addKey(0.0, 0.f, EasingType::Linear);
    al->addKey(2.0, 1.f, EasingType::Linear);
    CHECK(std::fabs(al->valueAt(1.0) - 0.5f) < 1e-4);

    // insertKeyAt preserves position on a linear segment
    AutoKey* mid = al->insertKeyAt(1.0);
    CHECK(mid && std::fabs(mid->value - 0.5f) < 1e-3);
    CHECK(al->keys.size() == 3);

    // bezier split: curve shape must be preserved
    al->keys.clear();
    al->addKey(0.0, 0.f, EasingType::Bezier);
    al->addKey(4.0, 1.f, EasingType::Linear);
    float atSplit = al->valueAt(1.5);
    float at07 = al->valueAt(0.7);
    float at30 = al->valueAt(3.0);
    al->insertKeyAt(1.5);
    CHECK(std::fabs(al->valueAt(1.5) - atSplit) < 5e-3);
    CHECK(std::fabs(al->valueAt(0.7) - at07) < 2e-2);
    CHECK(std::fabs(al->valueAt(3.0) - at30) < 2e-2);
}

static void testClipAutomations()
{
    // Timeline v2: automations live INSIDE the effect block, clip-local
    Sequence seq("Seq");
    auto* cl = static_cast<ClipLayer*>(seq.addLayer(Layer::LType::Clips, "FX"));
    Clip* c = cl->addClip(Clip::CType::Block, "Wave", 2.0, 4.0);

    ClipAutomation* a = c->addAutomation(ClipAutomation::AKind::Curve, "rate", "1:rate");
    a->rangeMin = 0.f;
    a->rangeMax = 2.f;
    a->addKey(0.0, 0.f);
    a->addKey(4.0, 2.f);
    CHECK(std::fabs(a->valueAt(2.0) - 1.f) < 1e-4);          // clip-local eval
    CHECK(c->findAutomationByTarget("1:rate") == a);

    ClipAutomation* g = c->addAutomation(ClipAutomation::AKind::Gradient, "color", "2:color");
    g->addGradKey(0.0, ImVec4(1, 0, 0, 1));
    g->addGradKey(4.0, ImVec4(0, 1, 0, 1));
    ImVec4 midC = g->colorAt(2.0);
    CHECK(std::fabs(midC.x - 0.5f) < 1e-3 && std::fabs(midC.y - 0.5f) < 1e-3);

    // moving the block costs nothing: locals are clip-relative by design
    c->startP->setValue(6.f, false);
    CHECK(std::fabs(a->valueAt(2.0) - 1.f) < 1e-4);

    // keys can never outlive the block
    a->addKey(9.0, 1.f);
    c->clampAutomations();
    CHECK(a->keys.size() == 2);

    // serialization: automations ride the clip json
    json snap = seq.save();
    Sequence seq2("Seq2");
    seq2.load(snap);
    Clip* c2 = seq2.findClip(c->id);
    CHECK(c2 && c2->automations.size() == 2);
    CHECK(c2->findAutomationByTarget("1:rate") != nullptr);
    CHECK(std::fabs(c2->findAutomationByTarget("1:rate")->valueAt(2.0) - 1.f) < 1e-4);

    // pencil / drawn points: RDP-simplified keys replace the swept range
    std::vector<std::pair<double, float>> stroke;
    for (int i = 0; i <= 40; i++) stroke.push_back({ i * 0.1, (i % 2) ? 1.f : 0.9f });
    a->applyDrawnPoints(stroke, 1, 0.05f);
    CHECK(a->keys.size() >= 2);
    CHECK(std::fabs(a->valueAt(2.0) - 0.95f) < 0.1f);

    // host-fed recorder
    ClipAutomation* r = c->addAutomation(ClipAutomation::AKind::Curve, "amount");
    r->recArm = true;
    for (int i = 0; i <= 20; i++) r->updateRecording(i * 0.1, i / 20.f);
    r->stopRecordingAndApply();
    CHECK(r->keys.size() >= 2);
    CHECK(std::fabs(r->valueAt(2.0) - 1.f) < 0.1f);

    // clearing the block's automation empties every row for redrawing but
    // keeps the rows (identity, target, range, expansion) — unlike removal
    CHECK(c->hasAutomationKeys());
    const size_t rows = c->automations.size();
    const uint64_t rateId = a->id;
    a->rangeMax = 2.f;
    a->expanded = true;
    a->selectedKeys.insert(a->keys.front().id);
    json pre = a->keysToJson();
    CHECK(c->clearAutomationKeys() == 3);       // rate, color, amount all had keys
    CHECK(!c->hasAutomationKeys());
    CHECK(c->automations.size() == rows);
    CHECK(c->findAutomation(rateId) == a && c->findAutomationByTarget("1:rate") == a);
    CHECK(a->keys.empty() && a->selectedKeys.empty() && !a->hasKeys());
    CHECK(g->gkeys.empty() && !g->hasKeys());
    CHECK(std::fabs(a->rangeMax - 2.f) < 1e-6 && a->expanded && a->target == "1:rate");
    CHECK(c->clearAutomationKeys() == 0);       // idempotent: nothing left to clear
    CHECK(!a->clearKeys());
    // the undo record is the row's keys json: restoring it brings back the
    // same keys (ids included), and the row can be redrawn from empty
    a->keysFromJson(pre);
    CHECK(a->keysToJson() == pre);
    CHECK(a->clearKeys());
    a->applyDrawnPoints(stroke, 1, 0.05f);
    CHECK(a->keys.size() >= 2 && c->hasAutomationKeys());
    // a take in flight is discarded by the clear (it would otherwise
    // re-populate the row when it stops); the arm state is kept. The
    // return value reports removed KEYS only — a dropped take is not an
    // undo-worthy change.
    r->recArm = true;
    for (int i = 0; i <= 10; i++) r->updateRecording(i * 0.1, 0.5f);
    CHECK(r->recording && r->recPoints.size() >= 2);
    CHECK(!r->clearKeys());                     // the block clear above left no key
    CHECK(!r->recording && r->recPoints.empty() && r->recArm && r->keys.empty());
    r->addKey(0.0, 0.1f);
    for (int i = 0; i <= 10; i++) r->updateRecording(i * 0.1, 0.5f);
    CHECK(r->recording && r->recPoints.size() >= 2);
    CHECK(r->clearKeys());                      // this time a key went
    CHECK(!r->recording && r->recPoints.empty() && r->recArm && r->keys.empty());
    r->updateRecording(1.2, 0.25f);             // armed: a fresh take starts here
    CHECK(r->recording && r->recPoints.size() == 1 && r->recPoints.front().first == 1.2);
    r->stopRecordingAndApply();                 // one point is not a take
    CHECK(r->keys.empty() && !r->recArm);
}

static void testClipOverlapRules()
{
    // two blocks on one track must not overlap in time
    Sequence seq("Seq");
    auto* cl = static_cast<ClipLayer*>(seq.addLayer(Layer::LType::Clips, "FX"));
    cl->addClip(Clip::CType::Block, "A", 2.0, 4.0); // 2..6
    CHECK(cl->spanFree(0.0, 2.0));
    CHECK(!cl->spanFree(1.0, 3.0));
    CHECK(cl->spanFree(6.0, 8.0));
    CHECK(std::fabs(cl->resolveOverlap(3.0, 2.0) - 6.0) < 1e-6 ||
          std::fabs(cl->resolveOverlap(3.0, 2.0) - 0.0) < 1e-6); // flush seat
    CHECK(std::fabs(cl->resolveOverlap(7.0, 2.0) - 7.0) < 1e-6); // free spot kept
    // between two blocks, the gap that fits wins
    cl->addClip(Clip::CType::Block, "B", 8.0, 4.0); // 8..12
    double seat = cl->resolveOverlap(5.0, 2.0);
    CHECK(std::fabs(seat - 6.0) < 1e-6); // the 6..8 gap
}

static void testRangeRemap()
{
    Sequence seq("Seq");
    auto* al = static_cast<AutomationLayer*>(seq.addLayer(Layer::LType::Automation, "Auto"));
    al->addKey(0.0, 0.f);
    al->addKey(1.0, 1.f);
    al->rangeRemapP->setValue(1); // proportional
    al->rangeMaxP->setValue(10.f);
    CHECK(std::fabs(al->keys[1].value - 10.f) < 1e-4);
}

static void testTriggersAndCues()
{
    Sequence seq("Seq");
    auto* tl = static_cast<TriggerLayer*>(seq.addLayer(Layer::LType::Triggers, "Trig"));
    TimeTrigger* t = tl->addTrigger(1.0, "Go");
    int fired = 0;
    tl->onTriggered = [&](TriggerLayer&, TimeTrigger&) { fired++; };
    seq.play();
    seq.update(0.5);  // 0 -> 0.5
    CHECK(fired == 0);
    seq.update(0.8);  // 0.5 -> 1.3, crosses 1.0
    CHECK(fired == 1);
    CHECK(t->fired);
    seq.setTime(0);   // reset
    CHECK(!t->fired);

    // cues
    seq.addCue(2.0, "A");
    seq.addCue(5.0, "B");
    CHECK(std::fabs(seq.nextCueTime(0.0, 1) - 2.0) < 1e-9);
    CHECK(std::fabs(seq.nextCueTime(3.0, 1) - 5.0) < 1e-9);
    CHECK(std::fabs(seq.nextCueTime(3.0, -1) - 2.0) < 1e-9);
}

static void testPlayModes()
{
    Sequence seq("Seq");
    seq.lengthP->setValue(4.f, false);
    seq.loopIn = 1.0;
    seq.loopOut = 3.0;
    seq.playModeP->setValue(2); // ping-pong
    seq.setTime(2.9);
    seq.play();
    seq.update(0.2); // 2.9 -> 3.1 -> reflect to 2.9, dir -1
    CHECK(seq.direction == -1);
    CHECK(seq.currentTime <= 3.0);
    seq.setTime(1.05);
    seq.update(0.2); // going down past 1.0 -> reflect up
    CHECK(seq.direction == 1);
}

static void testRippleEdits()
{
    Sequence seq("Seq");
    auto* cl = static_cast<ClipLayer*>(seq.addLayer(Layer::LType::Clips, "C"));
    auto* al = static_cast<AutomationLayer*>(seq.addLayer(Layer::LType::Automation, "A"));
    cl->addClip(Clip::CType::Block, "c1", 1.0, 2.0);
    cl->addClip(Clip::CType::Block, "c2", 6.0, 2.0);
    al->addKey(1.0, 0.f);
    al->addKey(7.0, 1.f);
    seq.addCue(6.5);

    seq.insertTime(4.0, 2.0);
    CHECK(std::fabs(cl->clips[0]->start() - 1.0) < 1e-6); // before insert point: unchanged
    CHECK(std::fabs(cl->clips[1]->start() - 8.0) < 1e-6); // shifted
    CHECK(std::fabs(al->keys[1].time - 9.0) < 1e-6);
    CHECK(std::fabs(seq.cues[0].time - 8.5) < 1e-6);

    seq.removeTimespan(4.0, 6.0);
    CHECK(std::fabs(cl->clips[1]->start() - 6.0) < 1e-6);
    CHECK(std::fabs(al->keys[1].time - 7.0) < 1e-6);
}

static void testSimplification()
{
    // RDP: a straight line should reduce to its endpoints
    std::vector<ImVec2> pts;
    for (int i = 0; i <= 100; i++) pts.push_back(ImVec2(i / 100.f, i / 100.f));
    std::vector<int> keep;
    simplifyRDP(pts, 0.01f, keep);
    CHECK(keep.size() == 2);

    // bezier fit of a sine arc: few segments, low error
    pts.clear();
    for (int i = 0; i <= 200; i++)
    {
        float x = i / 200.f;
        pts.push_back(ImVec2(x, 0.5f + 0.4f * std::sin(x * 6.28318f)));
    }
    std::vector<FittedCubic> cubics;
    fitCubicBeziers(pts, 0.01f, cubics);
    CHECK(!cubics.empty() && cubics.size() < 24);
    CHECK(std::fabs(cubics.front().p0.x - 0.f) < 1e-4);
    CHECK(std::fabs(cubics.back().p3.x - 1.f) < 1e-4);
}

static void testRecorder()
{
    Container src("Source");
    Parameter* sp = src.addFloat("Sig", 0.f, 0.f, 1.f);
    registerRoot(&src);

    Sequence seq("Seq");
    auto* al = static_cast<AutomationLayer*>(seq.addLayer(Layer::LType::Automation, "Rec"));
    al->recSourceP->setValue(sp->controlAddress(), false);
    al->recArmP->setValue(true, false);
    al->recSimplifyP->setValue(1, false); // RDP

    seq.play();
    for (int i = 0; i < 100; i++)
    {
        sp->setValue(i / 100.f, false);
        seq.update(0.02);
    }
    CHECK(al->recording);
    al->stopRecordingAndApply();
    CHECK(!al->recording);
    CHECK(al->keys.size() >= 2);
    // ramp 0..~1 over ~2s: check midpoint approximately half
    float v = al->valueAt(al->keys.front().time + (al->keys.back().time - al->keys.front().time) * 0.5);
    CHECK(v > 0.2f && v < 0.8f);
    unregisterRoot(&src);
    UndoManager::get().clear();
}

static void testManagerFramework()
{
    BaseManager m("Things");
    m.addDef("Basic/Thing", "Thing", [] { return std::make_unique<BaseItem>("Thing", "Thing"); });

    BaseItem* a = m.undoableAdd("Thing");
    CHECK(a && m.items.size() == 1);
    UndoManager::get().undo();
    CHECK(m.items.empty());
    UndoManager::get().redo();
    CHECK(m.items.size() == 1);
    a = m.items[0].get();

    m.undoableDuplicate({ a });
    CHECK(m.items.size() == 2);
    UndoManager::get().undo();
    CHECK(m.items.size() == 1);

    // reorder
    BaseItem* b = m.undoableAdd("Thing");
    (void)b;
    uint64_t firstUid = m.items[0]->uid;
    m.undoableMove(0, 1);
    CHECK(m.items[1]->uid == firstUid);
    UndoManager::get().undo();
    CHECK(m.items[0]->uid == firstUid);

    // save / load round trip
    json j = m.save();
    BaseManager m2("Things");
    m2.addDef("Basic/Thing", "Thing", [] { return std::make_unique<BaseItem>("Thing", "Thing"); });
    m2.load(j);
    CHECK(m2.items.size() == m.items.size());
    UndoManager::get().clear();
}

static void testCurve2D()
{
    Curve2D c("Path");
    c.addKey(ImVec2(0, 0));
    c.addKey(ImVec2(1, 0));
    c.addKey(ImVec2(1, 1));
    CHECK(std::fabs(c.length - 2.f) < 1e-3);
    ImVec2 p = c.valueAtNorm(0.25f);
    CHECK(std::fabs(p.x - 0.5f) < 1e-2 && std::fabs(p.y) < 1e-2);
    p = c.valueAtNorm(0.75f);
    CHECK(std::fabs(p.x - 1.f) < 1e-2 && std::fabs(p.y - 0.5f) < 1e-2);

    // insert preserves shape reasonably on a straight segment
    c.insertKeyAt(ImVec2(0.5f, 0.02f));
    CHECK(c.keys.size() == 4);
    CHECK(std::fabs(c.length - 2.f) < 5e-2);

    json j = c.save();
    Curve2D c2("Path2");
    c2.load(j);
    CHECK(c2.keys.size() == c.keys.size());
    CHECK(std::fabs(c2.length - c.length) < 1e-4);
}

static void testSequenceManager()
{
    SequenceManager sm;
    Sequence* s1 = sm.addSequence("One");
    sm.addSequence("Two");
    CHECK(sm.sequences.size() == 2);
    CHECK(sm.findByUid(s1->managerUid) == s1);
    json j = sm.save();
    SequenceManager sm2;
    sm2.load(j);
    CHECK(sm2.sequences.size() == 2);
    CHECK(sm2.sequences[0]->niceName == "One");
}

static void testGradientHold()
{
    Sequence seq("Seq");
    auto* gl = static_cast<GradientLayer*>(seq.addLayer(Layer::LType::Gradient, "G"));
    uint64_t aid = gl->addKey(0.0, ImVec4(0, 0, 0, 1))->id;
    gl->addKey(2.0, ImVec4(1, 1, 1, 1));
    gl->findKey(aid)->hold = true; // re-find: addKey may reallocate the vector
    ImVec4 mid = gl->colorAt(1.0);
    CHECK(mid.x < 0.01f); // held, no interpolation
}

static void testKeySelections()
{
    // ONE selection at a time: key selections live in per-row sets (embedded
    // automations of every block) and per-layer sets (classic lanes). The
    // editor sweeps them all on every plain click that is not a key, and
    // Delete removes keys before clips whenever any key is selected — both
    // ride these two model helpers.
    Sequence seq("Seq");
    auto* cl = static_cast<ClipLayer*>(seq.addLayer(Layer::LType::Clips, "FX"));
    Clip* a = cl->addClip(Clip::CType::Block, "A", 0.0, 4.0);
    Clip* b = cl->addClip(Clip::CType::Block, "B", 4.0, 4.0);
    ClipAutomation* ra = a->addAutomation(ClipAutomation::AKind::Curve, "rate");
    ClipAutomation* rb = b->addAutomation(ClipAutomation::AKind::Gradient, "color");
    auto* al = static_cast<AutomationLayer*>(seq.addLayer(Layer::LType::Automation, "Lane"));
    auto* gl = static_cast<GradientLayer*>(seq.addLayer(Layer::LType::Gradient, "Grad"));
    auto* tl = static_cast<TriggerLayer*>(seq.addLayer(Layer::LType::Triggers, "Trig"));
    const uint64_t ka = ra->addKey(1.0, 0.5f)->id;
    const uint64_t kb = rb->addGradKey(1.0, ImVec4(1, 0, 0, 1))->id;
    const uint64_t kl = al->addKey(1.0, 0.5f)->id;
    const uint64_t kg = gl->addKey(1.0, ImVec4(0, 1, 0, 1))->id;
    const uint64_t kt = tl->addTrigger(1.0, "Go")->id;
    CHECK(!seq.anyKeySelected());

    // every holder is seen on its own ...
    ra->selectedKeys.insert(ka);
    CHECK(seq.anyKeySelected());
    seq.clearKeySelections();
    CHECK(!seq.anyKeySelected() && ra->selectedKeys.empty());
    rb->selectedKeys.insert(kb);
    CHECK(seq.anyKeySelected());
    seq.clearKeySelections();
    al->selectedKeys.insert(kl);
    CHECK(seq.anyKeySelected());
    seq.clearKeySelections();
    gl->selectedKeys.insert(kg);
    CHECK(seq.anyKeySelected());
    seq.clearKeySelections();
    tl->selectedKeys.insert(kt);
    CHECK(seq.anyKeySelected());

    // ... and one sweep drops them all at once, keys untouched
    ra->selectedKeys.insert(ka);
    rb->selectedKeys.insert(kb);
    al->selectedKeys.insert(kl);
    gl->selectedKeys.insert(kg);
    seq.clearKeySelections();
    CHECK(!seq.anyKeySelected());
    CHECK(ra->selectedKeys.empty() && rb->selectedKeys.empty() && al->selectedKeys.empty() &&
          gl->selectedKeys.empty() && tl->selectedKeys.empty());
    CHECK(ra->keys.size() == 1 && rb->gkeys.size() == 1 && al->keys.size() == 1 &&
          gl->keys.size() == 1 && tl->triggers.size() == 1);
    // a selected clip is not a key selection
    a->select();
    CHECK(a->isSelected() && !seq.anyKeySelected());
    Selection::get().clear();
}

static void testWav()
{
    AudioBuffer buf = makeTone(0.5f, 440.f);
    CHECK(saveWavPcm16("/tmp/organic_t.wav", buf));
    AudioBuffer in;
    CHECK(loadWav("/tmp/organic_t.wav", in));
    Peaks pk;
    pk.build(in);
    float mn, mx;
    CHECK(pk.query(0.1, 0.2, mn, mx));
    CHECK(mx > 0.1f && mn < -0.1f);
}

int main()
{
    ImGui::CreateContext();

    testCoreParams();
    testSelectionScopes();
    testSequenceStructure();
    testClipAutomations();
    testClipOverlapRules();
    testKeySelections();
    testAutomation();
    testRangeRemap();
    testTriggersAndCues();
    testPlayModes();
    testRippleEdits();
    testSimplification();
    testRecorder();
    testManagerFramework();
    testCurve2D();
    testSequenceManager();
    testGradientHold();
    testWav();

    printf(fails == 0 ? "ALL TESTS PASSED\n" : "%d FAILURES\n", fails);
    ImGui::DestroyContext();
    return fails == 0 ? 0 : 1;
}
