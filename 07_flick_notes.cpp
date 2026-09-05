/*
 * Arcaea excavation notebook
 * Section 07: LogicFlickNote input gesture and judgement
 *
 * STATUS: Flick gameplay slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native code/data/control flow.
 *   RECONSTRUCTED = readable representation assembled from confirmed behaviour.
 *   UNRESOLVED    = exact semantic name/purpose is not yet proved.
 *
 * Scope of this section:
 *   - whether LogicFlickNote is wired into live gameplay processing
 *   - chart-side FlickNote shape retained in the binary
 *   - Flick spatial qualification
 *   - Flick gesture recognition
 *   - Flick timing gates
 *   - successful judgement and automatic LOST flow into ScoreState
 *
 * Deliberately out of scope:
 *   - Flick rendering / RenderFlickNote
 *   - full gameplay-space / camera transform derivation
 *   - full lane geometry
 *   - whether currently shipped official charts instantiate FlickNote
 *
 * This file builds directly on:
 *   02_note_fundamentals.cpp
 *
 * Useful native anchors from the investigated ARM64 build:
 *   chart FlickNote parser/construction          ~0x0A3AB64 / 0x0A3AD38
 *   chart FlickNote serializer                   ~0x12088C8
 *   live active-touch Flick branch               ~0x1485CD4
 *   Flick gesture evaluator                      ~0x16E0548
 *   rectangle-contains helper                    ~0x08ADBE8
 *   common automatic-miss Flick branch           ~0x0F80798
 *   ScoreState successful judgement              ~0x1730290
 *   ScoreState LOST path                         ~0x0868E54
 *
 * WARNING ABOUT SYMBOLS:
 * The binary is stripped. Nearby exported/library labels printed by objdump are
 * frequently unrelated and are NOT used as identities here. Readable names in
 * this file are reconstruction names unless explicitly described as surviving
 * RTTI/chart strings or CONFIRMED field behaviour.
 */

#include <cmath>
#include <cstdint>

// -----------------------------------------------------------------------------
// 1. Runtime identity: Flick is a direct LogicNote subtype
// -----------------------------------------------------------------------------

/*
 * CONFIRMED surviving RTTI:
 *
 *   LogicNote
 *      |
 *      +-- LogicFlickNote
 *
 * This is separate from both existing gameplay families:
 *
 *   LogicTapNote      -> one-shot tap timing judgement
 *   LogicLongNoteBase -> repeated hold/arc body events
 *   LogicFlickNote    -> one-shot directional gesture judgement
 */
struct LogicNote;
struct LogicFlickNote;
struct ScoreState;

// -----------------------------------------------------------------------------
// 2. Flick processing is still wired into the gameplay engine
// -----------------------------------------------------------------------------

/*
 * CONFIRMED:
 *
 * The active-touch gameplay routine dynamically identifies LogicFlickNote and
 * executes a dedicated Flick branch.
 *
 * Separately, the common note-update / automatic-miss routine identifies the
 * same RTTI and executes dedicated Flick expiry logic.
 *
 * Therefore LogicFlickNote is not merely a stale parser class or an unreferenced
 * historical type in this build: executable gameplay paths still know how to
 * process it.
 *
 * UNRESOLVED / IMPORTANT LIMIT:
 * The stripped archive intentionally does not contain a useful collection of
 * chart .aff payloads, so this section does NOT prove that a current official
 * shipping chart actually instantiates FlickNote. It proves runtime capability.
 */

// -----------------------------------------------------------------------------
// 3. Chart-side FlickNote representation
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from the surviving chart serializer and parser-side construction:
 *
 * A chart FlickNote contains:
 *   - one integer timestamp
 *   - four floating-point values
 *
 * The serializer emits the surviving chart token:
 *
 *   "flick("
 *
 * and serialises those values in the familiar shape:
 *
 *   flick(time, floatA, floatB, floatC, floatD)
 *
 * AFF commonly describes the four floats as position X/Y and direction X/Y.
 * That semantic interpretation is strongly consistent with the runtime object,
 * but this excavation did not finish the exact chart-object -> runtime-object
 * member mapping, so the chart-side member names remain RECONSTRUCTED here.
 */
struct FlickChartRecord {
    int32_t timeMs;       // CONFIRMED single chart timestamp
    float positionX;      // RECONSTRUCTED semantic name
    float positionY;      // RECONSTRUCTED semantic name
    float directionX;     // RECONSTRUCTED semantic name
    float directionY;     // RECONSTRUCTED semantic name
};

/*
 * The runtime LogicNote model has common start/end fields.
 *
 * RECONSTRUCTED, strongly supported:
 * Because the chart FlickNote has only one timestamp and behaves as a one-shot
 * note, the readable model is:
 *
 *   startTime == endTime == chart Flick timestamp
 *
 * The exact runtime factory assignment was not proved in this slice, so this is
 * deliberately NOT promoted to CONFIRMED.
 */

