/*
 * Arcaea excavation notebook
 * Section 19: LogicColor, Arc touch ownership, and Your Best Nightmare exception
 *
 * STATUS: LogicColor / Arc ownership refinement slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native code/data/control flow.
 *   RECONSTRUCTED = readable representation assembled from confirmed behaviour.
 *   UNRESOLVED    = exact semantic name/purpose is not yet proved.
 *
 * Scope of this section:
 *   - identify Section 12's reconstructed ArcTouchTracker as native LogicColor
 *   - refine the Arc ownership model from per-Arc tracker state to shared
 *     per-colour/channel LogicColor state
 *   - resolve the old `mode == 3` special case as LogicColor channel ID 3
 *   - separate mechanical re-acquisition lockout from red ownership-warning
 *     presentation state
 *   - identify the external condition which enables LogicArcNote +0x170
 *   - prove that the condition is specific to `yourbestnightmare`, ratingClass 3
 *   - reconstruct that chart's special LogicColor channel folding and colour-2
 *     ownership bypass
 *
 * Deliberately out of scope:
 *   - the artistic/design reason Your Best Nightmare uses this ownership model
 *   - every SpecialSceneYourBestNightmare visual effect
 *   - exact original member names for LogicColor's warning-state fields
 *   - unlocks, progression, challenge requirements, or account state
 *
 * This file builds directly on:
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
 * Useful native anchors from this build:
 *   LogicColor create/initialise helper                  ~0x0D93594
 *   LogicColor channel cache/factory                    ~0x0D51220
 *   LogicColor touch acceptance                         ~0x15A90C4
 *   LogicColor touch release                            ~0x13DB7A0
 *   LogicColor per-frame maintenance                    ~0x1482CB0
 *   LogicChart virtual initialiser                      ~0x173B358
 *   LogicChart YBN/ratingClass-3 branch                 ~0x173BC20 / ~0x173C2DC
 *   LogicArcNote +0x170 factory branches                ~0x18651D4 / ~0x18654E0
 *   `ratingClass` parser references                     ~0x0D9A278 / ~0x10E90CC
 *   selected-ratingClass difficulty lookup              ~0x0E17528
 *   selected-ratingClass resource lookup                ~0x1513C24
 *
 * WARNING ABOUT SYMBOLS:
 * `LogicColor` and `LogicChart` survive through RTTI/type-name evidence.
 * Names such as `ownershipWarningActive` below are reconstructed semantic names,
 * not recovered original source identifiers.
 */

#include <algorithm>
#include <cstdint>
#include <vector>

// -----------------------------------------------------------------------------
// 1. Section 12's ArcTouchTracker is actually native LogicColor
// -----------------------------------------------------------------------------

/*
 * CONFIRMED by surviving RTTI plus constructor/factory behaviour.
 *
 * Section 12 reconstructed Arc +0xB0 as a touch-ownership tracker because its
 * observed consumers were almost entirely ownership-related. The underlying
 * native class is broader: it is `LogicColor`.
 *
 * A LogicColor instance carries both:
 *   - a colour/channel identity used by Arc rendering/grouping
 *   - the touch-ID ownership state associated with that channel
 *
 * The important architectural correction is therefore:
 *
 *     old mental model:
 *         each Arc owns an independent tracker
 *
 *     refined model:
 *         Arc -> shared LogicColor channel -> touch ownership for that colour
 *
 * The factory reuses an existing LogicColor whose channel ID matches the
 * requested channel, rather than necessarily allocating one object per Arc.
 */
struct LogicColor {
    float tickIntervalMs;               // +0x0C, behaviour from Section 12

    bool proximityOwnershipBypass;      // +0x10
    int32_t proximityRefreshTimeMs;     // +0x14

    int32_t channelId;                  // +0x18, CONFIRMED colour/channel identity

    // +0x1C..+0x21 contain compact colour data used by presentation.
    // Exact original member grouping/name is not required here.
    uint8_t colourData[6];

    bool acceptedThisUpdate;            // +0x22
    bool hasNormalOwnershipHistory;     // +0x24

