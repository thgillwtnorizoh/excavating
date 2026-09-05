/*
 * Arcaea excavation notebook
 * Section 13: LogicArcNote sampled paths and connected-seam graph
 *
 * STATUS: Arc sampled-path / continuation-graph slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native code/data/control flow.
 *   RECONSTRUCTED = readable representation assembled from confirmed behaviour.
 *   UNRESOLVED    = exact semantic name/purpose is not yet proved.
 *
 * Scope of this section:
 *   - distinct roles of LogicArcNote sampled vectors +0xE8 and +0x100
 *   - render-only sampling multiplier at +0x118
 *   - exact population and meaning of the pointer vector at +0x138
 *   - reverse connected-Arc link at +0x150
 *   - the tiny connected-seam extension used during path sampling
 *   - connected-seam tracker retention after nominal Arc end
 *   - connected-seam long-event merge / score-unit preservation
 *
 * Deliberately out of scope:
 *   - re-deriving the Arc easing equations already closed in Section 05
 *   - full RenderArcNote mesh/shader/texture construction
 *   - exact public/chart-side semantic name of the +0x118 multiplier
 *   - Arc mode +0xA4 == 2 semantic purpose
 *   - unrelated ArcGroup presentation fields
 *
 * This file builds directly on:
 *   03_long_notes.cpp
 *   05_arc_path.cpp
 *   11_gameplay_space.cpp
 *   12_arc_contact_refinements.cpp
 *
 * Useful native anchors from the investigated ARM64 build:
 *   LogicArcNote sampled-path builder                 ~0x1925084
 *   primary gameplay sampled-path consumer            ~0x19F3D4C
 *   secondary RenderArcNote path consumer              ~0x13B5FF8
 *   LogicArcNote runtime initializer                   ~0x0C664F8
 *   chart/runtime Arc factory carrying chart +0x88     ~0x186514C / ~0x1865418
 *   connected-Arc postprocessor                        ~0x125E474
 *   +0x138 vector push/grow helper                      ~0x1205BF4
 *   connected-seam long-event adjustment               ~0x0BA81E8
 *   LogicArcNote destructor                             ~0x16E7084
 *
 * WARNING ABOUT SYMBOLS:
 * The binary is stripped. Readable field/function names below are reconstruction
 * names unless explicitly described as surviving RTTI/type/chart data or as
 * CONFIRMED field behaviour.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// -----------------------------------------------------------------------------
// 1. Selected Arc state relevant to this slice
// -----------------------------------------------------------------------------

struct Vec3 {
    float x;
    float y;
    float z;
};

struct LongTickEvent {
    int32_t timeMs;
    int32_t scoreUnits;
    uint8_t processedFlags;
    uint8_t padding[3];
};

struct LogicArcGroup;
struct LogicTimingEvent;

struct LogicArcNote {
    // Common LogicNote / LogicLongNoteBase fields omitted.

    int32_t startTimeMs;  // conceptual reference to +0x18
    int32_t endTimeMs;    // conceptual reference to +0x1C

    LogicTimingEvent* activeTiming; // +0x48, established Section 10

    float tickIntervalMs; // +0x70, established long-note machinery
    std::vector<LongTickEvent> tickEvents; // +0x78

    int32_t bodyMode;     // +0xA4; 0 = ordinary judged Arc body

    /*
     * CONFIRMED vector layouts:
     *
     *   +0xE8 / +0xF0 / +0xF8   : primary Vec3 vector begin/end/capacity
     *   +0x100/+0x108/+0x110    : secondary Vec3 vector begin/end/capacity
     *
     * Both contain 12-byte Vec3 samples of the SAME analytic Arc.
     */
    std::vector<Vec3> gameplaySamples; // +0xE8, RECONSTRUCTED name
    std::vector<Vec3> renderSamples;   // +0x100, RECONSTRUCTED name

    float renderSamplingMultiplier;    // +0x118, RECONSTRUCTED name

    /*
     * CONFIRMED pointer-vector layout:
     *
     *   +0x138/+0x140/+0x148 : begin/end/capacity
     *
     * Section 13 proves its exact population site: the connected-Arc
     * postprocessor pushes a later connected LogicArcNote* into this vector.
     */
    std::vector<LogicArcNote*> forwardContinuations; // +0x138

    LogicArcNote* previousContinuation; // +0x150, RECONSTRUCTED name

    int32_t easingType; // +0x15C, established Section 05

    LogicArcGroup* arcGroup; // +0xE0, established Section 12
};

