/*
 * Arcaea excavation notebook
 * Section 08: NotePosition, floor-lane geometry, and touch-to-lane mapping
 *
 * STATUS: floor-lane gameplay geometry slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native code/data/control flow.
 *   RECONSTRUCTED = readable representation assembled from confirmed behaviour.
 *   UNRESOLVED    = exact semantic name/purpose is not yet proved.
 *
 * Scope of this section:
 *   - surviving NotePosition RTTI and its two useful position modes
 *   - integer chart-lane -> internal lane-ID conversion
 *   - mirrored lane conversion
 *   - six reserved internal lane IDs and the normal four-lane subset
 *   - floor-lane centre spacing and touch boundaries
 *   - screen/touch -> gameplay-ground -> primary/adjacent lane mapping
 *   - how Tap and Hold consume those lane candidates
 *
 * Deliberately out of scope:
 *   - proving which chart command activates the two outer lanes
 *   - full enwidenlanes state machine / transition
 *   - full camera and screen-to-world derivation
 *   - rendering implementation beyond the small position consumer needed to
 *     prove lane centres
 *   - Flick input-rectangle construction
 *
 * This file builds directly on:
 *   02_note_fundamentals.cpp
 *   03_long_notes.cpp
 *   05_arc_path.cpp
 *   07_flick_notes.cpp
 *
 * Useful native anchors from the investigated ARM64 build:
 *   NotePosition RTTI string                         "12NotePosition"
 *   numeric value -> NotePosition converter          ~0x13A84EC
 *   discrete integer-lane NotePosition constructor   ~0x0DDD644
 *   floor-position consumer / lane centre conversion ~0x0F20CD4
 *   touch-begin floor-note candidate builder         ~0x0EEC024
 *   active-touch Hold refresh                        ~0x14858D4
 *   touch -> floor-lane candidate mapper             ~0x130733C
 *
 * WARNING ABOUT SYMBOLS:
 * The binary is stripped. Readable names below are reconstruction names unless
 * explicitly described as surviving RTTI/type/chart strings or confirmed field
 * behaviour.
 */

#include <cstdint>

// -----------------------------------------------------------------------------
// 1. NotePosition is the common horizontal-position abstraction
// -----------------------------------------------------------------------------

/*
 * CONFIRMED RTTI:
 *
 *   NotePosition : cocos2d::Ref
 *
 * A common LogicNote-side pointer around +0x20 is read by floor Tap/Hold input
 * code. Arc/path code also consumes NotePosition-like endpoint objects through
 * the same common position layer.
 *
 * This section therefore refines the earlier phrase "lane descriptor":
 * NotePosition is broader. It can represent either a discrete floor lane or a
 * free horizontal float position.
 *
 * Selected CONFIRMED fields:
 *
 *   +0x0C : position mode/kind integer
 *           observed values:
 *             0 -> free-float horizontal position
 *             1 -> discrete integer-lane position
 *             2 -> default/unset sentinel state
 *
 *   +0x10 : internal discrete lane ID when mode == 1
 *
 *   +0x14 : horizontal scalar
 *           - for discrete lanes it follows the half-step lane sequence
 *           - for free positions it stores the supplied/mirrored float
 *           - default/unset construction uses -100 sentinel behaviour
 *
 * The original enum/member names are unavailable.
 */
enum class NotePositionMode : int32_t {
    FreeFloat = 0,      // RECONSTRUCTED name
    DiscreteLane = 1,  // RECONSTRUCTED name
    Unset = 2,         // RECONSTRUCTED name
};

struct NotePosition {
    // cocos2d::Ref fields/vptr omitted.
    NotePositionMode mode; // +0x0C
    int32_t laneId;        // +0x10
    float horizontal;      // +0x14
};

// -----------------------------------------------------------------------------
// 2. Chart numeric values become NotePosition objects
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from one native converter using surviving RTTI/type names:
 *
 *   Num
 *    +-- IntValue
 *    +-- FloatValue
 *
 * IntValue -> discrete lane NotePosition.
 * FloatValue -> free-float NotePosition.
 *
 * A boolean gameplay modifier passed into this conversion horizontally mirrors
 * both forms. "mirror" is a RECONSTRUCTED semantic name based on the exact
 * reversal behaviour.
 */

