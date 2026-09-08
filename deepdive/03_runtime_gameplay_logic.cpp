// Arcaea native-engine deep dive
// Section 03: Runtime gameplay logic
//
// IMPORTANT:
// This is reconstructed reference pseudocode, NOT recovered original source.
// Evidence comes from current-binary disassembly, RTTI/typeinfo/vtables,
// source-to-runtime conversion paths, GameModel touch handlers, scheduler code,
// and earlier completed excavation notes that were rechecked against the same
// native anchors in this build.
//
// Evidence labels:
//   CONFIRMED     = directly supported by native data/control flow.
//   RECONSTRUCTED = readable semantic structure assembled from confirmed facts.
//   UNRESOLVED    = exact original name/design reason is not proved.
//
// Scope: main gameplay runtime logic only. Rendering is intentionally deferred
// except where a runtime field's purpose cannot otherwise be identified.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace reconstructed {

// ============================================================================
// 1. CONFIRMED runtime hierarchy
// ============================================================================

struct LogicEvent : cocos2d::Ref {};
struct LogicTimingEvent : LogicEvent {};

struct LogicNote : cocos2d::Ref {};
struct LogicTapNote : LogicNote {};
struct LogicArcTapNote : LogicTapNote {};
struct LogicLongNoteBase : LogicNote {};
struct LogicHoldNote : LogicLongNoteBase {};
struct LogicArcNote : LogicLongNoteBase {};

// RTTI for LogicFlickNote survives, but this investigated build has no
// LogicFlickNote class vtable and no runtime producer. Downstream handler code
// survives, but no live chart->LogicFlickNote bridge exists.
struct LogicFlickNote : LogicNote {}; // DORMANT in this build

struct LogicSceneControl : LogicNote {};
struct LogicCameraControl : LogicNote {};

// ============================================================================
// 2. CONFIRMED chart/source -> runtime conversion
// ============================================================================

/*
 * LogicChart walks source Note records and has live conversion routes for:
 *
 *   SimpleNote
 *   CameraControl
 *   SceneControl
 *   HoldNote
 *   ArcNote
 *
 * Timing records are converted separately to LogicTimingEvent streams.
 * No active FlickNote -> LogicFlickNote construction branch exists.
 *
 * Each runtime note is also assigned the latest LogicTimingEvent in its timing
 * group whose event time is <= the note's own time.
 */

LogicTimingEvent* selectActiveTiming(
    const std::vector<LogicTimingEvent*>& events,
    int32_t noteTimeMs)
{
    if (events.empty()) return nullptr;

    LogicTimingEvent* selected = events.front();
    for (LogicTimingEvent* event : events) {
        if (event->startTimeMs > noteTimeMs) break;
        selected = event;
    }
    return selected;
}

// ============================================================================
// 3. CONFIRMED global gameplay clock
// ============================================================================

/*
 * Judgement and expiry use ONE shared effective gameplay clock. TimingGroups do
 * not own private judgement clocks.
 *
 * Selected clock fields observed in the shared clock object:
 *   +0x20 synchronized/live source
 *   +0x28 common offset
 *   +0x2D selects synchronized/live path
 *   +0x34 fallback source
 *
 * The fallback branch has a confirmed extra -3000 ms pre-roll while its source
 * is <= 0.
 */
struct GameplayClock {
    int32_t synchronizedSource;
    int32_t commonOffset;
    bool useSynchronizedPath;
    int32_t fallbackSource;
};

int32_t effectiveGameplayTime(const GameplayClock& clock)
{
    if (clock.useSynchronizedPath) {
        return clock.synchronizedSource - clock.commonOffset;
    }

    int32_t t = clock.fallbackSource - clock.commonOffset;
    if (clock.fallbackSource <= 0) {
        t -= 3000;
    }
    return t;
}

// ============================================================================
// 4. CONFIRMED LogicTimingEvent representation
// ============================================================================

/*
 * Deep-dive Section 02 established source Timing as:
 *   time, bpm, beatsPerMeasure
 *
 * Runtime preserves raw BPM and also builds a highspeed-normalised spatial BPM.
 * LogicChart +0xF0 is the selected highspeed value and +0xF4 is the confirmed
 * scroll-scale formula highspeed * 180 / chartBaseBpm.
 */
struct LogicTimingEvent : LogicEvent {
    int32_t startTimeMs;            // +0x10
    int32_t endTimeMs;              // +0x14, same as start
    float effectiveSpatialBpm;      // +0x18
    float beatsPerMeasure;          // +0x1C
    float rawBpm;                   // +0x20
    float unknown24;                // +0x24, semantic still unresolved
    float secondsPerEffectiveBeat;  // +0x28
};

