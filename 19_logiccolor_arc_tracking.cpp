/*
 * Arcaea excavation notebook
 * Section 19: LogicColor and Your Best Nightmare green-Arc mechanics
 *
 * STATUS: LogicColor / YBN green-Arc refinement slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native code/data/control flow.
 *   RECONSTRUCTED = readable representation assembled from confirmed behaviour.
 *   UNRESOLVED    = exact semantic name/purpose is not yet proved.
 *
 * IMPORTANT CORRECTION TO THE FIRST VERSION OF THIS SECTION:
 *   LogicArcNote +0x170 is NOT merely an ownership-tracker bypass.
 *
 *   Its contact-hook effect is indeed to bypass LogicColor touch ownership, but
 *   the same flag is also consumed by two LogicArcNote judgement virtuals and by
 *   SpecialSceneYourBestNightmare. It is therefore better modelled as the
 *   runtime capability flag for the YBN special green-Arc subsystem.
 *
 * Scope:
 *   - identify Section 12's ArcTouchTracker as native LogicColor
 *   - resolve LogicColor channel identity and shared per-colour ownership
 *   - resolve channel ID 3's ownership-bypass behaviour
 *   - separate mechanical re-acquisition lockout from red rejection feedback
 *   - identify the exact YBN + ratingClass-3 construction context
 *   - reconstruct YBN colour-channel folding
 *   - resolve all three LogicArcNote behaviours altered by +0x170
 *   - resolve the SpecialSceneYourBestNightmare +5 RR success path
 *   - clearly separate generic green colour 2 from YBN's special use of it
 *
 * Deliberately out of scope:
 *   - artistic/design motivation for the YBN mechanic
 *   - the full SpecialSceneYourBestNightmare presentation choreography
 *   - every historical April Fools chart using green Arcs
 *   - the separate chart-level colour-3 Arc->ArcTap conversion subsystem
 *   - unlocks, progression, challenge requirements and account systems
 *
 * This file builds directly on:
 *   01_recollection_rate.cpp
 *   02_note_fundamentals.cpp
 *   04_arc_contact.cpp
 *   05_arc_path.cpp
 *   10_timinggroups.cpp
 *   12_arc_contact_refinements.cpp
 *   15_rendering_fundamentals.cpp
 *   17_song_specific_specials.cpp
 *
 * Investigated binary:
 *   libcocos2dcpp.so SHA-256
 *   3eaca4e6dabb3395f276f8915698d57675757d0df0970e716b23a3dc201c79be
 *
 * Useful native anchors:
 *   LogicColor create/initialise helper                  ~0x0D93594
 *   LogicColor channel cache/factory                    ~0x0D51220
 *   LogicColor touch acceptance                         ~0x15A90C4
 *   LogicColor touch release                            ~0x13DB7A0
 *   LogicColor per-frame maintenance                    ~0x1482CB0
 *
 *   LogicChart YBN/ratingClass-3 setup                  ~0x173BC20 / ~0x173C2DC
 *   LogicArcNote +0x170 factory writes                  ~0x18651D4 / ~0x18654E0
 *
 *   LogicArcNote successful-event virtual               ~0x17CE17C
 *   LogicArcNote LOST-event virtual                     ~0x100BC68
 *   LogicArcNote contact/ownership hook                 ~0x15DFE2C
 *
 *   SpecialSceneYourBestNightmare note-event hook       ~0x14F07DC
 *   YBN special LifeBar fan-out helper                  ~0x0E61688
 *   YBN per-LifeBar +5 gain path                        ~0x0EE189C
 *   common RR apply/change routine                      ~0x133FAC8
 *
 *   ScoreState successful judgement                     ~0x1730290
 *   point-note accepted-hit hook                        ~0x0965E28
 *   point-note LOST hook                                ~0x07E995C
 *   LogicLongNoteBase success virtual                   ~0x1314868
 *   LogicLongNoteBase LOST virtual                      ~0x107D66C
 *
 * WARNING ABOUT SYMBOLS:
 * `LogicColor`, `LogicChart`, `LogicArcNote`, and
 * `SpecialSceneYourBestNightmare` are supported by surviving RTTI/type strings.
 * Other member/helper names below are reconstructed semantic names.
 */