    int32_t assignedTouchId;            // +0x28, -1 = none
    int32_t releaseLockoutStartMs;      // +0x2C, -1 = inactive

    int32_t ownershipWarningActive;      // +0x30, RECONSTRUCTED name
    float ownershipWarningRemainingMs;  // +0x34, RECONSTRUCTED name
    int32_t ownershipWarningStartMs;     // +0x38, RECONSTRUCTED name
};

static int ownershipWindowMs(float tickIntervalMs)
{
    // CONFIRMED arithmetic from Section 12.
    return static_cast<int>(
        std::min(4.0f * tickIntervalMs, 1000.0f));
}

// -----------------------------------------------------------------------------
// 2. LogicColor is cached/reused by channel ID
// -----------------------------------------------------------------------------

/*
 * CONFIRMED around the LogicColor factory/cache helper ~0x0D51220.
 *
 * The Arc conversion path first derives a LogicColor channel ID from chart Arc
 * colour plus current LogicChart context. It then scans the existing LogicColor
 * collection and reuses an entry with matching +0x18.
 *
 * Only when the requested channel does not already exist is a new LogicColor
 * allocated/initialised.
 */
LogicColor* getOrCreateLogicColor(
    std::vector<LogicColor*>& colours,
    int32_t requestedChannelId)
{
    for (LogicColor* colour : colours) {
        if (colour->channelId == requestedChannelId) {
            return colour;
        }
    }

    LogicColor* colour = createLogicColor(requestedChannelId);
    colours.push_back(colour);
    return colour;
}

/*
 * CONFIRMED consequence:
 * Normal Arc touch ownership is naturally shared by Arcs which resolve to the
 * same LogicColor channel. Section 12's process/global claimed-touch list still
 * prevents independent fresh claims of one finger by unrelated ordinary colour
 * channels, while each LogicColor remembers its currently assigned touch ID.
 */

// -----------------------------------------------------------------------------
// 3. The old mysterious `mode == 3` is LogicColor channel ID 3
// -----------------------------------------------------------------------------

/*
 * Section 12 observed an integer at +0x18 and called it `mode` because its
 * semantic origin was unknown. The acceptance helper contained:
 *
 *     if (+0x18 == 3)
 *         return true;
 *
 * We can now state the real behaviour more precisely:
 *
 *     if (LogicColor.channelId == 3)
 *         ordinary touch-ID ownership checks are bypassed
 *
 * Channel ID 3 is therefore not a transient tracker mode. It is a distinct
 * colour/channel identity whose touch-ownership policy differs from ordinary
 * channels 0..2.
 *
 * Native colour initialisation also associates channel 3 with a neutral/grey
 * presentation, while channel 2 is the green Arc colour used by the special
 * chart discussed later in this file.
 */
bool logicColorAcceptsTouch(
    LogicColor& colour,
    int32_t touchId,
    int32_t nowMs,
    float currentTickIntervalMs)
{
    colour.tickIntervalMs = currentTickIntervalMs;

    // Mechanical release/re-entry lockout still runs before the special channel
    // branch, matching the native ordering established in Section 12.
    if (colour.releaseLockoutStartMs != -1) {
        const int timeout = ownershipWindowMs(colour.tickIntervalMs);

        if (nowMs - colour.releaseLockoutStartMs < timeout) {
            armOwnershipWarning(colour, nowMs);
            return false;
        }

        colour.releaseLockoutStartMs = -1;
    }

    // CONFIRMED special channel rule.
    if (colour.channelId == 3) {
        return true;
    }

    // Ordinary IDs 0..2 continue into the Section 12 ownership state machine:
    // same-ID acceptance, temporary proximity relaxation, assigned-ID rejection,
    // and globally exclusive fresh claim.
    return ordinaryLogicColorOwnershipCheck(colour, touchId, nowMs);
}

/*
 * Important boundary:
 * channel 3 bypasses ordinary finger-ID ownership, NOT Arc geometry.
 * The touch still has to reach the Arc contact hook after ordinary spatial
 * qualification.
 */

// -----------------------------------------------------------------------------
// 4. +0x2C is gameplay lockout; +0x30/+0x34/+0x38 are warning presentation
// -----------------------------------------------------------------------------

