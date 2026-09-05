/*
 * Arcaea excavation notebook
 * Section 04: LogicArcNote contact validity and body judgement
 *
 * STATUS: arc-contact / body-judgement slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native code/data/control flow.
 *   RECONSTRUCTED = readable representation assembled from confirmed behaviour.
 *   UNRESOLVED    = exact semantic name/purpose is not yet proved.
 *
 * Scope of this section:
 *   - how a normal arc becomes eligible for touch contact
 *   - how current arc contact is refreshed each gameplay update
 *   - how finger ownership/continuity constrains contact
 *   - how arc contact feeds the shared LogicLongNoteBase tick machinery
 *   - how trace/non-judged arc bodies differ from normal judged arcs
 *   - how release/re-entry affects still-pending arc-body events
 *
 * Deliberately out of scope:
 *   - the full arc easing/path interpolation formula
 *   - arc rendering meshes/textures
 *   - camera/timinggroup architecture beyond the minimum needed for hit testing
 *   - a complete ArcTap excavation
 *
 * This file builds directly on:
 *   02_note_fundamentals.cpp
 *   03_long_notes.cpp
 *
 * Useful native anchors from the investigated ARM64 build:
 *   common long-note judgement loop            ~0x0F8056C
 *   Arc no-contact predicate                   ~0x08D7788
 *   Arc per-update/contact reset               ~0x095D61C
 *   Arc touch hit-region helper                ~0x0927384
 *   Arc touch qualification predicate          ~0x0C98490
 *   Arc active-state setter                    ~0x0AD11B0
 *   Arc chart/runtime initialiser               ~0x0C664F8
 *   Arc touch/contact hook                     ~0x15DFE2C
 *   Arc touch-ownership tracker                ~0x15A90C4
 *   GameModel touch-begin arc gate             ~0x0EEC5E8
 *   GameModel active-touch refresh             ~0x1485C18
 *
 * WARNING ABOUT SYMBOLS:
 * The binary is stripped. Readable names below are reconstruction names unless
 * explicitly described as surviving RTTI/chart identifiers or CONFIRMED data.
 */

#include <algorithm>
#include <cstdint>

struct ScoreState;
struct TouchRecord;
struct GameModel;

// -----------------------------------------------------------------------------
// 1. Inheritance boundary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED RTTI:
 *
 *   LogicLongNoteBase
 *      |
 *      +-- LogicHoldNote
 *      +-- LogicArcNote
 *
 * CONFIRMED consequence from Section 03:
 * Both Hold and Arc use the same common long-note event vector and the same
 * successful/LOST event collectors. Their main gameplay difference before
 * ScoreState is how the note proves that contact is currently valid.
 */
struct LogicLongNoteBase {
    int startTimeMs;                 // established common field +0x18
    int endTimeMs;                   // established common field +0x1C

    uint8_t currentContact;          // readable reconstruction of +0x64
    uint8_t secondaryContactState;   // +0x65, exact separate meaning unresolved

    float tickIntervalMs;            // established common field around +0x70

    // Internal 12-byte long-event vector omitted here; see Section 03.
};

// -----------------------------------------------------------------------------
// 2. Selected LogicArcNote state
// -----------------------------------------------------------------------------

/*
 * CONFIRMED selected fields used by the contact path:
 *
 *   +0x64/+0x65 : transient contact state, cleared and re-established during
 *                  gameplay updates.
 *
 *   +0xA4       : integer mode propagated from the AFF arc argument after the
 *                  effect string. Runtime treats any non-zero value as a
 *                  non-touch-required / non-body-judged mode.
 *
 *   +0xB0       : pointer to a touch ownership/continuity tracker.
 *
 *   +0xD0       : byte refreshed by gameplay update. It is true while an
 *                  ordinary duration arc is currently active/eligible for the
 *                  normal per-frame contact test. "arcActive" is a readable
 *                  reconstructed name.
 *
 *   +0xD4/+0xD8 : cached current expected arc gameplay position used by the
 *                  touch qualification routine. Exact coordinate types are
 *                  deliberately abstracted here.
 *
 *   +0xE0       : optional arc state object used by extra continuity/timing
 *                  bookkeeping inside the contact hook. Exact semantics are
 *                  UNRESOLVED.
 *
 *   +0x120      : vector of attached LogicArcTapNote pointers / children.
 *
 *   +0x170      : byte which, when non-zero, bypasses the normal touch-owner
 *                  tracker gate. Exact higher-level reason is UNRESOLVED.
 */
