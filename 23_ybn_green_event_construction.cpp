/*
 * Arcaea excavation notebook
 * Section 23: Your Best Nightmare green-Arc event construction
 *
 * STATUS: YBN green-Arc one-unit construction reason resolved
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native control flow, constants,
 *                   object layout, chart/runtime conversion, or independent
 *                   consumers.
 *   RECONSTRUCTED = readable structure assembled from confirmed behaviour;
 *                   names may differ from original source.
 *   UNRESOLVED    = exact chart source values are not present in this APK and
 *                   therefore are not claimed without direct evidence.
 *
 * Scope of this section is deliberately narrow:
 *   1. prove that YBN +0x170 does not alter long-event construction
 *   2. reconstruct the common long-event count rule exactly enough to explain
 *      a one-unit Arc
 *   3. identify the Arc-specific generator parameter as connected-continuation
 *      state rather than a YBN switch
 *   4. prove that Arc seam post-processing does not reduce scoring-unit count
 *   5. separate YBN colour-2 Arcs from the unrelated colour-3 zero-time branch
 *   6. explain why a working YBN green Arc must have a small positive duration
 *   7. connect the single ordinary long-event unit to Section 19's dedicated
 *      +5 RR SpecialScene path
 *
 * This file refines:
 *   02_note_fundamentals.cpp
 *   03_long_notes.cpp
 *   12_arc_contact_refinements.cpp
 *   13_arc_path_refinements.cpp
 *   19_logiccolor_arc_tracking.cpp
 *
 * Useful native anchors from the investigated ARM64 build:
 *   LogicArcNote initializer / base-long init       ~0x0C664F8
 *   LogicLongNoteBase initializer                   ~0x17CB97C
 *   common long-event builder                       ~0x0D92558
 *   LogicArcNote long-event wrapper                 ~0x0BA81E8
 *   LogicChart long-event generation pass           ~0x173BD40
 *   special colour-3 zero-time conversion branch    ~0x1864FA4
 *   ordinary Arc construction branch                ~0x1865228
 *   YBN +0x170 installation                          ~0x18651D4 / ~0x18654DC
 *   YBN SpecialScene note hook                       ~0x14F07DC
 *   YBN +5 LifeBar helper                            ~0x0EE189C
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// -----------------------------------------------------------------------------
// 1. YBN's special flag is NOT a long-event construction parameter
// -----------------------------------------------------------------------------

/*
 * Section 19 established the YBN condition:
 *
 *     song == "yourbestnightmare"
 *     && selected ratingClass == 3
 *
 * which sets LogicChart +0x111.
 *
 * During chart Arc -> LogicArcNote construction, colour-2 Arcs in that context
 * receive:
 *
 *     LogicArcNote +0x170 = 1
 *
 * CONFIRMED ordering:
 *
 *   - LogicArcNote is first constructed and initialised normally.
 *   - LogicArcNote's initializer calls the ordinary LogicLongNoteBase
 *     initializer.
 *   - the base initializer calculates/stores the ordinary tick interval.
 *   - only after the constructor returns does the chart/runtime factory install
 *     +0x170 for YBN colour-2 Arcs.
 *
 * Event generation occurs later in a LogicChart pass. At that point +0x170
 * already exists, but neither the common event builder nor LogicArcNote's
 * Arc-specific wrapper reads it.
 *
 * Therefore +0x170 cannot alter:
 *   - raw BPM selected for tick generation
 *   - timingPointDensityFactor
 *   - tick interval
 *   - event start index
 *   - event count
 *   - midpoint fallback
 */
struct LogicLongNoteBase;
struct LogicArcNote;

// -----------------------------------------------------------------------------
// 2. The complete direct LogicChart +0x111 consumer set contains no event builder
// -----------------------------------------------------------------------------

/*
 * CONFIRMED by a complete direct-field sweep of the investigated .text:
 *
 * Reads of LogicChart +0x111 occur in four mechanic locations:
 *
 *   ~0x0D5123C  LogicColor construction/remapping
 *   ~0x0EE18D0  YBN dedicated +5 RR LifeBar helper
 *   ~0x18651D8  Arc factory branch which installs +0x170
 *   ~0x18654E0  duplicate Arc factory branch which installs +0x170
 *
 * The chart-context byte is written during LogicChart setup around ~0x173BC50.
 *
 * There is no direct +0x111 read in:
 *   - LogicLongNoteBase event generation
 *   - LogicArcNote event post-processing
 *   - the LogicChart long-event generation pass
 *
 * This reinforces the control-flow proof above: YBN does not own a private
 * "generate one green tick" path.
 */

// -----------------------------------------------------------------------------
// 3. Common long-event builder
// -----------------------------------------------------------------------------