LogicTimingEvent makeTimingEvent(
    int32_t timeMs,
    float rawBpm,
    float beatsPerMeasure,
    float scrollScale)
{
    LogicTimingEvent out{};
    out.startTimeMs = timeMs;
    out.endTimeMs = timeMs;
    out.rawBpm = rawBpm;
    out.beatsPerMeasure = beatsPerMeasure;
    out.effectiveSpatialBpm = rawBpm * scrollScale;
    out.secondsPerEffectiveBeat = 60.0f / out.effectiveSpatialBpm;
    return out;
}

// Gameplay split:
//   rawBpm                  -> Hold/Arc tick timing
//   effectiveSpatialBpm / beat duration -> note/Arc spatial progression
//   global GameplayClock    -> actual judgement/expiry timestamp

// ============================================================================
// 5. CONFIRMED common LogicNote state
// ============================================================================

/*
 * Selected common fields recovered from the shared LogicNote initialiser and
 * point-note virtual hooks.
 *
 * +0x0C point-note LOST flag
 * +0x0D point-note successful-hit flag
 * +0x10 input/hit payload, NOT the Judgement enum
 * +0x18 start time
 * +0x1C end time
 * +0x20/+0x28 start/end NotePosition pointers
 * +0x48 active LogicTimingEvent pointer
 * +0x50 timing-group ID
 * +0x54 inputEnabled (from timinggroup noinput)
 * +0x60 monotonically assigned runtime serial
 */
struct LogicNoteSelected {
    bool lost;                       // +0x0C for point-note path
    bool hit;                        // +0x0D for point-note path
    int32_t inputPayload;            // +0x10, exact semantic name unresolved

    int32_t startTimeMs;             // +0x18
    int32_t endTimeMs;               // +0x1C

    NotePosition* startPosition;     // +0x20
    NotePosition* endPosition;       // +0x28

    LogicTimingEvent* activeTiming;  // +0x48
    int32_t timingGroupId;           // +0x50
    bool inputEnabled;               // +0x54

    uint32_t runtimeSerial;          // +0x60
};

// Point-note resolution is state-based, not object deletion:
bool pointNoteResolved(const LogicNoteSelected& note)
{
    return note.hit || note.lost;
}

// Core scheduler/touch passes skip already-resolved point notes. No per-hit
// deletion is required for correctness.

// ============================================================================
// 6. CONFIRMED floor touch -> lane candidates
// ============================================================================

/*
 * Raw screen X is NOT compared to a note sprite. Touches are projected into
 * gameplay/floor space first. The primary lane classifier then uses gameplay X.
 * Normal four lanes are internal IDs 2..5; IDs 1 and 6 are reserved outer lanes.
 *
 * Exact primary thresholds:
 *   x < -850         -> lane 1 only when outer lanes active, otherwise lane 2
 *   x < -425         -> lane 2
 *   x < 0            -> lane 3
 *   x < +425         -> lane 4
 *   x == +425        -> no primary lane (confirmed native seam)
 *   x <= +850        -> lane 5
 *   x > +850         -> lane 6 only when outer lanes active, otherwise lane 5
 *
 * A touch-width scale can additionally produce one neighbouring lane candidate.
 */
struct LaneCandidates {
    int32_t primary;
    int32_t adjacent;
};

bool noteLaneMatchesTouch(
    const NotePosition& position,
    const LaneCandidates& touch)
{
    return position.mode == NotePositionMode::DiscreteLane &&
           (position.laneId == touch.primary ||
            position.laneId == touch.adjacent);
}

// ============================================================================
// 7. CONFIRMED point-note candidate and judgement state machine
// ============================================================================

enum class Judgement : int32_t {
    MaxPure = 0,
    Pure    = 1,
    Far     = 2,
    Lost    = 3 // conceptual value; ScoreState LOST has a separate entry path
};

enum class TimingSide : int32_t {
    ExactOrUnused = 0,
    Early = 1, // semantic reconstruction from comparison direction
    Late  = 2
};

/*
 * On touch-begin GameModel builds a temporary candidate list:
 *   - ordinary floor LogicTapNote: lane-ID filter
 *   - LogicHoldNote: lane + hold acquisition-time filter
 *   - LogicArcNote: inspect its LogicArcTapNote children and spatially test them
 *
 * Candidates are sorted by note +0x18 start timestamp. Point candidates then use
 * the common point-note timing routine.
 */