// -----------------------------------------------------------------------------
// 4. Selected LogicFlickNote runtime state
// -----------------------------------------------------------------------------

struct Vec2 {
    float x;
    float y;
};

struct Rect {
    float x;
    float y;
    float width;
    float height;
};

/*
 * CONFIRMED by direct field use in the Flick gameplay path:
 *
 *   +0x74/+0x78 : 2D vector used as the required gesture direction
 *   +0x7C..+0x88: four-float axis-aligned input rectangle
 *   +0x8C       : tracked/assigned touch identifier, with -1 meaning unassigned
 *   +0x90/+0x94: remembered first qualifying position for that touch
 *
 * Original member names are unavailable, therefore the names below are
 * RECONSTRUCTED even though their runtime behaviour is confirmed.
 */
struct LogicFlickNote /* : LogicNote */ {
    // Common LogicNote state omitted; see 02_note_fundamentals.cpp.

    Vec2 requiredDirection;  // +0x74 / +0x78
    Rect inputRegion;        // +0x7C .. +0x88

    int32_t trackedTouchId;  // +0x8C, -1 = unassigned
    Vec2 initialTouchPoint;  // +0x90 / +0x94
};

// -----------------------------------------------------------------------------
// 5. Flick has a real spatial gate
// -----------------------------------------------------------------------------

/*
 * CONFIRMED call order in the active-touch Flick branch:
 *
 *   1. identify LogicFlickNote
 *   2. reject already-resolved note state through common virtual predicates
 *   3. check the Flick's early timing gate
 *   4. copy the four floats at Flick +0x7C into a temporary rectangle
 *   5. test the transformed gameplay/input-space touch point against that rect
 *   6. only if the point is inside, call the Flick gesture evaluator
 *
 * The rectangle helper is an ordinary inclusive axis-aligned contains test:
 */
static bool contains(const Rect& r, Vec2 p)
{
    return p.x >= r.x &&
           p.x <= r.x + r.width &&
           p.y >= r.y &&
           p.y <= r.y + r.height;
}

/*
 * Gameplay consequence:
 * A Flick is not recognised merely because the player swiped in the right
 * direction somewhere on screen. The currently transformed touch point must be
 * within the Flick's own gameplay-space input region before gesture evaluation.
 *
 * The exact derivation and friendly dimensions of this rectangle are deferred
 * to the later gameplay-space / lane-geometry excavation.
 */

// -----------------------------------------------------------------------------
// 6. Gesture recognition: cumulative directional displacement
// -----------------------------------------------------------------------------

static float length(Vec2 v)
{
    return std::sqrt(v.x*v.x + v.y*v.y);
}

static float dot(Vec2 a, Vec2 b)
{
    return a.x*b.x + a.y*b.y;
}

static constexpr float kPi = 3.14159265358979323846f;

/*
 * CONFIRMED native behaviour of the dedicated evaluator around ~0x16E0548.
 *
 * Inputs include:
 *   - LogicFlickNote*
 *   - touch identifier
 *   - current transformed 2D touch position
 *   - current/effective time argument
 *
 * The current-time argument is passed by the caller but is not materially used
 * in the direction/distance calculation itself.
 *
 * Touch ownership:
 *   - if +0x8C == -1, the first qualifying touch ID is stored
 *   - that touch's current position is stored at +0x90/+0x94
 *   - if +0x8C is already assigned to another ID, evaluation returns false
 *   - for the same ID, displacement is always measured from the remembered
 *     first qualifying position
 *
 * The remembered position is NOT advanced every update. Therefore this is
 * cumulative displacement, not instantaneous movement or swipe velocity.
 *
 * Success requirements for ordinary finite non-zero vectors:
 *
 *   displacementLength > 106.0
 *   angularError        < 45 degrees
 *
 * Both boundaries are strict:
 *   exactly 106 units is insufficient
 *   exactly 45 degrees is insufficient
 */
static bool evaluateFlickGesture(
    LogicFlickNote& flick,
    int touchId,
    Vec2 currentPoint)
{
    if (flick.trackedTouchId == -1) {
        flick.trackedTouchId = touchId;
        flick.initialTouchPoint = currentPoint;
    }
    else if (flick.trackedTouchId != touchId) {
        return false;
    }

    const Vec2 displacement {
        currentPoint.x - flick.initialTouchPoint.x,
        currentPoint.y - flick.initialTouchPoint.y,
    };

    const float displacementLength = length(displacement);
    const float directionLength = length(flick.requiredDirection);

    const float cosine =
        dot(flick.requiredDirection, displacement) /
        (directionLength * displacementLength);

    const float angleDegrees =
        std::acos(cosine) * 180.0f / kPi;

    if (angleDegrees >= 45.0f) {
        return false;
    }

    if (displacementLength <= 106.0f) {
        return false;
    }

    return true;
}