struct LongEvent {
    int32_t timeMs;
    int32_t scoreUnits;
    uint8_t processedFlags;
    uint8_t padding[3];
};

static_assert(sizeof(LongEvent) == 12);

struct LongEventBuildInputs {
    int32_t startTimeMs;
    int32_t endTimeMs;
    float tickIntervalMs;

    /*
     * RECONSTRUCTED friendly name.
     *
     * For Arcs this parameter comes from LogicArcNote +0xA0, established by the
     * connected-Arc archaeology as continuation/head state.
     */
    bool connectedContinuation;
};

/*
 * CONFIRMED arithmetic from ~0x0D92558.
 *
 * Let:
 *
 *     D = end - start
 *     I = tick interval
 *     q = trunc(D / I)
 *
 * For positive D/I, `fcvtzs` is equivalent to floor for this purpose.
 *
 * Event index begins at:
 *
 *     connectedContinuation ? 0 : 1
 *
 * Every normal event is stored as:
 *
 *     { truncate(start + I*index), 1, unprocessed }
 *
 * The loop only visits indices through q-1.
 *
 * If NO normal event was created and D != 0, the builder creates exactly one
 * fallback event at the temporal midpoint:
 *
 *     start + 0.5 * D
 *
 * If D == 0, the fallback is explicitly skipped and the event vector stays
 * empty.
 */
std::vector<LongEvent> buildCommonLongEvents(const LongEventBuildInputs& in)
{
    std::vector<LongEvent> out;

    const int32_t duration = in.endTimeMs - in.startTimeMs;
    const int32_t q = static_cast<int32_t>(
        static_cast<float>(duration) / in.tickIntervalMs);

    int32_t index = in.connectedContinuation ? 0 : 1;

    while (index < q) {
        const int32_t eventTime = static_cast<int32_t>(
            static_cast<float>(in.startTimeMs)
            + in.tickIntervalMs * static_cast<float>(index));

        if (eventTime < in.endTimeMs) {
            out.push_back({eventTime, 1, 0, {0, 0, 0}});
        }

        ++index;
    }

    if (out.empty() && duration != 0) {
        const int32_t midpoint = static_cast<int32_t>(
            static_cast<float>(in.startTimeMs)
            + 0.5f * static_cast<float>(duration));

        out.push_back({midpoint, 1, 0, {0, 0, 0}});
    }

    return out;
}

// -----------------------------------------------------------------------------
// 4. Exact scoring-unit count bands
// -----------------------------------------------------------------------------

/*
 * Because every freshly-built event starts with scoreUnits == 1, the common
 * builder's total scoring-unit count before Arc seam merging is straightforward.
 *
 * For duration D == 0:
 *
 *     units = 0
 *
 * For D > 0, let q = trunc(D / I).
 *
 * Ordinary Arc head / non-continuation:
 *
 *     q <= 1  -> no regular event -> midpoint fallback -> 1 unit
 *     q == 2  -> regular index 1                    -> 1 unit
 *     q >= 3  -> regular indices 1 .. q-1           -> q-1 units
 *
 * Therefore an ordinary Arc head has EXACTLY ONE unit when:
 *
 *     q <= 2
 *
 * Connected continuation:
 *
 *     q == 0  -> no regular event -> midpoint fallback -> 1 unit
 *     q == 1  -> regular index 0                     -> 1 unit
 *     q >= 2  -> regular indices 0 .. q-1            -> q units
 *
 * Therefore a connected continuation has EXACTLY ONE unit when:
 *
 *     q <= 1
 *
 * These conditions are safer to preserve than replacing them with idealised
 * real-number inequalities because the native implementation uses float
 * division followed by integer conversion.
 */
int commonLongScoreUnits(
    int32_t startTimeMs,
    int32_t endTimeMs,
    float tickIntervalMs,
    bool connectedContinuation)
{
    const int32_t duration = endTimeMs - startTimeMs;

    if (duration == 0) {
        return 0;
    }

    const int32_t q = static_cast<int32_t>(
        static_cast<float>(duration) / tickIntervalMs);

    if (connectedContinuation) {
        return q <= 1 ? 1 : q;
    }

    return q <= 1 ? 1 : (q - 1);
}

static bool producesExactlyOneLongUnit(
    int32_t startTimeMs,
    int32_t endTimeMs,
    float tickIntervalMs,
    bool connectedContinuation)
{
    return commonLongScoreUnits(
        startTimeMs,
        endTimeMs,
        tickIntervalMs,
        connectedContinuation) == 1;
}

// -----------------------------------------------------------------------------
// 5. The generator parameter is Arc continuation state, not YBN state
// -----------------------------------------------------------------------------

