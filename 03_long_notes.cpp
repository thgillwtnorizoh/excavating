/*
 * Arcaea excavation notebook
 * Section 03: Long-note hold input and tick judgement
 *
 * STATUS: LogicHoldNote gameplay slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native code/data/control flow.
 *   RECONSTRUCTED = readable representation assembled from confirmed behaviour.
 *   UNRESOLVED    = exact semantic name/purpose is not yet proved.
 *
 * Scope of this section:
 *   - how LogicHoldNote becomes associated with touch input
 *   - how current contact is refreshed before judgement
 *   - how generated long-note events become successful or LOST
 *   - how release/re-press behaves
 *   - how each event enters ScoreState
 *
 * Deliberately out of scope:
 *   - arc geometry and arc rendering
 *   - full timinggroup/camera behaviour
 *   - unlock/progression systems
 *
 * This file builds on 02_note_fundamentals.cpp rather than restating the
 * complete note hierarchy and point-note judgement system.
 *
 * Useful native anchors from the investigated ARM64 build:
 *   LogicLongNoteBase event construction       ~0x0D92558
 *   LogicLongNoteBase completion predicate     ~0x13CE570
 *   LogicHoldNote contact hook                 ~0x14557B4
 *   GameModel touch-begin hold selection       ~0x0EEC024
 *   GameModel active-touch refresh             ~0x14858D4
 *   common long-note judgement loop            ~0x0F8056C
 *   overdue long-event collector               ~0x0C6E938
 *   ScoreState successful judgement            ~0x1730290
 *   ScoreState LOST path                       ~0x0868E54
 *
 * WARNING ABOUT SYMBOLS:
 * The binary is stripped. Names in this file are readable reconstruction names
 * unless explicitly described as surviving RTTI/type names or CONFIRMED data.
 */

#include <cstdint>
#include <vector>

// -----------------------------------------------------------------------------
// 1. Established long-note event representation
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from Section 02:
 *
 *   - Holds and arcs derive from LogicLongNoteBase.
 *   - LogicLongNoteBase owns a vector of 12-byte internal event records.
 *   - event time is the first 32-bit field.
 *   - the second 32-bit field is generated as 1 by the common builder.
 *   - processedFlags bit 0 is the completion/processed bit.
 *
 * NEW in this section:
 * The second field is accumulated by both the successful-event and LOST-event
 * collectors, and the resulting sum controls how many times ScoreState receives
 * the corresponding judgement. Therefore it behaves as an event scoring-unit
 * count / multiplicity.
 *
 * "scoreUnits" is RECONSTRUCTED naming. The observed common builder writes 1.
 * This section does not prove whether another path can intentionally create a
 * value greater than 1.
 */
struct LongTickEvent {
    int32_t timeMs;       // CONFIRMED
    int32_t scoreUnits;   // RECONSTRUCTED semantic name; common builder writes 1
    uint8_t processedFlags;
    uint8_t padding[3];
};

static_assert(sizeof(LongTickEvent) == 12);

static bool isProcessed(const LongTickEvent& event)
{
    // CONFIRMED: bit 0 is what the native completion predicate tests.
    return (event.processedFlags & 1) != 0;
}

static void markProcessed(LongTickEvent& event)
{
    // CONFIRMED effect of both success and LOST collectors.
    event.processedFlags |= 1;
}

// -----------------------------------------------------------------------------
// 2. Selected long-note / hold state used by this slice
// -----------------------------------------------------------------------------

struct LogicNote;
struct ScoreState;
struct Touch;

struct LogicLongNoteBase /* : LogicNote */ {
    // Common LogicNote fields omitted here; see Section 02.

    int startTimeMs;  // conceptual reference to established +0x18 field
    int endTimeMs;    // conceptual reference to established +0x1C field