struct ArcGameplayPoint {
    float horizontal;
    int32_t verticalOrQuantizedCoordinate;
};

struct ArcTouchTracker;

struct LogicArcNote : LogicLongNoteBase {
    int traceOrNonJudgedMode;      // +0xA4, semantic strongly tied to AFF trace slot
    ArcTouchTracker* tracker;      // +0xB0

    bool arcActive;                // +0xD0, reconstructed name
    ArcGameplayPoint expectedPoint;// +0xD4/+0xD8, readable abstraction

    void* arcContinuityState;      // +0xE0, UNRESOLVED
    bool bypassTrackerGate;        // +0x170, UNRESOLVED reason

    // Attached ArcTap vector and other geometry/render state omitted.
};

// -----------------------------------------------------------------------------
// 3. Normal arc body versus trace/non-judged arc body
// -----------------------------------------------------------------------------

/*
 * CONFIRMED chart/runtime mapping:
 *
 * The surviving AFF arc serializer writes the familiar arc(...) arguments.
 * The integer/boolean argument immediately after the effect string is carried
 * through the arc factory and stored at LogicArcNote +0xA4.
 *
 * Runtime code handles values 0, 1 and 2 in the construction path, so this file
 * deliberately does NOT model +0xA4 as a strict C++ bool even though classic AFF
 * syntax exposes a trace boolean.
 *
 * Gameplay behaviour is unambiguous:
 *
 *   +0xA4 == 0:
 *       - body touch qualification is allowed
 *       - no-contact long-event LOST processing is allowed
 *
 *   +0xA4 != 0:
 *       - arc-body touch qualification immediately rejects
 *       - Arc's no-contact virtual predicate returns false
 *       - therefore the body does not generate ordinary long-note LOST events
 *
 * This is the native basis for treating non-zero +0xA4 as trace/non-judged
 * arc-body mode.
 *
 * CONFIRMED wrinkle:
 * During initialisation, runtime can also promote +0xA4 from zero to non-zero
 * under a special vector/content condition. The exact semantic reason for that
 * automatic promotion is UNRESOLVED, so do not reduce this field to merely
 * "the literal chart trace token" in all circumstances.
 */
static bool arcBodyRequiresTouch(const LogicArcNote& arc)
{
    return arc.traceOrNonJudgedMode == 0;
}

// -----------------------------------------------------------------------------
// 4. Per-update arc state: current point and current activity
// -----------------------------------------------------------------------------

/*
 * CONFIRMED control flow:
 * A gameplay update routine computes the arc's current path-related state,
 * determines whether the ordinary arc is currently active within its playable
 * time interval, and calls a tiny setter equivalent to:
 *
 *   arc->+0xD0 = value & 1;
 *
 * The same update stores the current expected arc gameplay point at +0xD4/+0xD8
 * while the arc is in the relevant time range.
 *
 * The exact easing/path formula which produces that point is intentionally the
 * NEXT excavation target, not part of this section.
 */
void updateArcPathCache(LogicArcNote& arc, int effectiveTimeMs)
{
    // RECONSTRUCTED shape around confirmed behaviour.
    const bool timeInsideArc =
        effectiveTimeMs >= arc.startTimeMs &&
        effectiveTimeMs <= arc.endTimeMs;

    const bool pathStateIsUsable = computePathStateIsUsable(arc, effectiveTimeMs);

    arc.arcActive = timeInsideArc && pathStateIsUsable;

    if (effectiveTimeMs <= arc.endTimeMs) {
        arc.expectedPoint = computeArcExpectedPoint(arc, effectiveTimeMs);
    }
}

// -----------------------------------------------------------------------------
// 5. Arc contact is transient, like Hold contact
// -----------------------------------------------------------------------------

/*
 * CONFIRMED:
 * Arc's per-update virtual clears the halfword beginning at +0x64, therefore:
 *
 *   +0x64 = 0
 *   +0x65 = 0
 *
 * Qualified active touches later in the gameplay update must reassert these
 * bytes. Contact is therefore a frame/update-local latch, not a permanent
 * "this arc was once touched" flag.
 */
void clearArcContactForUpdate(LogicArcNote& arc)
{
    arc.currentContact = 0;
    arc.secondaryContactState = 0;
}

// -----------------------------------------------------------------------------
// 6. Touch-begin acquisition gate
// -----------------------------------------------------------------------------