/*
 * CONFIRMED LogicChart long-event generation pass around ~0x173BD40:
 *
 *   for every LogicLongNoteBase:
 *       if it is not a LogicArcNote:
 *           generatorParameter = 0
 *
 *       if it is a LogicArcNote:
 *           run the Arc's connection/path helper
 *           generatorParameter = (LogicArcNote +0xA0 != 0)
 *
 *       invoke the long-event virtual
 *
 * Section 12/13 established +0xA0 as connected-continuation/head state.
 *
 * No +0x170 or LogicChart +0x111 value participates here.
 */
bool eventBuilderStartsAtIndexZero(const LogicArcNote& arc)
{
    return arc.connectedContinuationA0 != 0;
}

// -----------------------------------------------------------------------------
// 6. LogicArcNote's post-builder seam merge does NOT reduce scoring units
// -----------------------------------------------------------------------------

/*
 * Section 13 established that LogicArcNote's event wrapper (~0x0BA81E8):
 *
 *   1. calls the common builder
 *   2. searches for an eligible connected judged continuation
 *   3. under the tiny-seam merge condition, removes the final event record and
 *      sets the previous event's scoreUnits to 2
 *
 * Conceptually:
 *
 *     before:  [ ..., {units=1}, {units=1} ]
 *     after:   [ ..., {units=2} ]
 *
 * The number of event RECORDS decreases, but total scoreUnits is conserved.
 *
 * This is important for YBN: the special +5 path is tied to successful long
 * units. A seam merge cannot turn a two-unit Arc into a one-unit healing Arc;
 * it only packs two units into one event record.
 */
int totalScoreUnits(const std::vector<LongEvent>& events)
{
    int total = 0;
    for (const LongEvent& e : events) {
        total += e.scoreUnits;
    }
    return total;
}

void mergeLastTwoArcEventsPreservingUnits(std::vector<LongEvent>& events)
{
    if (events.size() < 2) {
        return;
    }

    events.pop_back();
    events.back().scoreUnits = 2;
}

// -----------------------------------------------------------------------------
// 7. The nearby +1 ms constructor is NOT the YBN green mechanism
// -----------------------------------------------------------------------------

/*
 * One nearby Arc factory branch had previously made zero-duration construction
 * look suspicious because it passes:
 *
 *     runtimeEnd = chartEnd + 1
 *
 * Section 23 resolves that branch identity.
 *
 * CONFIRMED branch conditions around ~0x1864FA4:
 *
 *     LogicChart +0x110 != 0
 *     chart Arc colour/index == 3
 *     chart Arc body mode == 0
 *     chart start == chart end
 *
 * This is the separate extended chart-colour-3 zero-time conversion mechanism
 * already kept distinct from LogicColor channel 3.
 *
 * YBN green uses chart colour/index == 2, so it does NOT enter this branch.
 * It takes the ordinary Arc construction branch around ~0x1865228, where chart
 * start/end are passed through as the normal Arc interval.
 *
 * Therefore do NOT explain YBN green's one unit with the colour-3 +1 ms trick.
 */
static bool isSpecialColour3ZeroTimeConversion(
    bool specialGameplaySpace110,
    int chartColour,
    int chartBodyMode,
    int32_t chartStart,
    int32_t chartEnd)
{
    return
        specialGameplaySpace110 &&
        chartColour == 3 &&
        chartBodyMode == 0 &&
        chartStart == chartEnd;
}

// -----------------------------------------------------------------------------
// 8. Zero-duration YBN green would produce ZERO units
// -----------------------------------------------------------------------------

/*
 * This is the decisive negative result.
 *
 * YBN green is colour 2 and therefore uses ordinary start/end construction.
 * The common long-event builder explicitly performs its midpoint fallback only
 * when:
 *
 *     end - start != 0
 *
 * Thus:
 *
 *     YBN green chart start == end
 *         -> runtime duration == 0
 *         -> no regular event
 *         -> midpoint fallback suppressed
 *         -> 0 long-event units
 *         -> no successful YBN +5 callback can occur
 *
 * Since the observed YBN healing Arcs each produce one successful special unit,
 * they cannot be zero-duration colour-2 Arcs in this build.
 *
 * The durable engine-side conclusion is:
 *
 *     YBN green Arcs are ordinary positive-duration LogicArcNotes whose chart
 *     duration/tick geometry falls into the common builder's one-unit band.
 */

// -----------------------------------------------------------------------------
// 9. One-unit YBN construction model
// -----------------------------------------------------------------------------

struct YbnGreenArcBuildContext {
    int32_t startTimeMs;
    int32_t endTimeMs;
    float tickIntervalMs;
    bool connectedContinuation;

    bool ybnRatingClass3;
    int chartColour;
};

bool isYbnSpecialGreen(const YbnGreenArcBuildContext& arc)
{
    return arc.ybnRatingClass3 && arc.chartColour == 2;
}

