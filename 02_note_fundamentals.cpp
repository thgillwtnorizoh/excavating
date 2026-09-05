/*
 * Arcaea excavation notebook
 * Section 02: Fundamental note object model and judgement flow
 *
 * STATUS: fundamental slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native code/data.
 *   RECONSTRUCTED = readable representation assembled from confirmed behaviour.
 *   UNRESOLVED    = exact semantic name/purpose is not yet proved.
 *
 * This section deliberately stops before hold-specific input processing,
 * arc geometry/rendering, timinggroups, lane geometry, and special effects.
 * It establishes the common runtime note model those later sections build on.
 *
 * Useful native anchors from the investigated ARM64 build:
 *   point-note judgement candidate routine      ~0x160CE28
 *   ScoreState successful judgement             ~0x1730290
 *   ScoreState LOST path                        ~0x0868E54
 *   LogicLongNoteBase initialisation             ~0x17CB97C
 *   LogicLongNoteBase tick/event construction    ~0x0D92558
 *   LogicLongNoteBase completion predicate       ~0x13CE570
 *   point-note accepted-hit hook                 ~0x0965E28
 *   point-note LOST setter                       ~0x07E995C
 *   point-note resolved predicate                ~0x13EDAE0
 *
 * WARNING ABOUT SYMBOLS:
 * The binary is stripped. Nearby exported symbol names printed by objdump are
 * unrelated library symbols and are NOT used as function identities here.
 */

#include <cstdint>
#include <vector>

// -----------------------------------------------------------------------------
// 1. Runtime class hierarchy
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from surviving C++ RTTI/typeinfo relationships:
 *
 *   cocos2d::Ref
 *      |
 *      +-- LogicNote
 *            |
 *            +-- LogicTapNote
 *            |      |
 *            |      +-- LogicArcTapNote
 *            |
 *            +-- LogicLongNoteBase
 *            |      |
 *            |      +-- LogicHoldNote
 *            |      +-- LogicArcNote
 *            |
 *            +-- LogicFlickNote
 *
 * Gameplay consequence:
 *   - ArcTaps belong to the point/tap lineage.
 *   - Holds and arcs share the long-note interval/tick machinery.
 *   - Flicks are another direct LogicNote subtype and are deferred here.
 */

struct LogicNote;
struct LogicTapNote;
struct LogicArcTapNote;
struct LogicLongNoteBase;
struct LogicHoldNote;
struct LogicArcNote;
struct LogicFlickNote;

// Separate RTTI also survives for RenderTapNote, RenderArcTapNote,
// RenderHoldNote, RenderArcNote, and RenderFlickNote.
// CONFIRMED architectural conclusion: gameplay logic and note rendering are
// represented by distinct class families rather than one monolithic object.

// -----------------------------------------------------------------------------
// 2. Judgement values and timing side
// -----------------------------------------------------------------------------

enum class Judgement : int {
    MaxPure = 0, // CONFIRMED
    Pure    = 1, // CONFIRMED
    Far     = 2, // CONFIRMED
    Lost    = 3  // conceptual value; LOST uses a separate ScoreState entry path
};

enum class TimingSide : int {
    ExactOrUnused = 0,
    Early         = 1, // RECONSTRUCTED from comparison direction
    Late          = 2  // RECONSTRUCTED from comparison direction
};

// -----------------------------------------------------------------------------
// 3. Common LogicNote state
// -----------------------------------------------------------------------------

/*
 * Selected CONFIRMED object fields seen in the common note initialiser:
 *
 *   +0x0C : point-note LOST/resolved-by-miss flag
 *   +0x0D : point-note successful-hit flag
 *   +0x10 : integer payload written by the point-note hit hook
 *           UNRESOLVED exact semantic meaning; do NOT call this judgement.
 *   +0x18 : start time
 *   +0x1C : end time
 *   +0x60 : monotonically assigned runtime note serial/index
 *
 * The common initialiser also retains several pointers/indices whose exact
 * semantic names are not yet needed for the fundamental mental model.
 */

struct LogicNote {
    bool lost = false;      // readable reconstruction of +0x0C for point notes
    bool hit = false;       // readable reconstruction of +0x0D for point notes

    int unknownHitPayload = -1; // +0x10, UNRESOLVED semantic name

    int startTimeMs = 0;    // +0x18, CONFIRMED
    int endTimeMs = 0;      // +0x1C, CONFIRMED