/*
 * CONFIRMED:
 * GameModel dynamically identifies LogicArcNote candidates on touch-begin,
 * transforms the finger into gameplay-space coordinates, and calls the same
 * Arc qualification predicate used during normal active-touch refresh, but with
 * a context flag set to true.
 *
 * In this touch-begin context the effective time is advanced by 120 ms before
 * the arc-start comparison.
 *
 * Therefore the acquisition gate can recognise an arc up to roughly 120 ms
 * before its start through this branch.
 *
 * IMPORTANT:
 * The touch-begin routine is an acquisition/selection gate. The confirmed
 * per-frame current-contact latch is asserted by the active-touch refresh path
 * described below; do not collapse those two stages into one imaginary call.
 */
static constexpr int kArcTouchBeginLookaheadMs = 120;

// -----------------------------------------------------------------------------
// 7. Arc geometric contact predicate
// -----------------------------------------------------------------------------

/*
 * CONFIRMED high-level behaviour of the predicate around ~0x0C98490:
 *
 *   1. derive effective gameplay time
 *   2. in touch-begin context, allow +120 ms lookahead for start acquisition
 *   3. reject non-touch-required / trace-mode arc bodies (+0xA4 != 0)
 *   4. require the arc to be at a playable/active phase
 *   5. obtain the arc's expected gameplay position for this time
 *   6. transform the finger to the corresponding gameplay-space representation
 *   7. test the finger against a camera/screen-scaled hit region around the
 *      expected arc point
 *
 * For ordinary non-zero-duration arcs during active-touch refresh, +0xD0 is the
 * cached active gate used by this predicate.
 *
 * A start==end special case obtains its expected point differently; that edge
 * case is not needed to understand normal arc-following gameplay.
 */
bool geometricallyQualifiesArcTouch(
    GameModel& game,
    const TouchRecord& touch,
    LogicArcNote& arc,
    bool touchBeginContext)
{
    if (!arcBodyRequiresTouch(arc)) {
        return false;
    }

    int time = effectiveGameplayTime(game);

    if (touchBeginContext) {
        if (time + kArcTouchBeginLookaheadMs < arc.startTimeMs && !arc.arcActive) {
            return false;
        }
    } else {
        if (!arc.arcActive && arc.startTimeMs != arc.endTimeMs) {
            return false;
        }
    }

    const ArcGameplayPoint expected =
        (arc.startTimeMs == arc.endTimeMs)
            ? computeZeroDurationArcPoint(arc)
            : arc.expectedPoint;

    const auto fingerPoint = transformTouchToArcGameplaySpace(game, touch);

    return isInsideArcHitRegion(game, fingerPoint, expected);
}

// -----------------------------------------------------------------------------
// 8. The hit region is NOT a simple radius test
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from the helper around ~0x0927384:
 *
 * The contact helper performs camera/screen dependent coordinate conversion and
 * then checks separate horizontal and vertical bounds around the expected point,
 * plus a third coordinate/depth/front-plane condition.
 *
 * A float constant 212.0 is passed by the Arc predicate, but native arithmetic
 * scales and combines that value with other screen/game-space quantities.
 * Therefore the correct gameplay description is:
 *
 *   "camera/screen-scaled rectangular arc hit region"
 *
 * NOT:
 *
 *   distance(finger, arc) <= 212
 *
 * The exact human-friendly dimensions/configuration of the final box remain
 * UNRESOLVED and are not required for this gameplay slice.
 */
bool isInsideArcHitRegion(
    GameModel& game,
    /* transformed touch point */ auto finger,
    ArcGameplayPoint expected)
{
    // RECONSTRUCTED abstraction only.
    const auto bounds = buildCameraScaledArcBounds(game, expected, 212.0f);

    return finger.x >= bounds.left &&
           finger.x <= bounds.right &&
           finger.y >= bounds.bottom &&
           finger.y <= bounds.top &&
           finger.depth >= bounds.minimumDepth;
}

// -----------------------------------------------------------------------------
// 9. Geometry alone is not enough: touch ownership/continuity tracker
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from the tracker helper around ~0x15A90C4:
 *
 * The Arc-specific contact hook normally consults a tracker at +0xB0 after the
 * geometric predicate has succeeded.
 *
 * Inputs include:
 *   - touch identifier from TouchRecord +0x34
 *   - effective current time
 *   - integer tick interval
 *
 * Strongly supported tracker behaviour:
 *   - a field at +0x28 stores an assigned/owned touch ID
 *   - the same assigned touch ID has a direct acceptance path
 *   - an unassigned tracker can claim a touch ID
 *   - before a fresh claim, a global list of already-claimed touch IDs is
 *     consulted; an already-listed ID is not simply claimed again
 *   - successful fresh claim records the ID both in the tracker and the global
 *     ownership list
 *   - another tracker mode can bypass the exact-ID equality requirement
 *   - a time-bounded state uses:
 *
 *         min(4 * tickInterval, 1000 ms)
 *
 *     during ownership/continuity transitions
 *
 * The exact semantic names of the tracker's many byte/int flags are UNRESOLVED.
 * In particular, this section does NOT claim that the 4*tick/1000 interval is
 * formally named a "reacquisition window" by the game.
 */