// -----------------------------------------------------------------------------
// 2. There are two sampled copies of one analytic Arc
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from the builder around ~0x1925084:
 *
 * The easing enum, endpoints, TimingEvent-derived spatial timing, and ordinary
 * Arc path evaluator are shared. The builder does NOT select different easing
 * mathematics for +0xE8 and +0x100.
 *
 * Instead, the same analytic curve is tessellated twice at different sampling
 * steps and stored in two separate Vec3 vectors.
 *
 * Therefore the architecture is:
 *
 *       chart endpoints + easing
 *                |
 *                v
 *         analytic Arc curve
 *             /       \
 *            /         \
 *           v           v
 *        +0xE8        +0x100
 *       primary       secondary
 *       samples        samples
 */

// -----------------------------------------------------------------------------
// 3. +0xE8 is the gameplay/contact polyline
// -----------------------------------------------------------------------------

/*
 * CONFIRMED:
 * The live Arc gameplay position solver consumes the Vec3 segments stored in
 * +0xE8. It walks neighbouring samples, interpolates/projects the appropriate
 * segment for current gameplay time, and ultimately updates the established:
 *
 *     LogicArcNote +0xD4 / +0xD8
 *
 * expected gameplay point used by Arc contact geometry.
 *
 * Thus +0xE8 is not merely a rendering cache.
 */
Vec3 sampleGameplayPolyline(
    const LogicArcNote& arc,
    float normalizedProgress)
{
    // RECONSTRUCTED readable equivalent of the established segment walk.
    return interpolateSampledPolyline(
        arc.gameplaySamples,
        normalizedProgress);
}

// -----------------------------------------------------------------------------
// 4. +0x100 is the denser RenderArcNote tessellation polyline
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from a separate routine around ~0x13B5FF8:
 *
 *   - the routine receives a LogicNote-like object
 *   - dynamically casts it to LogicArcNote through surviving RTTI
 *   - reads the Vec3 vector at +0x100 directly
 *   - walks successive samples and sends segment-derived geometry through
 *     RenderArcNote/presentation-side objects
 *
 * Therefore +0x100 is a presentation/render tessellation path, not another
 * hidden gameplay hit curve.
 */
void rebuildRenderedArcSegments(const LogicArcNote& arc)
{
    for (size_t i = 0; i + 1 < arc.renderSamples.size(); ++i) {
        const Vec3& a = arc.renderSamples[i];
        const Vec3& b = arc.renderSamples[i + 1];
        emitArcRenderSegment(a, b);
    }
}

// -----------------------------------------------------------------------------
// 5. Sampling density: nominal duration chooses 14 or 7
// -----------------------------------------------------------------------------

/*
 * CONFIRMED native branch:
 *
 *     nominalDuration = endTime - startTime
 *
 *     if nominalDuration < 1000 ms:
 *         baseDensity = 14
 *     else:
 *         baseDensity = 7
 *
 * The effective duration used to normalise path progress may be extended by a
 * connected continuation seam, described below. The 14-vs-7 choice itself is
 * based on the Arc's own nominal duration.
 */
static float baseSamplingDensity(const LogicArcNote& arc)
{
    return
        (arc.endTimeMs - arc.startTimeMs) < 1000
            ? 14.0f
            : 7.0f;
}

// -----------------------------------------------------------------------------
// 6. +0x118 increases ONLY the secondary/render sampling density
// -----------------------------------------------------------------------------

/*
 * CONFIRMED initializer behaviour:
 *
 *   - a float supplied to LogicArcNote initialisation is stored at +0x118
 *   - values below 1.0 are replaced with 1.0
 *
 * The chart/runtime factory loads that supplied float from chart Arc +0x88.
 *
 * The sampled-path builder then uses +0x118 only when deriving the secondary
 * +0x100 path's step. The primary +0xE8 path does not multiply its density by
 * this value.
 *
 * With:
 *
 *     D = effective path duration in seconds
 *     B = base density (14 or 7)
 *     M = max(chartValue, 1.0)
 *
 * the observed step sizes are structurally:
 *
 *     gameplayStep ~= 1 / (D * B)
 *     renderStep   ~= 1 / (D * B * M)
 *
 * Larger M therefore inserts more samples into the visible/render path while
 * leaving gameplay/contact sampling resolution unchanged.
 *
 * "renderSamplingMultiplier" is a RECONSTRUCTED semantic name. It behaves as a
 * tessellation/smoothness multiplier, but this slice does not claim recovery of
 * the original chart/member name.
 */