    int runtimeSerial = 0;  // +0x60, CONFIRMED monotonic assignment

    // Many additional fields intentionally omitted.
};

// -----------------------------------------------------------------------------
// 4. Point notes collapse the interval to one instant
// -----------------------------------------------------------------------------

/*
 * CONFIRMED in LogicTapNote initialisation:
 *
 *   startTime = suppliedTime
 *   endTime   = suppliedTime
 *
 * Therefore a tap is represented by the same common time interval fields as
 * other LogicNotes, but its interval is degenerate: start == end.
 */

struct LogicTapNote : LogicNote {
    void initialise(int timeMs)
    {
        startTimeMs = timeMs;
        endTimeMs = timeMs;
        lost = false;
        hit = false;
        unknownHitPayload = -1;
    }

    /*
     * RECONSTRUCTED around confirmed virtual hooks.
     *
     * Successful hit:
     *   - emits a generic gameplay event elsewhere
     *   - sets +0x0D = true
     *   - stores a secondary input/hit payload at +0x10
     *
     * IMPORTANT:
     * The judgement enum is NOT stored at +0x10. ScoreState receives judgement
     * separately and passes a different argument into this hook.
     */
    bool acceptSuccessfulHit(int judgementTimeMs, int inputPayload)
    {
        (void)judgementTimeMs;
        hit = true;
        unknownHitPayload = inputPayload;
        return true;
    }

    // CONFIRMED behaviour of the point-note LOST hook.
    bool markLost(bool value)
    {
        lost = value;
        return true;
    }

    // CONFIRMED structure: point-note resolution is hit OR lost.
    bool isResolved() const
    {
        return hit || lost;
    }
};

struct LogicArcTapNote : LogicTapNote {
    // CONFIRMED inheritance only in this section.
    // ArcTap-specific placement/rendering is deferred.
};

// -----------------------------------------------------------------------------
// 5. Fundamental point-note timing windows
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from the native point-note judgement candidate routine.
 * The routine computes an absolute timing error against note start time.
 *
 *   absolute error       result
 *   --------------       ------------------------------
 *        0..25 ms        MAX PURE
 *       26..50 ms        PURE
 *       51..100 ms       FAR
 *      101..120 ms       LOST through ScoreState miss path
 *         >120 ms        rejected by this candidate routine
 *
 * The comparisons are inclusive at 25, 50, 100, and 120 ms.
 *
 * PURE and FAR additionally carry Early/Late information:
 *   currentTime < noteTime  -> 1 (RECONSTRUCTED as Early)
 *   currentTime >= noteTime -> 2 (RECONSTRUCTED as Late)
 *
 * MAX PURE passes timing-side value 0.
 *
 * CAVEAT:
 * This describes the candidate judgement routine itself. Scheduling/automatic
 * miss logic can resolve a note independently, so this section does not claim
 * every side of every outer timing window is equally reachable during play.
 */

struct ScoreState;

bool tryJudgePointNote(
    LogicTapNote& note,
    int currentJudgementTimeMs,
    int inputPayload,
    ScoreState& score)
{
    // CONFIRMED: already-hit or already-lost point notes are rejected.
    if (note.lost || note.hit) {
        return false;
    }

    const int signedError = currentJudgementTimeMs - note.startTimeMs;
    const int absError = signedError < 0 ? -signedError : signedError;

    const TimingSide side =
        (currentJudgementTimeMs < note.startTimeMs)
            ? TimingSide::Early
            : TimingSide::Late;

    if (absError <= 25) {
        // RECONSTRUCTED call shape around confirmed ScoreState routine.
        registerSuccessfulJudgement(
            score, note, Judgement::MaxPure,
            TimingSide::ExactOrUnused,
            currentJudgementTimeMs, inputPayload);
        return true;
    }

    if (absError <= 50) {
        registerSuccessfulJudgement(
            score, note, Judgement::Pure, side,
            currentJudgementTimeMs, inputPayload);
        return true;
    }

    if (absError <= 100) {
        registerSuccessfulJudgement(
            score, note, Judgement::Far, side,
            currentJudgementTimeMs, inputPayload);
        return true;
    }

    if (absError <= 120) {
        // CONFIRMED: this is the real ScoreState LOST path, not a weak FAR.
        registerLost(score, note, currentJudgementTimeMs);
        return true;
    }

    return false;
}