bool tryJudgePointNote(
    LogicTapNote& note,
    int32_t nowMs,
    int32_t inputPayload,
    ScoreState& score)
{
    if (note.hit || note.lost) return false;

    const int32_t signedError = nowMs - note.startTimeMs;
    const int32_t absError = std::abs(signedError);
    const TimingSide side =
        (nowMs < note.startTimeMs) ? TimingSide::Early : TimingSide::Late;

    if (absError <= 25) {
        registerSuccessfulJudgement(
            score, note, Judgement::MaxPure,
            TimingSide::ExactOrUnused, nowMs, inputPayload);
        return true;
    }
    if (absError <= 50) {
        registerSuccessfulJudgement(
            score, note, Judgement::Pure, side, nowMs, inputPayload);
        return true;
    }
    if (absError <= 100) {
        registerSuccessfulJudgement(
            score, note, Judgement::Far, side, nowMs, inputPayload);
        return true;
    }
    if (absError <= 120) {
        registerLost(score, note, nowMs);
        return true;
    }
    return false;
}

/*
 * IMPORTANT boundary note:
 * Those are the candidate routine's own inclusive bands. Automatic scheduling
 * can resolve an overdue note independently, so do not interpret 120 ms as a
 * universally reachable symmetric late-input window in every outer state.
 */

// LogicTapNote successful hook:
bool LogicTapNote_acceptHit(
    LogicTapNote& note,
    int32_t judgementTimeMs,
    int32_t inputPayload)
{
    (void)judgementTimeMs;
    note.hit = true;
    note.inputPayload = inputPayload;
    return true;
}

bool LogicTapNote_acceptLost(LogicTapNote& note)
{
    note.lost = true;
    return true;
}

// ============================================================================
// 8. CONFIRMED ScoreState judgement fan-out
// ============================================================================

struct LifeBarState;

struct ScoreState {
    int32_t maxPureCount; // observed +0x20 family
    int32_t pureCount;    // broad Pure count; MaxPure increments this too
    int32_t farCount;
    int32_t lostCount;

    // Exact offsets omitted here where not required for the model.
    std::vector<LifeBarState*> lifeBars;

    // More timing/statistical fields exist and are updated by successful hits.
};

void registerSuccessfulJudgement(
    ScoreState& score,
    LogicNote& note,
    Judgement judgement,
    TimingSide side,
    int32_t judgementTimeMs,
    int32_t inputPayload)
{
    // CONFIRMED ordering: note virtual hook happens FIRST.
    if (!noteAcceptSuccessfulHit(note, judgementTimeMs, inputPayload)) {
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
            return;
    }

    // CONFIRMED synchronous fan-out to every active LifeBarState.
    for (LifeBarState* bar : score.lifeBars) {
        lifeBarSuccessfulJudgement(
            *bar, &note, judgement, side,
            judgementTimeMs, inputPayload);
    }

    // ScoreState also records timing/statistical information here.
}

void registerLost(
    ScoreState& score,
    LogicNote& note,
    int32_t eventTimeMs)
{
    // CONFIRMED separate miss path and ordering.
    if (!noteAcceptLost(note)) {
        return;
    }

    ++score.lostCount;

    for (LifeBarState* bar : score.lifeBars) {
        lifeBarLost(*bar, &note, eventTimeMs);
    }
}

// ============================================================================
// 9. CONFIRMED baseline LifeBarState / Recollection propagation
// ============================================================================

/*
 * Selected confirmed LifeBarState fields:
 *   +0x10 current Recollection Rate
 *   +0x8C Recollection Factor (RF)
 *   +0x98 LOST-damage percentage scale; 100 = normal
 *   +0xA0 gauge mode
 *   +0xC8 CharacterAbility*
 *
 * This section only records the baseline Normal/Easy/Hard mechanics necessary
 * to close judgement propagation. Special partner/gauge mechanics are separate
 * modifiers and are not expanded here.
 */
enum class GaugeMode : int32_t {
    Normal = 0,
    Easy   = 1,
    Hard   = 2
};

struct CharacterAbility {
    virtual float modifyGain(float gain, Judgement judgement);
    virtual float modifyLoss(float loss, Judgement judgement);
};

struct LifeBarState {
    float recollectionRate;       // +0x10
    float recollectionFactor;     // +0x8C
    float lossScalePercent;       // +0x98
    GaugeMode gaugeMode;          // +0xA0
    CharacterAbility* ability;    // +0xC8

    float minimum;
    float maximum;
    bool deadOrDisabled;
};

float calculateRecollectionFactor(int32_t noteCount, bool reduceToEightyPercent)
{
    float rf;

    if (noteCount < 400) {
        rf = 0.2f + 80.0f / float(noteCount);
    } else if (noteCount < 600) {
        rf = 0.2f + 32.0f / float(noteCount);
    } else {
        rf = 0.08f + 96.0f / float(noteCount);
    }

    if (reduceToEightyPercent) rf *= 0.8f;
    return rf;
}