static float clampRenderSamplingMultiplier(float chartValue)
{
    return std::max(chartValue, 1.0f);
}

// -----------------------------------------------------------------------------
// 7. Important consequence: visual smoothness does not retessellate hit logic
// -----------------------------------------------------------------------------

/*
 * CONFIRMED architectural consequence:
 *
 * Increasing +0x118 can produce a denser visible Arc without increasing the
 * number of +0xE8 samples used by the live expected-point/contact solver.
 *
 * Both polylines still approximate the same exact Section-05 analytic curve.
 * This is not a second easing system. It is independent tessellation density.
 *
 * Readable mental model:
 *
 *   M = 1:
 *       gameplay : o-----o-----o-----o
 *       render   : o-----o-----o-----o   (conceptual density example)
 *
 *   larger M:
 *       gameplay : o-----o-----o-----o
 *       render   : o-o-o-o-o-o-o-o-o-o
 */

// -----------------------------------------------------------------------------
// 8. +0x138 is the FORWARD connected-Arc continuation vector
// -----------------------------------------------------------------------------

/*
 * This is the main new closure of Section 13.
 *
 * CONFIRMED exact writer:
 * The connected-Arc postprocessor around ~0x125E474 calls a helper at
 * ~0x1205BF4. That helper is a normal pointer-vector push_back implementation
 * whose object-relative storage is exactly:
 *
 *     +0x138 : begin
 *     +0x140 : end
 *     +0x148 : capacity end
 *
 * The helper has only this one call site in the investigated binary.
 *
 * At that call site:
 *
 *     x0 = previous Arc
 *     x1 = connected next Arc
 *
 * so the native effect is:
 *
 *     previous.forwardContinuations.push_back(&next);
 *
 * Immediately afterward the postprocessor also writes:
 *
 *     next +0x150 = previous;
 *
 * Thus the Arc chain has a directional graph in addition to the shared
 * LogicArcGroup from Section 12.
 */
void linkConnectedArcDirectionally(
    LogicArcNote& previous,
    LogicArcNote& next)
{
    previous.forwardContinuations.push_back(&next);
    next.previousContinuation = &previous;
}

// -----------------------------------------------------------------------------
// 9. The directional graph uses the SAME seam criteria as LogicArcGroup
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from the surrounding connected-Arc postprocessor.
 * A later Arc is linked when the candidate seam satisfies the established
 * connected-Arc conditions:
 *
 *   abs(next.startTime - previous.endTime) <= 9 ms
 *
 *   abs(next.xStart - previous.xEnd) < 0.1 chart-X units
 *
 *   next.yStart == previous.yEnd
 *
 * and body-mode compatibility:
 *
 *   previous mode == 0  -> next mode must == 0
 *   previous mode != 0  -> next mode must != 0
 *
 * The same pass also creates/propagates LogicArcGroup, marks continuation byte
 * +0xA0, and may mark the direction-changing seam byte +0x6C established in
 * Section 12.
 *
 * Therefore +0x138 is NOT a second unrelated family of Arc associations.
 * It is the directional continuation representation produced by the same Arc
 * seam recogniser, while +0xE0 LogicArcGroup is shared chain-level state.
 */
bool qualifiesAsConnectedContinuation(
    const LogicArcNote& previous,
    const LogicArcNote& next)
{
    if (abs(next.startTimeMs - previous.endTimeMs) > 9) {
        return false;
    }

    if (std::fabs(nextChartXStart(next) - previousChartXEnd(previous)) >= 0.1f) {
        return false;
    }

    if (nextChartYStart(next) != previousChartYEnd(previous)) {
        return false;
    }

    const bool previousJudgedBody = previous.bodyMode == 0;
    const bool nextJudgedBody = next.bodyMode == 0;

    return previousJudgedBody == nextJudgedBody;
}

// -----------------------------------------------------------------------------
// 10. +0x138 extends the sampled path only across a tiny tolerated seam gap
// -----------------------------------------------------------------------------

