/*
 * Arcaea excavation notebook
 * Section 10: TimingGroups, LogicTimingEvent, and effective gameplay time
 *
 * STATUS: gameplay-timing / TimingGroup slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native code/data/control flow.
 *   RECONSTRUCTED = readable representation assembled from confirmed behaviour.
 *   UNRESOLVED    = exact semantic name/purpose is not yet proved.
 *
 * Scope of this section:
 *   - how TimingGroup attributes are propagated into chart/runtime notes
 *   - noinput and fadingholds gameplay-facing behaviour
 *   - runtime LogicTimingEvent representation
 *   - how each note receives the active preceding TimingEvent from its group
 *   - which TimingEvent values affect long-note ticks and spatial/path motion
 *   - the global clock used by point judgement and automatic gameplay updates
 *   - the recurring conditional -3000 ms branch carried unresolved since 02
 *   - separation of judgement time from timing-dependent visual/spatial motion
 *
 * Deliberately out of scope:
 *   - full TimingGroup/chart parser reconstruction
 *   - exact visual implementation of anglex/angley/fadingholds/trace colour
 *   - full camera and screen/gameplay-space transformation
 *   - exact semantic name of every TimingEvent field and global clock field
 *
 * This file builds directly on:
 *   02_note_fundamentals.cpp
 *   03_long_notes.cpp
 *   04_arc_contact.cpp
 *   05_arc_path.cpp
 *   07_flick_notes.cpp
 *
 * Useful native anchors from the investigated ARM64 build:
 *   TimingGroup attribute propagation             ~0x08C847C
 *   LogicTimingEvent initialiser                   ~0x0BE2B98
 *   chart timing -> LogicTimingEvent construction  ~0x1862FD0
 *   note -> active TimingEvent selection           ~0x1863EF4
 *   common LogicNote initialiser                   ~0x1902FFC
 *   point-note judgement candidate                 ~0x160CE28
 *   long-note initialiser / BPM tick arithmetic    ~0x17CB97C
 *   Arc sampled-path builder timing use            ~0x1925084
 *   common gameplay scheduler / auto-miss          ~0x0F8056C
 *   active-touch gameplay processing               ~0x1485C18
 *
 * WARNING ABOUT SYMBOLS:
 * The binary is stripped. Readable field/type names below are reconstruction
 * names unless explicitly described as surviving RTTI/type/chart strings or
 * CONFIRMED field behaviour.
 */

#include <cstdint>
#include <cmath>

// -----------------------------------------------------------------------------
// 1. TimingGroup is not a separate judgement clock
// -----------------------------------------------------------------------------

/*
 * CONFIRMED architectural result:
 *
 * TimingGroup handling splits into two useful layers before/during runtime:
 *
 *   A. group attributes are propagated into child chart-note records and later
 *      copied into the corresponding LogicNote subclasses;
 *
 *   B. timing() entries become per-group LogicTimingEvent streams. Each runtime
 *      note is constructed with a pointer to the latest TimingEvent whose event
 *      time is <= that note's time.
 *
 * Point judgement itself does NOT convert the global gameplay clock through the
 * note's TimingEvent. Note timestamps remain ordinary absolute chart/gameplay
 * timestamps and are compared directly with the global effective gameplay time.
 *
 * Therefore this is NOT the engine model:
 *
 *   TimingGroup -> private group clock -> note judgement
 *
 * It is closer to:
 *
 *   TimingGroup attributes -----------------------> note flags/metadata
 *
 *   group timing() stream -> active TimingEvent --> ticks / spatial progression
 *
 *   global gameplay clock ------------------------> judgement / expiry
 */

// -----------------------------------------------------------------------------
// 2. Selected TimingGroup attributes are flattened into child notes
// -----------------------------------------------------------------------------

/*
 * CONFIRMED parser-side propagation from surviving attribute strings/branches:
 *
 *   "noinput"
 *       -> child chart Note byte +0x0C = 0
 *
 *   "fadingholds"
 *       -> child chart Note byte +0x0D = 1
 *
 *   "anglex=<integer>"
 *       -> child chart Note integer +0x10
 *
 *   "angley=<integer>"
 *       -> child chart Note integer +0x14
 *
 * A trace-colour attribute prefix also exists. For timing records in such a
 * group, an RGB-like value is parsed from hexadecimal byte pairs and retained
 * in chart timing metadata. Its exact user-facing/native attribute spelling and
 * all consumers are deliberately not expanded here.
 *
 * Important architecture:
 * These values are copied into child records while the group is processed. The
 * judgement engine does not need to ask a persistent TimingGroup object whether
 * a note has `noinput` each frame.
 */