/*
 * CONFIRMED refinement of Section 12.
 *
 * The mechanical release/re-acquisition rule is stored at:
 *
 *     LogicColor +0x2C = release-lockout start time
 *
 * A rejected ownership attempt can additionally arm the following state:
 *
 *     +0x30 : active/rejection-feedback latch
 *     +0x38 : feedback start time
 *     +0x34 : feedback remaining time
 *
 * The exact original names are unavailable, but their role is no longer
 * ambiguous because RenderArcNote consumes the remaining-time field.
 */
void armOwnershipWarning(LogicColor& colour, int32_t nowMs)
{
    colour.ownershipWarningActive = 1;

    if (colour.ownershipWarningRemainingMs < 0.0f) {
        colour.ownershipWarningStartMs = nowMs;
    }
}

void updateOwnershipWarning(LogicColor& colour, int32_t nowMs)
{
    if (colour.ownershipWarningActive == 0) {
        colour.ownershipWarningRemainingMs = -1.0f;
        return;
    }

    const int duration = ownershipWindowMs(colour.tickIntervalMs);
    const int elapsed =
        std::max(nowMs - colour.ownershipWarningStartMs, 0);
    const int remaining = duration - elapsed;

    colour.ownershipWarningRemainingMs =
        remaining >= 0
            ? static_cast<float>(remaining)
            : -1.0f;
}

/*
 * CONFIRMED renderer consequence:
 * While the warning countdown is active, judged Arc presentation is blended
 * toward the warning colour:
 *
 *     RGB(230, 50, 50)
 *
 * The exact interpolation curve is presentation detail and is not promoted here
 * unless separately needed.
 *
 * Therefore the clean state separation is:
 *
 *     +0x2C
 *       gameplay: temporary re-acquisition rejection after release
 *
 *     +0x30/+0x34/+0x38
 *       presentation: red ownership/conflict warning after rejection
 *
 * They use the same timeout family but are not the same mechanism.
 */
struct Rgb8 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

static constexpr Rgb8 kOwnershipWarningColour = {230, 50, 50};

// -----------------------------------------------------------------------------
// 5. Arc +0x170 remains an ownership-only bypass
// -----------------------------------------------------------------------------

/*
 * Section 12 proved the contact-hook ordering:
 *
 *     Arc touch geometry
 *         -> if Arc +0x170 == 0:
 *                ask LogicColor to accept/own the touch
 *            else:
 *                skip LogicColor ownership check
 *         -> update ordinary Arc contact state
 *
 * Thus +0x170 does NOT mean auto-hit and does NOT enlarge/bypass the hitbox.
 */
struct LogicArcNoteSelectedFields {
    LogicColor* colour;            // conceptual Arc +0xB0
    bool bypassColourOwnership;    // Arc +0x170, RECONSTRUCTED name
};

bool acceptGeometricallyQualifiedArcTouch(
    LogicArcNoteSelectedFields& arc,
    int32_t touchId,
    int32_t nowMs,
    float tickIntervalMs)
{
    if (!arc.bypassColourOwnership) {
        if (!logicColorAcceptsTouch(
                *arc.colour,
                touchId,
                nowMs,
                tickIntervalMs)) {
            return false;
        }
    }

    return true;
}

// -----------------------------------------------------------------------------
// 6. The external +0x170 context is native LogicChart state
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from RTTI and the virtual initialiser around ~0x173B358.
 *
 * Section 12 observed a context byte around +0x111 but could not identify its
 * owner. It belongs to the LogicChart-side construction context.
 *
 * The relevant branch compares the exact content/song ID:
 *
 *     "yourbestnightmare"
 *
 * and additionally checks an integer at a retained chart-selection object
 * around +0x124.
 *
 * A deeper parser/resource audit resolves that integer below.
 */
struct LogicChartSelectedState {
    bool yourBestNightmareOwnershipRule; // conceptual +0x111
};

// -----------------------------------------------------------------------------
// 7. The +0x124 qualifier is selected ratingClass / difficulty class
// -----------------------------------------------------------------------------

