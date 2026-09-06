/*
 * Arcaea excavation notebook
 * Section 18: Flick runtime disconnection in the investigated build
 *
 * STATUS: Flick runtime-instantiation refinement complete for this binary
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native code/data/control flow.
 *   RECONSTRUCTED = readable representation assembled from confirmed behaviour.
 *   UNRESOLVED    = exact semantic name/purpose is not yet proved.
 *
 * Scope of this section:
 *   - refine Section 07's Flick result from "handler capability survives" to the
 *     stronger build-specific conclusion that runtime Flick construction is no
 *     longer linked into this target binary
 *   - prove the distinction using RTTI/vtable relocation structure
 *   - audit every LogicFlickNote RTTI code reference in .text
 *   - distinguish RenderFlickNote construction from LogicFlickNote construction
 *   - bound the surviving touch-ID lifecycle code
 *   - explain why the historical chart-float -> runtime AABB formula cannot be
 *     recovered from this build without inventing missing code
 *
 * Deliberately out of scope:
 *   - claiming Flick never existed or never worked in other Arcaea builds
 *   - guessing the historical +0x7C..+0x88 AABB constructor formula
 *   - guessing historical finger-release semantics from absent code
 *
 * This file builds directly on:
 *   07_flick_notes.cpp
 *   11_gameplay_space.cpp
 *   15_rendering_fundamentals.cpp
 *
 * Investigated binary:
 *   libcocos2dcpp.so SHA-256
 *   3eaca4e6dabb3395f276f8915698d57675757d0df0970e716b23a3dc201c79be
 *
 * Useful native anchors:
 *   LogicFlickNote RTTI name                           rodata ~0x47AE34
 *   LogicFlickNote typeinfo object                    ~0x1A89CA0
 *   sole external relocation to that typeinfo         GOT ~0x1B964D0
 *   Flick gesture evaluator                           ~0x16E0548
 *   evaluator's only caller                           ~0x1485E8C
 *   active-touch LogicFlickNote RTTI cast             ~0x1485CD4
 *   automatic-expiry LogicFlickNote test              ~0x0F80678
 *   RenderFlickNote factory-side Flick type test      ~0x0AE6E20
 *
 * WARNING ABOUT SYMBOLS:
 * The binary is stripped and nearby exported-library symbol names printed by
 * objdump are often unrelated to these game functions. Addresses are evidence
 * anchors only.
 */

#include <cstdint>

// -----------------------------------------------------------------------------
// 1. Section 07 proved surviving Flick HANDLERS, not live instantiation
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from Section 07 and rechecked here:
 *
 * Surviving code still knows how a LogicFlickNote WOULD behave if one were
 * present in the runtime note collection:
 *
 *   - active-touch processing can dynamic_cast to LogicFlickNote
 *   - +0x7C..+0x88 is consumed as an axis-aligned input rectangle
 *   - +0x8C is a tracked touch ID, with -1 meaning unassigned
 *   - +0x90/+0x94 stores the first qualifying touch point
 *   - required displacement is >106 gameplay units
 *   - required angular error is <45 degrees
 *   - successful recognition enters ScoreState as judgement 0
 *   - unresolved Flicks have a dedicated late-expiry path
 *   - RenderFlickNote construction/update support also survives
 *
 * Those are genuine executable handlers.
 *
 * What Section 07 did NOT prove was that this build can still construct a
 * most-derived LogicFlickNote object from a parsed chart FlickNote.
 * This section resolves that missing architectural question.
 */

// -----------------------------------------------------------------------------
// 2. Chart FlickNote still exists as a normal instantiated chart-side class
// -----------------------------------------------------------------------------

/*
 * CONFIRMED:
 * The chart-side FlickNote parser/serializer survives and its class has ordinary
 * RTTI/vtable structure in this binary.
 *
 * Selected chart payload established earlier:
 *
 *   timestamp
 *   float0
 *   float1
 *   float2
 *   float3
 *
 * Therefore the parser side is not merely a dead string literal.
 * A real chart FlickNote object can still be represented by this build.
 */
struct ChartFlickNote {
    int32_t timeMs;
    float value0;
    float value1;
    float value2;
    float value3;
};

// -----------------------------------------------------------------------------
// 3. LogicFlickNote RTTI survives, but its class vtable does not
// -----------------------------------------------------------------------------

