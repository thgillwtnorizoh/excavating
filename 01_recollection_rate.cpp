/*
 * Arcaea excavation notebook
 * Section 01: Recollection Rate fundamentals
 *
 * STATUS: fundamental section complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native code/data.
 *   RECONSTRUCTED = readable representation assembled from confirmed behaviour.
 *   UNRESOLVED    = exact semantic name/purpose is not yet proved.
 *
 * Useful native anchors from the investigated ARM64 build:
 *   RF calculation                    ~0x1553F60..0x1554054
 *   LifeBarState successful judgement ~0x16E4AB0
 *   ScoreState judgement fan-out      ~0x173032C..0x1730354
 *   LOST/miss path                    ~0x1869E90
 *   common RR apply/change routine    ~0x133FAC8
 *   Hard 30-RR crossing logic         ~0x120DEDC..0x120DF18
 *
 * Important observed LifeBarState fields:
 *   +0x10 current Recollection Rate
 *   +0x8C Recollection Factor (RF)
 *   +0x98 LOST-damage percentage scaler; 100 = normal damage
 *   +0xA0 gauge mode
 *   +0xC8 CharacterAbility*
 */

#include <algorithm>
#include <vector>

// -----------------------------------------------------------------------------
// 1. Fundamental enums
// -----------------------------------------------------------------------------

enum class Judgement {
    MaxPure = 0, // CONFIRMED: increments Max Pure and Pure counters
    Pure    = 1, // CONFIRMED: increments Pure counter
    Far     = 2, // CONFIRMED: increments Far counter
    Lost    = 3  // CONFIRMED conceptually; processed through separate miss path
};

enum class GaugeMode {
    Normal = 0, // CONFIRMED
    Easy   = 1, // CONFIRMED
    Hard   = 2, // CONFIRMED

    // Other gauge modes exist but are deliberately outside this section.
};

enum class ClearType {
    Fail        = 0, // CONFIRMED by clear-type asset mapping
    NormalClear = 1,
    FullRecall  = 2,
    PureMemory  = 3,
    EasyClear   = 4,
    HardClear   = 5
};

// -----------------------------------------------------------------------------
// 2. Recollection Factor
// -----------------------------------------------------------------------------

/*
 * CONFIRMED.
 *
 * Arcaea derives per-note RR gain from total chart note count N.
 * A PURE and MAX PURE both gain one RF.
 * A FAR gains half an RF.
 */
float calculateRecollectionFactor(int noteCount, bool unknownEightyPercentFlag)
{
    float rf;

    if (noteCount < 400) {
        rf = 0.2f + 80.0f / static_cast<float>(noteCount);
    }
    else if (noteCount < 600) {
        rf = 0.2f + 32.0f / static_cast<float>(noteCount);
    }
    else {
        rf = 0.08f + 96.0f / static_cast<float>(noteCount);
    }

    // CONFIRMED arithmetic.
    // UNRESOLVED semantic name of the chart/gameplay flag controlling it.
    if (unknownEightyPercentFlag) {
        rf *= 0.8f;
    }

    return rf;
}

// Example:
//   N = 1000
//   RF = 0.08 + 96/1000 = 0.176
//   PURE = +0.176 RR
//   FAR  = +0.088 RR

// -----------------------------------------------------------------------------
// 3. Minimal gameplay-side model
// -----------------------------------------------------------------------------

struct CharacterAbility {
    // These are conceptual hooks. Exact original method names are not claimed.
    virtual float modifyGain(float gain, Judgement judgement) { return gain; }
    virtual float modifyLoss(float loss, Judgement judgement) { return loss; }
};

struct Note {
    virtual ~Note() = default;
};

/*
 * CONFIRMED type name from RTTI/native checks.
 * LOST damage explicitly checks whether the note is LogicLongNoteBase.
 */
struct LogicLongNoteBase : Note {
};

struct LifeBarState {
    GaugeMode gaugeMode = GaugeMode::Normal;

    float recollectionRate = 0.0f;     // observed around +0x10
    float recollectionFactor = 0.0f;   // observed +0x8C
    float lossScalePercent = 100.0f;   // observed +0x98

    float minimum = 0.0f;
    float maximum = 100.0f;

    bool deadOrDisabled = false;

    CharacterAbility* ability = nullptr;

    // UNRESOLVED semantic name. The result resolver contains an explicit
    // zero/depletion failure flag used by Hard-like survival gauges.
    bool failWhenDepleted = false;
};

// -----------------------------------------------------------------------------
// 4. Starting state
// -----------------------------------------------------------------------------

/*
 * CONFIRMED for the ordinary gauge modes in scope.
 *
 * Normal/Easy build upward from zero.
 * Hard belongs to the full-start family and begins at 100.
 */
void initialiseGauge(LifeBarState& bar)
{
    switch (bar.gaugeMode) {
        case GaugeMode::Normal:
        case GaugeMode::Easy:
            bar.recollectionRate = 0.0f;
            break;

        case GaugeMode::Hard:
            bar.recollectionRate = 100.0f;
            bar.failWhenDepleted = true; // semantic reconstruction
            break;
    }
}