/*
 * CONFIRMED discrete constructor behaviour for handled integer values 0..5.
 *
 * Without mirroring:
 *
 *   chart integer   internal lane ID   stored horizontal
 *        0                 1                -0.5
 *        1                 2                 0.0
 *        2                 3                 0.5
 *        3                 4                 1.0
 *        4                 5                 1.5
 *        5                 6                 2.0
 *
 * With horizontal mirroring the order reverses:
 *
 *   chart integer   internal lane ID
 *        0                 6
 *        1                 5
 *        2                 4
 *        3                 3
 *        4                 2
 *        5                 1
 *
 * The native switch explicitly contains cases 0..5. Values outside this range
 * are not needed for the normal gameplay model and are intentionally omitted.
 */
NotePosition makeDiscreteLanePosition(int chartLane, bool mirror)
{
    NotePosition out{};
    out.mode = NotePositionMode::DiscreteLane;

    // RECONSTRUCTED closed form of the confirmed native switch table.
    out.laneId = mirror
        ? (6 - chartLane)
        : (chartLane + 1);

    out.horizontal = (out.laneId - 2) * 0.5f;
    return out;
}

/*
 * CONFIRMED FloatValue conversion:
 *
 *   ordinary -> horizontal = value
 *   mirrored -> horizontal = 1.0 - value
 *
 * laneId is zero in this mode.
 *
 * This is one important bridge between floor-lane and Arc archaeology: the same
 * NotePosition abstraction can carry a discrete floor lane or a continuous Arc
 * endpoint-like horizontal value.
 */
NotePosition makeFreePosition(float value, bool mirror)
{
    NotePosition out{};
    out.mode = NotePositionMode::FreeFloat;
    out.laneId = 0;
    out.horizontal = mirror ? (1.0f - value) : value;
    return out;
}

// -----------------------------------------------------------------------------
// 3. Internal lane numbering reserves six positions
// -----------------------------------------------------------------------------

/*
 * CONFIRMED structural conclusion:
 *
 *   internal IDs 1..6 exist as ordinary discrete NotePosition values.
 *
 * Normal four-lane chart values 1..4 map to internal IDs 2..5.
 * Integer values 0 and 5 map to the two reserved outer IDs 1 and 6.
 *
 * Therefore the engine's internal lane numbering is deliberately wider than
 * the ordinary four-lane playfield:
 *
 *          extra      normal normal normal normal      extra
 *   ID       1           2      3      4      5           6
 *
 * This section proves the reservation and geometry. It deliberately does NOT
 * yet claim the higher-level chart/runtime command responsible for activating
 * IDs 1 and 6; that is the next excavation target.
 */

// -----------------------------------------------------------------------------
// 4. Native floor-lane centre spacing
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from a gameplay/render position consumer which branches on
 * NotePosition mode.
 *
 * For a valid discrete lane ID 1..6 it computes an integer horizontal position
 * equivalent to:
 *
 *   x = (laneId - 1) * 425 - 1063;
 *
 * giving:
 *
 *   lane ID      native integer centre
 *      1                -1063
 *      2                 -638
 *      3                 -213
 *      4                  212
 *      5                  637
 *      6                 1062
 *
 * The exact ideal half-step geometry is approximately:
 *
 *   -1062.5, -637.5, -212.5, +212.5, +637.5, +1062.5
 *
 * but the observed consumer uses integer arithmetic and the -1063 constant.
 * Do not silently replace the native integer result when exact reproduction is
 * required.
 *
 * Adjacent lane centres are therefore CONFIRMED to be 425 gameplay units apart.
 */
static int discreteLaneCentreX(int laneId)
{
    if (laneId < 1 || laneId > 6) {
        return 212; // observed fallback in this consumer; semantic not important
    }

    return (laneId - 1) * 425 - 1063;
}

// -----------------------------------------------------------------------------
// 5. Touches are projected into floor/gameplay space before lane selection
// -----------------------------------------------------------------------------