/*
 * Gameplay interpretation:
 *
 *            required direction
 *                   ----->
 *              < 45 deg
 *                 /
 *       start  o----------------o current touch
 *                    > 106
 *
 * The first qualifying point arms/anchors the gesture. The note succeeds only
 * after that same touch ID has moved sufficiently far in approximately the
 * configured direction.
 *
 * This is NOT a velocity test. No minimum px/ms or units/ms threshold was found.
 */

// -----------------------------------------------------------------------------
// 7. Timing gates are Flick-specific, not tap PURE/FAR windows
// -----------------------------------------------------------------------------

/*
 * CONFIRMED active-touch gate:
 *
 * Gesture processing is skipped while:
 *
 *   effectiveGameplayTime <= startTime - 200 ms
 *
 * Therefore the Flick branch becomes eligible only once:
 *
 *   effectiveGameplayTime > startTime - 200 ms
 *
 * CONFIRMED automatic expiry gate:
 *
 * An unresolved Flick is sent to ScoreState's LOST path only once:
 *
 *   effectiveGameplayTime > endTime + 200 ms
 *
 * The late comparison is also strict.
 *
 * These are independent confirmed runtime gates. If the strongly reconstructed
 * point-note model startTime==endTime==chartTime is correct, they form an
 * approximately +/-200 ms gesture opportunity around the chart timestamp.
 *
 * IMPORTANT:
 * Do not replace this mechanic with LogicTapNote's +/-25/50/100/120 ms timing
 * table. Flick does not use that point-tap grading routine.
 */
static bool flickGestureProcessingIsOpen(
    int effectiveGameplayTime,
    int startTime)
{
    return effectiveGameplayTime > startTime - 200;
}

static bool flickHasExpired(
    int effectiveGameplayTime,
    int endTime)
{
    return effectiveGameplayTime > endTime + 200;
}

// -----------------------------------------------------------------------------
// 8. Successful Flick always enters ScoreState as judgement 0
// -----------------------------------------------------------------------------

/*
 * CONFIRMED after evaluateFlickGesture() returns true:
 *
 * The active-touch gameplay routine calls the established ScoreState successful
 * judgement entry with arguments equivalent to:
 *
 *   note        = Flick
 *   judgement   = 0
 *   timingSide  = 0
 *   eventTime   = current effective gameplay time
 *   inputPayload= -1
 *
 * Section 02 established judgement 0 as MAX PURE.
 *
 * Therefore a recognised Flick does not receive MAX PURE/PURE/FAR according to
 * when the gesture finishes. A valid gesture is reported as judgement 0.
 */
void registerSuccessfulFlick(
    ScoreState& score,
    LogicFlickNote& flick,
    int effectiveGameplayTime)
{
    // RECONSTRUCTED call shape around the confirmed ScoreState invocation.
    registerSuccessfulJudgement(
        score,
        flick,
        /* Judgement::MaxPure */ 0,
        /* timing side */        0,
        effectiveGameplayTime,
        /* input payload */     -1);
}

/*
 * Consequence inherited from established ScoreState/LifeBarState behaviour:
 *   - successful Flick increments the judgement-0 / MAX PURE accounting
 *   - every active LifeBarState receives a normal successful judgement
 *   - ordinary successful gauge gain is therefore the same +RF path as other
 *     judgement-0 successes, subject to already-established modifiers
 */

// -----------------------------------------------------------------------------
// 9. Automatic Flick LOST uses the ordinary ScoreState miss path
// -----------------------------------------------------------------------------

/*
 * CONFIRMED common gameplay-update behaviour:
 *
 * After identifying LogicFlickNote and rejecting already-resolved notes, the
 * scheduler compares effective gameplay time against Flick endTime + 200.
 * Once strictly later, it calls the established ScoreState LOST entry.
 */
void expireFlickIfNecessary(
    ScoreState& score,
    LogicFlickNote& flick,
    int effectiveGameplayTime,
    int endTime)
{
    if (flickAlreadyResolved(flick)) {
        return;
    }

    if (effectiveGameplayTime > endTime + 200) {
        registerLost(score, flick, effectiveGameplayTime);
    }
}

/*
 * CONFIRMED architectural consequence:
 * LogicFlickNote is NOT a LogicLongNoteBase subtype.
 * Therefore LifeBarState does not classify its LOST as one of the repeated
 * long-note events whose gauge damage is halved.
 *
 * Flick LOST follows the ordinary one-shot note LOST path.
 */