// -----------------------------------------------------------------------------
// 5. Successful judgement gain
// -----------------------------------------------------------------------------

float baseGainForJudgement(const LifeBarState& bar, Judgement judgement)
{
    switch (judgement) {
        case Judgement::MaxPure:
        case Judgement::Pure:
            // CONFIRMED: identical baseline RR gain.
            return bar.recollectionFactor;

        case Judgement::Far:
            // CONFIRMED literal 0.5 multiplication in native routine.
            return bar.recollectionFactor * 0.5f;

        case Judgement::Lost:
            // LOST does not use the successful-judgement path.
            return 0.0f;
    }

    return 0.0f;
}

// -----------------------------------------------------------------------------
// 6. LOST damage
// -----------------------------------------------------------------------------

/*
 * CONFIRMED ordinary standalone-note losses:
 *
 *   Normal = 2.0 RR
 *   Easy   = 1.2 RR
 *   Hard   = 9.0 RR in the high region, 5.0 RR in the low region
 *
 * The native implementation internally forms half-sized coefficients and then
 * doubles them for a normal note. LogicLongNoteBase skips that doubling, so a
 * long-note loss event does half the standalone-note damage.
 */
float baseStandaloneLostDamage(const LifeBarState& bar)
{
    switch (bar.gaugeMode) {
        case GaugeMode::Normal:
            return 2.0f;

        case GaugeMode::Easy:
            return 1.2f;

        case GaugeMode::Hard:
            // CONFIRMED 30-RR seam in native logic.
            return (bar.recollectionRate <= 30.0f) ? 5.0f : 9.0f;
    }

    return 0.0f;
}

bool isLongNote(const Note* note)
{
    // Represents the confirmed RTTI/type check against LogicLongNoteBase.
    return dynamic_cast<const LogicLongNoteBase*>(note) != nullptr;
}

float calculateLostDamage(const LifeBarState& bar, const Note* note)
{
    float loss = baseStandaloneLostDamage(bar);

    // CONFIRMED: long-note loss events receive half ordinary event damage.
    if (isLongNote(note)) {
        loss *= 0.5f;
    }

    // CONFIRMED: +0x98 is treated as a percentage, 100 = 1.0x.
    loss *= bar.lossScalePercent / 100.0f;

    if (bar.ability != nullptr) {
        // RECONSTRUCTED interface around confirmed ability-modifier hooks.
        loss = bar.ability->modifyLoss(loss, Judgement::Lost);
    }

    return loss;
}

// Baseline table before abilities:
//
//                  standalone LOST      long-note loss event
//   Normal             -2.0                    -1.0
//   Easy               -1.2                    -0.6
//   Hard high          -9.0                    -4.5
//   Hard low           -5.0                    -2.5

// -----------------------------------------------------------------------------
// 7. Hard's 30-RR crossing seam
// -----------------------------------------------------------------------------

/*
 * CONFIRMED behaviour:
 *   - Hard uses a stronger loss above the low-RR region.
 *   - The native code explicitly treats 30 RR as a boundary.
 *   - When a single loss crosses from above 30 to below 30, an additional
 *     correction is performed using 0.5 * (before - 30).
 *
 * The exact original flag/name surrounding the correction is still unresolved,
 * so this function intentionally preserves the observed arithmetic without
 * inventing a polished game-design name for it.
 */
float applyHardBoundaryCorrection(
    float before,
    float tentativeAfter,
    bool boundaryCorrectionEnabled)
{
    if (!boundaryCorrectionEnabled) {
        return tentativeAfter;
    }

    if (before > 30.0f && tentativeAfter < 30.0f) {
        tentativeAfter += 0.5f * (before - 30.0f);
    }

    return tentativeAfter;
}

// -----------------------------------------------------------------------------
// 8. Common RR application
// -----------------------------------------------------------------------------

void applyRecollectionChange(
    LifeBarState& bar,
    float gain,
    float loss,
    bool hardBoundaryCorrectionEnabled = true)
{
    if (bar.deadOrDisabled) {
        return;
    }

    const float before = bar.recollectionRate;
    float after = before + gain - loss;

    if (bar.gaugeMode == GaugeMode::Hard && loss > 0.0f) {
        after = applyHardBoundaryCorrection(
            before,
            after,
            hardBoundaryCorrectionEnabled);
    }

    // CONFIRMED common clamp architecture. Ordinary gauges use 0..100.
    after = std::clamp(after, bar.minimum, bar.maximum);

    bar.recollectionRate = after;

    // RECONSTRUCTED survival state.
    // Runtime/result code has explicit depletion/dead handling for Hard-like
    // gauges; exact original field/method name is not claimed here.
    if (bar.failWhenDepleted && bar.recollectionRate <= 0.0f) {
        bar.deadOrDisabled = true;
    }
}