/*
 * CONFIRMED path-builder prologue:
 *
 *     effectiveEnd = arc.endTime
 *
 *     for each continuation in arc.+0x138:
 *         effectiveEnd = max(effectiveEnd, continuation.startTime)
 *
 * Because membership itself requires a start/end difference of at most 9 ms,
 * this is NOT an arbitrary long path-horizon extension.
 *
 * It can extend a predecessor whose continuation starts slightly AFTER its own
 * nominal end by no more than the accepted seam tolerance. A continuation that
 * begins slightly before the predecessor's nominal end does not shorten it.
 *
 * RECONSTRUCTED purpose:
 * This behaves as a tiny seam-gap normalisation so the predecessor's sampled
 * path reaches the accepted continuation seam rather than leaving a several-ms
 * hole between two objects which the postprocessor considers connected.
 */
int effectiveArcSamplingEnd(const LogicArcNote& arc)
{
    int effectiveEnd = arc.endTimeMs;

    for (const LogicArcNote* next : arc.forwardContinuations) {
        if (next) {
            effectiveEnd = std::max(effectiveEnd, next->startTimeMs);
        }
    }

    return effectiveEnd;
}

// -----------------------------------------------------------------------------
// 11. Connected continuations also retain Arc ownership past nominal end
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from the Arc update/contact machinery investigated across Sections
 * 12 and 13:
 *
 * Once an Arc passes its own nominal end, +0x138 is consulted before its normal
 * tracker is reset. If at least one forward continuation is an ordinary judged
 * body Arc (mode +0xA4 == 0), the predecessor can retain continuity/ownership
 * state instead of immediately tearing it down at its own end.
 *
 * This is separate from:
 *   - the min(4*tickInterval,1000) release re-acquisition lockout
 *   - the min(2*tickInterval,500) long-event LOST grace
 *   - LogicArcGroup transient contact bytes
 */