// -----------------------------------------------------------------------------
// 3. noinput: input eligibility, not a time transformation
// -----------------------------------------------------------------------------

/*
 * CONFIRMED chart -> runtime path:
 *
 *   chart Note +0x0C -> runtime LogicNote +0x54
 *
 * This copy occurs across the relevant note-construction branches.
 *
 * CONFIRMED live input behaviour:
 * Touch-begin and active-touch note processing read LogicNote +0x54 and skip the
 * note when it is zero before performing Tap/Hold/Arc/Flick input handling.
 *
 * Readable semantic name:
 */
struct LogicNoteTimingContext;

struct LogicNoteSelectedFields {
    int32_t startTimeMs;               // common +0x18
    int32_t endTimeMs;                 // common +0x1C
    LogicNoteTimingContext* timing;     // common +0x48, refined below
    uint8_t inputEnabled;               // common +0x54
};

static bool acceptsPlayerInput(const LogicNoteSelectedFields& note)
{
    return note.inputEnabled != 0;
}

/*
 * Gameplay consequence:
 * `noinput` suppresses player input/candidate processing. It is not implemented
 * by shifting the note's judgement timestamp or inventing a private clock.
 *
 * Other scheduler/visual behaviour may still process such a note according to
 * its class and state; this section only claims the confirmed player-input gate.
 */

// -----------------------------------------------------------------------------
// 4. fadingholds and angle attributes belong to presentation/spatial metadata
// -----------------------------------------------------------------------------

/*
 * CONFIRMED Hold construction:
 *
 *   chart child +0x0D -> LogicHoldNote +0xA9
 *
 * The runtime +0xA9 flag is consumed in Hold update/presentation-side logic
 * together with contact/render state. No evidence was found that it changes the
 * judgement clock or long-event timestamp arithmetic.
 *
 * Therefore `fadingholds` is safely classified as Hold presentation behaviour,
 * while its exact fade curve remains intentionally outside this section.
 *
 * Likewise, chart child +0x10/+0x14 (anglex/angley) are converted/scaled into
 * runtime floating values in note construction under the relevant mode. Their
 * observed use belongs to orientation/spatial behaviour, not ScoreState timing.
 *
 * This section deliberately does not assign exact runtime member names or a
 * complete angle transform without tracing the later camera/render consumers.
 */

// -----------------------------------------------------------------------------
// 5. LogicTimingEvent is a real runtime event type
// -----------------------------------------------------------------------------

/*
 * CONFIRMED surviving RTTI:
 *
 *   LogicEvent
 *      |
 *      +-- LogicTimingEvent
 *
 * The LogicTimingEvent initialiser receives:
 *
 *   integer eventTime
 *   float   sourceValue0
 *   float   sourceValue1
 *   float   contextMultiplier
 *
 * and stores exactly:
 *
 *   +0x10 = eventTime
 *   +0x14 = eventTime
 *   +0x18 = sourceValue0 * contextMultiplier
 *   +0x1C = sourceValue1
 *   +0x20 = sourceValue0
 *   +0x28 = 60.0 / (sourceValue0 * contextMultiplier)
 *
 * Following chart -> runtime construction proves:
 *
 *   sourceValue0 = chart Timing +0x1C
 *   sourceValue1 = chart Timing +0x20
 *   contextMultiplier = conversion/gameplay context +0xF4
 *
 * Independent long-note arithmetic proves runtime +0x20 behaves as raw tempo /
 * BPM. Therefore sourceValue0 can be given a readable raw-tempo name.
 *
 * The exact semantic name of chart Timing +0x20 / runtime +0x1C is NOT proved in
 * this slice. Do not silently name it from external AFF conventions.
 */
struct LogicTimingEvent {
    int32_t startTimeMs;              // +0x10, CONFIRMED time
    int32_t endTimeMs;                // +0x14, same value in this event