/*
 * CONFIRMED high-level lane-mapper order:
 *
 *   touch/input point
 *        |
 *        v
 *   screen/camera transform helpers
 *        |
 *        v
 *   ground/gameplay-space point
 *        |
 *        +-- horizontal coordinate -> lane classifier
 *        |
 *        +-- depth/front-plane validity checks
 *
 * The common path checks a depth-like coordinate against approximately
 * -500..+500 before accepting ordinary floor input. A special camera/projection
 * branch performs different checks including a 900-unit threshold.
 *
 * Full camera mathematics is intentionally deferred. The important result here
 * is that lane selection is NOT performed directly on raw screen X pixels.
 */
struct GroundTouchPoint {
    float horizontal;
    float depthLike;
};

// -----------------------------------------------------------------------------
// 6. Exact primary-lane thresholds
// -----------------------------------------------------------------------------

/*
 * CONFIRMED lane thresholds in transformed gameplay-space X:
 *
 *   -850, -425, 0, +425, +850
 *
 * The mapper first truncates the ordinary horizontal float toward zero to an
 * integer before its primary classification.
 *
 * A separate float field on the gameplay state, read around +0x74, controls
 * whether the two reserved outer lanes can be selected. The exact original
 * member name and its connection to a chart instruction are UNRESOLVED here.
 * "outerLanesEnabled" below is therefore RECONSTRUCTED naming.
 *
 * Ordinary behaviour:
 *
 *   x < -850             -> lane 1 only when outer lanes enabled
 *   x < -425             -> lane 2
 *   x < 0                -> lane 3
 *   x < +425             -> lane 4
 *   +425 < x <= +850     -> lane 5
 *   x > +850             -> lane 6 only when outer lanes enabled,
 *                            otherwise lane 5
 *
 * When outer lanes are disabled, far-left input similarly collapses into lane 2.
 *
 * CONFIRMED odd native seam:
 *   the primary integer classifier explicitly returns no lane for x == +425.
 * This asymmetry is preserved as evidence rather than "fixed" in pseudocode.
 */
static int classifyPrimaryLane(int x, bool outerLanesEnabled)
{
    if (outerLanesEnabled && x < -850) {
        return 1;
    }

    if (x < -425) {
        return 2;
    }

    if (x < 0) {
        return 3;
    }

    if (x < 425) {
        return 4;
    }

    if (x == 425) {
        return 0; // CONFIRMED native exact-equality seam
    }

    if (x <= 850) {
        return 5;
    }

    return outerLanesEnabled ? 6 : 5;
}

/*
 * Gameplay interpretation of the geometry:
 *
 * lane centres (approx):
 *
 *   -1062.5    -637.5    -212.5     212.5      637.5     1062.5
 *       1          2          3          4          5          6
 *          -850       -425        0         425        850
 *             ^          ^         ^          ^
 *             touch-classification boundaries
 *
 * The normal four lanes are IDs 2..5. The -850/+850 boundaries matter when the
 * outer IDs 1/6 are separately active; otherwise the mapper clamps horizontal
 * overflow into the normal edge lanes 2/5 rather than treating those values as
 * hard rejection edges.
 */

// -----------------------------------------------------------------------------
// 7. One touch can legitimately produce an adjacent lane candidate
// -----------------------------------------------------------------------------

/*
 * CONFIRMED return representation of the native lane mapper:
 *
 *   low  32 bits -> primary lane ID
 *   high 32 bits -> optional alternate/adjacent lane ID
 *
 * Tap and Hold matching accept either value.
 *
 * The alternate lane is generated only when a gameplay float around +0x60 is
 * greater than 1.0. Its exact native name is UNRESOLVED. Behaviour strongly
 * supports interpreting it as an effective horizontal touch-width scale.
 *
 * Native arithmetic:
 *
 *   halfExtraWidth = (widthScale - 1.0) * 425 * 0.5
 *
 * The mapper classifies horizontal - halfExtraWidth and
 * horizontal + halfExtraWidth. If one shifted classification differs from the
 * primary lane, that differing adjacent lane is returned as the alternate.
 * Otherwise alternate = 0.
 *
 * A surviving configuration string "touchSizeWidth" exists elsewhere in the
 * binary, but this section does NOT prove that it is the original name/source of
 * gameplay field +0x60. Keep that linkage RECONSTRUCTED.
 */