/*
 * CONFIRMED Itanium C++ ABI evidence.
 *
 * LogicFlickNote typeinfo object:
 *
 *     ~0x1A89CA0
 *
 * It is __si_class_type_info and names the ordinary LogicNote base.
 *
 * Exhaustive relocation audit for references to ~0x1A89CA0 finds exactly one
 * external pointer:
 *
 *     ~0x1B964D0 -> ~0x1A89CA0
 *
 * and ~0x1B964D0 lies in .got.
 *
 * There is NO relocation from .data.rel.ro placing this typeinfo into a class
 * vtable.
 *
 * Contrast with instantiated sibling runtime note classes:
 *
 *   LogicTapNote:
 *       typeinfo referenced from a .data.rel.ro vtable slot + GOT
 *
 *   LogicHoldNote:
 *       typeinfo referenced from a .data.rel.ro vtable slot + GOT
 *
 *   LogicArcNote:
 *       typeinfo referenced from a .data.rel.ro vtable slot + GOT
 *
 *   LogicArcTapNote:
 *       typeinfo referenced from a .data.rel.ro vtable slot + GOT
 *
 *   LogicFlickNote:
 *       GOT reference only
 *
 * A most-derived polymorphic LogicFlickNote object cannot use LogicNote's base
 * vtable and still satisfy dynamic_cast<LogicFlickNote*>. Its object vptr needs
 * a vtable whose RTTI entry identifies LogicFlickNote.
 *
 * Therefore the absence of any LogicFlickNote vtable is direct binary evidence
 * that this target build does not instantiate LogicFlickNote objects.
 *
 * This is stronger than "constructor not found".
 */

// -----------------------------------------------------------------------------
// 4. Every LogicFlickNote RTTI code reference is a downstream consumer
// -----------------------------------------------------------------------------

/*
 * CONFIRMED by decoding every exact .text reference to the sole Flick typeinfo
 * GOT slot. There are only six:
 *
 *   ~0x0AE6E20
 *   ~0x0B166AC
 *   ~0x0E66004
 *   ~0x0F80678
 *   ~0x11EA14C
 *   ~0x1485CD4
 *
 * Their roles are all type tests / downstream handling of an object that would
 * already have to exist.
 *
 * Most importantly, ~0x0AE6E20 can initially look constructor-like because it
 * allocates roughly 0x2D0 bytes after identifying a Flick logic object.
 * Following the branch proves the allocation is RenderFlickNote, matching
 * Section 15's renderer size and LogicNote -> RenderNote factory architecture.
 * It is NOT LogicFlickNote construction.
 *
 * The other references participate in gameplay scans, expiry/type filtering and
 * active-touch handling. None allocates or installs a LogicFlickNote vptr.
 *
 * Thus the complete RTTI-reference set has consumers, but no producer.
 */

// -----------------------------------------------------------------------------
// 5. The ordinary chart -> runtime note factory also has no Flick branch
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from the already-investigated chart/runtime construction family:
 * ordinary runtime promotion has explicit routes for the live note/control
 * families used by the game, including Simple/Tap, Hold, Arc, SceneControl and
 * related objects.
 *
 * Parsed chart FlickNote is not promoted through that switch.
 *
 * A dedicated secondary producer was then searched for using the stronger RTTI
 * evidence above. Because no LogicFlickNote class vtable exists, no valid hidden
 * producer can construct a polymorphic LogicFlickNote elsewhere in this binary.
 */

// -----------------------------------------------------------------------------
// 6. Consequence: the historical runtime AABB constructor is absent here
// -----------------------------------------------------------------------------

struct Rect {
    float x;
    float y;
    float width;
    float height;
};

struct LogicFlickNoteSurvivingLayout {
    float requiredDirectionX; // +0x74
    float requiredDirectionY; // +0x78
    Rect inputRegion;         // +0x7C..+0x88
    int32_t trackedTouchId;   // +0x8C
    float anchorX;            // +0x90
    float anchorY;            // +0x94
};

/*
 * CONFIRMED surviving consumer behaviour:
 * The active-touch branch copies +0x7C..+0x88 into a temporary Rect and runs an
 * ordinary inclusive rectangle containment helper before gesture evaluation.
 *
 * What is NOT present in this build is the producer which would convert chart
 * Flick values/gameplay geometry into those four runtime floats.
 *
 * Therefore this mapping remains intentionally UNRESOLVED:
 *
 *     Chart FlickNote four floats
 *            |
 *            X   producer absent in this binary
 *            |
 *            v
 *     LogicFlickNote +0x7C..+0x88
 *
 * Guessing the AABB from lane/Arc hit constants would be reconstruction without
 * native support and is explicitly rejected.
 */

// -----------------------------------------------------------------------------
// 7. Surviving touch-ID evaluator is one-way ownership code
// -----------------------------------------------------------------------------