// -----------------------------------------------------------------------------
// 6. ScoreState owns score counters and judgement fan-out
// -----------------------------------------------------------------------------

struct LifeBarState;

struct ScoreState {
    int maxPureCount = 0;
    int pureCount = 0;
    int farCount = 0;
    int lostCount = 0;

    std::vector<LifeBarState*> lifeBars;

    // Other score/timing/statistics state intentionally omitted.
};

/*
 * CONFIRMED control flow:
 *
 * Successful judgement first calls a note virtual hook. Only if that hook
 * accepts the event does ScoreState increment counters and fan the event out to
 * every active LifeBarState.
 *
 * MAX PURE increments both the Max-Pure counter and the broad Pure counter.
 * PURE increments the broad Pure counter.
 * FAR increments the Far counter.
 */
void registerSuccessfulJudgement(
    ScoreState& score,
    LogicNote& note,
    Judgement judgement,
    TimingSide side,
    int judgementTimeMs,
    int inputPayload)
{
    if (!noteAcceptSuccessfulHit(
            note, judgementTimeMs, inputPayload)) {
        return;
    }

    switch (judgement) {
        case Judgement::MaxPure:
            ++score.maxPureCount;
            ++score.pureCount;
            break;

        case Judgement::Pure:
            ++score.pureCount;
            break;

        case Judgement::Far:
            ++score.farCount;
            break;

        case Judgement::Lost:
            return; // separate path
    }

    for (LifeBarState* bar : score.lifeBars) {
        lifeBarSuccessfulJudgement(
            *bar, &note, judgement, side,
            judgementTimeMs, inputPayload);
    }

    // CONFIRMED: ScoreState also records timing/statistical information here.
    // For ordinary point notes this uses judgementTime - note.startTime.
}

/*
 * CONFIRMED LOST path:
 *
 *   1. ask the note's virtual LOST hook whether this loss is accepted
 *   2. if accepted, increment ScoreState LOST count
 *   3. fan the LOST event to every LifeBarState
 *   4. perform additional score/ability bookkeeping
 */
void registerLost(
    ScoreState& score,
    LogicNote& note,
    int eventTimeMs)
{
    if (!noteAcceptLost(note)) {
        return;
    }

    ++score.lostCount;

    for (LifeBarState* bar : score.lifeBars) {
        lifeBarLost(*bar, &note, eventTimeMs);
    }
}

// -----------------------------------------------------------------------------
// 7. Why LogicLongNoteBase exists
// -----------------------------------------------------------------------------

/*
 * The key architectural difference is now CONFIRMED:
 *
 * Point note:
 *   one successful hit -> whole note hit/resolved
 *   one LOST           -> whole note lost/resolved
 *
 * Long note:
 *   successful-event hook -> returns true without setting whole-note hit flag
 *   LOST-event hook       -> returns true without setting whole-note lost flag
 *   completion            -> determined from an internal event/tick vector
 *
 * Therefore one LogicLongNoteBase object is an interval containing multiple
 * independently scoreable/resolvable events. This is why the gauge system can
 * receive repeated long-note losses and apply half ordinary-note LOST damage.
 */

struct LongTickEvent {
    int32_t timeMs;          // CONFIRMED first 4 bytes
    int32_t unknownValue;    // CONFIRMED created as 1; semantic UNRESOLVED
    uint8_t processedFlags;  // CONFIRMED low bit used by completion predicate
    uint8_t padding[3];
};

static_assert(sizeof(LongTickEvent) == 12);

struct LogicLongNoteBase : LogicNote {
    // CONFIRMED selected fields:
    float tickIntervalMs = 0.0f;            // around +0x70
    std::vector<LongTickEvent> tickEvents;  // vector storage around +0x78

    /*
     * CONFIRMED virtual behaviour in the base class.
     * It deliberately does not resolve the whole note on each event.
     */
    bool acceptSuccessfulEvent(int eventTimeMs, int inputPayload)
    {
        (void)eventTimeMs;
        (void)inputPayload;
        return true;
    }

    bool acceptLostEvent(bool value)
    {
        (void)value;
        return true;
    }

    /*
     * CONFIRMED completion predicate:
     *   - empty vector => complete
     *   - otherwise every tick must have processedFlags bit 0 set
     */
    bool areAllTickEventsProcessed() const
    {
        for (const LongTickEvent& event : tickEvents) {
            if ((event.processedFlags & 1) == 0) {
                return false;
            }
        }

        return true;
    }
};