bool hasJudgedForwardContinuation(const LogicArcNote& arc)
{
    for (const LogicArcNote* next : arc.forwardContinuations) {
        if (next && next->bodyMode == 0) {
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// 12. Connected judged seams can merge the final two long-event units
// -----------------------------------------------------------------------------

/*
 * CONFIRMED virtual Arc long-event initialisation wrapper around ~0x0BA81E8:
 *
 *   1. run the common LogicLongNoteBase event builder
 *   2. scan +0x138 for at least one judged-body continuation (mode 0)
 *   3. if none exists, keep the common event vector unchanged
 *   4. if one exists, perform a small tick-boundary test using:
 *
 *          D = endTime - startTime
 *          T = tickInterval
 *
 *      Native integer-conversion structure is equivalent to testing whether
 *      subtracting 2 ms crosses the final tick quotient boundary:
 *
 *          trunc(D / T) - 1 == trunc((D - 2) / T)
 *
 *   5. if the condition passes and at least two event records exist:
 *
 *          remove the final 12-byte event record
 *          set the NEW final event's scoreUnits field to 2
 *
 * Section 03 established scoreUnits as multiplicity: both success and LOST
 * paths consume that many ScoreState units.
 *
 * Therefore this transformation preserves two scoring units while representing
 * them with one remaining event record at the seam.
 *
 * RECONSTRUCTED purpose:
 * This is consistent with de-duplicating/merging a tick that lands essentially
 * on a connected Arc boundary, avoiding two distinct event timestamps for what
 * the chain treats as one continuous seam. The structural mutation and 2 ms
 * quotient check are CONFIRMED; the developer-facing rationale/name is not.
 */
void mergeConnectedSeamTickIfNecessary(LogicArcNote& arc)
{
    if (!hasJudgedForwardContinuation(arc)) {
        return;
    }

    const float D =
        static_cast<float>(arc.endTimeMs - arc.startTimeMs);
    const float T = arc.tickIntervalMs;

    const int qAtEnd = static_cast<int>(D / T);
    const int qTwoMsEarlier = static_cast<int>((D - 2.0f) / T);

    const bool crossesBoundaryWithinTwoMs =
        (qAtEnd - 1) == qTwoMsEarlier;

    if (!crossesBoundaryWithinTwoMs) {
        return;
    }

    if (arc.tickEvents.size() < 2) {
        return;
    }

    arc.tickEvents.pop_back();
    arc.tickEvents.back().scoreUnits = 2;
}

// -----------------------------------------------------------------------------
// 13. +0xE0 LogicArcGroup and +0x138 continuation graph are complementary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED architectural split:
 *
 * Connected Arc recognition produces BOTH:
 *
 *   Arc +0xE0   -> LogicArcGroup*
 *                  shared chain-level transient contact/presentation state
 *
 *   Arc +0x138  -> vector<next Arc*>
 *   Arc +0x150  -> previous Arc*
 *                  directional predecessor/continuation relationships
 *
 * They are not aliases and do not replace one another.
 *
 * The directional graph is useful for operations that need to know specifically
 * what happens AFTER one Arc's nominal end: seam path extension, tracker
 * retention, and terminal tick merging.
 *
 * LogicArcGroup is useful for state shared across the connected chain while it
 * is being played.
 */

// -----------------------------------------------------------------------------
// 14. Compact Arc geometry / seam model
// -----------------------------------------------------------------------------

/*
 *                         analytic Arc
 *                              |
 *                  +-----------+-----------+
 *                  |                       |
 *                  v                       v
 *               +0xE8                  +0x100
 *        gameplay/contact path       render path
 *                  |                       |
 *                  |                  density *= +0x118
 *                  |                       |
 *                  v                       v
 *          expected contact point      visible segments
 *
 *
 * connected seam:
 *
 *      previous Arc                     next Arc
 *   +---------------+                +---------------+
 *   |               | -- +0x138 ---> |               |
 *   |               | <-- +0x150 --- |               |
 *   +---------------+                +---------------+
 *           \                               /
 *            +-------- LogicArcGroup -------+
 *
 * +0x138 drives predecessor-specific seam behaviour:
 *   - extend sampled end to a slightly-late connected start (<= seam tolerance)
 *   - retain judged Arc ownership across nominal end
 *   - merge near-boundary long-event units when required
 */

// -----------------------------------------------------------------------------
// 15. CONFIRMED / RECONSTRUCTED / UNRESOLVED summary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED
 * ---------
 * - +0xE8 is a Vec3 sampled polyline consumed by live Arc gameplay positioning.
 * - +0x100 is another Vec3 sampled polyline consumed by RenderArcNote-side code.
 * - Both sample the same analytic easing/path model from Section 05.
 * - nominal Arc duration <1000 ms selects base density 14; otherwise 7.
 * - +0x118 is clamped to >=1.0 during Arc initialisation.
 * - the chart/runtime factory supplies +0x118 from chart Arc float +0x88.
 * - +0x118 multiplies the +0x100/render density, not +0xE8 gameplay density.
 * - +0x138/+0x140/+0x148 is a pointer vector owned by LogicArcNote.
 * - its exact push_back helper is ~0x1205BF4 and has one call site here.
 * - the connected-Arc postprocessor pushes next Arc into previous Arc +0x138.
 * - the same postprocessor writes previous Arc into next Arc +0x150.
 * - +0x138 uses the same <=9 ms / <0.1 X / equal Y / body-class seam criteria
 *   as the connected-Arc grouping logic.
 * - the path builder uses max(own end, forward-continuation start) as sampling end.
 * - because of the seam criterion this extension is only a tiny tolerated gap,
 *   never an arbitrary far-future horizon.
 * - judged forward continuations participate in tracker retention past own end.
 * - judged forward continuations can trigger the native ~2 ms tick-boundary
 *   adjustment which removes the final event and assigns scoreUnits=2 to the
 *   new final event, preserving two ScoreState units in one record.
 * - LogicArcGroup +0xE0 and directional +0x138/+0x150 links are separate but are
 *   produced by the same connected-Arc recognition pass.
 *
 * RECONSTRUCTED
 * -------------
 * - names `gameplaySamples`, `renderSamples`, `renderSamplingMultiplier`
 * - names `forwardContinuations` and `previousContinuation`
 * - interpretation of the <=9 ms effective-end extension as seam-gap filling
 * - interpretation of the two-unit event collapse as seam tick de-duplication
 *
 * UNRESOLVED
 * ----------
 * - exact original/public semantic name of chart Arc +0x88 / runtime +0x118
 * - exact reason the seam long-event adjustment uses a 2 ms quotient probe
 * - whether malformed/branching content can create multiple incoming parents and
 *   how +0x150 should be interpreted in that nonordinary case
 * - detailed RenderArcNote mesh/shader construction beyond confirmed +0x100 use
 */