/*
 * CONFIRMED behavioural identity from multiple independent native/data paths.
 * Exact original C++ member name is not recovered.
 *
 * Evidence chain:
 *
 *   1. Native parsers reference the literal key `ratingClass` and parse it as an
 *      integer for difficulty records.
 *
 *   2. The parsed integer is stored in the corresponding difficulty object.
 *
 *   3. LogicChart later reads retained selection-context +0x124 and passes that
 *      integer into the song difficulty-table lookup.
 *
 *   4. The same selected integer participates in chart/resource lookup.
 *
 *   5. The bundled song list contains ratingClass 3 for Your Best Nightmare,
 *      matching the hard-coded `== 3` branch exactly.
 *
 *   6. The same LogicChart initialiser contains another song-specific branch for
 *      `dropdead` which compares the same +0x124 field against 2, reinforcing
 *      that this field is a selected difficulty/rating class rather than an
 *      unrelated YBN-only enum.
 *
 * Therefore `selectedRatingClass` is a safe behavioural name.
 */
struct ChartSelectionContext {
    // Other selection/runtime members omitted.
    int32_t selectedRatingClass; // conceptual +0x124, behavioural identity CONFIRMED
};

bool shouldEnableYourBestNightmareOwnershipRule(
    const char* songId,
    const ChartSelectionContext& selection)
{
    return
        stringEquals(songId, "yourbestnightmare") &&
        selection.selectedRatingClass == 3;
}

/*
 * This corrects the earlier provisional interpretation "song == YBN".
 * The special rule is narrower:
 *
 *     Your Best Nightmare, ratingClass 3 only
 */

// -----------------------------------------------------------------------------
// 8. YBN ratingClass 3 changes LogicColor channel assignment
// -----------------------------------------------------------------------------

/*
 * CONFIRMED in the LogicColor factory/channel-selection path.
 *
 * Under the YBN ratingClass-3 context, chart Arc colours 0 and 1 are folded onto
 * the same ordinary LogicColor ownership channel.
 *
 * Chart colour 2 remains a separate channel and retains its green presentation.
 *
 * Readable model:
 *
 *     ordinary chart:
 *         chart colour -> ordinary corresponding LogicColor channel
 *
 *     YBN ratingClass 3:
 *         chart colour 0 --+
 *                         +--> shared ordinary LogicColor ownership channel
 *         chart colour 1 --+
 *
 *         chart colour 2 ----> separate green LogicColor channel
 */
int32_t deriveYbnLogicColorChannel(
    int32_t chartColourIndex,
    bool ybnRatingClass3)
{
    if (ybnRatingClass3 && chartColourIndex <= 1) {
        return 1; // observed special folding target
    }

    return ordinaryArcColourChannelMapping(chartColourIndex);
}

// -----------------------------------------------------------------------------
// 9. YBN ratingClass 3 also removes colour-2 Arcs from ordinary ownership
// -----------------------------------------------------------------------------

/*
 * CONFIRMED Arc factory condition around ~0x18651D4 and duplicate creation path
 * around ~0x18654E0:
 *
 *     LogicChart special byte != 0
 *     && chartArc.colorIndex == 2
 *         -> LogicArcNote +0x170 = 1
 *
 * Combining that with the now-resolved LogicChart condition gives:
 */
bool shouldBypassArcColourOwnership(
    const char* songId,
    int32_t selectedRatingClass,
    int32_t chartArcColourIndex)
{
    return
        stringEquals(songId, "yourbestnightmare") &&
        selectedRatingClass == 3 &&
        chartArcColourIndex == 2;
}

/*
 * Compact gameplay model:
 *
 *     YOUR BEST NIGHTMARE, ratingClass 3
 *
 *       chart colour 0 ----+
 *                            +--> shared LogicColor ownership channel
 *       chart colour 1 ----+
 *
 *       chart colour 2 --------> separate green LogicColor
 *                                  |
 *                                  +--> Arc +0x170 = 1
 *                                       skip ordinary finger-ID ownership
 *
 * Geometry and normal Arc contact/judgement processing remain active.
 *
 * Thus colour 2 is not an auto-hit colour. It is a colour whose Arcs are exempt
 * from the ordinary LogicColor touch-ID ownership requirement in this specific
 * song/difficulty context.
 */