    float effectiveTempoLike;         // +0x18 = rawTempo * contextMultiplier
    float secondaryTimingValue;       // +0x1C, semantic UNRESOLVED
    float rawTempoBpm;                // +0x20, behaviour CONFIRMED

    float unknown24;                  // omitted/placeholder in readable layout
    float secondsPerEffectiveBeat;    // +0x28 = 60 / effectiveTempoLike
};

static void initialiseTimingEvent(
    LogicTimingEvent& event,
    int32_t timeMs,
    float rawTempo,
    float secondaryValue,
    float contextMultiplier)
{
    event.startTimeMs = timeMs;
    event.endTimeMs = timeMs;

    event.effectiveTempoLike = rawTempo * contextMultiplier;
    event.secondaryTimingValue = secondaryValue;
    event.rawTempoBpm = rawTempo;

    event.secondsPerEffectiveBeat =
        60.0f / event.effectiveTempoLike;
}

/*
 * UNRESOLVED:
 * The context +0xF4 value is itself calculated as a ratio of gameplay/config
 * values elsewhere. It is unquestionably a real scale applied to spatial/effective
 * timing progression, but this section does not prove the original semantic
 * name strongly enough to call it a TimingGroup speed, chart speed, etc.
 */

// -----------------------------------------------------------------------------
// 6. Every note receives the latest preceding TimingEvent in its group
// -----------------------------------------------------------------------------

/*
 * CONFIRMED chart -> runtime construction around ~0x1863EF4.
 *
 * For a note at `noteTime`, the factory walks the timing-event vector belonging
 * to the current group and keeps advancing while:
 *
 *   timingEvent.time <= noteTime
 *
 * The latest such event is supplied to the note-specific constructor.
 *
 * The common LogicNote initialiser at ~0x1902FFC directly stores that pointer:
 *
 *   LogicNote +0x48 = supplied LogicTimingEvent*
 *
 * while also writing the note's ordinary absolute time at +0x18/+0x1C.
 */
static LogicTimingEvent* selectTimingForNote(
    LogicTimingEvent** begin,
    LogicTimingEvent** end,
    int32_t noteTimeMs)
{
    if (begin == end) {
        return nullptr;
    }

    LogicTimingEvent* selected = *begin;

    for (LogicTimingEvent** it = begin; it != end; ++it) {
        LogicTimingEvent* candidate = *it;

        if (candidate->startTimeMs > noteTimeMs) {
            break;
        }

        selected = candidate;
    }

    return selected;
}

/*
 * RECONSTRUCTED field declaration with CONFIRMED pointer identity:
 */
struct LogicNoteWithTiming {
    int32_t startTimeMs;
    int32_t endTimeMs;
    // ...
    LogicTimingEvent* activeTiming; // actual common note field +0x48
};

// -----------------------------------------------------------------------------
// 7. Raw TimingEvent BPM affects long-note event density
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from LogicLongNoteBase initialisation, now with +0x48 identified.
 *
 * The long-note constructor reads:
 *
 *   note.activeTiming->+0x20
 *
 * i.e. the raw tempo value, not the context-scaled +0x18 field.
 *
 * Arithmetic already established in Sections 02/03:
 */
static float calculateLongTickIntervalMs(
    const LogicTimingEvent& timing,
    float timingPointDensityFactor)
{
    const float tempo = std::fabs(timing.rawTempoBpm);
    const float beatMs = 60000.0f / tempo;

    const float subdivision =
        (tempo >= 255.0f) ? 1.0f : 2.0f;

    return beatMs / subdivision / timingPointDensityFactor;
}

/*
 * Therefore TimingEvent context materially affects gameplay through BPM-based
 * long-note tick/event generation even though it does not alter the note's
 * point-judgement clock.
 */

// -----------------------------------------------------------------------------
// 8. The context-scaled beat duration is used by Arc/spatial progression
// -----------------------------------------------------------------------------