float successfulGaugeGain(const LifeBarState& bar, Judgement judgement)
{
    if (judgement == Judgement::MaxPure || judgement == Judgement::Pure)
        return bar.recollectionFactor;
    if (judgement == Judgement::Far)
        return bar.recollectionFactor * 0.5f;
    return 0.0f;
}

float baselineStandaloneLostDamage(const LifeBarState& bar)
{
    switch (bar.gaugeMode) {
        case GaugeMode::Normal: return 2.0f;
        case GaugeMode::Easy:   return 1.2f;
        case GaugeMode::Hard:
            return (bar.recollectionRate <= 30.0f) ? 5.0f : 9.0f;
    }
    return 0.0f;
}

float baselineLostDamage(const LifeBarState& bar, const LogicNote* note)
{
    float loss = baselineStandaloneLostDamage(bar);

    // CONFIRMED: one long-note LOST event has half standalone-note damage.
    if (dynamic_cast<const LogicLongNoteBase*>(note) != nullptr) {
        loss *= 0.5f;
    }

    loss *= bar.lossScalePercent / 100.0f;
    return loss;
}

/*
 * Hard also has a confirmed 30-RR crossing correction when one loss crosses from
 * above 30 to below 30:
 *
 *   after += 0.5 * (before - 30)
 *
 * Common application then clamps to the gauge's allowed range. Ability hooks can
 * alter gain/loss before the final application.
 */

// ============================================================================
// 10. CONFIRMED LogicLongNoteBase event model
// ============================================================================

struct LongTickEvent {
    int32_t timeMs;
    int32_t scoreUnits;      // RECONSTRUCTED semantic name; common builder = 1
    uint8_t processedFlags;  // bit 0 = processed
    uint8_t padding[3];
};
static_assert(sizeof(LongTickEvent) == 12);

struct LogicLongNoteBaseSelected : LogicNoteSelected {
    uint8_t currentContact64;      // semantics subclass-specific
    uint8_t secondaryContact65;
    uint8_t longEventState66;
    uint8_t directionSeamRule6C;   // Arc continuation direction-change trigger

    float tickIntervalMs;          // +0x70
    std::vector<LongTickEvent> tickEvents; // vector storage +0x78 family
};

float calculateLongTickIntervalMs(
    const LogicTimingEvent& timing,
    float timingPointDensityFactor)
{
    const float bpm = std::fabs(timing.rawBpm);
    const float beatMs = 60000.0f / bpm;
    const float subdivision = (bpm >= 255.0f) ? 1.0f : 2.0f;
    return beatMs / subdivision / timingPointDensityFactor;
}

void buildLongTickEvents(
    LogicLongNoteBaseSelected& note,
    bool includeIndexZero)
{
    note.tickEvents.clear();

    const int32_t duration = note.endTimeMs - note.startTimeMs;
    const int32_t count = int(float(duration) / note.tickIntervalMs);
    const int32_t first = includeIndexZero ? 0 : 1;

    for (int32_t i = first; i < count; ++i) {
        const int32_t t = int(
            float(note.startTimeMs) + note.tickIntervalMs * float(i));

        if (t < note.endTimeMs) {
            note.tickEvents.push_back({t, 1, 0, {0,0,0}});
        }
    }

    if (note.tickEvents.empty() && duration != 0) {
        const int32_t midpoint = int(
            float(note.startTimeMs) + 0.5f * float(duration));
        note.tickEvents.push_back({midpoint, 1, 0, {0,0,0}});
    }
}

bool allLongEventsProcessed(const LogicLongNoteBaseSelected& note)
{
    for (const LongTickEvent& e : note.tickEvents) {
        if ((e.processedFlags & 1) == 0) return false;
    }
    return true;
}

int collectSuccessfulLongEvents(
    LogicLongNoteBaseSelected& note,
    int32_t nowMs)
{
    const int32_t cutoff = int(nowMs + 0.5f * note.tickIntervalMs);

    int eligible = 0;
    while (eligible < int(note.tickEvents.size()) &&
           note.tickEvents[eligible].timeMs <= cutoff) {
        ++eligible;
    }

    int units = 0;
    for (int i = eligible - 1; i >= 0; --i) {
        LongTickEvent& e = note.tickEvents[i];
        if ((e.processedFlags & 1) != 0) break;
        units += e.scoreUnits;
        e.processedFlags |= 1;
    }

    if (units > 0) note.longEventState66 = 1;
    return units;
}