struct ArcTouchTracker {
    int tickIntervalMs;        // selected observed state
    int assignedTouchId;       // RECONSTRUCTED name for +0x28

    // Numerous transition/ownership fields omitted.
};

bool trackerAcceptsArcTouch(
    ArcTouchTracker& tracker,
    int touchId,
    int currentTimeMs,
    int tickIntervalMs)
{
    // RECONSTRUCTED readable core from confirmed branches.
    tracker.tickIntervalMs = tickIntervalMs;

    updateTrackerTransitionTimers(
        tracker,
        currentTimeMs,
        std::min(4 * tickIntervalMs, 1000));

    if (trackerModeAcceptsAnyTouch(tracker)) {
        return true;
    }

    if (tracker.assignedTouchId == touchId) {
        markTrackerContactCurrent(tracker);
        return true;
    }

    if (tracker.assignedTouchId == -1) {
        if (globalArcTouchOwnershipContains(touchId)) {
            return handleAlreadyClaimedTouchTransition(tracker, touchId, currentTimeMs);
        }

        tracker.assignedTouchId = touchId;
        globalArcTouchOwnershipAdd(touchId);
        markTrackerContactCurrent(tracker);
        return true;
    }

    return handleDifferentTouchTransition(tracker, touchId, currentTimeMs);
}

// -----------------------------------------------------------------------------
// 10. Arc-specific qualified-contact hook
// -----------------------------------------------------------------------------

/*
 * CONFIRMED around ~0x15DFE2C:
 *
 * After GameModel has already established geometric validity, the Arc-specific
 * contact hook receives:
 *
 *   Arc*
 *   TouchRecord*
 *   effectiveCurrentTime
 *
 * Unless +0x170 is non-zero, it asks the ownership tracker to accept the touch.
 * If the tracker rejects, the hook returns WITHOUT setting +0x64.
 *
 * If contact survives that gate, additional optional bookkeeping through the
 * +0xE0 state object may adjust continuity/timing state.
 *
 * The successful tail writes the halfword 0x0101 at +0x64:
 *
 *   +0x64 = 1
 *   +0x65 = 1
 *
 * This is the exact hand-off into the common LogicLongNoteBase judgement path.
 */
bool acceptQualifiedArcContact(
    LogicArcNote& arc,
    const TouchRecord& touch,
    int currentTimeMs)
{
    if (!arc.bypassTrackerGate) {
        const int touchId = touchIdentifier(touch);

        if (!trackerAcceptsArcTouch(
                *arc.tracker,
                touchId,
                currentTimeMs,
                static_cast<int>(arc.tickIntervalMs))) {
            return false;
        }
    }

    updateOptionalArcContinuityState(arc, touch, currentTimeMs); // UNRESOLVED detail

    arc.currentContact = 1;
    arc.secondaryContactState = 1;
    return true;
}

// -----------------------------------------------------------------------------
// 11. GameModel active-touch refresh
// -----------------------------------------------------------------------------

/*
 * CONFIRMED architecture:
 *
 *     active touch
 *          |
 *          v
 *   dynamic_cast LogicArcNote
 *          |
 *          v
 *   arc geometric predicate
 *          |
 *          v
 *   Arc virtual +0x60 contact hook
 *          |
 *          v
 *   ownership/continuity tracker
 *          |
 *          v
 *   +0x64/+0x65 = 1
 *          |
 *          v
 *   common long-note judgement loop
 */
void refreshArcContactFromActiveTouches(
    GameModel& game,
    LogicArcNote& arc)
{
    for (const TouchRecord& touch : activeGameplayTouches(game)) {
        if (!geometricallyQualifiesArcTouch(game, touch, arc, false)) {
            continue;
        }

        acceptQualifiedArcContact(
            arc,
            touch,
            effectiveGameplayTime(game));
    }
}