/*
 * CONFIRMED Arc path/spatial use:
 * Arc construction/path code follows LogicArcNote +0x48 to its TimingEvent and
 * reads TimingEvent +0x28 while constructing sampled path/spatial progression.
 * Other setup code similarly combines time spans with +0x28 while deriving
 * movement/path-related values.
 *
 * Since +0x28 is exactly:
 *
 *   60 / (rawTempo * contextMultiplier)
 *
 * it is safely described mathematically as seconds per effective beat.
 *
 * This gives the key split:
 *
 *   raw +0x20 tempo          -> long-note tick density
 *   scaled +0x28 beat length -> path / spatial-motion calculations
 *
 * Neither observation means point notes are judged in "local TimingGroup time".
 */

// -----------------------------------------------------------------------------
// 9. One global effective gameplay clock drives judgement and expiry
// -----------------------------------------------------------------------------

/*
 * CONFIRMED repeatedly in:
 *   - point-note judgement
 *   - automatic miss processing
 *   - Hold/Arc long-event processing
 *   - Flick processing
 *   - scene-control scheduling
 *   - active-touch qualification
 *
 * A shared clock-like object contains selected fields:
 *
 *   +0x20 : integer source used when byte +0x2D is non-zero
 *   +0x28 : common integer offset subtracted from either source
 *   +0x2D : selects synchronized/live path versus fallback path
 *   +0x34 : integer source used by the fallback path
 *
 * Exact original class/member names are unavailable. The readable names below
 * intentionally describe control flow rather than claiming recovered names.
 */
struct GameplayClock {
    int32_t synchronizedSource; // +0x20
    int32_t commonOffset;       // +0x28
    bool useSynchronizedPath;   // +0x2D
    int32_t fallbackSource;     // +0x34
};

/*
 * CONFIRMED arithmetic. This is the resolution of the old mysterious
 * "timing-related +/-3000 ms" branch carried since Section 02.
 *
 * It is NOT +/-3000.
 * It is a conditional -3000 ms adjustment on one fallback clock path only.
 */
static int32_t effectiveGameplayTime(const GameplayClock& clock)
{
    if (clock.useSynchronizedPath) {
        return clock.synchronizedSource - clock.commonOffset;
    }

    int32_t result =
        clock.fallbackSource - clock.commonOffset;

    if (clock.fallbackSource <= 0) {
        result -= 3000;
    }

    return result;
}

/*
 * CONFIRMED clock synchronisation clue:
 * A separate setup function stores two reference values into the clock and sets
 * +0x2D = 1. Callers obtain those references from std::chrono steady_clock and
 * system_clock millisecond-like values.
 *
 * This strongly supports describing the +0x2D path as synchronized/live timing.
 * The exact meaning of each stored reference and the reason for the fallback
 * pre-roll shift remain outside what this section can name with certainty.
 */

// -----------------------------------------------------------------------------
// 10. Point-note judgement proves TimingEvent does not transform hit time
// -----------------------------------------------------------------------------

/*
 * CONFIRMED directly in the point-note candidate routine ~0x160CE28.
 *
 * The function reads:
 *
 *   note +0x18          -> absolute note timestamp
 *   global clock fields -> effective gameplay timestamp as above
 *
 * and computes the ordinary absolute timing error.
 *
 * Critically, the routine does NOT read LogicNote +0x48 at all.
 * No LogicTimingEvent field participates in the MAX PURE / PURE / FAR / LOST
 * boundary calculation.
 */
static int32_t absoluteTimingErrorMs(
    const LogicNoteWithTiming& note,
    const GameplayClock& clock)
{
    const int32_t now = effectiveGameplayTime(clock);
    const int32_t delta = now - note.startTimeMs;
    return delta < 0 ? -delta : delta;
}

/*
 * The established point windows remain:
 *
 *   0..25   -> MAX PURE
 *   26..50  -> PURE
 *   51..100 -> FAR
 *   101..120-> LOST through the point candidate path
 *   >120    -> candidate rejected there
 *
 * Automatic miss scheduling uses the same global effective gameplay clock.
 */

// -----------------------------------------------------------------------------
// 11. noinput does not mean "do not judge time"
// -----------------------------------------------------------------------------

/*
 * Combining Sections 2, 3, 9, and 10 gives an important gameplay distinction:
 *
 *   noinput note:
 *       player-input candidate path is disabled
 *
 *   note timing:
 *       remains an ordinary absolute chart timestamp processed against the same
 *       global scheduler/effective clock unless its note-class/control logic says
 *       otherwise
 *
 * Therefore `noinput` is an input-policy flag, not a TimingGroup-local clock or
 * timestamp modifier.
 */