int collectOverdueLostLongEvents(
    LogicLongNoteBaseSelected& note,
    int32_t nowMs)
{
    int eligible = 0;

    const int32_t probeCutoff = int(
        nowMs - std::min(0.5f * note.tickIntervalMs, 500.0f));
    const int probeCount = countEventsAtOrBefore(note.tickEvents, probeCutoff);

    if (probeCount == 1 && note.directionSeamRule6C != 0) {
        eligible = 1;
    } else {
        const float grace = std::min(2.0f * note.tickIntervalMs, 500.0f);
        const int32_t lossCutoff = int(nowMs - grace);
        eligible = countEventsAtOrBefore(note.tickEvents, lossCutoff);
    }

    int units = 0;
    for (int i = eligible - 1; i >= 0; --i) {
        LongTickEvent& e = note.tickEvents[i];
        if ((e.processedFlags & 1) != 0) break;
        units += e.scoreUnits;
        e.processedFlags |= 1;
    }

    if (units > 0) note.longEventState66 = 0;
    return units;
}

/*
 * Every successful long-event score unit enters ScoreState as judgement 0
 * (MaxPure class), side 0, input payload -1. Long events do not run the point
 * note +/-25/50/100 ms judgement ladder.
 *
 * Every expired long-event score unit enters ScoreState's separate LOST path.
 */

// ============================================================================
// 11. CONFIRMED Hold runtime state machine
// ============================================================================

struct LogicHoldNoteSelected : LogicLongNoteBaseSelected {
    void* unknownA0;
    bool engagedByEligibleTouch; // +0xA8, reconstructed name
    bool fadingHolds;            // +0xA9, copied from source modifier
};

bool tryEngageHold(
    LogicHoldNoteSelected& hold,
    const LaneCandidates& touchLanes,
    int32_t nowMs)
{
    if (!noteLaneMatchesTouch(*hold.startPosition, touchLanes)) return false;
    if (nowMs >= hold.endTimeMs) return false;
    if (hold.startTimeMs >= nowMs + 100) return false;

    hold.engagedByEligibleTouch = true;
    return true;
}

void refreshHoldContact(
    LogicHoldNoteSelected& hold,
    const LaneCandidates& touchLanes,
    int32_t nowMs)
{
    if (!hold.engagedByEligibleTouch) return;
    if (nowMs < hold.startTimeMs || nowMs >= hold.endTimeMs) return;
    if (!noteLaneMatchesTouch(*hold.startPosition, touchLanes)) return;

    hold.currentContact64 = 1;
    hold.secondaryContact65 = 1;
}

void processHoldEvents(
    LogicHoldNoteSelected& hold,
    ScoreState& score,
    int32_t nowMs)
{
    if (hold.currentContact64) {
        const int units = collectSuccessfulLongEvents(hold, nowMs);
        for (int i = 0; i < units; ++i) {
            registerSuccessfulJudgement(
                score, hold, Judgement::MaxPure,
                TimingSide::ExactOrUnused, nowMs, -1);
        }
    } else {
        const int units = collectOverdueLostLongEvents(hold, nowMs);
        for (int i = 0; i < units; ++i) {
            registerLost(score, hold, nowMs);
        }
    }
}

/*
 * Release does NOT instantly miss the next Hold event. Removing the touch means
 * currentContact64 stops being refreshed, then pending events age toward the
 * overdue cutoff. Re-press before expiry can still satisfy unprocessed events.
 * Already-processed LOST events cannot be recovered.
 */

// ============================================================================
// 12. CONFIRMED Arc body runtime state
// ============================================================================

enum class ArcBodyMode : int32_t {
    JudgedBody = 0,
    TraceOrSkyline = 1, // friendly name; source token is true
    Designant = 2      // native token identity confirmed
};

struct LogicArcGroup;
struct LogicColor;

struct LogicArcNoteSelected : LogicLongNoteBaseSelected {
    bool connectedContinuation; // +0xA0, reconstructed name
    ArcBodyMode bodyMode;        // +0xA4
    LogicColor* logicColor;      // +0xB0, native RTTI identity confirmed

    bool arcActive;              // +0xD0, reconstructed name
    float expectedX;             // +0xD4 family
    int32_t expectedYOrQuantized;// +0xD8 family; exact coordinate type omitted

    LogicArcGroup* arcGroup;     // +0xE0, confirmed identity

    std::vector<LogicArcTapNote*> arcTaps; // +0x120 family

    bool special170;             // +0x170; special-system semantics deferred
};

bool ordinaryArcBodyIsJudged(const LogicArcNoteSelected& arc)
{
    return arc.bodyMode == ArcBodyMode::JudgedBody;
}

/*
 * Runtime auto-promotion is confirmed:
 * if chart mode is 0 and ArcTap children exist, LogicArcNote initialisation
 * changes body mode to 1. Thus an Arc carrying ArcTaps does not remain an
 * ordinary judged body through this initialiser.
 */

// ============================================================================
// 13. CONFIRMED LogicColor touch ownership for judged Arc bodies
// ============================================================================

