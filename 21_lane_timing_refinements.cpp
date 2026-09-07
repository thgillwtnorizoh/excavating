/*
 * Arcaea excavation notebook
 * Section 21: lane-classifier and timing-model refinements
 *
 * STATUS: lane/timing semantic refinement slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native control flow, constants,
 *                   strings, data layout, or independent consumers.
 *   RECONSTRUCTED = readable structure assembled from confirmed behaviour;
 *                   names may differ from original source.
 *   UNRESOLVED    = exact original semantic name or design reason is not proved.
 *
 * Scope of this section is deliberately narrow:
 *   1. resolve the exact `x == +425` floor-lane classifier seam
 *   2. identify the remaining LogicTimingEvent timing value as measure beat count
 *   3. identify LogicChart +0xF0/+0xF4 as highspeed and BPM-normalised scroll scale
 *   4. bound the shared fallback-clock `-3000 ms` pre-roll behaviour
 *
 * This file refines:
 *   08_lane_geometry.cpp
 *   10_timinggroups.cpp
 *   11_gameplay_space.cpp
 *
 * It intentionally does NOT branch into rendering polish, SpecialScene systems,
 * unlock/progression logic, or unrelated chart metadata.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>

// -----------------------------------------------------------------------------
// 1. Exact +425 floor-lane seam
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from the primary floor-lane classifier.
 *
 * The native comparison sequence is genuinely asymmetric around +425:
 *
 *     x <  425 -> lane 4
 *     x >  425 && x <= 850 -> lane 5
 *     x == 425 -> no primary lane
 *
 * No later remap repairs the zero result.
 *
 * This is different from the other ordinary lane boundaries and is therefore
 * preserved exactly rather than silently normalised into a mathematically tidy
 * half-open interval model.
 */
static int classifyPrimaryLaneExact(
    int x,
    bool outerLanesEnabled)
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
        return 0;
    }

    if (x <= 850) {
        return 5;
    }

    return outerLanesEnabled ? 6 : 5;
}

/*
 * CONFIRMED practical consequence:
 * The ordinary projected floor X is converted to integer by truncation toward
 * zero before the primary classifier.
 *
 * Therefore for positive coordinates:
 *
 *     425.0 <= projectedX < 426.0
 *           -> integer x == 425
 *           -> primary lane == 0
 *
 * So the seam is a one-integer-unit transformed-space sliver, not only the exact
 * floating-point point 425.0.
 *
 * RECONSTRUCTED design interpretation:
 * There is no surrounding gameplay state, note type, or special chart behaviour
 * attached to this hole. The evidence therefore fits an implementation-level
 * strict-comparison quirk much better than an intentional lane mechanic.
 */
static int classifyProjectedFloorX(
    float projectedX,
    bool outerLanesEnabled)
{
    const int nativeIntegerX = static_cast<int>(projectedX);
    return classifyPrimaryLaneExact(nativeIntegerX, outerLanesEnabled);
}

// -----------------------------------------------------------------------------
// 2. LogicTimingEvent: raw BPM versus measure beat count
// -----------------------------------------------------------------------------

/*
 * Section 10 established the chart -> runtime copy shape:
 *
 *   chart Timing +0x1C -> sourceValue0
 *   chart Timing +0x20 -> sourceValue1
 *
 * and runtime construction:
 *
 *   LogicTimingEvent +0x18 = sourceValue0 * contextMultiplier
 *   LogicTimingEvent +0x1C = sourceValue1
 *   LogicTimingEvent +0x20 = sourceValue0
 *   LogicTimingEvent +0x28 = 60 / (sourceValue0 * contextMultiplier)
 *
 * The remaining semantic identities can now be separated by independent native
 * consumers.
 *
 * CONFIRMED behavioural identities:
 *
 *   sourceValue0 / chart +0x1C / runtime +0x20
 *       = raw musical BPM
 *
 *   sourceValue1 / chart +0x20 / runtime +0x1C
 *       = beat count used to form a measure interval
 *
 * A separate timing consumer computes the equivalent of:
 *
 *     beatDurationMs    = 60000 / rawBpm
 *     measureDurationMs = beatDurationMs * sourceValue1
 *
 * and advances chart time using that measure duration.
 *
 * `beatsPerMeasure` is therefore a safe readable behavioural name. This does not
 * claim the exact original source member identifier.
 */
struct LogicTimingEventRefined {
    int32_t startTimeMs;              // +0x10
    int32_t endTimeMs;                // +0x14, equal to start for timing events

    float effectiveSpatialBpm;        // +0x18
    float beatsPerMeasure;            // +0x1C
    float rawBpm;                     // +0x20

    float unknown24;                  // +0x24, intentionally not renamed here
    float secondsPerEffectiveBeat;    // +0x28
};

static float beatDurationMs(float rawBpm)
{
    return 60000.0f / std::fabs(rawBpm);
}

static float measureDurationMs(
    float rawBpm,
    float beatsPerMeasure)
{
    return beatDurationMs(rawBpm) * beatsPerMeasure;
}