// -----------------------------------------------------------------------------
// 12. Arc body reuses the common long-note success/LOST machinery
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from Sections 02/03 plus Arc-specific virtuals:
 *
 * Normal judged Arc (+0xA4 == 0):
 *
 *   currentContact != 0
 *       -> collect newly eligible long events through the common success path
 *       -> success horizon = now + 0.5 * tickInterval
 *       -> each score unit enters ScoreState as judgement 0
 *          (MAX PURE / shiny PURE class), timing side 0, payload -1
 *
 *   currentContact == 0
 *       -> Arc virtual +0x58 returns true
 *       -> collect sufficiently overdue events through the common LOST path
 *       -> ordinary LOST horizon =
 *
 *              now - min(2 * tickInterval, 500 ms)
 *
 *       -> each expired score unit enters ScoreState LOST
 *
 *   The special first-event +0x6C exception documented in Section 03 still
 *   exists and is intentionally not renamed here.
 *
 * Trace/non-judged Arc (+0xA4 != 0):
 *
 *   geometric touch qualification rejects
 *   Arc virtual +0x58 returns false
 *   therefore ordinary body tick LOST processing is also suppressed
 */
void processArcBodyJudgement(
    LogicArcNote& arc,
    ScoreState& score,
    int nowMs)
{
    if (!arcBodyRequiresTouch(arc)) {
        // RECONSTRUCTED concise effect of confirmed Arc virtual behaviour.
        return;
    }

    if (arc.currentContact) {
        const int units = collectSuccessfulLongEvents(
            arc,
            nowMs + static_cast<int>(0.5f * arc.tickIntervalMs));

        for (int i = 0; i < units; ++i) {
            registerSuccessfulJudgement(
                score,
                arc,
                /* judgement */ 0,
                /* timing side */ 0,
                nowMs,
                /* payload */ -1);
        }
    } else {
        const int graceMs = std::min(
            static_cast<int>(2.0f * arc.tickIntervalMs),
            500);

        const int units = collectExpiredLongEvents(
            arc,
            nowMs - graceMs);

        for (int i = 0; i < units; ++i) {
            registerLost(score, arc, nowMs);
        }
    }
}

// -----------------------------------------------------------------------------
// 13. Gameplay interpretation of release, leaving the path, and re-entry
// -----------------------------------------------------------------------------

/*
 * CONFIRMED mechanical sequence for a normal judged arc:
 *
 *     finger inside valid arc hit region
 *               + tracker accepts touch
 *                         |
 *                         v
 *                    +0x64 = 1
 *                         |
 *                         v
 *              eligible events succeed
 *
 * If the finger is released OR moves outside the accepted region:
 *
 *   - +0x64 is not refreshed for that update
 *   - pending long events are NOT instantly LOST merely because contact vanished
 *   - they age toward the common long-note LOST cutoff
 *
 * If the SAME tracked finger re-enters the valid region before a pending event
 * expires, the tracker has a direct same-ID acceptance path and contact can be
 * reasserted. The still-pending event can therefore succeed.
 *
 * Once an event has actually been marked processed as LOST, later contact does
 * not resurrect it; this is inherited from LogicLongNoteBase event processing.
 *
 * Different-finger reassignment is more constrained. The tracker has explicit
 * ownership/transition state and a global claimed-touch list. This section can
 * prove that reassignment machinery exists, but not yet reduce every transition
 * flag into a simple player-facing rule. Therefore:
 *
 *   same-finger re-entry before expiry = supported by confirmed control flow
 *   arbitrary instant finger swapping   = NOT claimed here
 */

// -----------------------------------------------------------------------------
// 14. ArcTaps remain separate point notes
// -----------------------------------------------------------------------------

/*
 * CONFIRMED:
 * LogicArcNote owns/iterates attached LogicArcTapNote children around +0x120.
 * LogicArcTapNote belongs to the LogicTapNote lineage, not LogicLongNoteBase.
 *
 * Consequently:
 *   - arc-body contact/ticks use the long-note machinery described above
 *   - ArcTaps retain separate point-note resolution/judgement behaviour
 *   - a trace/non-judged body does not imply its attached ArcTaps disappear from
 *     the point-note system
 *
 * ArcTap-specific input/position details are intentionally deferred.
 */

// -----------------------------------------------------------------------------
// 15. Compact reconstructed mental model
// -----------------------------------------------------------------------------