struct LaneCandidates {
    int primary;
    int adjacent; // 0 means none
};

LaneCandidates mapGroundTouchToLanes(
    float horizontal,
    bool outerLanesEnabled,
    float horizontalTouchWidthScale)
{
    LaneCandidates out{};

    out.primary = classifyPrimaryLane(
        static_cast<int>(horizontal),
        outerLanesEnabled);

    if (out.primary == 0 || horizontalTouchWidthScale <= 1.0f) {
        return out;
    }

    const float halfExtra =
        (horizontalTouchWidthScale - 1.0f) * 425.0f * 0.5f;

    const int leftCandidate =
        classifyHorizontalLikeNative(
            horizontal - halfExtra,
            outerLanesEnabled);

    const int rightCandidate =
        classifyHorizontalLikeNative(
            horizontal + halfExtra,
            outerLanesEnabled);

    if (leftCandidate != out.primary) {
        out.adjacent = leftCandidate;
    }
    else if (rightCandidate != out.primary) {
        out.adjacent = rightCandidate;
    }

    return out;
}

/*
 * The shifted classifier uses float comparisons internally rather than exactly
 * repeating the integer truncation of the primary classifier, so the helper
 * above is deliberately named "LikeNative" rather than pretending one generic
 * mathematical classifier reproduces every boundary bit-for-bit.
 */

// -----------------------------------------------------------------------------
// 8. Tap candidate selection uses lane IDs, not a sprite rectangle
// -----------------------------------------------------------------------------

/*
 * CONFIRMED touch-begin control flow:
 *
 *   1. transform touch into floor/gameplay space
 *   2. compute primary + optional adjacent lane IDs
 *   3. iterate active LogicNotes
 *   4. dynamic-cast ordinary floor point candidates to LogicTapNote
 *   5. read note +0x20 -> NotePosition
 *   6. read NotePosition +0x10 -> internal lane ID
 *   7. accept candidate if it equals primary OR adjacent touch lane
 *   8. later run the point timing routine from Section 02
 *
 * Therefore floor Tap spatial filtering is fundamentally lane-ID matching.
 */
static bool floorLaneMatchesTouch(
    const NotePosition& position,
    LaneCandidates touchLanes)
{
    if (position.mode != NotePositionMode::DiscreteLane) {
        return false;
    }

    return position.laneId == touchLanes.primary ||
           position.laneId == touchLanes.adjacent;
}

// -----------------------------------------------------------------------------
// 9. Hold uses the SAME lane mapper at acquisition and during contact refresh
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from both live paths:
 *
 * Touch-begin Hold acquisition:
 *   - compute the same LaneCandidates pair
 *   - compare Hold NotePosition lane ID against either candidate
 *   - apply the already-established Hold timing/engagement checks
 *
 * Active-touch Hold refresh:
 *   - transform each active touch again
 *   - call the SAME lane mapper
 *   - compare against the same Hold NotePosition lane ID
 *   - only matching contact re-latches the Hold's current-contact state
 *
 * Consequence:
 * Hold contact is tied to the current floor lane, not merely to the fact that
 * the original finger remains down somewhere on screen. Horizontal movement can
 * stop/re-establish qualified Hold contact through this shared lane mapping.
 */

// -----------------------------------------------------------------------------
// 10. Flick remains a separate spatial system
// -----------------------------------------------------------------------------