#include <algorithm>
#include <cstdint>
#include <vector>

// -----------------------------------------------------------------------------
// 1. Arc +0xB0 points to native LogicColor
// -----------------------------------------------------------------------------

/*
 * CONFIRMED.
 *
 * Section 12 reconstructed Arc +0xB0 as a touch-ownership tracker. The native
 * RTTI identity of that object is `LogicColor`.
 *
 * This class combines two concepts:
 *
 *   1. presentation colour/channel identity
 *   2. touch-ID ownership state for that colour/channel
 *
 * Normal Arcs therefore do not each own an isolated tracker. Arcs which resolve
 * to the same LogicColor share the ownership state of that colour channel.
 */
struct LogicColor {
    float tickIntervalMs;               // +0x0C

    bool nearbyOwnershipBypass;         // +0x10, temporary relaxation
    int32_t nearbyRefreshTimeMs;        // +0x14

    int32_t channelId;                  // +0x18, CONFIRMED

    // +0x1C..+0x21: compact native colour data.
    uint8_t colourData[6];

    bool acceptedThisUpdate;            // +0x22
    bool hasNormalOwnershipHistory;     // +0x24

    int32_t assignedTouchId;            // +0x28, -1 = none
    int32_t releaseLockoutStartMs;      // +0x2C, -1 = inactive

    int32_t rejectionFeedbackActive;    // +0x30, RECONSTRUCTED name
    float rejectionFeedbackRemainingMs;// +0x34, RECONSTRUCTED name
    int32_t rejectionFeedbackStartMs;   // +0x38, RECONSTRUCTED name
};

static int ownershipWindowMs(float tickIntervalMs)
{
    // CONFIRMED from the native ownership helpers.
    return static_cast<int>(
        std::min(4.0f * tickIntervalMs, 1000.0f));
}

// -----------------------------------------------------------------------------
// 2. LogicColor objects are cached by channel ID
// -----------------------------------------------------------------------------

/*
 * CONFIRMED around ~0x0D51220.
 *
 * The chart/runtime conversion derives a LogicColor channel, scans the existing
 * colour vector, and reuses the object whose +0x18 channel matches.
 */
LogicColor* getOrCreateLogicColor(
    std::vector<LogicColor*>& colours,
    int32_t channelId)
{
    for (LogicColor* colour : colours) {
        if (colour->channelId == channelId) {
            return colour;
        }
    }

    LogicColor* colour = createLogicColor(channelId);
    colours.push_back(colour);
    return colour;
}

/*
 * Practical model:
 *
 *     Arc A --+
 *             +--> LogicColor(channel X) --> one ordinary owned touch ID
 *     Arc B --+
 *
 * Different ordinary LogicColors also coordinate through the global claimed-ID
 * set reconstructed in Section 12.
 */

// -----------------------------------------------------------------------------
// 3. LogicColor channel ID 3 is a special ownership channel
// -----------------------------------------------------------------------------

/*
 * CONFIRMED refinement of Section 12's old `mode == 3` branch.
 *
 * +0x18 is the LogicColor channel ID, not a transient tracker mode.
 *
 * Native acceptance ordering is approximately:
 *
 *   release lockout
 *      -> if channelId == 3: accept
 *      -> otherwise ordinary exact-ID/global ownership checks
 *
 * Therefore channel 3 does not claim/enforce ordinary exclusive finger identity.
 * Geometry is still checked before Arc contact reaches this ownership layer.
 */
bool logicColorAcceptsTouch(
    LogicColor& colour,
    int32_t touchId,
    int32_t nowMs,
    float currentTickIntervalMs)
{
    colour.tickIntervalMs = currentTickIntervalMs;

    if (colour.releaseLockoutStartMs != -1) {
        const int timeout = ownershipWindowMs(colour.tickIntervalMs);

        if (nowMs - colour.releaseLockoutStartMs < timeout) {
            armRejectionFeedback(colour, nowMs);
            return false;
        }

        colour.releaseLockoutStartMs = -1;
    }

    if (colour.channelId == 3) {
        return true;
    }

    return ordinaryLogicColorOwnershipCheck(colour, touchId, nowMs);
}

// -----------------------------------------------------------------------------
// 4. Do NOT confuse LogicColor channel 3 with chart Arc colour 3
// -----------------------------------------------------------------------------