    /*
     * The real object contains these at the established long-note offsets:
     *
     *   +0x64 : transient byte reset by common note update logic
     *   +0x65 : companion transient byte, also reset/set with +0x64 for holds
     *   +0x66 : byte set after successful long-event batches and cleared after
     *           LOST batches; exact higher-level semantic meaning UNRESOLVED
     *   +0x6C : byte affecting a special first-event expiry branch;
     *           exact semantic meaning UNRESOLVED
     *   +0x70 : tick interval
     *   +0x78 : vector storage for 12-byte LongTickEvent records
     *
     * IMPORTANT:
     * +0x64 is interpreted as current qualified hold contact in LogicHoldNote's
     * path below. Do not automatically assign that same semantic name to arcs.
     */
    uint8_t transient64;
    uint8_t transient65;
    uint8_t longEventState66;
    uint8_t unknownFirstEventRule6C;

    float tickIntervalMs;
    std::vector<LongTickEvent> tickEvents;

    bool allEventsProcessed() const
    {
        for (const LongTickEvent& event : tickEvents) {
            if (!isProcessed(event)) {
                return false;
            }
        }
        return true;
    }
};

struct LogicHoldNote : LogicLongNoteBase {
    /*
     * CONFIRMED derived-state observations:
     *
     *   +0xA0 : a pointer stored by LogicHoldNote initialisation.
     *           Exact object/type semantic is UNRESOLVED and is unnecessary for
     *           the contact/judgement model here.
     *
     *   +0xA8 : initialised to 0, set to 1 when GameModel accepts an eligible
     *           touch-begin for this hold, and required by later active-touch
     *           matching before the hold contact hook is called.
     *
     * "engagedByEligibleTouch" is RECONSTRUCTED naming.
     */
    void* unknownHoldPointerA0;
    bool engagedByEligibleTouch;
};

// -----------------------------------------------------------------------------
// 3. Hold touch-begin: selection and engagement
// -----------------------------------------------------------------------------

/*
 * CONFIRMED control flow in GameModel's touch-begin handling:
 *
 *   1. Iterate gameplay LogicNotes.
 *   2. Dynamic-cast relevant candidates to LogicHoldNote.
 *   3. Compare a lane/input identifier from the hold's associated descriptor
 *      against touch-derived lane/input identifiers.
 *   4. Check the hold's active time region.
 *   5. Associate the touch with the selected hold.
 *   6. Set LogicHoldNote +0xA8 = 1.
 *
 * The observed timing tests are structurally equivalent to:
 *
 *   effectiveTime < hold.endTime
 *   hold.startTime < effectiveTime + 100
 *
 * Thus a hold can be engaged during its interval and can be selected slightly
 * before its start (approximately 100 ms by this branch). It is not restricted
 * to a one-shot judgement at startTime.
 *
 * The original class/member name for the lane descriptor is UNRESOLVED, so the
 * readable interface below intentionally stays generic.
 */
bool tryEngageHoldOnTouchBegin(
    LogicHoldNote& hold,
    Touch& touch,
    int effectiveTimeMs)
{
    if (!laneIdentifierMatches(hold, touch)) {
        return false;
    }

    if (effectiveTimeMs >= hold.endTimeMs) {
        return false;
    }

    if (hold.startTimeMs >= effectiveTimeMs + 100) {
        return false;
    }

    associateTouchWithHold(touch, hold);
    hold.engagedByEligibleTouch = true;
    return true;
}

// -----------------------------------------------------------------------------
// 4. Contact is refreshed before long-note judgement
// -----------------------------------------------------------------------------

/*
 * CONFIRMED pieces:
 *
 * A common note-update routine clears both bytes +0x64 and +0x65.
 *
 * GameModel then has an active-touch processing pass which:
 *   - iterates currently active Touch objects,
 *   - finds LogicHoldNote candidates,
 *   - checks matching lane/input identity,
 *   - checks the current time against the hold interval,
 *   - requires hold +0xA8 != 0,
 *   - calls a LogicHoldNote virtual hook.
 *
 * LogicHoldNote's hook at ~0x14557B4 sets +0x64 and +0x65 to 1. It also emits
 * another gameplay event whose exact higher-level meaning is UNRESOLVED.
 *
 * In the main gameplay update, the active-touch pass runs immediately before
 * the common long-note judgement pass.
 *
 * RECONSTRUCTED gameplay meaning:
 *   LogicHoldNote +0x64 is a transient "qualified contact for this update"
 *   latch. It is not the persistent touch-begin/engagement flag; +0xA8 serves
 *   that separate role.
 */