/*
 * CONFIRMED evaluator at ~0x16E0548:
 */
bool survivingFlickOwnershipGate(
    LogicFlickNoteSurvivingLayout& flick,
    int32_t touchId,
    float touchX,
    float touchY)
{
    if (flick.trackedTouchId == -1) {
        flick.trackedTouchId = touchId;
        flick.anchorX = touchX;
        flick.anchorY = touchY;
    }
    else if (flick.trackedTouchId != touchId) {
        return false;
    }

    // Direction/distance test follows; see Section 07.
    return evaluateSurvivingFlickDisplacement(flick, touchX, touchY);
}

/*
 * CONFIRMED call-graph result:
 * This evaluator has exactly one caller in the binary, the active-touch Flick
 * branch around ~0x1485E8C.
 *
 * The evaluator never writes -1 back to +0x8C.
 * It only performs:
 *
 *   unassigned -> claim first qualifying touch
 *   same ID    -> continue
 *   other ID   -> reject
 */

// -----------------------------------------------------------------------------
// 8. No surviving Flick-specific touch-release/reset path is present
// -----------------------------------------------------------------------------

/*
 * CONFIRMED bounded evidence:
 *
 * - every LogicFlickNote RTTI code reference has been audited; none belongs to
 *   common touch-end/release handling
 *
 * - the known Arc touch-release helper resets ArcTouchTracker +0x28, not Flick
 *   +0x8C
 *
 * - a direct scan for reset-shaped stores of -1 to an object +0x8C finds several
 *   candidates, but the actual functions are FreeType FT_Stroker_* internals;
 *   they are unrelated structures which happen to share the same offset
 *
 * - the positively identified gameplay-side Flick write to +0x8C is the active
 *   evaluator's touch-ID CLAIM
 *
 * Historical release behaviour in a build which truly instantiated Flicks is
 * still UNRESOLVED. This binary cannot answer that question because it has no
 * live LogicFlickNote construction path.
 */

// -----------------------------------------------------------------------------
// 9. Correct architectural interpretation
// -----------------------------------------------------------------------------

/*
 * CONFIRMED for this exact investigated build:
 *
 *     chart syntax / chart class
 *               FlickNote
 *                   |
 *                   | parser + serializer survive
 *                   v
 *            chart-side object
 *                   |
 *                   X  no surviving runtime promotion
 *                   |
 *                   v
 *          LogicFlickNote instance
 *                   X  cannot be produced
 *
 * Meanwhile orphaned downstream machinery survives:
 *
 *     LogicFlickNote RTTI
 *     active-touch Flick branch
 *     gesture evaluator
 *     automatic expiry branch
 *     RenderFlickNote factory/update support
 *
 * Readable description:
 * Flick is a partially decommissioned runtime feature in this binary. Parser and
 * downstream handler archaeology remains, but the bridge which would create a
 * live LogicFlickNote has been removed/disconnected.
 *
 * `partially decommissioned` is RECONSTRUCTED wording for the confirmed binary
 * architecture. It is NOT a claim about developer intent or other versions.
 */

// -----------------------------------------------------------------------------
// 10. CONFIRMED / RECONSTRUCTED / UNRESOLVED summary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED
 * ---------
 * - chart FlickNote parser/serializer/class representation survives
 * - LogicFlickNote RTTI survives and identifies LogicNote as its base
 * - LogicFlickNote typeinfo has only a GOT relocation and no class-vtable RTTI
 *   relocation in this target binary
 * - instantiated sibling Logic note classes do have ordinary class vtables
 * - all six exact code references to Flick RTTI are downstream consumers
 * - the apparent ~0x2D0 allocation is RenderFlickNote, not LogicFlickNote
 * - the ordinary chart/runtime factory has no Flick promotion branch
 * - the gesture evaluator has one caller: active-touch processing
 * - evaluator ownership only claims/retains one touch ID and never resets it
 * - no Flick-specific release/reset RTTI path survives
 *
 * RECONSTRUCTED
 * -------------
 * - describing Flick as "partially decommissioned" in this build
 * - readable field/function names used above
 *
 * UNRESOLVED
 * ----------
 * - exact historical chart-float -> LogicFlickNote AABB construction formula
 * - historical touch-release/reset semantics when Flick instantiation existed
 * - which Arcaea version/build removed or disconnected runtime Flick creation
 * - whether another version still constructs Flicks
 *
 * These unresolved historical questions do not block the build-specific result:
 * this investigated binary contains no viable producer for LogicFlickNote.
 */
