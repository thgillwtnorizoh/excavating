/*
 * Arcaea excavation notebook
 * Section 12: LogicArcNote touch ownership, connected-Arc continuity, and
 *             contact-state refinements
 *
 * STATUS: Arc contact refinement slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native code/data/control flow.
 *   RECONSTRUCTED = readable representation assembled from confirmed behaviour.
 *   UNRESOLVED    = exact semantic name/purpose is not yet proved.
 *
 * Scope of this section:
 *   - exact Arc touch-tracker claim / reject / release flow
 *   - meaning of the min(4*tickInterval,1000 ms) timer
 *   - global exclusion of already-claimed touch IDs
 *   - temporary nearby-Arc ownership override and its 500 ms lifetime
 *   - the special LogicArcNote +0x170 tracker bypass
 *   - confirmation that LogicArcNote +0xE0 is LogicArcGroup*
 *   - how connected Arc segments are grouped
 *   - continuation flag +0xA0
 *   - direction-change seam flag +0x6C and its relationship to the special
 *     first-event expiry rule established in Section 03
 *   - per-frame ArcGroup contact propagation
 *
 * Deliberately out of scope:
 *   - exact user-facing semantic names of tracker modes
 *   - exact meanings of every LogicArcGroup byte/float
 *   - Arc rendering / group presentation
 *   - full meaning of the feature which enables LogicArcNote +0x170
 *   - the remaining sampled-path refinements from Section 05
 *
 * This file builds directly on:
 *   03_long_notes.cpp
 *   04_arc_contact.cpp
 *   05_arc_path.cpp
 *   11_gameplay_space.cpp
 *
 * Useful native anchors from the investigated ARM64 build:
 *   touch-tracker per-frame update                 ~0x1482CB0
 *   touch-tracker acceptance                      ~0x15A90C4
 *   touch-tracker release                         ~0x13DB7A0
 *   Arc contact hook                              ~0x15DFE2C
 *   LogicArcGroup getter                          ~0x12418D8
 *   LogicArcNote::setArcGroup-like setter         ~0x165286C
 *   connected-Arc group builder                   ~0x125E304
 *   connected-Arc direction seam test             ~0x125E724
 *   nearby-Arc ownership override                 ~0x19F42D8
 *   +0x170 runtime-factory branches                ~0x18651D4 / ~0x18654DC
 *   LogicArcNote runtime initialiser               ~0x0C664F8
 *
 * WARNING ABOUT SYMBOLS:
 * The binary is stripped. Readable names below are reconstruction names unless
 * explicitly described as surviving RTTI/type strings or confirmed behaviour.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// -----------------------------------------------------------------------------
// 1. Contact has four distinct layers
// -----------------------------------------------------------------------------

/*
 * Sections 04 and 11 established the first and last pieces of Arc contact:
 *
 *   touch
 *     -> camera/gameplay-space geometric qualification
 *     -> Arc touch-ownership tracker
 *     -> Arc +0x64/+0x65 current-contact latch
 *     -> common LogicLongNoteBase success/LOST event processing
 *
 * Section 12 proves that the tracker itself is not a judgement grace system.
 * There are separate mechanisms for:
 *
 *   1. geometric qualification
 *   2. touch-ID ownership / exclusivity
 *   3. re-acquisition lockout after release
 *   4. long-note event LOST grace
 *
 * In particular:
 *
 *   tracker release lockout = min(4*tickInterval, 1000 ms)
 *   long-event LOST grace   = min(2*tickInterval,  500 ms)
 *
 * They are independent timers with different jobs.
 */

// -----------------------------------------------------------------------------
// 2. Selected Arc touch-tracker state
// -----------------------------------------------------------------------------

/*
 * CONFIRMED field behaviour from the tracker helper family.
 * Original type/member names are unavailable.
 */
struct ArcTouchTracker {
    float tickIntervalMs;           // +0x0C, refreshed by acceptance calls

    bool proximityOwnershipBypass;  // +0x10, temporary exact-ID relaxation
    int32_t proximityRefreshTimeMs; // +0x14, -1 when inactive