/*
 * Normal Arc body:
 *
 *   chart arc
 *      |
 *      +-- +0xA4 == 0 -------------------------------+
 *      |                                               |
 *      v                                               |
 *   gameplay update                                   |
 *      |                                               |
 *      +-- cache active state (+0xD0)                 |
 *      +-- cache expected path point (+0xD4/+0xD8)    |
 *      +-- clear transient contact (+0x64/+0x65)      |
 *      |                                               |
 *      v                                               |
 *   active touch transformed to gameplay space         |
 *      |                                               |
 *      v                                               |
 *   screen/camera-scaled arc hit-region test            |
 *      |                                               |
 *      v                                               |
 *   touch ownership/continuity tracker                  |
 *      |                                               |
 *      v                                               |
 *   +0x64/+0x65 = 1                                   |
 *      |                                               |
 *      v                                               |
 *   shared LogicLongNoteBase event processor            |
 *      |                                               |
 *      +-- contact -> judgement 0 successes            |
 *      +-- no contact -> overdue LOST events -----------+
 *
 * Trace/non-judged body:
 *
 *   +0xA4 != 0
 *      |
 *      +-- no body touch qualification
 *      +-- no ordinary body LOST tick processing
 *
 * Attached ArcTaps remain separate LogicTapNote-derived objects.
 */

// -----------------------------------------------------------------------------
// 16. CONFIRMED / RECONSTRUCTED / UNRESOLVED summary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED
 * ---------
 * - LogicArcNote derives from LogicLongNoteBase.
 * - Arc uses the same transient +0x64/+0x65 contact latch concept as Hold.
 * - +0x64/+0x65 are cleared and must be refreshed by qualified contact.
 * - touch-begin arc acquisition uses the Arc geometric predicate with +120 ms
 *   early lookahead.
 * - normal active-touch refresh uses the same predicate without that lookahead.
 * - the Arc predicate rejects +0xA4 != 0 bodies.
 * - +0xA4 comes from the AFF arc argument slot after effect, with runtime mode
 *   values beyond a strict bool and an additional automatic-promotion path.
 * - ordinary duration arcs use +0xD0 as an active gate during per-frame contact.
 * - gameplay update caches current expected path position at +0xD4/+0xD8.
 * - Arc contact uses a camera/screen-scaled box-like hit region, not a simple
 *   circular 212-unit distance check.
 * - Arc's contact hook normally asks a touch-ID ownership tracker for approval.
 * - same assigned touch ID has a direct acceptance path.
 * - a fresh unassigned tracker can claim an unowned touch ID and records it in
 *   a global claimed-touch list.
 * - successful Arc contact writes 0x0101 to +0x64/+0x65.
 * - normal Arc no-contact predicate enables common long-note LOST processing.
 * - non-zero +0xA4 suppresses that body LOST path.
 * - successful/LOST arc-body events enter ScoreState through the same long-note
 *   event machinery established in Section 03.
 * - attached ArcTaps are separate LogicTapNote-derived children.
 *
 * RECONSTRUCTED
 * -------------
 * - names such as arcActive, expectedPoint, assignedTouchId,
 *   traceOrNonJudgedMode, and ownership tracker.
 * - the compact helper/function boundaries written in this file.
 * - the player-facing interpretation that +0xA4 is trace/non-judged arc-body
 *   mode; the native behaviour is confirmed, while exact original member names
 *   are not recovered.
 *
 * UNRESOLVED
 * ----------
 * - the exact semantic names of +0x65, +0x68, +0x170 and the +0xE0 object.
 * - why/when +0x170 bypasses the ownership tracker.
 * - the full meaning of the tracker's transition flags.
 * - an exact simple rule for switching an already-owned arc to a different
 *   finger in every tracker state.
 * - the exact human-friendly dimensions/configuration of the arc hit region.
 * - the special start==end arc contact case beyond its confirmed branch.
 * - the previously observed +/-3000 ms effective-time correction semantics.
 * - the special initialisation condition that can auto-promote +0xA4.
 * - the complete easing/path interpolation formula that produces +0xD4/+0xD8.
 */

// -----------------------------------------------------------------------------
// 17. Next narrow excavation target
// -----------------------------------------------------------------------------

/*
 * NEXT:
 *   Minimum LogicArcNote path interpolation/geometry required to answer:
 *
 *     "Given arc(startX, endX, easing, startY, endY) and current chart time,
 *      how does gameplay compute the expected arc point cached at +0xD4/+0xD8?"
 *
 * Trace only that calculation and its easing modes.
 * Do NOT expand into RenderArcNote mesh construction, shaders, camera systems,
 * or the full timinggroup architecture unless the gameplay-space calculation
 * absolutely requires a small piece of them.
 */