/*
 * Section 07 left Flick's +0x7C..+0x88 rectangle unresolved.
 *
 * This lane excavation did NOT find the live Flick gesture branch routing that
 * rectangle through the discrete floor-lane-ID mapper above. Flick continues to
 * use its own transformed-point AABB test before directional gesture evaluation.
 *
 * Therefore do not infer a Flick rectangle directly from the 425-unit lane width
 * without additional evidence.
 *
 * SECTION 07 EVIDENCE UPGRADE FOUND DURING THIS SLICE:
 * The runtime Flick factory around ~0x081D3E4 writes the one chart timestamp into
 * BOTH common LogicNote fields +0x18 and +0x1C.
 *
 * Thus the formerly reconstructed statement is now CONFIRMED:
 *
 *   LogicFlickNote.startTime == LogicFlickNote.endTime == chart Flick time
 *
 * Section 07's timing gates can therefore be understood as an actual one-shot
 * Flick window opening after time-200 and expiring after time+200.
 */

// -----------------------------------------------------------------------------
// 11. Compact gameplay model
// -----------------------------------------------------------------------------

/*
 * RECONSTRUCTED from confirmed pieces:
 *
 * screen touch
 *     |
 *     v
 * project to gameplay-ground coordinates
 *     |
 *     v
 * horizontal lane mapper
 *     |
 *     +--> primary internal lane
 *     |
 *     +--> optional adjacent lane from touch-width overlap
 *     |
 *     v
 * compare against note's discrete NotePosition lane ID
 *     |
 *     +-----------------------------+
 *     |                             |
 *     v                             v
 * LogicTapNote                  LogicHoldNote
 *     |                             |
 * point timing                    acquisition /
 * Section 02                     contact refresh
 *                                   Section 03
 */

// -----------------------------------------------------------------------------
// 12. CONFIRMED / RECONSTRUCTED / UNRESOLVED summary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED
 * ---------
 * - RTTI/type name NotePosition survives and derives from cocos2d::Ref.
 * - NotePosition supports free-float, discrete-lane, and unset/default states.
 * - Discrete lane IDs occupy internal values 1..6.
 * - Normal integer lanes 1..4 map to internal IDs 2..5.
 * - Integer positions 0 and 5 map to reserved outer IDs 1 and 6.
 * - A boolean conversion path reverses horizontal lane order.
 * - Adjacent lane centres are separated by 425 gameplay units.
 * - A native consumer places internal lanes near
 *     -1063, -638, -213, 212, 637, 1062.
 * - Touch lane thresholds are -850, -425, 0, 425, 850.
 * - A gameplay float gates whether outer IDs 1/6 are separately selectable.
 * - The lane mapper returns primary + optional adjacent lane IDs.
 * - Alternate-lane width arithmetic is (scale-1)*425*0.5.
 * - Tap touch-begin accepts either returned lane ID.
 * - Hold touch-begin and active-contact refresh use the same two-lane mapper.
 * - LogicFlickNote startTime and endTime are both written from its one chart
 *   timestamp by the runtime factory.
 *
 * RECONSTRUCTED
 * -------------
 * - NotePositionMode / laneId / horizontal member names.
 * - The boolean conversion parameter is named "mirror" from its behaviour.
 * - +0x60 is named horizontalTouchWidthScale from its lane-overlap arithmetic.
 * - +0x74 lane-mapper state is described as outerLanesEnabled.
 * - Ideal half-step centre values ending in .5 are mathematical descriptions;
 *   exact observed lane-centre consumer uses integer arithmetic.
 *
 * UNRESOLVED
 * ----------
 * - original names of NotePosition mode values and member fields.
 * - exact source/name of gameplay field +0x60; "touchSizeWidth" survives but
 *   direct identity is not yet proved.
 * - exact semantic/source of gameplay field +0x74.
 * - why the primary classifier contains the exact x==425 no-lane seam.
 * - complete screen/camera -> gameplay-ground transform.
 * - Flick input-rectangle construction.
 *
 * NEXT NARROW TARGET
 * ------------------
 * Trace chart command "enwidenlanes" into runtime state and compare the widened
 * state against this now-known baseline:
 *
 *   - which field activates outer lanes 1 and 6
 *   - transition timing / interpolation
 *   - whether visual track width and input geometry move together
 *   - note-position eligibility during transition
 *   - how the command restores ordinary four-lane play
 *
 * Stop once enwidenlanes gameplay behaviour is understandable; do not expand
 * into unrelated camera/effect architecture.
 */