/*
 * Earlier reconstruction called +0xB0 an Arc touch tracker. Surviving RTTI now
 * identifies it as native LogicColor. LogicColor combines colour-channel state
 * with touch ownership. Arcs on the same channel can share the same object.
 */
struct LogicColor {
    float tickIntervalMs;            // +0x0C
    bool nearbyOwnershipBypass;      // +0x10
    int32_t nearbyRefreshTimeMs;     // +0x14
    int32_t channelId;               // +0x18

    bool acceptedThisUpdate;         // +0x22
    bool hasNormalOwnershipHistory;  // +0x24
    int32_t assignedTouchId;         // +0x28, -1 means none
    int32_t releaseLockoutStartMs;   // +0x2C

    // Additional rejection-feedback fields omitted from baseline logic model.
};

static int32_t arcOwnershipLockoutMs(float tickIntervalMs)
{
    return int(std::min(4.0f * tickIntervalMs, 1000.0f));
}

/*
 * Ordinary acceptance order:
 *  1. active release lockout rejects touches
 *  2. channelId == 3 bypasses ordinary ownership identity checks
 *  3. same assigned touch ID accepts
 *  4. nearby ownership relaxation accepts without exact-ID equality
 *  5. another ID cannot replace an ordinary still-assigned ID
 *  6. unassigned channel may freshly claim only a globally unclaimed touch ID
 *
 * A process/global claimed-touch-ID list enforces ordinary cross-channel
 * exclusivity. Geometry is checked BEFORE this ownership layer.
 */

/*
 * On release of the currently assigned touch:
 *   assignedTouchId = -1
 *   remove ID from global claimed list
 *   optionally start min(4*tickInterval,1000) re-acquisition lockout
 *
 * This timer is different from the long-event LOST grace:
 *   ownership lockout      = min(4*tick,1000)
 *   event LOST grace       = min(2*tick,500)
 */

// ============================================================================
// 14. CONFIRMED Arc geometric qualification -> long-event judgement
// ============================================================================

/*
 * Every frame normal judged Arcs cache their current expected path point and an
 * active/playable flag. Transient body contact bytes +0x64/+0x65 are cleared and
 * must be re-established by currently qualified touches.
 *
 * Touch-begin acquisition uses the same spatial predicate with about +120 ms
 * lookahead toward Arc start.
 *
 * Active-touch qualification:
 *   - body mode must be 0
 *   - Arc must be in the proper active phase
 *   - touch is transformed to gameplay/sky space
 *   - expected path point is tested using camera/screen-scaled bounds
 *   - this is a box/axis-bound test, NOT a simple radius <= 212 test
 *   - LogicColor ownership must then accept the touch, unless a separate special
 *     runtime flag bypasses ownership after geometry
 *
 * Successful tail:
 *   arc.currentContact64 = 1
 *   arc.secondaryContact65 = 1
 *   and connected LogicArcGroup contact state is refreshed.
 */

void processArcBodyEvents(
    LogicArcNoteSelected& arc,
    ScoreState& score,
    int32_t nowMs)
{
    if (!ordinaryArcBodyIsJudged(arc)) {
        return; // no ordinary body success or body LOST processing
    }

    if (arc.currentContact64) {
        const int units = collectSuccessfulLongEvents(arc, nowMs);
        for (int i = 0; i < units; ++i) {
            registerSuccessfulJudgement(
                score, arc, Judgement::MaxPure,
                TimingSide::ExactOrUnused, nowMs, -1);
        }
    } else {
        const int units = collectOverdueLostLongEvents(arc, nowMs);
        for (int i = 0; i < units; ++i) {
            registerLost(score, arc, nowMs);
        }
    }
}

// ============================================================================
// 15. CONFIRMED connected Arc continuation / seam logic
// ============================================================================

/*
 * Connected Arc postprocessing accepts near-contiguous pieces using conditions
 * including:
 *   abs(next.startTime - previous.endTime) <= 9 ms
 *   abs(next.xStart - previous.xEnd) < 0.1
 *   next.yStart == previous.yEnd
 *
 * Every accepted continuation receives +0xA0 = 1.
 *
 * If the sign of X movement OR Y movement changes across the seam, continuation
 * +0x6C = 1. This is the same byte used by the special first-event overdue path
 * in LogicLongNoteBase.
 *
 * Thus +0x6C is no longer an anonymous modifier: on Arc continuations it marks a
 * connected seam where movement direction changes.
 *
 * LogicArcGroup (+0xE0) carries shared current contact/timing state across
 * connected pieces. Relevant group contact bytes are reset each frame and
 * refreshed when a member Arc obtains valid contact.
 */