/*
 * IMPORTANT NAMESPACE SEPARATION.
 *
 *   LogicColor.channelId == 3
 *       = runtime colour/ownership channel discussed above.
 *
 *   chart Arc colour/index == 3
 *       = a separate extended chart mechanism previously excavated as a
 *         zero-duration Arc conversion path producing LogicArcTapNote-like
 *         runtime behaviour.
 *
 * They happen to use the integer 3 but are not the same concept.
 *
 * The chart-colour-3 conversion is intentionally deferred to its own refinement
 * rather than being mixed into LogicColor ownership semantics here.
 */

// -----------------------------------------------------------------------------
// 5. +0x2C is mechanical lockout; +0x30/+0x34/+0x38 are red feedback
// -----------------------------------------------------------------------------

/*
 * CONFIRMED.
 *
 * Mechanical state:
 *
 *     +0x2C = release/re-acquisition lockout start
 *
 * Rejection presentation state:
 *
 *     +0x30 = feedback latch
 *     +0x38 = feedback start time
 *     +0x34 = remaining feedback time
 *
 * The warning countdown uses the same min(4*tickInterval,1000) duration family,
 * but it is not itself what rejects a touch.
 */
void armRejectionFeedback(LogicColor& colour, int32_t nowMs)
{
    colour.rejectionFeedbackActive = 1;

    if (colour.rejectionFeedbackRemainingMs < 0.0f) {
        colour.rejectionFeedbackStartMs = nowMs;
    }
}

void updateRejectionFeedback(LogicColor& colour, int32_t nowMs)
{
    if (colour.rejectionFeedbackActive == 0) {
        colour.rejectionFeedbackRemainingMs = -1.0f;
        return;
    }

    const int duration = ownershipWindowMs(colour.tickIntervalMs);
    const int elapsed =
        std::max(nowMs - colour.rejectionFeedbackStartMs, 0);
    const int remaining = duration - elapsed;

    colour.rejectionFeedbackRemainingMs =
        remaining >= 0
            ? static_cast<float>(remaining)
            : -1.0f;
}

/*
 * CONFIRMED RenderArcNote consumer:
 * an active rejection countdown blends a judged Arc toward RGB(230,50,50).
 */
struct Rgb8 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

static constexpr Rgb8 kOwnershipWarningColour = {230, 50, 50};

// -----------------------------------------------------------------------------
// 6. YBN special context = exact song ID + selected ratingClass 3
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from LogicChart construction and ratingClass/resource lookup.
 *
 * The old anonymous context byte around LogicChart +0x111 is enabled when:
 *
 *     song/content ID == "yourbestnightmare"
 *     && selected ratingClass == 3
 *
 * The selected integer previously observed at the associated context +0x124 is
 * behaviourally identified as selected ratingClass / difficulty class through:
 *
 *   - native `ratingClass` integer parsing
 *   - difficulty-table lookup
 *   - chart/resource lookup
 *   - a separate `dropdead` branch testing the same field against 2
 */
struct ChartSelectionContext {
    int32_t selectedRatingClass; // conceptual +0x124
};

bool isYourBestNightmareSpecialContext(
    const char* songId,
    const ChartSelectionContext& selection)
{
    return
        stringEquals(songId, "yourbestnightmare") &&
        selection.selectedRatingClass == 3;
}

// -----------------------------------------------------------------------------
// 7. YBN folds chart colours 0 and 1 onto one LogicColor channel
// -----------------------------------------------------------------------------

/*
 * CONFIRMED directly in ~0x0D51220.
 *
 * When LogicChart +0x111 is active:
 *
 *     chart colour 0 -> LogicColor channel 1
 *     chart colour 1 -> LogicColor channel 1
 *     chart colour 2 -> remains channel 2
 *
 * Thus YBN ratingClass 3 deliberately changes the ownership-channel topology.
 */
int32_t deriveLogicColorChannel(
    int32_t chartColourIndex,
    bool ybnSpecialContext)
{
    if (ybnSpecialContext && chartColourIndex <= 1) {
        return 1;
    }

    return ordinaryArcColourChannelMapping(chartColourIndex);
}