// -----------------------------------------------------------------------------
// 10. Full readable Flick gameplay model
// -----------------------------------------------------------------------------

/*
 * RECONSTRUCTED from the confirmed control flow above.
 */
bool processFlickTouch(
    ScoreState& score,
    LogicFlickNote& flick,
    int startTime,
    int effectiveGameplayTime,
    int touchId,
    Vec2 transformedTouchPoint)
{
    if (flickAlreadyResolved(flick)) {
        return false;
    }

    if (effectiveGameplayTime <= startTime - 200) {
        return false;
    }

    if (!contains(flick.inputRegion, transformedTouchPoint)) {
        return false;
    }

    if (!evaluateFlickGesture(
            flick,
            touchId,
            transformedTouchPoint)) {
        return false;
    }

    registerSuccessfulFlick(
        score,
        flick,
        effectiveGameplayTime);

    return true;
}

/*
 * Compact mental model:
 *
 *   transformed touch enters Flick region
 *                  |
 *                  v
 *       first matching touch claims Flick
 *       and establishes anchor position
 *                  |
 *                  v
 *      cumulative displacement from anchor
 *                  |
 *           +------+------+
 *           |             |
 *       <=106 or       >106 units
 *       >=45 deg       and <45 deg
 *           |             |
 *           v             v
 *         wait       judgement 0
 *                       MAX PURE
 *
 * If unresolved past endTime + 200 ms:
 *
 *                  LOST
 */

// -----------------------------------------------------------------------------
// 11. CONFIRMED / RECONSTRUCTED / UNRESOLVED summary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED
 * ---------
 * - LogicFlickNote RTTI exists as a direct LogicNote subtype.
 * - Dedicated Flick code is wired into both active-touch and auto-miss gameplay
 *   loops in this build.
 * - Chart FlickNote stores one integer time and four floats and serialises with
 *   the surviving "flick(" token.
 * - Runtime +0x74/+0x78 is used as the required 2D gesture direction.
 * - Runtime +0x7C..+0x88 is used as a four-float AABB input region.
 * - Runtime +0x8C stores a tracked touch ID with -1 as the unassigned sentinel.
 * - Runtime +0x90/+0x94 stores the initial qualifying position for that ID.
 * - Another touch ID is rejected after assignment inside the gesture evaluator.
 * - Gesture displacement is cumulative from the first qualifying point.
 * - Success requires displacement >106 and angular error <45 degrees.
 * - No velocity threshold is present in the gesture evaluator.
 * - Gesture processing begins only after time > startTime-200.
 * - Automatic LOST occurs only after time > endTime+200.
 * - Successful gesture calls ScoreState with judgement=0, timingSide=0,
 *   payload=-1.
 * - Flick LOST uses the ordinary ScoreState LOST path, not long-note tick loss.
 *
 * RECONSTRUCTED
 * -------------
 * - chart float semantic names positionX/Y and directionX/Y.
 * - startTime == endTime == the single chart Flick timestamp.
 * - friendly names requiredDirection, inputRegion, trackedTouchId,
 *   initialTouchPoint.
 * - one-shot resolved behaviour is represented here with the common
 *   flickAlreadyResolved() abstraction rather than claiming original member
 *   names for its successful/lost flags.
 *
 * UNRESOLVED
 * ----------
 * - exact chart-float -> runtime-member assignment in the runtime Flick factory.
 * - exact formula deriving +0x7C..+0x88 from chart position/gameplay geometry.
 * - whether another input-lifecycle path resets +0x8C after a finger is lifted;
 *   the gesture evaluator itself never switches to a different assigned ID.
 * - exact Flick-specific successful/lost virtual-hook implementation, if any,
 *   beyond the confirmed ScoreState call sites and resolved-state gating.
 * - whether any current official chart in this stripped archive/build actually
 *   instantiates FlickNote. Runtime support is confirmed; chart usage is not.
 */

// -----------------------------------------------------------------------------
// 12. Next excavation boundary
// -----------------------------------------------------------------------------

/*
 * The runtime note family is now sufficiently mapped for gameplay purposes:
 *
 *   Tap     -> lane/point input -> timing grade -> ScoreState
 *   ArcTap  -> Arc path position -> point timing grade -> ScoreState
 *   Hold    -> lane contact -> repeated long events -> ScoreState
 *   Arc     -> path/contact tracking -> repeated long events -> ScoreState
 *   Flick   -> spatial region + directional displacement -> judgement 0
 *
 * The next useful excavation is therefore ground/lane gameplay geometry:
 *   - lane identifiers
 *   - touch-to-lane mapping
 *   - exact ground-lane coordinates/boundaries
 *   - how Tap and Hold spatial filtering consume that state
 *
 * That gives us a stable base before investigating `enwidenlane`.
 */