// ============================================================================
// 16. CONFIRMED ArcTap runtime model
// ============================================================================

struct LogicArcTapNoteSelected : LogicTapNote {
    // +0x80..+0x8B cached 12-byte parent-path point
    Vec3 cachedPathPoint;
    LogicArcNote* parentArc; // +0x90, semantic identity strongly reconstructed
};

/*
 * ArcTap is NOT an Arc-body tick.
 *
 * Parent Arc path construction evaluates each ArcTap timestamp through the same
 * Arc easing/path machinery and caches a path point in the child.
 *
 * On touch-begin GameModel visits Arc parents, spatially tests child ArcTaps in
 * sky/gameplay space, and pushes qualified children into the SAME temporary
 * point-candidate list as ordinary taps. That list is sorted by timestamp.
 *
 * ArcTap then uses the exact common point-note timing routine:
 *   <=25  MaxPure
 *   <=50  Pure
 *   <=100 Far
 *   <=120 LOST candidate branch
 *
 * Parent current Arc-body contact is NOT required for ArcTap point judgement.
 */

/*
 * ArcTap overrides its hit and LOST hooks. Local hit/lost state is written first.
 * If parent Arc body mode == 2, the hook then vetoes ScoreState fan-out, so local
 * resolution can occur without normal score/gauge accounting. Modes other than
 * 2 use the ordinary point-note ScoreState path.
 *
 * Automatic ArcTap miss handling is parent-owned and checks unresolved children
 * after they are overdue (observed > childTime + 100 ms branch).
 */

// ============================================================================
// 17. CONFIRMED runtime SceneControl and CameraControl form
// ============================================================================

/*
 * SceneControl source strings are converted to compact command IDs before live
 * gameplay handling. Runtime does not repeatedly compare the original AFF string
 * in the frame loop.
 */
struct LogicSceneControlSelected : LogicNoteSelected {
    int32_t commandId;       // +0x64 family
    float floatParameter;    // +0x68
    int32_t intParameter;    // +0x6C
};

/*
 * CameraControl keeps the six floats in source order. Runtime groups the first
 * three as movement and the second three as a separate orientation/rotation
 * vector. Mirror negates movement-X and the sixth/rotation-Z-like component.
 */
struct LogicCameraControlSelected : LogicNoteSelected {
    float moveX, moveY, moveZ;      // +0x64..+0x6C
    float rotateX, rotateY, rotateZ;// +0x70..+0x78, semantic name reconstructed
    std::string easing;             // +0x80
    int32_t duration;               // +0x98
};

// ============================================================================
// 18. CONFIRMED touch lifetime in GameModel
// ============================================================================

/*
 * GameModel::initializeTouchEvents() installs four Cocos touch callbacks.
 *
 * Touch-begin:
 *   - retain/register Touch* in GameModel's active-touch collection
 *   - project touch into gameplay space
 *   - build point/Hold/ArcTap candidates
 *   - immediate point-note judgement can happen here
 *   - Hold and Arc acquisition state can be established here
 *
 * Touch-end / cancel:
 *   - find and remove Touch* from active-touch collection
 *   - release retained Touch*
 *   - propagate release to relevant Hold/LogicColor/Arc ownership state
 *
 * Therefore notes do not poll raw Android touch state. GameModel maintains a
 * gameplay-facing collection of active Cocos touches.
 */

// ============================================================================
// 19. CONFIRMED logic-critical update order
// ============================================================================

/*
 * The investigated main GameModel update function contains the following
 * confirmed ordering around the core gameplay passes:
 *
 *   A. derive/update effective gameplay time and timing/spatial state
 *
 *   B. update/reset per-frame note/Arc state
 *      - Arc path/current-position state
 *      - LogicColor per-frame ownership state
 *      - LogicArcGroup current-contact state
 *      - transient long-note contact bytes are prepared for refresh
 *
 *   C. ACTIVE-TOUCH REFRESH
 *      native call anchor ~0x14858D4
 *      - active touches are projected again
 *      - Hold current contact is re-latched when lane/time match
 *      - Arc geometry + LogicColor ownership can re-latch Arc current contact
 *      - surviving dormant Flick consumer branch also exists here, but no live
 *        LogicFlickNote producer exists in this build
 *
 *   D. COMMON GAMEPLAY SCHEDULER / AUTO-MISS / LONG-EVENT PASS
 *      native anchor ~0x0F8056C
 *      - runs immediately AFTER active-touch refresh in the main update
 *      - skips resolved point notes
 *      - expires overdue point-like notes through ScoreState LOST path
 *      - for long notes, currentContact selects success vs overdue-LOST scanner
 *      - ArcTap children receive their parent-owned automatic-miss checks
 *      - ScoreState calls occur synchronously as events resolve
 *
 *   E. subsequent gameplay/session state handling continues after scheduler
 *      (special scene/session transitions etc. are outside this baseline section)
 *
 * The key guarantee is C -> D: current-frame input contact is refreshed before
 * long-note tick judgement/expiry is evaluated.
 */