/*
 * Readable picture:
 *
 *     chart colour 0 ----+
 *                         +--> shared LogicColor channel 1
 *     chart colour 1 ----+
 *
 *     chart colour 2 --------> separate green LogicColor channel 2
 */

// -----------------------------------------------------------------------------
// 8. Generic chart colour 2 is green; it is NOT inherently a no-score Arc
// -----------------------------------------------------------------------------

/*
 * CONFIRMED by the factory condition and prior colour reconstruction.
 *
 * Chart colour/index 2 is the generic green Arc substrate.
 *
 * Crucially, LogicArcNote +0x170 is NOT set merely because colour == 2.
 * It is set only under the YBN special context described above.
 *
 * Therefore a colour-2 Arc outside that context retains ordinary LogicArcNote
 * successful-event, LOST-event and LogicColor ownership behaviour.
 *
 * This explains why other content can use green Arcs as ordinary scoring Arcs.
 */

// -----------------------------------------------------------------------------
// 9. Arc +0x170 is the YBN special green-Arc capability flag
// -----------------------------------------------------------------------------

/*
 * CONFIRMED factory condition around ~0x18651D4 / ~0x18654E0:
 *
 *     if (LogicChart.+0x111 != 0 && chartArc.colorIndex == 2)
 *         LogicArcNote.+0x170 = 1;
 *
 * Combining the resolved LogicChart context gives:
 */
bool shouldEnableYbnGreenSpecial(
    const char* songId,
    int32_t selectedRatingClass,
    int32_t chartArcColourIndex)
{
    return
        stringEquals(songId, "yourbestnightmare") &&
        selectedRatingClass == 3 &&
        chartArcColourIndex == 2;
}

struct LogicArcNoteSelectedFields {
    LogicColor* logicColor;          // conceptual +0xB0
    bool ybnGreenSpecial;            // +0x170, RECONSTRUCTED name
};

/*
 * The first version of Section 19 called this an ownership-only flag. That was
 * too narrow. Native code consults +0x170 in THREE LogicArcNote virtual paths.
 */

// -----------------------------------------------------------------------------
// 10. +0x170 effect #1: suppress ordinary successful-event accounting
// -----------------------------------------------------------------------------

/*
 * CONFIRMED LogicArcNote virtual around ~0x17CE17C:
 *
 *     return (Arc +0x170 == 0);
 *
 * Vtable comparison aligns this slot with the successful judgement/event hook:
 *
 *   LogicTapNote / common point note:
 *       marks accepted hit and returns true
 *
 *   LogicLongNoteBase:
 *       returns true for each long event
 *
 *   LogicArcNote:
 *       returns !+0x170
 *
 * Section 02 proves ScoreState calls this note virtual BEFORE it increments
 * ordinary judgement counters and BEFORE it fans the success out to LifeBarState.
 * A false result aborts that ordinary accounting path.
 */
bool acceptArcSuccessfulEvent(const LogicArcNoteSelectedFields& arc)
{
    return !arc.ybnGreenSpecial;
}

/*
 * Consequence for a YBN +0x170 Arc event:
 *
 *   - no ordinary Pure/Far/Max-Pure score/judgement counter increment
 *   - no ordinary successful-judgement LifeBarState fan-out
 *   - downstream ordinary streak/combo bookkeeping is skipped with the same
 *     early return
 *
 * Practical result: YBN special green Arcs do not contribute ordinary score or
 * combo despite still having their own special-success behaviour below.
 */

// -----------------------------------------------------------------------------
// 11. +0x170 effect #2: suppress ordinary LOST accounting
// -----------------------------------------------------------------------------

/*
 * CONFIRMED LogicArcNote virtual around ~0x100BC68:
 *
 *     return (Arc +0x170 == 0);
 *
 * Vtable comparison aligns this with the common LOST-event hook:
 *
 *   point note LOST hook       -> accepts the LOST
 *   LogicLongNoteBase LOST     -> returns true
 *   LogicArcNote LOST          -> returns !+0x170
 *
 * Section 02 proves ScoreState asks this virtual before incrementing LOST count
 * and before LifeBarState LOST fan-out.
 */
bool acceptArcLostEvent(const LogicArcNoteSelectedFields& arc)
{
    return !arc.ybnGreenSpecial;
}