struct LogicHoldNote : LogicLongNoteBase {
    // CONFIRMED inheritance.
    // Hold-specific input/continuous-contact processing is deferred.
};

struct LogicArcNote : LogicLongNoteBase {
    // CONFIRMED inheritance.
    // Arc overrides several long-note virtuals; geometry/path/rendering and
    // arc-specific event behaviour are deliberately deferred.
};

// -----------------------------------------------------------------------------
// 8. Common long-note tick interval
// -----------------------------------------------------------------------------

/*
 * CONFIRMED native arithmetic performed during LogicLongNoteBase init:
 *
 *   tempoMagnitude = abs(float field at timing-like object +0x20)
 *   beatLikeMs     = 60000 / tempoMagnitude
 *   divisor        = tempoMagnitude >= 255 ? 1 : 2
 *   tickInterval   = beatLikeMs / divisor / suppliedDensityFactor
 *
 * A surviving game string "TimingPointDensityFactor" strongly supports the
 * density interpretation, and 60000 / value strongly indicates BPM/tempo.
 * However this section has not yet proved the original field/member names.
 *
 * So:
 *   arithmetic                         = CONFIRMED
 *   +0x20 semantic name "BPM/tempo"    = RECONSTRUCTED
 *   supplied float as density factor   = RECONSTRUCTED, strongly supported
 *
 * With the reconstructed semantics and density = 1:
 *   tempo < 255  -> one tick interval per half beat
 *   tempo >=255  -> one tick interval per beat
 */
float calculateCommonLongTickInterval(
    float tempoLikeValue,
    float timingPointDensityFactor)
{
    const float tempoMagnitude = absoluteValue(tempoLikeValue);
    const float beatLikeMs = 60000.0f / tempoMagnitude;

    const float subdivisionDivisor =
        (tempoMagnitude >= 255.0f) ? 1.0f : 2.0f;

    return beatLikeMs
         / subdivisionDivisor
         / timingPointDensityFactor;
}

// -----------------------------------------------------------------------------
// 9. Common long-note tick/event construction
// -----------------------------------------------------------------------------

/*
 * CONFIRMED behaviour of LogicLongNoteBase's event builder.
 *
 * The boolean/phase argument chooses whether index 0 or index 1 is the first
 * candidate:
 *
 *   argument true  -> first index = 0
 *   argument false -> first index = 1
 *
 * Its original semantic name is UNRESOLVED, so "includeIndexZero" below is a
 * readable structural name rather than a claim about the original source.
 *
 * Candidate timestamps are:
 *
 *   trunc(startTime + tickInterval * index)
 *
 * The exact end timestamp is excluded.
 *
 * If no event was generated and duration != 0, one event is inserted at the
 * midpoint. Thus a short non-zero long note still gets one common event.
 */
void buildCommonLongTickEvents(
    LogicLongNoteBase& note,
    bool includeIndexZero)
{
    note.tickEvents.clear();

    const int durationMs = note.endTimeMs - note.startTimeMs;
    const int count = truncateToInt(
        static_cast<float>(durationMs) / note.tickIntervalMs);

    const int firstIndex = includeIndexZero ? 0 : 1;

    for (int i = firstIndex; i < count; ++i) {
        const int eventTime = truncateToInt(
            static_cast<float>(note.startTimeMs)
            + note.tickIntervalMs * static_cast<float>(i));

        if (eventTime < note.endTimeMs) {
            note.tickEvents.push_back(LongTickEvent{
                eventTime,
                1,       // CONFIRMED literal, semantic UNRESOLVED
                0,       // not processed yet
                {0, 0, 0}
            });
        }
    }

    if (note.tickEvents.empty() && durationMs != 0) {
        const int midpoint = truncateToInt(
            static_cast<float>(note.startTimeMs)
            + 0.5f * static_cast<float>(durationMs));

        note.tickEvents.push_back(LongTickEvent{
            midpoint,
            1,
            0,
            {0, 0, 0}
        });
    }
}

// -----------------------------------------------------------------------------
// 10. ScoreState treats long-event timestamps specially
// -----------------------------------------------------------------------------

/*
 * CONFIRMED additional evidence that the 12-byte records are gameplay events:
 * after a successful judgement, ScoreState dynamically checks whether the note
 * is a LogicLongNoteBase. For a long note it walks the event vector backwards
 * and selects the latest event timestamp <= the judgement time for subsequent
 * score/statistic bookkeeping.
 *
 * RECONSTRUCTED helper:
 */