    int32_t mode;                   // +0x18, exact enum semantics UNRESOLVED

    bool acceptedThisUpdate;        // +0x22, reset every tracker update
    bool hasNormalOwnershipHistory; // +0x24, set on fresh normal claim

    int32_t assignedTouchId;        // +0x28, -1 = unassigned
    int32_t releaseLockoutStartMs;  // +0x2C, -1 = inactive

    int32_t transitionState;        // +0x30, exact semantic UNRESOLVED
    float transitionRemainingMs;    // +0x34, derived countdown or -1
    int32_t transitionStartMs;      // +0x38
};

static int ownershipTimeoutMs(float tickIntervalMs)
{
    // CONFIRMED arithmetic.
    return static_cast<int>(
        std::min(4.0f * tickIntervalMs, 1000.0f));
}

// -----------------------------------------------------------------------------
// 3. Global touch-ID exclusivity
// -----------------------------------------------------------------------------

/*
 * CONFIRMED:
 * A process/global vector of claimed touch IDs is consulted by Arc trackers.
 * A tracker may freshly claim a touch only when that ID is not already present.
 * On ordinary tracker release, the ID is removed from this global vector.
 *
 * This means touch ownership is not merely local to one Arc. Normal Arc trackers
 * coordinate through a shared claimed-ID set so one touch is not independently
 * freshly claimed by arbitrary trackers at the same time.
 */
static std::vector<int32_t> gClaimedArcTouchIds; // readable representation only

static bool isGloballyClaimed(int32_t touchId)
{
    return std::find(
               gClaimedArcTouchIds.begin(),
               gClaimedArcTouchIds.end(),
               touchId)
        != gClaimedArcTouchIds.end();
}

// -----------------------------------------------------------------------------
// 4. Tracker acceptance: exact behavioural state machine
// -----------------------------------------------------------------------------

/*
 * CONFIRMED control flow around ~0x15A90C4.
 *
 * The important ordering is:
 *
 *   A. active release lockout is checked FIRST and rejects all touches
 *   B. one special tracker mode (mode == 3) accepts immediately
 *   C. same assigned ID OR temporary proximity bypass accepts
 *   D. a different ID is rejected while an ordinary assignment still exists
 *   E. an unassigned tracker may freshly claim only an unclaimed global ID
 *
 * The min(4*tickInterval,1000) period is therefore NOT a grace interval during
 * which another finger may immediately take over. It is a temporary re-entry /
 * re-acquisition lockout after ordinary release.
 */
bool trackerAcceptsTouch(
    ArcTouchTracker& tracker,
    int32_t touchId,
    int32_t nowMs,
    float currentTickIntervalMs)
{
    tracker.tickIntervalMs = currentTickIntervalMs;

    // 1. Re-acquisition lockout after ordinary release.
    if (tracker.releaseLockoutStartMs != -1) {
        const int timeout =
            ownershipTimeoutMs(tracker.tickIntervalMs);

        if (nowMs - tracker.releaseLockoutStartMs >= timeout) {
            tracker.releaseLockoutStartMs = -1;
        }
        else {
            if (!tracker.acceptedThisUpdate) {
                tracker.transitionState = 1;
                if (tracker.transitionRemainingMs < 0.0f) {
                    tracker.transitionStartMs = nowMs;
                }
            }
            return false;
        }
    }

    // 2. Special mode bypasses ordinary ownership tests.
    if (tracker.mode == 3) {
        return true;
    }

    // 3. Same finger, or a temporary nearby-Arc ownership relaxation.
    if (tracker.assignedTouchId == touchId ||
        tracker.proximityOwnershipBypass) {
        tracker.acceptedThisUpdate = true;
        tracker.transitionRemainingMs = -1.0f;
        tracker.transitionStartMs = -1;
        return true;
    }

    // 4. Another finger cannot replace a still-assigned ordinary finger.
    if (tracker.assignedTouchId != -1) {
        if (!tracker.acceptedThisUpdate) {
            tracker.transitionState = 1;
            if (tracker.transitionRemainingMs < 0.0f) {
                tracker.transitionStartMs = nowMs;
            }
        }
        return false;
    }

    // 5. Fresh claim must respect the global claimed-ID set.
    if (isGloballyClaimed(touchId)) {
        if (!tracker.acceptedThisUpdate) {
            tracker.transitionState = 1;
            tracker.transitionStartMs = nowMs;
        }
        return false;
    }

    tracker.acceptedThisUpdate = true;
    tracker.hasNormalOwnershipHistory = true;
    tracker.assignedTouchId = touchId;

    gClaimedArcTouchIds.push_back(touchId);

    tracker.transitionRemainingMs = -1.0f;
    tracker.transitionStartMs = -1;
    return true;
}