/*
 * Therefore YBN +0x170 green Arcs:
 *
 *   - do not increment ordinary LOST count through this path
 *   - do not apply ordinary long-note LOST RR damage through this path
 */

// -----------------------------------------------------------------------------
// 12. +0x170 effect #3: bypass LogicColor finger ownership
// -----------------------------------------------------------------------------

/*
 * CONFIRMED Arc contact hook around ~0x15DFE2C.
 *
 * Geometry is checked before this hook.
 *
 * Ordinary Arc:
 *     geometry -> LogicColor ownership -> Arc contact
 *
 * +0x170 Arc:
 *     geometry -------------------------> Arc contact
 *
 * Thus +0x170 does NOT auto-hit and does NOT bypass the hitbox.
 */
bool acceptGeometricallyQualifiedArcTouch(
    LogicArcNoteSelectedFields& arc,
    int32_t touchId,
    int32_t nowMs,
    float tickIntervalMs)
{
    if (!arc.ybnGreenSpecial) {
        if (!logicColorAcceptsTouch(
                *arc.logicColor,
                touchId,
                nowMs,
                tickIntervalMs)) {
            return false;
        }
    }

    return true;
}

// -----------------------------------------------------------------------------
// 13. SpecialSceneYourBestNightmare also recognizes +0x170 Arcs
// -----------------------------------------------------------------------------

/*
 * CONFIRMED note-event hook around ~0x14F07DC.
 *
 * The surviving SpecialSceneYourBestNightmare virtual performs:
 *
 *     LogicNote*
 *        -> dynamic_cast<LogicArcNote*>
 *        -> require Arc +0x170 != 0
 *        -> invoke YBN special success/gauge helper
 *
 * This is the decisive evidence that +0x170 is a broader YBN capability marker,
 * not merely a local ownership flag.
 */
void onYbnSpecialNoteEvent(
    SpecialSceneYourBestNightmare& scene,
    LogicNote* note)
{
    LogicArcNote* arc = dynamic_cast<LogicArcNote*>(note);

    if (!arc || !arc->ybnGreenSpecial) {
        return;
    }

    applyYbnSpecialGreenGain(scene);
}

// -----------------------------------------------------------------------------
// 14. YBN special green success gives a base +5 RR
// -----------------------------------------------------------------------------

/*
 * CONFIRMED native chain:
 *
 *   SpecialSceneYourBestNightmare note hook  ~0x14F07DC
 *       -> helper                           ~0x0E61688
 *       -> per-LifeBarState path            ~0x0EE189C
 *       -> common RR apply                  ~0x133FAC8
 *
 * The per-LifeBar path contains a literal:
 *
 *     5.0f
 *
 * and passes the resulting gain into the same common RR application routine
 * established in Section 01.
 *
 * If a CharacterAbility is present, the +5 base gain passes through the active
 * ability's modifier hooks before the final common apply/clamp path.
 */
static constexpr float kYbnGreenBaseGain = 5.0f;

void applyYbnSpecialGreenGain(
    std::vector<LifeBarState*>& lifeBars)
{
    for (LifeBarState* bar : lifeBars) {
        float gain = kYbnGreenBaseGain;

        if (bar->ability != nullptr) {
            gain = applyRelevantAbilityGainModifiers(
                *bar->ability,
                gain);
        }

        applyRecollectionChange(
            *bar,
            /* gain */ gain,
            /* loss */ 0.0f);
    }
}

/*
 * This is separate from ordinary note-success gauge fan-out, which +0x170 has
 * already suppressed through the LogicArcNote successful-event virtual.
 *
 * So the special green Arc does NOT receive ordinary RF gain plus 5.
 * It receives the dedicated YBN special base gain path instead.
 */

// -----------------------------------------------------------------------------
// 15. Compact YBN green-Arc state machine
// -----------------------------------------------------------------------------

/*
 * RECONSTRUCTED from the confirmed mechanisms above.
 */
void buildArcForChart(
    RuntimeContext& runtime,
    LogicChart& chart,
    const ChartArc& chartArc)
{
    const bool ybnContext =
        chart.songId == "yourbestnightmare" &&
        chart.selection.selectedRatingClass == 3;

    const int32_t logicColorChannel =
        deriveLogicColorChannel(
            chartArc.colorIndex,
            ybnContext);

    LogicArcNote* arc = createOrdinaryLogicArc(chartArc);
    arc->logicColor =
        getOrCreateLogicColor(
            runtime.logicColors,
            logicColorChannel);

    arc->ybnGreenSpecial =
        ybnContext && chartArc.colorIndex == 2;
}