int chooseRelevantLongEventTime(
    const LogicLongNoteBase& note,
    int judgementTimeMs)
{
    for (auto it = note.tickEvents.rbegin();
         it != note.tickEvents.rend();
         ++it) {
        if (it->timeMs <= judgementTimeMs) {
            return it->timeMs;
        }
    }

    // Exact native fallback outside the found-event case is intentionally not
    // generalised here; caller context decides what to do when no event matches.
    return note.startTimeMs;
}

// -----------------------------------------------------------------------------
// 11. Gameplay mental model
// -----------------------------------------------------------------------------

/*
 * POINT NOTE
 * ----------
 *
 *             LogicTapNote
 *                  |
 *          start == end time
 *                  |
 *          input candidate arrives
 *                  |
 *          absolute timing error
 *        /      |       |       \
 *    <=25    <=50    <=100    <=120
 *      |        |       |         |
 *   MaxPure   Pure     Far       LOST
 *      \        |       /         |
 *       \       |      /          |
 *        successful path       miss path
 *              |                  |
 *          note.hit = true    note.lost = true
 *              \                  /
 *                resolved forever
 *
 *
 * LONG NOTE BASE
 * --------------
 *
 *        start time ---------------- end time
 *             |     |     |     |
 *             +-----+-----+-----+---- generated event timestamps
 *                         |
 *              12-byte event records
 *                         |
 *        each successful/lost event is accepted
 *        without resolving the entire note object
 *                         |
 *           completion = every event processed
 *
 * This is the fundamental bridge from ordinary taps into holds and arcs.
 */

// -----------------------------------------------------------------------------
// 12. CONFIRMED / RECONSTRUCTED / UNRESOLVED summary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED
 * ---------
 * - C++ RTTI hierarchy shown in section 1.
 * - Tap startTime == endTime.
 * - Point-note hit flag (+0x0D), LOST flag (+0x0C), and hit-or-lost resolution.
 * - Judgement bands: <=25 Max Pure, <=50 Pure, <=100 Far, <=120 LOST.
 * - ScoreState owns judgement counters and fans events to LifeBarState objects.
 * - MAX PURE increments Max-Pure plus broad-Pure counters.
 * - LogicLongNoteBase uses repeated event semantics rather than resolving on one
 *   hit/miss.
 * - Common long-note event vector uses 12-byte records with timestamp first and
 *   processed bit 0 at byte +8.
 * - End timestamp is excluded from ordinary generated candidates.
 * - Empty non-zero-duration long notes receive a midpoint fallback event.
 * - Tick interval arithmetic including constants 60000 and 255.
 * - Logic* and Render* note class families exist separately.
 *
 * RECONSTRUCTED
 * -------------
 * - Timing-side values 1 = Early, 2 = Late.
 * - timing-like +0x20 float is BPM/tempo.
 * - supplied long-note float is TimingPointDensityFactor.
 * - helper/function/member names used throughout this file.
 *
 * UNRESOLVED
 * ----------
 * - exact semantic name of LogicNote +0x10 hit payload.
 * - exact original names of several common LogicNote pointers/indices.
 * - exact semantic meaning of LongTickEvent's second int (created as 1).
 * - original semantic name of the tick-builder phase/index-zero boolean.
 * - exact scheduling relationship between the 101..120 input LOST band and
 *   automatic late-miss processing.
 * - exact timing-state member names and the alternate branch's +/-3000 wrap
 *   correction used before the judgement windows are applied.
 * - how hold contact marks/processes each tick.
 * - how arc tracking marks/processes each tick.
 * - arc-specific event generation, path geometry, rendering, and ArcTap layout.
 */

// -----------------------------------------------------------------------------
// 13. Next excavation boundary
// -----------------------------------------------------------------------------

/*
 * The next sensible slice is long-note event PROCESSING, not arc rendering yet:
 *
 *   1. Trace LogicHoldNote's contact/input path.
 *   2. Identify where LongTickEvent.processedFlags bit 0 is set.
 *   3. Determine exactly when a hold tick becomes successful vs LOST.
 *   4. Compare that contract with LogicArcNote's overrides.
 *
 * Once that common event lifecycle is understood, arc geometry can be studied
 * without confusing scoring/tick logic with rendering/path mathematics.
 */