// -----------------------------------------------------------------------------
// 5. Ordinary finger release starts the 4*tick / 1000 ms lockout
// -----------------------------------------------------------------------------

/*
 * CONFIRMED around ~0x13DB7A0.
 *
 * Only release of the currently assigned touch ID matters.
 * The assignment is immediately cleared and the touch ID removed from the
 * global claim list.
 *
 * If this tracker has normal ownership history and is NOT currently under the
 * nearby-Arc bypass, the release time becomes +0x2C and starts the lockout.
 *
 * Therefore:
 *
 *     assigned finger lifts
 *          |
 *          +-> assignment becomes -1 immediately
 *          +-> global touch claim removed immediately
 *          +-> tracker nevertheless rejects re-acquisition temporarily
 *
 * That is subtly different from simply retaining the old touch assignment.
 */
void releaseTrackedTouch(
    ArcTouchTracker& tracker,
    int32_t touchId,
    int32_t releaseTimeMs)
{
    if (tracker.assignedTouchId != touchId) {
        return;
    }

    tracker.assignedTouchId = -1;

    auto it = std::find(
        gClaimedArcTouchIds.begin(),
        gClaimedArcTouchIds.end(),
        touchId);

    if (it != gClaimedArcTouchIds.end()) {
        gClaimedArcTouchIds.erase(it);
    }

    if (tracker.hasNormalOwnershipHistory &&
        !tracker.proximityOwnershipBypass) {
        tracker.releaseLockoutStartMs = releaseTimeMs;
    }

    if (tracker.proximityOwnershipBypass) {
        tracker.transitionState = 0;
    }
}

/*
 * The ordinary touch-end caller supplies current effective gameplay time.
 * Special nearby-Arc release paths may supply zero after first enabling the
 * proximity bypass; because the bypass is active, those paths do not start the
 * ordinary +0x2C lockout.
 */

// -----------------------------------------------------------------------------
// 6. Per-frame tracker maintenance
// -----------------------------------------------------------------------------

/*
 * CONFIRMED around ~0x1482CB0.
 *
 * +0x22 is cleared every update, proving it is a per-update acceptance latch.
 *
 * The temporary +0x10 nearby-Arc bypass expires 500 ms after its last refresh.
 *
 * +0x30/+0x34/+0x38 maintain a countdown using the same
 * min(4*tickInterval,1000) duration. Their exact high-level presentation/state
 * meaning is still UNRESOLVED, so this reconstruction records their arithmetic
 * without inventing an enum name.
 */
void updateArcTouchTracker(
    ArcTouchTracker& tracker,
    int32_t nowMs)
{
    tracker.acceptedThisUpdate = false;

    if (tracker.proximityRefreshTimeMs != -1 &&
        tracker.proximityRefreshTimeMs + 500 < nowMs) {
        tracker.proximityOwnershipBypass = false;
        tracker.proximityRefreshTimeMs = -1;
    }

    if (tracker.transitionState != 0) {
        const int timeout =
            ownershipTimeoutMs(tracker.tickIntervalMs);

        const int elapsed =
            std::max(nowMs - tracker.transitionStartMs, 0);

        const int remaining = timeout - elapsed;

        tracker.transitionRemainingMs =
            remaining >= 0
                ? static_cast<float>(remaining)
                : -1.0f;
    }
    else {
        tracker.transitionRemainingMs = -1.0f;
    }
}