/*
 * During play:
 *
 *   YBN green touch
 *       |
 *       +-> must still pass ordinary Arc geometry
 *       |
 *       +-> skips LogicColor exact finger ownership
 *       |
 *       +-> drives ordinary Arc contact/event processing
 *       |
 *       +-> ordinary successful-event accounting is rejected
 *       |      -> no ordinary score/combo/LifeBar success fan-out
 *       |
 *       +-> ordinary LOST-event accounting is rejected
 *       |      -> no ordinary LOST count / RR loss
 *       |
 *       +-> SpecialSceneYourBestNightmare recognizes +0x170 success
 *              -> dedicated base +5 RR path
 */

// -----------------------------------------------------------------------------
// 16. About the archived "one judgement tick each" observation
// -----------------------------------------------------------------------------

/*
 * PRIOR EXCAVATION EVIDENCE, NOT RE-DERIVED TO THE SAME STANDARD IN THIS PASS:
 * the archived YBN investigation recorded one special judgement tick/event per
 * green Arc.
 *
 * The current binary pass fully explains what happens WHEN the +0x170 special
 * success callback occurs, but this section does not yet prove the exact
 * construction/data reason that each YBN green Arc supplies only one such event.
 *
 * Therefore:
 *
 *   observed one-event behaviour        = retained prior evidence
 *   exact event-generation mechanism    = UNRESOLVED here
 *
 * This uncertainty does not change the scoring/RR semantics reconstructed above.
 */

// -----------------------------------------------------------------------------
// 17. Final separation of the three colour concepts
// -----------------------------------------------------------------------------

/*
 * Keep these namespaces separate:
 *
 *   chart colour/index 2
 *       generic green Arc substrate
 *
 *   YBN ratingClass-3 + chart colour 2
 *       generic green Arc + runtime +0x170 special capability
 *
 *   LogicColor.channelId 3
 *       special runtime ownership channel which accepts without ordinary
 *       exclusive touch-ID enforcement
 *
 *   chart colour/index 3
 *       separate extended chart conversion path into ArcTap-like runtime
 *       behaviour; not the same as LogicColor channel 3
 */

// -----------------------------------------------------------------------------
// 18. Evidence classification
// -----------------------------------------------------------------------------

/*
 * CONFIRMED:
 *   - Arc +0xB0 points to native LogicColor.
 *   - LogicColor is shared/cached by channel ID.
 *   - LogicColor channel ID 3 bypasses ordinary exclusive finger ownership.
 *   - +0x2C is the actual release/re-acquisition lockout start.
 *   - +0x30/+0x34/+0x38 drive red ownership-rejection feedback.
 *   - the warning colour target is RGB(230,50,50).
 *   - YBN special context is exact song `yourbestnightmare`, ratingClass 3.
 *   - under that context chart colours 0/1 fold onto LogicColor channel 1.
 *   - chart colour 2 remains the separate green channel.
 *   - only YBN ratingClass-3 colour-2 Arcs receive +0x170.
 *   - +0x170 successful-event virtual returns false.
 *   - +0x170 LOST-event virtual returns false.
 *   - +0x170 contact hook skips LogicColor ownership but not geometry.
 *   - SpecialSceneYourBestNightmare recognizes LogicArcNote +0x170.
 *   - the special success path applies a base +5 RR before normal modifier/clamp
 *     machinery.
 *
 * RECONSTRUCTED:
 *   - names such as ybnGreenSpecial and rejectionFeedbackActive.
 *   - practical "no ordinary score/combo" wording around the confirmed early
 *     rejection of ScoreState's ordinary success-accounting path.
 *
 * UNRESOLVED / retained prior evidence:
 *   - exact original source names for the affected virtuals.
 *   - exact event-construction reason behind the archived one-event-per-green-Arc
 *     observation.
 *   - artistic/design motivation for YBN's colour-channel topology.
 *   - chart-colour-3 conversion details, deferred to another refinement.
 */