bool ybnGreenHasOneOrdinaryLongUnit(
    const YbnGreenArcBuildContext& arc)
{
    if (!isYbnSpecialGreen(arc)) {
        return false;
    }

    return producesExactlyOneLongUnit(
        arc.startTimeMs,
        arc.endTimeMs,
        arc.tickIntervalMs,
        arc.connectedContinuation);
}

/*
 * Readable flow:
 *
 *   chart colour 2 Arc in YBN BYD
 *            |
 *            v
 *   ordinary LogicArcNote construction
 *            |
 *            +-- ordinary raw BPM / TPDF tick interval
 *            |
 *            v
 *   factory installs +0x170
 *            |
 *            v
 *   later ordinary long-event generation
 *            |
 *            +-- start index from +0xA0 continuation state
 *            +-- NO YBN event-count override
 *            |
 *            v
 *   positive short duration falls in one-unit band
 *            |
 *            v
 *        one successful long unit
 *            |
 *            +-- ordinary ScoreState success hook rejected by +0x170
 *            |      -> no ordinary score/combo/RR
 *            |
 *            +-- SpecialSceneYourBestNightmare recognises +0x170
 *                   -> dedicated base +5 RR path
 */

// -----------------------------------------------------------------------------
// 10. Relationship to musical BPM
// -----------------------------------------------------------------------------

/*
 * Section 10/21 proved:
 *
 *     tickInterval =
 *         (60000 / abs(rawBpm))
 *         / subdivision
 *         / TimingPointDensityFactor
 *
 * where subdivision is 2 below 255 BPM and 1 at/above 255 BPM.
 *
 * Bundled song metadata gives Your Best Nightmare bpm_base = 190, but bpm_base
 * is NOT sufficient to prove the active TimingEvent raw BPM or the chart's
 * TimingPointDensityFactor at every green Arc.
 *
 * Therefore exact YBN green duration thresholds in milliseconds are not claimed
 * without the chart payload.
 *
 * Purely illustrative example, if active raw BPM is 190 and TPDF == 1:
 *
 *     I ~= 157.895 ms
 *
 * one-unit ordinary Arc head:
 *     trunc(D/I) <= 2
 *     approximately D < 473.684 ms
 *
 * one-unit connected continuation:
 *     trunc(D/I) <= 1
 *     approximately D < 315.789 ms
 *
 * These numbers illustrate the native quantisation; they are not presented as
 * recovered timestamps for the shipped YBN chart.
 */

// -----------------------------------------------------------------------------
// 11. What is and is not solved
// -----------------------------------------------------------------------------

/*
 * CONFIRMED
 * ---------
 * - YBN +0x170 is installed after ordinary Arc/base-long initialisation.
 * - The ordinary tick interval is established without +0x170.
 * - Common event generation does not read +0x170 or LogicChart +0x111.
 * - Arc event generation receives continuation state from Arc +0xA0.
 * - The common builder starts at event index 1 for an ordinary Arc head and 0
 *   for a connected continuation.
 * - A positive long note with no regular event receives one midpoint event.
 * - A zero-duration long note receives no midpoint fallback and has zero events.
 * - Exact one-unit bands are q<=2 for an ordinary Arc head and q<=1 for a
 *   connected continuation, where q=trunc(duration/tickInterval).
 * - Arc seam merging can reduce event-record count but preserves scoreUnits by
 *   combining two 1-unit records into one 2-unit record.
 * - The nearby chart-colour-3 zero-time branch is separate and may pass end+1.
 * - YBN green is chart colour 2 and takes the ordinary Arc interval path.
 * - Therefore a functioning one-unit YBN green Arc must have positive duration
 *   and lie in the ordinary one-unit quantisation band.
 * - Section 19's +0x170 success gate then suppresses ordinary accounting while
 *   SpecialSceneYourBestNightmare handles that successful unit through the
 *   dedicated base +5 RR path.
 *
 * RECONSTRUCTED
 * -------------
 * - Friendly names such as `connectedContinuation`, `one-unit band`, and
 *   `ybnRatingClass3`.
 * - The high-level phrase "chart geometry lands the Arc in the one-unit band";
 *   the underlying duration/tick arithmetic itself is confirmed.
 *
 * UNRESOLVED / DATA NOT PRESENT IN THIS APK
 * -----------------------------------------
 * - Exact source-chart start/end timestamps for each YBN green Arc.
 * - Exact active TimingEvent/TPDF values at each individual green Arc unless a
 *   chart payload is supplied separately.
 *
 * Those missing chart-source numbers do NOT leave the runtime mechanism
 * unresolved. The engine-side reason for "one judgement unit" is now closed:
 * there is no YBN one-tick generator; YBN special accounting is layered on top
 * of an ordinary short positive-duration Arc which naturally quantises to one
 * long-event scoring unit.
 */