// -----------------------------------------------------------------------------
// 7. Nearby judged Arcs can temporarily relax exact touch ownership
// -----------------------------------------------------------------------------

struct Vec2 {
    float x;
    float y;
};

/*
 * CONFIRMED in the main Arc update around ~0x19F42D8.
 *
 * The game compares certain pairs of active judged Arcs whose tracker modes
 * differ. When their current expected gameplay positions are within about
 * 200 units, one tracker's temporary ownership-bypass state is refreshed:
 *
 *     tracker +0x10 = 1
 *     tracker +0x14 = current effective gameplay time
 *
 * Its current ordinary touch assignment is also released through the special
 * bypass-aware release path.
 *
 * Since the tracker updater clears this bypass after 500 ms without refresh,
 * the relaxation is explicitly temporary.
 *
 * RECONSTRUCTED gameplay interpretation:
 * Closely neighbouring active Arcs may relax rigid one-Arc/one-touch ownership
 * so the tracker does not enforce exact touch identity in that local situation.
 *
 * Do NOT interpret this as a universal finger-switch rule. Geometry still has
 * to qualify the touch, and the exact semantic names of the differing tracker
 * modes remain unresolved.
 */
void refreshNearbyArcOwnershipOverride(
    ArcTouchTracker& tracker,
    int32_t nowMs)
{
    tracker.proximityOwnershipBypass = true;
    tracker.proximityRefreshTimeMs = nowMs;
}

// -----------------------------------------------------------------------------
// 8. LogicArcNote +0x170 bypasses only the ownership tracker
// -----------------------------------------------------------------------------

struct LogicArcGroup;

struct LogicArcNote /* : LogicLongNoteBase */ {
    // Common/established fields omitted.

    float tickIntervalMs;             // established +0x70 conceptual field
    ArcTouchTracker* tracker;          // +0xB0
    LogicArcGroup* arcGroup;           // +0xE0, CONFIRMED in this section

    bool currentContact64;
    bool currentContact65;

    bool trackerBypass170;             // +0x170, semantic name RECONSTRUCTED
};

/*
 * CONFIRMED Arc contact-hook ordering around ~0x15DFE2C:
 *
 * Geometry has already been checked by the caller before this hook runs.
 *
 * If +0x170 == 0, the normal touch tracker must accept the touch.
 * If +0x170 != 0, that ownership check is skipped.
 *
 * The Arc still required its ordinary geometric qualification before reaching
 * this hook. Therefore +0x170 is an ownership-tracker bypass, NOT an auto-hit or
 * hitbox bypass.
 */

/*
 * CONFIRMED factory condition:
 * +0x170 is initialised false, but selected runtime creation paths set it when:
 *
 *     contextByteAt0x111 != 0
 *     && chartArc.colorIndex == 2
 *
 * The chart Arc color/index field itself was established in Section 05.
 *
 * UNRESOLVED:
 * The exact semantic meaning of that external context byte and why color/index
 * 2 receives this tracker bypass are not proved. Do not assign a user-facing
 * feature or accessibility name without further evidence.
 */

// -----------------------------------------------------------------------------
// 9. +0xE0 is confirmed as LogicArcGroup*
// -----------------------------------------------------------------------------

/*
 * CONFIRMED evidence:
 *
 *   - surviving RTTI string: "13LogicArcGroup"
 *   - a 48-byte cocos2d::Ref-like object is allocated during Arc postprocessing
 *   - a surviving "setArcGroup" string occurs alongside the Arc setter path
 *   - a tiny Arc getter returns [arc + 0xE0]
 *   - the setter stores/retains a LogicArcGroup pointer at Arc +0xE0
 *
 * Therefore the previous Section 04 field:
 *
 *     LogicArcNote +0xE0
 *
 * can now be promoted from "optional continuity/timing bookkeeping object" to:
 *
 *     LogicArcGroup*
 */