/*
 * Gameplay meaning:
 * The timing event carries two different notions at once:
 *
 *   raw musical rhythm
 *       -> raw BPM
 *       -> long-note tick/event spacing
 *
 *   spatial progression rhythm
 *       -> BPM scaled by chart/player scroll context
 *       -> Arc/path/note movement calculations
 *
 * This is why changing note speed does not need to alter the music's actual beat
 * grid or the judgement clock.
 */

// -----------------------------------------------------------------------------
// 3. LogicChart +0xF0 is selected highspeed / note-speed value
// -----------------------------------------------------------------------------

/*
 * CONFIRMED behavioural identity from two independent paths.
 *
 * A gameplay/session path reads LogicChart +0xF0, multiplies it by 10, converts
 * it to integer, and forwards that encoded value with chart/session metadata.
 *
 * Separately, the settings path stores/parses `highspeed` using the same
 * value-times-ten representation.
 *
 * This matching representation and the construction behaviour below identify
 * +0xF0 as the selected highspeed / note-speed value strongly enough to use that
 * semantic name in the reconstruction.
 */
struct LogicChartScrollContext {
    float highspeed;   // +0xF0, behavioural identity CONFIRMED
    float scrollScale; // +0xF4, formula CONFIRMED below
};

static int encodeHighspeedForSession(float highspeed)
{
    return static_cast<int>(highspeed * 10.0f);
}

// -----------------------------------------------------------------------------
// 4. LogicChart +0xF4 normalises note speed against chart base BPM
// -----------------------------------------------------------------------------

/*
 * CONFIRMED construction formula:
 *
 *     scrollScale = highspeed * 180 / bpmBase
 *
 * where `bpmBase` comes from the selected song/difficulty metadata used by the
 * LogicChart construction path.
 *
 * The exact original field/member name is not recovered; `scrollScale` is a
 * readable behavioural name.
 */
static float calculateScrollScale(
    float highspeed,
    float bpmBase)
{
    return highspeed * 180.0f / bpmBase;
}

/*
 * CONFIRMED TimingEvent construction then applies that scale to every local
 * timing BPM:
 *
 *     effectiveSpatialBpm = timingBpm * scrollScale
 *
 *     secondsPerEffectiveBeat = 60 / effectiveSpatialBpm
 */
static LogicTimingEventRefined makeTimingEventRefined(
    int32_t timeMs,
    float timingBpm,
    float beatsPerMeasure,
    float scrollScale)
{
    LogicTimingEventRefined out{};

    out.startTimeMs = timeMs;
    out.endTimeMs = timeMs;

    out.rawBpm = timingBpm;
    out.beatsPerMeasure = beatsPerMeasure;

    out.effectiveSpatialBpm =
        timingBpm * scrollScale;

    out.secondsPerEffectiveBeat =
        60.0f / out.effectiveSpatialBpm;

    return out;
}

/*
 * The normalization becomes especially clear when the current timing BPM equals
 * the chart's selected `bpmBase`:
 *
 *     effectiveSpatialBpm
 *       = bpmBase * (highspeed * 180 / bpmBase)
 *       = highspeed * 180
 *
 * So the chart's own base BPM cancels out.
 *
 * Gameplay interpretation:
 * Two songs with very different musical BPM can still present a comparable base
 * note-travel speed for the same selected highspeed. Local BPM changes can still
 * change spatial progression relative to that base because each timing event
 * multiplies its own raw BPM by the same chart scroll scale.
 */
static float effectiveSpatialBpmAtBaseTempo(float highspeed)
{
    return highspeed * 180.0f;
}

// -----------------------------------------------------------------------------
// 5. Raw BPM remains independent for long-note tick timing
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from LogicLongNoteBase.
 *
 * Long-note tick generation follows LogicTimingEvent +0x20, the RAW BPM field,
 * not +0x18 and not +0x28.
 *
 * Therefore selected highspeed changes scroll/path progression but does not
 * rewrite the musical tick interval used by Hold/Arc long-note events.
 */
static float calculateLongTickIntervalMs(
    const LogicTimingEventRefined& timing,
    float timingPointDensityFactor)
{
    const float bpm = std::fabs(timing.rawBpm);
    const float beatMs = 60000.0f / bpm;

    const float subdivision =
        bpm >= 255.0f ? 1.0f : 2.0f;

    return
        beatMs
        / subdivision
        / timingPointDensityFactor;
}

/*
 * The clean separation is therefore:
 *
 *   GLOBAL GAMEPLAY CLOCK
 *       -> note judgement timestamps / expiry
 *
 *   RAW TIMING BPM
 *       -> musical long-note tick spacing
 *
 *   RAW BPM * scrollScale
 *       -> spatial/path progression
 *
 * Highspeed belongs to the third layer, not the first two.
 */

// -----------------------------------------------------------------------------
// 6. `dropdead` ratingClass 2 has a native minimum scroll scale
// -----------------------------------------------------------------------------