void gameplayUpdateCore(GameModel& game)
{
    const int32_t now = effectiveGameplayTime(*game.clock);

    updateTimingAndSpatialState(game, now);
    resetAndUpdatePerFrameArcAndContactState(game, now);

    refreshGameplayFromActiveTouches(game, now); // ~0x14858D4

    runSchedulerAndAutomaticJudgement(game, now); // ~0x0F8056C
}

/*
 * Point-note success is partly EVENT-DRIVEN rather than waiting for this frame
 * scheduler:
 *
 *   touch-begin callback
 *       -> spatial candidate list
 *       -> sorted point candidates
 *       -> tryJudgePointNote immediately
 *       -> ScoreState immediately
 *       -> LifeBarState immediately
 *
 * By contrast, automatic misses and long-note ticks are FRAME/SCHEDULER driven.
 * This two-entry design is important when reconstructing exact timing behaviour.
 */

// ============================================================================
// 20. Full beginner-readable runtime mental model
// ============================================================================

/*
 * CHART LOAD
 * -----------
 * source Note
 *    |
 *    +--> active TimingEvent for this timing group
 *    |
 *    v
 * LogicNote object
 *
 *
 * TAP / ARCTAP
 * ------------
 * finger begins
 *    |
 *    v
 * spatially eligible candidate?
 *    |
 *    v
 * sort candidates by note time
 *    |
 *    v
 * timing error
 *  | <=25  -> MaxPure
 *  | <=50  -> Pure
 *  | <=100 -> Far
 *  ` <=120 -> LOST candidate path
 *    |
 *    v
 * ScoreState
 *    |
 *    v
 * every LifeBarState
 *
 *
 * HOLD / JUDGED ARC BODY
 * ----------------------
 * long note owns generated internal tick events
 *
 * every frame:
 *   clear temporary contact
 *         |
 *   active touch still qualifies?
 *      /             \
 *    yes              no
 *     |                |
 * contact=1        contact stays 0
 *     |                |
 *     v                v
 * success ticks    overdue LOST ticks
 * now + .5*tick    now - min(2*tick,500)
 *     |                |
 *     +-------+--------+
 *             |
 *             v
 *        ScoreState
 *             |
 *             v
 *      LifeBarState(s)
 *
 * Each long tick is its own scoring/gauge event. The whole Hold/Arc object does
 * not become globally "hit" after the first successful tick.
 */

// ============================================================================
// 21. CONFIRMED dormant Flick boundary
// ============================================================================

/*
 * Stronger than merely "factory branch not found":
 *
 *   - chart FlickNote parser/class exists
 *   - LogicFlickNote RTTI exists
 *   - downstream Flick gameplay handler code exists
 *   - RenderFlickNote support exists
 *   - BUT LogicFlickNote's own polymorphic class vtable is absent in this build
 *   - every LogicFlickNote RTTI code reference is a downstream consumer/type test
 *   - no producer can construct a valid most-derived LogicFlickNote object here
 *
 * Therefore this build contains dormant historical Flick scaffolding, not a live
 * gameplay note path. Do not guess the missing source-float -> runtime hit-region
 * constructor formula.
 */

// ============================================================================
// 22. What remains deliberately unresolved, without blocking Section 03
// ============================================================================

/*
 * UNRESOLVED / DEFERRED ORIGINAL-NAME DETAILS
 * -------------------------------------------
 * - exact original semantic name of LogicNote +0x10 input/hit payload
 * - exact original names for +0x64/+0x65/+0x66 contact/event bytes
 * - exact original names for several LogicArcGroup fields
 * - full developer rationale for connected direction-change +0x6C first-event
 *   expiry treatment
 * - full semantic identity of every special Arc +0x170 context; baseline runtime
 *   behaviour is known but special-system details belong to their own chapters
 * - exact original names for CameraControl's second Vec3 components
 * - exact final camera-scaled Arc/ArcTap hit-region dimensions belong to the
 *   gameplay-space/render investigations
 * - final displayed numerical-score formula is not reconstructed in this section;
 *   this section closes judgement counters, note resolution and gauge fan-out
 *
 * These are not missing fundamental runtime control flow. The main gameplay note
 * state machine, timing windows, long-event system, Hold/Arc contact logic,
 * ArcTap point judgement, ScoreState/LifeBarState propagation, touch lifetime,
 * and logic-critical update ordering are all sufficiently recovered.
 */

} // namespace reconstructed