void LogicHoldNote_contactHook(LogicHoldNote& hold)
{
    hold.transient64 = 1;
    hold.transient65 = 1;

    // Other emitted event omitted; semantic UNRESOLVED.
}

void refreshHoldContactFromActiveTouches(
    std::vector<Touch*>& activeTouches,
    std::vector<LogicNote*>& logicNotes,
    int effectiveTimeMs)
{
    for (Touch* touch : activeTouches) {
        for (LogicNote* note : logicNotes) {
            LogicHoldNote* hold = dynamicCastToLogicHoldNote(note);
            if (!hold) {
                continue;
            }

            if (!hold->engagedByEligibleTouch) {
                continue;
            }

            if (effectiveTimeMs < hold->startTimeMs ||
                effectiveTimeMs >= hold->endTimeMs) {
                continue;
            }

            if (!laneIdentifierMatches(*hold, *touch)) {
                continue;
            }

            LogicHoldNote_contactHook(*hold);
        }
    }
}

/*
 * CONFIRMED release consequence:
 * A touch-end path removes the ended Touch* from GameModel's active-touch list.
 * Therefore a released finger no longer re-latches +0x64 on later updates.
 */

// -----------------------------------------------------------------------------
// 5. Common long-note judgement fork
// -----------------------------------------------------------------------------

/*
 * CONFIRMED in the common long-note judgement loop:
 *
 *   if longNote +0x64 is non-zero:
 *       run the successful-event collector
 *   else:
 *       query a virtual predicate
 *       for LogicHoldNote / its inherited base behaviour this predicate is true
 *       -> run the overdue-LOST collector
 *
 * LogicArcNote changes part of this decision and is deliberately deferred to a
 * later section.
 *
 * This is the central hold-note state machine:
 *
 *     qualified contact now      -> satisfy eligible pending events
 *     no qualified contact now   -> age pending events toward LOST
 */