// -----------------------------------------------------------------------------
// 12. Full reconstructed TimingGroup gameplay model
// -----------------------------------------------------------------------------

/*
 * RECONSTRUCTED readable architecture from the confirmed pieces:
 *
 *   timinggroup(attributes) {
 *       timing(...)
 *       notes...
 *   }
 *              |
 *              +-----------------------------+
 *              |                             |
 *              v                             v
 *     attributes flattened          group timing stream
 *     into child chart notes                 |
 *              |                             v
 *              |                  LogicTimingEvent objects
 *              |                             |
 *              |                latest event <= note time
 *              |                             |
 *              +-------------+---------------+
 *                            v
 *                         LogicNote
 *                    absolute start/end time
 *                    activeTiming pointer +48
 *                    input/presentation flags
 *                            |
 *             +--------------+---------------+
 *             |                              |
 *             v                              v
 *       global clock                   TimingEvent data
 *             |                              |
 *      judgement / expiry          ticks / path / motion
 *
 * There is no evidence here for a separate per-group judgement clock.
 */

// -----------------------------------------------------------------------------
// 13. CONFIRMED / RECONSTRUCTED / UNRESOLVED summary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED
 * ---------
 * - LogicTimingEvent RTTI survives as a LogicEvent subtype.
 * - TimingGroup attributes such as noinput/fadingholds/angles are propagated
 *   into child note metadata rather than queried from a group every judgement.
 * - chart noinput state reaches LogicNote +0x54.
 * - live touch processing skips notes with +0x54 == 0.
 * - chart fadingholds state reaches LogicHoldNote +0xA9 and is used outside
 *   judgement-clock arithmetic.
 * - LogicTimingEvent stores raw tempo at +0x20.
 * - +0x18 is raw tempo multiplied by a context scale.
 * - +0x28 is exactly 60/(raw tempo * context scale).
 * - runtime note construction selects the latest group TimingEvent whose time is
 *   <= the note time.
 * - common LogicNote +0x48 stores that LogicTimingEvent pointer.
 * - raw tempo +0x20 controls long-note tick spacing.
 * - effective beat duration +0x28 participates in Arc/spatial path calculations.
 * - point judgement reads note +0x18 and the global clock, not note +0x48.
 * - one shared effective-time calculation feeds judgement and scheduler paths.
 * - the old +/-3000 mystery is specifically a -3000 adjustment when the fallback
 *   source is selected and fallbackSource <= 0.
 *
 * RECONSTRUCTED
 * -------------
 * - field names synchronizedSource/commonOffset/fallbackSource.
 * - `useSynchronizedPath` as a readable name for clock +0x2D.
 * - `effectiveTempoLike` and `secondsPerEffectiveBeat` as mathematical names.
 * - grouping of angle/fadingholds behaviour under presentation/spatial metadata.
 *
 * UNRESOLVED
 * ----------
 * - original semantic/member name of chart Timing +0x20 / runtime +0x1C.
 * - original name/meaning of the context multiplier at conversion state +0xF4.
 * - exact original class/member names of the shared gameplay clock.
 * - exact semantic reason the fallback clock subtracts an additional 3000 ms
 *   while its source value is non-positive.
 * - complete visual formulas for fadingholds, anglex/angley, and trace colour.
 *
 * These unresolved names do not block the gameplay-timing architecture.
 */

// -----------------------------------------------------------------------------
// 14. Next excavation boundary
// -----------------------------------------------------------------------------

/*
 * With judgement time separated from timing-dependent spatial progression, the
 * next narrow target should be gameplay-space/camera fundamentals:
 *
 *   screen touch
 *       -> projection/camera conversion
 *       -> floor gameplay coordinates
 *       -> sky/Arc gameplay coordinates
 *       -> lane / Arc / ArcTap / Flick spatial hit regions
 *
 * That should remain a gameplay transform excavation, not a full renderer or
 * graphics-engine map. Once the coordinate spaces are understood, rendering and
 * the remaining Arc/Flick hitbox dimensions can be approached from a stable
 * geometric baseline.
 */