struct LogicArcGroup {
    // cocos2d::Ref-like base occupies the beginning of the object.

    bool contactFlag10;       // +0x10, exact semantic name UNRESOLVED
    bool presentationFlag11;  // +0x11, exact semantic name UNRESOLVED
    bool contactFlag12;       // +0x12, exact semantic name UNRESOLVED

    float timingState14;      // +0x14, exact semantic name UNRESOLVED

    // +0x18/+0x20/+0x28 form a vector<int>-like storage region.
    std::vector<int32_t> relatedTimes;

    int32_t unknown2C;
};

// -----------------------------------------------------------------------------
// 10. Which Arcs become one connected Arc group?
// -----------------------------------------------------------------------------

/*
 * CONFIRMED postprocessing around ~0x125E304.
 *
 * The game recognises a continuation when the end of one compatible Arc and the
 * start of another satisfy approximately:
 *
 *   |next.startTime - previous.endTime| <= 9 ms
 *   |next.xStart    - previous.xEnd|    < 0.1 chart-X units
 *    next.yStart    == previous.yEnd
 *
 * Judged/non-judged compatibility is also checked before grouping.
 *
 * If the previous Arc has no LogicArcGroup yet, a new 48-byte group is created.
 * The same group pointer is attached to the continuation Arc and can propagate
 * down a chain of connected pieces.
 *
 * This is strong evidence that LogicArcGroup represents continuity across Arc
 * segments rather than arbitrary Arc proximity.
 */
bool arcEndpointsConnect(
    int previousEndTimeMs,
    int nextStartTimeMs,
    float previousXEnd,
    float nextXStart,
    float previousYEnd,
    float nextYStart)
{
    return
        std::abs(nextStartTimeMs - previousEndTimeMs) <= 9 &&
        std::fabs(nextXStart - previousXEnd) < 0.1f &&
        nextYStart == previousYEnd;
}

// -----------------------------------------------------------------------------
// 11. Arc +0xA0 is a connected-continuation flag
// -----------------------------------------------------------------------------

/*
 * CONFIRMED trigger:
 * Whenever the postprocessor accepts an Arc as the next connected piece, it
 * writes:
 *
 *     nextArc +0xA0 = 1
 *
 * This happens independent of whether the direction changes at the seam.
 *
 * `isConnectedArcContinuation` is therefore a safe RECONSTRUCTED behavioural
 * name for +0xA0.
 */
struct ArcConnectionFlags {
    bool isConnectedArcContinuation;       // +0xA0, RECONSTRUCTED name
    bool directionChangesAtConnectedStart; // +0x6C, RECONSTRUCTED name
};

static int signOf(float value)
{
    if (value > 0.0f) return +1;
    if (value < 0.0f) return -1;
    return 0;
}

// -----------------------------------------------------------------------------
// 12. +0x6C is set when direction changes across a connected seam
// -----------------------------------------------------------------------------

/*
 * CONFIRMED around ~0x125E724.
 *
 * The postprocessor compares the signs of horizontal and vertical movement in
 * the previous Arc and continuation Arc:
 *
 *     previousXDirection = sign(previous.xEnd - previous.xStart)
 *     nextXDirection     = sign(next.xEnd     - next.xStart)
 *
 *     previousYDirection = sign(previous.yEnd - previous.yStart)
 *     nextYDirection     = sign(next.yEnd     - next.yStart)
 *
 * Every accepted continuation gets +0xA0 = 1.
 * If EITHER X-direction OR Y-direction differs across the join, it additionally
 * gets +0x6C = 1.
 */