/*
 * CONFIRMED song/difficulty-specific construction branch:
 *
 *     songId == "dropdead"
 *     && selectedRatingClass == 2
 *
 * forces the derived scroll scale to be at least 2.5.
 *
 * This does not change the generic formula above; it is a narrow post-formula
 * exception applied to that exact song/difficulty context.
 */
static float applyDropdeadScrollMinimum(
    const char* songId,
    int32_t selectedRatingClass,
    float scrollScale)
{
    if (stringEquals(songId, "dropdead") &&
        selectedRatingClass == 2) {
        return std::max(scrollScale, 2.5f);
    }

    return scrollScale;
}

// -----------------------------------------------------------------------------
// 7. Shared gameplay clock fallback and the exact -3000 ms branch
// -----------------------------------------------------------------------------

/*
 * Section 10 identified one global effective gameplay clock used by point
 * judgement, automatic note expiry, long-note processing, Flick handling,
 * SceneControl scheduling, and other gameplay systems.
 *
 * The arithmetic is CONFIRMED:
 *
 *   synchronized/live path:
 *       synchronizedSource - commonOffset
 *
 *   fallback path:
 *       fallbackSource - commonOffset
 *       and, only when fallbackSource <= 0, subtract another 3000 ms
 */
struct GameplayClockRefined {
    int32_t synchronizedSource; // conceptual +0x20
    int32_t commonOffset;       // conceptual +0x28
    bool useSynchronizedPath;   // conceptual +0x2D
    int32_t fallbackSource;     // conceptual +0x34
};

static int32_t effectiveGameplayTimeMs(
    const GameplayClockRefined& clock)
{
    if (clock.useSynchronizedPath) {
        return
            clock.synchronizedSource
            - clock.commonOffset;
    }

    int32_t result =
        clock.fallbackSource
        - clock.commonOffset;

    if (clock.fallbackSource <= 0) {
        result -= 3000;
    }

    return result;
}

/*
 * What this means mechanically:
 *
 * The fallback clock has two phases:
 *
 *   fallbackSource > 0
 *       -> ordinary positive-running gameplay time
 *
 *   fallbackSource <= 0
 *       -> gameplay timeline is deliberately shifted 3000 ms earlier
 *
 * Because the same rule is used across multiple note/scheduler systems, this is
 * a shared pre-zero gameplay timeline, not a note-specific timing correction.
 *
 * RECONSTRUCTED purpose:
 * The behaviour is consistent with synthesising a three-second chart pre-roll
 * when the fallback source itself cannot provide meaningful negative pre-start
 * time.
 *
 * UNRESOLVED:
 * This build does not expose enough trustworthy linkage to name the exact native
 * producer of `fallbackSource` or prove that it is specifically an audio/media
 * playback position. Do not promote that interpretation beyond reconstruction.
 */

// -----------------------------------------------------------------------------
// 8. Compact final model
// -----------------------------------------------------------------------------

/*
 * FLOOR INPUT
 *   projected float X
 *       -> truncate toward zero
 *       -> exact native lane classifier
 *       -> x == +425 produces no primary lane
 *
 * TIMING EVENT
 *   raw BPM -------------------------------> long-note tick spacing
 *      |
 *      +-- * (highspeed * 180 / bpmBase)
 *               |
 *               v
 *         effective spatial BPM
 *               |
 *               v
 *         Arc/path/note travel progression
 *
 *   beatsPerMeasure
 *       -> (60000 / rawBPM) * beatsPerMeasure
 *       -> measure-duration timing consumer
 *
 * JUDGEMENT
 *   shared effective gameplay clock
 *       -> synchronized path when available
 *       -> fallback path otherwise
 *       -> fallbackSource <= 0 synthesises an extra -3000 ms pre-zero phase
 *
 * These systems are related but intentionally not collapsed into one clock or
 * one universal speed variable.
 */

// -----------------------------------------------------------------------------
// 9. Evidence summary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED:
 *   - primary lane classifier returns 0 for integer x == +425
 *   - positive projected X in [425, 426) truncates into that seam
 *   - chart Timing +0x1C / runtime +0x20 is raw BPM
 *   - chart Timing +0x20 / runtime +0x1C is used as measure beat count
 *   - LogicChart +0xF0 behaves as selected highspeed
 *   - LogicChart +0xF4 = highspeed * 180 / selected bpmBase
 *   - TimingEvent effective spatial BPM = raw BPM * +0xF4
 *   - raw BPM, not effective spatial BPM, drives long-note tick spacing
 *   - dropdead ratingClass 2 clamps scroll scale to at least 2.5
 *   - fallback gameplay time subtracts 3000 ms when fallbackSource <= 0
 *
 * RECONSTRUCTED:
 *   - source-level reason for the +425 hole is an implementation comparison quirk
 *   - semantic name `scrollScale`
 *   - the -3000 branch synthesises pre-roll for a source that bottoms out at zero
 *
 * UNRESOLVED:
 *   - exact original names for LogicChart +0xF0/+0xF4
 *   - exact native producer/meaning of GameplayClock fallbackSource
 *   - exact source-level design motivation for the +425 comparison asymmetry
 */