// -----------------------------------------------------------------------------
// 9. LifeBarState event entry points
// -----------------------------------------------------------------------------

void onSuccessfulJudgement(LifeBarState& bar, Judgement judgement)
{
    if (bar.deadOrDisabled) {
        return;
    }

    float gain = baseGainForJudgement(bar, judgement);

    if (bar.ability != nullptr) {
        // RECONSTRUCTED interface around confirmed ability hooks.
        gain = bar.ability->modifyGain(gain, judgement);
    }

    applyRecollectionChange(bar, gain, 0.0f);
}

void onLost(LifeBarState& bar, const Note* note)
{
    if (bar.deadOrDisabled) {
        return;
    }

    const float loss = calculateLostDamage(bar, note);
    applyRecollectionChange(bar, 0.0f, loss);
}

// -----------------------------------------------------------------------------
// 10. ScoreState fan-out
// -----------------------------------------------------------------------------

struct ScoreState {
    int maxPureCount = 0;
    int pureCount = 0;
    int farCount = 0;
    int lostCount = 0;

    // CONFIRMED architecture: ScoreState owns/iterates multiple LifeBarState*s.
    std::vector<LifeBarState*> lifeBars;

    void registerHit(const Note* note, Judgement judgement)
    {
        switch (judgement) {
            case Judgement::MaxPure:
                ++maxPureCount;
                ++pureCount;
                break;

            case Judgement::Pure:
                ++pureCount;
                break;

            case Judgement::Far:
                ++farCount;
                break;

            case Judgement::Lost:
                // LOST arrives through the separate miss path.
                return;
        }

        // CONFIRMED: each active LifeBarState receives the judgement.
        for (LifeBarState* bar : lifeBars) {
            onSuccessfulJudgement(*bar, judgement);
        }
    }

    void registerLost(const Note* note)
    {
        ++lostCount;

        // RECONSTRUCTED around confirmed separate LOST fan-out path.
        for (LifeBarState* bar : lifeBars) {
            onLost(*bar, note);
        }
    }
};

// -----------------------------------------------------------------------------
// 11. Authoritative clear classification
// -----------------------------------------------------------------------------

/*
 * CONFIRMED overall hierarchy from the native clear-type resolver:
 *
 *   no LOST + no FAR -> Pure Memory
 *   no LOST          -> Full Recall
 *   otherwise gauge-specific clear/fail result
 *
 * Normal and Easy require final RR >= 70.
 * Hard is survival/depletion based and does NOT require final RR >= 70.
 */
ClearType determineClearType(const ScoreState& score, const LifeBarState& bar)
{
    if (score.lostCount == 0) {
        if (score.farCount == 0) {
            return ClearType::PureMemory;
        }

        return ClearType::FullRecall;
    }

    switch (bar.gaugeMode) {
        case GaugeMode::Normal:
            return (bar.recollectionRate >= 70.0f)
                ? ClearType::NormalClear
                : ClearType::Fail;

        case GaugeMode::Easy:
            return (bar.recollectionRate >= 70.0f)
                ? ClearType::EasyClear
                : ClearType::Fail;

        case GaugeMode::Hard:
            // CONFIRMED principle: survival, not the 70-RR threshold.
            // Native result code contains explicit zero/negative depletion
            // checks plus a boolean whose exact original semantic name remains
            // unresolved.
            return (!bar.deadOrDisabled && bar.recollectionRate > 0.0f)
                ? ClearType::HardClear
                : ClearType::Fail;
    }

    return ClearType::Fail;
}

// -----------------------------------------------------------------------------
// 12. Mental model
// -----------------------------------------------------------------------------

/*
 *                        total chart notes
 *                               |
 *                               v
 *                     calculate RF once
 *                               |
 *                               v
 *                           ScoreState
 *                               |
 *                judgement / miss arrives
 *                   /          |          \
 *              Max/Pure       FAR         LOST
 *                  |            |            |
 *                 +RF         +RF/2      gauge-mode loss
 *                                             |
 *                                      long-note x0.5
 *                                             |
 *                                      lossScale /100
 *                   \            |            /
 *                    \           |           /
 *                         LifeBarState
 *                               |
 *                      CharacterAbility
 *                               |
 *                           clamp RR
 *                               |
 *                              HPBar
 *                               |
 *                            display
 *
 * HPBar is presentation. LifeBarState is the actual gauge model.
 */

// -----------------------------------------------------------------------------
// 13. Intentionally deferred
// -----------------------------------------------------------------------------

/*
 * Not part of this completed fundamental section:
 *   - Tempest / Fatalis / Overflow / Irruption / Forlorn / Echelon etc.
 *   - CHUNITHM and other specialised gauges
 *   - partner-specific ability details
 *   - unlock requirements
 *   - challenge progression
 *
 * Those systems should be studied later as modifications to this baseline,
 * rather than re-deriving the entire Recollection system each time.
 */