void processHoldLongEvents(
    LogicHoldNote& hold,
    ScoreState& score,
    int effectiveTimeMs)
{
    if (hold.transient64 != 0) {
        const int units = collectSuccessfulLongEvents(
            hold, effectiveTimeMs);

        for (int i = 0; i < units; ++i) {
            registerLongSuccessInScoreState(
                score,
                hold,
                /* judgement */ 0,
                /* timingSide */ 0,
                effectiveTimeMs,
                /* inputPayload */ -1);
        }
    }
    else {
        // The inherited predicate used by the Hold path is CONFIRMED true.
        if (longNoteAllowsOverdueLoss(hold)) {
            const int units = collectOverdueLostLongEvents(
                hold, effectiveTimeMs);

            for (int i = 0; i < units; ++i) {
                registerLongLostInScoreState(
                    score,
                    hold,
                    effectiveTimeMs);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// 6. Successful hold event collection
// -----------------------------------------------------------------------------

/*
 * CONFIRMED arithmetic:
 *
 *   successCutoff = effectiveTime + 0.5 * tickInterval
 *
 * The native code counts events whose timestamps are at or before that cutoff,
 * then walks the newly eligible region backwards. Each previously unprocessed
 * event is marked processed and its second 32-bit field is accumulated.
 * Processing stops at an already-processed boundary.
 *
 * If at least one scoring unit was collected, long-note byte +0x66 is set to 1.
 *
 * For every accumulated unit, the long-note judgement loop calls ScoreState's
 * successful-judgement path with:
 *
 *   judgement  = 0   (MAX PURE / shiny PURE in the established mapping)
 *   timingSide = 0
 *   eventTime  = current effective gameplay time
 *   payload    = -1
 *
 * Therefore hold ticks do NOT use the tap-note MAX PURE / PURE / FAR timing
 * windows. Their success is contact/state based, and successful units enter
 * ScoreState as judgement 0.
 */
int collectSuccessfulLongEvents(
    LogicLongNoteBase& note,
    int effectiveTimeMs)
{
    const int cutoff = truncateToInt(
        effectiveTimeMs + 0.5f * note.tickIntervalMs);

    int eligibleCount = 0;
    while (eligibleCount < static_cast<int>(note.tickEvents.size()) &&
           note.tickEvents[eligibleCount].timeMs <= cutoff) {
        ++eligibleCount;
    }

    int collectedUnits = 0;

    for (int i = eligibleCount - 1; i >= 0; --i) {
        LongTickEvent& event = note.tickEvents[i];

        if (isProcessed(event)) {
            break;
        }

        collectedUnits += event.scoreUnits;
        markProcessed(event);
    }

    if (collectedUnits > 0) {
        note.longEventState66 = 1;
    }

    return collectedUnits;
}

// -----------------------------------------------------------------------------
// 7. LOST hold event collection and release grace
// -----------------------------------------------------------------------------

/*
 * CONFIRMED standard expiry arithmetic in the overdue-event helper:
 *
 *   lossGrace = min(2 * tickInterval, 500 ms)
 *   lossCutoff = effectiveTime - lossGrace
 *
 * Pending events at or before lossCutoff are eligible to become LOST.
 * As on the success path, the native code walks the eligible region backwards,
 * stops at an already-processed boundary, sums event scoring units, and sets
 * each consumed event's processed bit.
 *
 * If at least one scoring unit is lost, +0x66 is cleared to 0.
 * ScoreState's LOST entry path is then called once per collected scoring unit.
 *
 * This produces a real release/re-press grace period. Releasing does not itself
 * immediately convert the nearest pending tick to LOST. The event must first
 * age beyond the expiry cutoff while contact is absent.
 */
int collectOverdueLostLongEvents(
    LogicLongNoteBase& note,
    int effectiveTimeMs)
{
    int eligibleCount = 0;

    /*
     * CONFIRMED special first-event branch:
     *
     *   probeGrace = min(0.5 * tickInterval, 500 ms)
     *   probeCutoff = effectiveTime - probeGrace
     *   probeCount = number of events <= probeCutoff
     *
     * If probeCount == 1 and byte +0x6C is non-zero, event index 0 is selected
     * through this exceptional branch.
     *
     * The exact semantic meaning and enabling condition of +0x6C remain
     * UNRESOLVED. Do not name it as a gameplay option/modifier without proof.
     */
    const int probeCutoff = truncateToInt(
        effectiveTimeMs -
        minimum(0.5f * note.tickIntervalMs, 500.0f));

    const int probeCount = countEventsAtOrBefore(
        note.tickEvents, probeCutoff);

    if (probeCount == 1 && note.unknownFirstEventRule6C != 0) {
        eligibleCount = 1;
    }
    else {
        const float graceMs = minimum(
            2.0f * note.tickIntervalMs,
            500.0f);

        const int lossCutoff = truncateToInt(
            effectiveTimeMs - graceMs);

        eligibleCount = countEventsAtOrBefore(
            note.tickEvents, lossCutoff);
    }

    int lostUnits = 0;

    for (int i = eligibleCount - 1; i >= 0; --i) {
        LongTickEvent& event = note.tickEvents[i];

        if (isProcessed(event)) {
            break;
        }

        lostUnits += event.scoreUnits;
        markProcessed(event);
    }

    if (lostUnits > 0) {
        note.longEventState66 = 0;
    }

    return lostUnits;
}

// -----------------------------------------------------------------------------
// 8. Release and re-press behaviour
// -----------------------------------------------------------------------------

/*
 * CONFIRMED / directly reconstructed from the above control flow:
 *
 * Continuous hold:
 *   - active matching touch repeatedly re-latches +0x64
 *   - success collector consumes newly eligible events
 *   - each event becomes permanently processed
 *
 * Release:
 *   - Touch* is removed from the active-touch list
 *   - +0x64 is no longer re-latched
 *   - judgement loop enters the overdue-LOST path
 *   - pending events remain recoverable until their LOST cutoff is reached
 *
 * Re-press before expiry:
 *   - touch-begin can select the same hold while it is still in its time range
 *   - +0xA8 is/set remains engaged
 *   - active-touch refresh re-latches +0x64
 *   - success collector consumes still-unprocessed pending events
 *
 * Re-press after an event became LOST:
 *   - LOST collector has already set that event's processed bit
 *   - later success scans stop at/skip the processed frontier
 *   - the already-consumed LOST event cannot be recovered
 *
 * Mid-hold pickup:
 *   - touch-begin eligibility is not restricted to the original hold start
 *   - therefore a hold can be picked up after its beginning
 *   - earlier events already expired as LOST remain lost; still-pending events
 *     can be satisfied after contact resumes
 */

// -----------------------------------------------------------------------------
// 9. What belongs to LogicLongNoteBase vs LogicHoldNote
// -----------------------------------------------------------------------------

/*
 * CONFIRMED architectural division:
 *
 * LogicLongNoteBase provides / owns:
 *   - common interval timing fields
 *   - tickInterval at +0x70
 *   - vector of internal events at +0x78
 *   - common event construction
 *   - processed-bit completion test
 *   - long-note event acceptance hooks which do not resolve the whole note
 *   - common transient state reset around +0x64/+0x65
 *   - inherited predicate used by Hold which permits overdue-event losses
 *
 * Common GameModel long-note processing provides:
 *   - successful event-window scanning
 *   - overdue LOST scanning
 *   - repeated ScoreState dispatch according to event scoring units
 *
 * LogicHoldNote adds / specialises:
 *   - derived state around +0xA0/+0xA8
 *   - Hold-specific participation in touch-begin lane/time selection
 *   - requirement that an eligible Hold engagement exists before later active
 *     touches can assert contact
 *   - a contact hook which turns a currently valid matching touch into the
 *     transient +0x64/+0x65 state consumed by common long-note judgement
 *
 * This explains why holds and arcs can share tick records while having different
 * definitions of what counts as valid contact.
 */

// -----------------------------------------------------------------------------
// 10. ScoreState entry and gauge consequence
// -----------------------------------------------------------------------------

/*
 * CONFIRMED:
 *
 * Successful hold units use the same ScoreState successful-judgement function
 * established in Section 02. The note's successful-event virtual accepts the
 * event without setting a whole-note "hit" flag. Score counters then update and
 * the event is forwarded to every active LifeBarState.
 *
 * LOST hold units similarly use the same ScoreState LOST path. The long-note
 * LOST hook accepts the event without setting a whole-note "lost" flag, then
 * ScoreState increments LOST and forwards the event to LifeBarState.
 *
 * This closes the loop with Section 01:
 * LifeBarState recognises LogicLongNoteBase losses and applies half ordinary
 * LOST damage because one long-note object can generate multiple LOST events.
 */

// Readable call shapes only. These are RECONSTRUCTED signatures.
void registerLongSuccessInScoreState(
    ScoreState& score,
    LogicLongNoteBase& note,
    int judgement,
    int timingSide,
    int eventTimeMs,
    int inputPayload);

void registerLongLostInScoreState(
    ScoreState& score,
    LogicLongNoteBase& note,
    int eventTimeMs);

// -----------------------------------------------------------------------------
// 11. Gameplay mental model
// -----------------------------------------------------------------------------

/*
 * A hold is NOT judged as a sequence of mini tap notes with +/- timing windows.
 *
 * Instead:
 *
 *   touch begins in correct lane/time region
 *                |
 *                v
 *       Hold becomes engaged (+0xA8)
 *                |
 *                v
 *   every update, currently active matching touch?
 *           /                         \
 *         yes                          no
 *          |                            |
 *          v                            v
 *   latch +0x64                  leave +0x64 clear
 *          |                            |
 *          v                            v
 * success-event scan             overdue-LOST scan
 * event.time <=                  event.time <=
 * now + 0.5*interval             now - min(2*interval,500)
 *          |                            |
 *          +------------+---------------+
 *                       |
 *                       v
 *             mark event processed
 *                       |
 *                       v
 *      ScoreState success (judgement 0)
 *                 OR ScoreState LOST
 *                       |
 *                       v
 *             LifeBarState fan-out
 *
 * Because LOST uses an expiry cutoff instead of immediate release failure,
 * short release/re-press gaps can preserve pending events. Once an event has
 * been processed as LOST, it is no longer recoverable.
 */

// -----------------------------------------------------------------------------
// 12. CONFIRMED / RECONSTRUCTED / UNRESOLVED summary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED
 * ---------
 * - LogicHoldNote derives from LogicLongNoteBase.
 * - Hold touch-begin matching is performed externally in GameModel.
 * - Hold touch-begin checks lane/input identity and a hold time region.
 * - Eligible touch-begin sets Hold +0xA8 = 1.
 * - Current active touches are processed before the long-note judgement pass.
 * - Hold's contact hook sets +0x64 and +0x65 to 1.
 * - Common update logic clears +0x64/+0x65.
 * - +0x64 non-zero selects successful long-event processing for Hold.
 * - +0x64 clear selects the overdue-event LOST path for Hold.
 * - Successful eligibility uses now + 0.5 * tickInterval.
 * - Standard LOST expiry uses now - min(2 * tickInterval, 500 ms).
 * - Both paths set LongTickEvent processed bit 0.
 * - Both paths sum the event's second 32-bit field.
 * - That sum determines the number of ScoreState calls.
 * - Common generated events contain second-field value 1.
 * - Successful hold units enter ScoreState as judgement 0 / timing-side 0.
 * - LOST units enter the established ScoreState LOST path.
 * - Touch-end removes the touch from the active-touch collection.
 * - A processed event cannot later be judged again.
 *
 * RECONSTRUCTED
 * -------------
 * - +0xA8 readable name: engagedByEligibleTouch.
 * - Hold interpretation of +0x64: qualified/current contact latch.
 * - LongTickEvent +4 readable name: scoreUnits / multiplicity.
 * - The overall model is an engagement + per-update contact + event-expiry
 *   state machine rather than exact-timestamp mini-tap judgements.
 * - Re-press before expiry recovers still-pending events because touch contact
 *   returns the note to the success scanner before those events are processed
 *   by the LOST scanner.
 *
 * UNRESOLVED
 * ----------
 * - Original semantic/member names for +0xA0, +0xA8, +0x64, +0x65, +0x66.
 * - Exact meaning/enabling source of LogicLongNoteBase byte +0x6C.
 * - Higher-level meaning of the extra event emitted by Hold's contact hook.
 * - Original class/member names for the lane/input descriptor used in matching.
 * - Whether any non-common builder deliberately creates LongTickEvent +4 > 1.
 * - The separate +/-3000 ms timing-related branch noted in Section 02 remains
 *   outside this slice and unresolved.
 */

// -----------------------------------------------------------------------------
// 13. Next narrow excavation target
// -----------------------------------------------------------------------------

/*
 * With Hold now understood, the next useful slice is:
 *
 *   LogicArcNote contact-validity state -> shared long-event success/LOST path
 *
 * Questions for that slice:
 *   - which LogicArcNote state replaces Hold's simple lane/contact latch?
 *   - which base virtuals Arc overrides around long-event eligibility?
 *   - how losing/reacquiring arc contact changes pending tick events
 *   - only the minimum arc path/position math necessary to explain contact
 *
 * Do NOT jump directly into the complete arc renderer or camera system.
 */