// -----------------------------------------------------------------------------
// 10. Gameplay vs presentation boundary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED gameplay-affecting state:
 *
 *   - LogicColor assigned touch ID / global claim coordination
 *   - release re-acquisition lockout at +0x2C
 *   - channel-ID-3 ownership bypass
 *   - YBN ratingClass-3 channel folding
 *   - YBN ratingClass-3 colour-2 Arc +0x170 ownership bypass
 *
 * CONFIRMED presentation state:
 *
 *   - LogicColor colour data
 *   - +0x30/+0x34/+0x38 rejection-warning state
 *   - RenderArcNote blending toward RGB(230,50,50) while warning is active
 *
 * Importantly, the red warning is downstream feedback from ownership rejection.
 * It is not what causes the rejection.
 */

// -----------------------------------------------------------------------------
// 11. Final reconstructed flow
// -----------------------------------------------------------------------------

/*
 * RECONSTRUCTED from the confirmed mechanics above.
 */
void buildRuntimeArc(
    RuntimeContext& runtime,
    LogicChart& chart,
    const ChartArc& chartArc)
{
    const bool ybnSpecial =
        chart.songId == "yourbestnightmare" &&
        chart.selection.selectedRatingClass == 3;

    const int32_t logicColorChannel =
        deriveYbnLogicColorChannel(
            chartArc.colorIndex,
            ybnSpecial);

    LogicColor* logicColor =
        getOrCreateLogicColor(
            runtime.logicColors,
            logicColorChannel);

    LogicArcNote* arc = createOrdinaryLogicArc(chartArc);
    arc->colour = logicColor;

    arc->bypassColourOwnership =
        ybnSpecial && chartArc.colorIndex == 2;
}

bool processArcTouch(
    LogicArcNoteSelectedFields& arc,
    const Touch& touch,
    int32_t nowMs)
{
    if (!arcGeometryQualifies(arc, touch)) {
        return false;
    }

    if (!arc.bypassColourOwnership &&
        !logicColorAcceptsTouch(
            *arc.colour,
            touch.id,
            nowMs,
            currentArcTickInterval(arc))) {
        return false;
    }

    return applyOrdinaryArcContact(arc, touch, nowMs);
}

// -----------------------------------------------------------------------------
// 12. What remains unresolved
// -----------------------------------------------------------------------------

/*
 * UNRESOLVED, but not required to understand this mechanic:
 *
 *   - original names of +0x30/+0x34/+0x38
 *   - exact artistic intent behind the red ownership-warning interpolation
 *   - why YBN ratingClass 3 specifically folds colours 0/1 and exempts colour 2
 *   - full SpecialSceneYourBestNightmare visual choreography
 *
 * None of these uncertainties change the recovered gameplay model.
 */

// -----------------------------------------------------------------------------
// 13. Compact findings
// -----------------------------------------------------------------------------

/*
 * CONFIRMED:
 *   - Section 12's Arc tracker is native LogicColor.
 *   - LogicColor objects are reused by colour/channel ID.
 *   - `mode == 3` was actually LogicColor channel ID 3.
 *   - channel ID 3 bypasses ordinary exclusive touch-ID ownership.
 *   - +0x2C is the mechanical release/re-acquisition lockout start.
 *   - +0x30/+0x34/+0x38 drive red rejection-warning presentation.
 *   - RenderArcNote warns toward RGB(230,50,50).
 *   - LogicChart's +0x111 special state is enabled for exact song ID
 *     `yourbestnightmare` when selected ratingClass == 3.
 *   - selected context +0x124 behaves as selected ratingClass/difficulty class.
 *   - under that YBN context, chart colours 0/1 share one LogicColor channel.
 *   - chart colour 2 remains separate and gets Arc +0x170.
 *   - Arc +0x170 skips LogicColor ownership only; geometry still applies.
 *
 * RECONSTRUCTED:
 *   - member names such as ownershipWarningActive and bypassColourOwnership.
 *   - the compact helper/function structure used in this notebook.
 *
 * UNRESOLVED:
 *   - original source identifiers and design motivation for the special rule.
 */