void applyConnectedArcFlags(
    ArcConnectionFlags& nextFlags,
    float previousXStart,
    float previousXEnd,
    float previousYStart,
    float previousYEnd,
    float nextXStart,
    float nextXEnd,
    float nextYStart,
    float nextYEnd)
{
    nextFlags.isConnectedArcContinuation = true;

    const bool xDirectionChanged =
        signOf(previousXEnd - previousXStart)
        != signOf(nextXEnd - nextXStart);

    const bool yDirectionChanged =
        signOf(previousYEnd - previousYStart)
        != signOf(nextYEnd - nextYStart);

    if (xDirectionChanged || yDirectionChanged) {
        nextFlags.directionChangesAtConnectedStart = true;
    }
}

/*
 * This resolves an old mystery from Section 03.
 *
 * LogicLongNoteBase +0x6C controls the exceptional first-event overdue branch:
 *
 *   probeCutoff = now - min(0.5*tickInterval,500)
 *   if exactly one event is at/before probeCutoff && +0x6C != 0:
 *       select the first event through the special path
 *   else:
 *       use ordinary now - min(2*tickInterval,500) expiry
 *
 * Section 12 proves the Arc-side trigger for that flag:
 *
 *   +0x6C is armed on a connected continuation whose X or Y motion direction
 *   changes across the seam.
 *
 * The exact developer intent of that special first-event treatment is still
 * RECONSTRUCTED/UNRESOLVED, but it is no longer an anonymous chart option.
 */

// -----------------------------------------------------------------------------
// 13. LogicArcGroup carries per-frame contact state across connected pieces
// -----------------------------------------------------------------------------

/*
 * CONFIRMED in the main Arc update:
 * For Arcs with a LogicArcGroup, group bytes +0x10/+0x11/+0x12 are reset each
 * frame before current contact is rebuilt.
 */
void resetArcGroupForFrame(LogicArcGroup& group)
{
    group.contactFlag10 = false;
    group.presentationFlag11 = false;
    group.contactFlag12 = false;
}

/*
 * CONFIRMED Arc contact-hook tail:
 * After geometry and tracker ownership pass, Arc +0x64/+0x65 are latched true.
 * If a group exists, the hook additionally updates group timing state and sets:
 *
 *     group +0x10 = 1
 *     group +0x12 = 1
 *
 * A common long-note success propagation path also sets group +0x12.
 * Separate presentation code reads +0x10/+0x11/+0x14.
 *
 * Therefore the group definitely carries shared connected-Arc contact/timing
 * state, but exact semantic names for the individual bytes are not yet proved.
 */
void markArcGroupContact(LogicArcGroup& group)
{
    group.contactFlag10 = true;
    group.contactFlag12 = true;
}

// -----------------------------------------------------------------------------
// 14. Full readable Arc contact hook
// -----------------------------------------------------------------------------

/*
 * RECONSTRUCTED from confirmed call ordering.
 * The geometric hit test from Sections 04/11 occurs BEFORE this function.
 */
bool acceptQualifiedArcTouch(
    LogicArcNote& arc,
    int32_t touchId,
    int32_t nowMs)
{
    if (!arc.trackerBypass170) {
        if (!trackerAcceptsTouch(
                *arc.tracker,
                touchId,
                nowMs,
                arc.tickIntervalMs)) {
            return false;
        }
    }

    if (arc.arcGroup) {
        // Confirmed existence of a connected-group timing update here.
        // Exact +0x14 formula/member semantics deliberately omitted.
        updateConnectedArcGroupTiming(*arc.arcGroup, arc, nowMs);
    }

    arc.currentContact64 = true;
    arc.currentContact65 = true;

    if (arc.arcGroup) {
        markArcGroupContact(*arc.arcGroup);
    }

    return true;
}

// -----------------------------------------------------------------------------
// 15. What happens when the player changes fingers?
// -----------------------------------------------------------------------------

/*
 * CONFIRMED ordinary case:
 *
 *   - while a normal touch remains assigned, a different touch ID is rejected
 *   - when the assigned finger lifts, assignment is cleared
 *   - ordinary release starts the re-acquisition lockout
 *   - during that lockout ALL touches are rejected by this tracker
 *   - after min(4*tickInterval,1000) expires, normal fresh claiming resumes
 *
 * Therefore ordinary different-finger replacement is intentionally not
 * instantaneous.
 *
 * Separate exceptions exist:
 *   - tracker mode == 3 bypasses the ordinary ownership tests
 *   - nearby active judged Arcs can temporarily enable +0x10, which makes exact
 *     assigned touch ID irrelevant for acceptance and expires after 500 ms
 *   - Arc +0x170 skips the ownership tracker entirely after geometry succeeds
 *
 * These exceptions should NOT be merged into one generic "finger switch grace"
 * because their triggers and lifetimes are mechanically distinct.
 */

// -----------------------------------------------------------------------------
// 16. Complete layered mental model
// -----------------------------------------------------------------------------

/*
 *                    screen touch
 *                         |
 *                         v
 *              camera/gameplay geometry
 *                         |
 *                qualified Arc region?
 *                         |
 *                         v
 *                 ownership tracker
 *                  /      |       \
 *                 /       |        \
 *        normal exact   nearby    +0x170
 *          touch ID     override   bypass
 *                 \       |        /
 *                  \      |       /
 *                         v
 *                Arc +0x64/+0x65
 *                         |
 *             +-----------+-----------+
 *             |                       |
 *             v                       v
 *      LogicArcGroup contact     long-note events
 *      across connected pieces        |
 *                                     v
 *                                 ScoreState
 *
 * Ordinary release:
 *
 *      assigned touch lifts
 *             |
 *             v
 *       assignment cleared
 *       global claim removed
 *             |
 *             v
 *     re-acquisition lockout
 *     min(4*tick,1000 ms)
 *
 * Long-note LOST is a different clock:
 *
 *     no current contact
 *             |
 *             v
 *     event expiry grace
 *     min(2*tick,500 ms)
 */

// -----------------------------------------------------------------------------
// 17. CONFIRMED / RECONSTRUCTED / UNRESOLVED summary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED
 * ---------
 * - tracker +0x28 is the assigned touch ID; -1 means unassigned.
 * - normal fresh claims are globally exclusive through a shared touch-ID list.
 * - tracker +0x22 is reset every frame and marks acceptance during that update.
 * - ordinary release clears assignment and removes the global claim immediately.
 * - ordinary release can start +0x2C re-acquisition lockout.
 * - active +0x2C rejects all touches until min(4*tickInterval,1000) expires.
 * - this ownership timeout is separate from long-note min(2*tick,500) LOST grace.
 * - tracker mode ==3 bypasses ordinary ownership checks.
 * - nearby active judged Arcs can enable +0x10 ownership relaxation.
 * - +0x10 expires 500 ms after its last refresh.
 * - Arc +0x170 bypasses the tracker only after geometric qualification.
 * - +0x170 is enabled by a runtime context condition together with Arc color 2.
 * - Arc +0xE0 is LogicArcGroup*.
 * - connected groups are built from nearly contiguous Arc endpoints, including
 *   <=9 ms time separation, <0.1 chart-X separation, and matching Y endpoint.
 * - connected continuation Arcs receive +0xA0=1.
 * - if X or Y direction changes across the join, the continuation gets +0x6C=1.
 * - +0x6C is the same flag controlling the special first-event expiry branch.
 * - LogicArcGroup contact bytes are reset each frame and refreshed by contact.
 *
 * RECONSTRUCTED
 * -------------
 * - field names such as releaseLockoutStartMs, proximityOwnershipBypass,
 *   isConnectedArcContinuation, and directionChangesAtConnectedStart
 * - interpretation of nearby-Arc +0x10 as local ownership relaxation
 * - interpretation of LogicArcGroup as continuity state across connected pieces
 *
 * UNRESOLVED
 * ----------
 * - exact semantic names for tracker modes, especially values other than 3
 * - exact meaning of tracker +0x30/+0x34/+0x38 presentation/state countdown
 * - exact distinct meanings of LogicArcGroup +0x10/+0x11/+0x12/+0x14
 * - exact semantic identity of the context byte enabling Arc +0x170
 * - developer rationale for the direction-change +0x6C first-event treatment
 */
