/*
 * Arcaea excavation notebook
 * Section 17: song-specific in-play special branches
 *
 * STATUS: focused Riven Pilgrimage / Designant special-scene slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native code/data/control flow.
 *   RECONSTRUCTED = readable representation assembled from confirmed behaviour.
 *   UNRESOLVED    = exact semantic name/purpose is not yet proved.
 *
 * Scope of this section:
 *   - the hard-coded `rivenpilgrim` intervention in lane-widen collapse
 *   - exact condition and 500 ms duration override used by that branch
 *   - the native SpecialSceneDesignantChallenge runtime class
 *   - song/content-ID selection of that Designant special-scene object
 *   - its use of the shared effective gameplay clock
 *   - the paired DESIGNANT Arc-body / ArcTap presentation factors
 *   - the DESIGNANT-parent ArcTap event which arms their 500 ms transition
 *   - separation of actual gameplay-state mutation from presentation-only state
 *
 * Deliberately out of scope:
 *   - every historical SpecialScene* class in the binary
 *   - unlock/progression/challenge-requirement logic surrounding special songs
 *   - exact artistic details of the Designant video/shader timeline
 *   - exact semantic name of every SpecialSceneDesignantChallenge member
 *   - assuming that native Tokens::DESIGNANT and song-ID `designant` are one
 *     mechanism; they remain distinct layers which happen to interact here
 *
 * This file builds directly on:
 *   06_arctaps.cpp
 *   09_enwidenlanes.cpp
 *   10_timinggroups.cpp
 *   14_arc_mode_designant.cpp
 *   15_rendering_fundamentals.cpp
 *   16_scenecontrols.cpp
 *
 * Useful native anchors from the investigated ARM64 build:
 *   widening/spatial updater + rivenpilgrim check        ~0x10A7550 / 0x10A7664
 *   nearby fifth-power helper                            ~0x0A4517C
 *   special-scene song/content selector                  ~0x1887128
 *   selector `designant` comparison                      ~0x1887910
 *   SpecialSceneDesignantChallenge factory               ~0x1819588
 *   Designant special per-frame virtual                  ~0x10E5124
 *   Designant DESIGNANT-ArcTap callback                  ~0x0D526FC
 *   Designant ArcTap presentation-factor setter          ~0x18C7A40
 *   Designant Arc-body presentation-factor setter        ~0x086D018
 *   generic gameplay-scene factor reset                  ~0x0E2EAC8
 *
 * WARNING ABOUT SYMBOLS:
 * The binary is stripped. `SpecialSceneDesignantChallenge` and
 * `SpecialSceneModifier` survive through RTTI/type-name evidence. Readable
 * helper/member names below are reconstruction names unless explicitly stated.
 */

#include <algorithm>
#include <cstdint>
#include <string>

// -----------------------------------------------------------------------------
// 1. Riven Pilgrimage has a direct exception inside ordinary lane-widen updating
// -----------------------------------------------------------------------------

/*
 * Section 09 established the generic lane-widen state:
 *
 *   +0x68 : transition start time
 *   +0x6C : integer target
 *   +0x70 : transition duration
 *   +0x74 : current lane-widen progress
 *
 * and the generic collapse arithmetic:
 *
 *   t = (effectiveGameplayTime - startTime) / duration
 *   target < 1 -> progress = clamp(1 - t, 0, 1)
 *
 * CONFIRMED inside the same updater:
 * The current song/content identifier is compared against the exact native
 * literal `rivenpilgrim`.
 *
 * When all three conditions hold:
 *
 *   songId == "rivenpilgrim"
 *   laneTarget <= 0
 *   currentLaneWidenProgress < 1.0
 *
 * native code overwrites the lane transition duration with exactly 500.0.
 *
 * It does NOT rewrite the transition start timestamp or target in this branch.
 */
struct WideningGameplayState {
    int32_t cameraStartTimeMs;       // +0x54
    int32_t cameraTarget;            // +0x58
    float cameraDurationMs;          // +0x5C
    float cameraWidthScale;          // +0x60
    float previousCameraWidthScale;  // +0x64

    int32_t lanesStartTimeMs;        // +0x68
    int32_t lanesTarget;             // +0x6C
    float lanesDurationMs;           // +0x70
    float laneWidenProgress;         // +0x74
};

void applyRivenPilgrimLaneCollapseException(
    const std::string& songId,
    WideningGameplayState& state)
{
    if (songId == "rivenpilgrim" &&
        state.lanesTarget <= 0 &&
        state.laneWidenProgress < 1.0f) {
        state.lanesDurationMs = 500.0f;
    }
}

/*
 * The generic updater then immediately continues using the ordinary formula.
 * Therefore the durable behavioural statement is:
 *
 *   Riven Pilgrimage can force a collapse-related `enwidenlanes` transition to
 *   use a 500 ms duration when the collapse target is non-positive and the
 *   previous lane-widen progress is still below the fully-wide endpoint.
 *
 * Because +0x74 directly gates whether outer lanes 1 and 6 can be independently
 * selected (Section 09), this is a genuine gameplay/input-state intervention,
 * not merely a track-render animation exception.
 */

// -----------------------------------------------------------------------------
// 2. The nearby fifth-power helper does not provide a second Riven state update
// -----------------------------------------------------------------------------

/*
 * CONFIRMED native helper around ~0x0A4517C:
 *
 *   helper(x) = 1 + (x - 1)^5
 *
 * The Riven branch calls it with:
 *
 *   x = 1 - currentLaneWidenProgress
 *
 * which algebraically produces:
 *
 *   1 - progress^5
 *
 * However, in this exact call path the floating-point return value is immediately
 * overwritten by the subsequent generic widening calculation. The helper has no
 * observed side effects.
 *
 * Therefore this section does NOT invent a Riven easing curve from that call.
 * The confirmed durable effect is the 500.0 duration write above.
 */
static float fifthPowerHelper(float x)
{
    const float y = x - 1.0f;
    return 1.0f + y*y*y*y*y;
}

// -----------------------------------------------------------------------------
// 3. Why Riven needs the exception remains unresolved
// -----------------------------------------------------------------------------

/*
 * CONFIRMED:
 *   - the branch is specific to exact ID `rivenpilgrim`
 *   - it applies only to the lane-widen transition descriptor
 *   - this branch does not overwrite the adjacent enwidencamera duration field
 *   - it is conditional on collapse target <=0 and progress <1
 *
 * RECONSTRUCTED plausible interpretation:
 * The condition is compatible with a special handling case for collapse while
 * widening has not reached the ordinary fully-wide endpoint.
 *
 * UNRESOLVED:
 * The original design reason for choosing this condition and exactly 500 ms is
 * not proved by the code traced here. Do not turn the plausible interrupted-
 * widening explanation into a confirmed design statement.
 */

// -----------------------------------------------------------------------------
// 4. `designant` song/content ID selects a native Designant special-scene class
// -----------------------------------------------------------------------------

/*
 * CONFIRMED surviving RTTI/type-name evidence:
 *
 *   SpecialSceneModifier
 *      |
 *      +-- SpecialSceneDesignantChallenge
 *
 * The selector around ~0x1887128 repeatedly compares one string field against
 * hard-coded song/content identifiers. Nearby cases include exact IDs such as
 * `axiumcrisis`, `antagonism`, `dantalion`, `equilibrium`, `etherstrike` and
 * `lamentrain`.
 *
 * The exact literal `designant` is tested around ~0x1887910. On equality the
 * selector calls the factory around ~0x1819588 and stores the resulting special-
 * scene pointer.
 *
 * Therefore this is a song/content-ID selection path, distinct from the chart
 * lexer token Tokens::DESIGNANT established in Section 14.
 */
struct SpecialSceneModifier;
struct SpecialSceneDesignantChallenge : SpecialSceneModifier;

SpecialSceneModifier* createSpecialSceneForSong(
    const std::string& currentSongId,
    GameSceneContext& context)
{
    // Many unrelated song-specific cases deliberately omitted.

    if (currentSongId == "designant") {
        return createSpecialSceneDesignantChallenge(context);
    }

    return createOtherOrDefaultSpecialScene(context);
}

/*
 * CONFIRMED factory shape around ~0x1819588:
 *   - allocation size is 0x380 bytes in this build
 *   - Designant-specific state is zero-initialised
 *   - the vtable/type identity is SpecialSceneDesignantChallenge
 *   - fields around +0x2F8 and +0x300 onward participate in its runtime timeline
 *
 * Original source member names are unavailable.
 */

// -----------------------------------------------------------------------------
// 5. Designant's special-scene update uses the SAME effective gameplay clock
// -----------------------------------------------------------------------------

/*
 * CONFIRMED through the SpecialSceneDesignantChallenge vtable and sibling
 * SpecialScene* RTTI/lambda names:
 * A late virtual slot for the Designant class points to ~0x10E5124. The same slot
 * in sibling special-scene classes is associated with surviving
 * `performSpecialSongUpdate(GameScene*)` names.
 *
 * Calling the Designant function a `performSpecialSongUpdate` equivalent is
 * therefore RECONSTRUCTED naming around a CONFIRMED class-owned per-frame path.
 *
 * The function directly reproduces the global clock arithmetic from Section 10:
 *
 *   synchronized path:
 *       now = source20 - commonOffset
 *
 *   fallback path:
 *       now = fallbackSource - commonOffset
 *       if (fallbackSource <= 0)
 *           now -= 3000
 *
 * The same update also calls the already-established common effective-time
 * helper elsewhere in the routine.
 *
 * Thus Designant's special timeline is synchronised to ordinary gameplay time,
 * including the fallback -3000 ms pre-chart path. It is not driven by an
 * independent wall-clock-only animation timer.
 */
int32_t effectiveGameplayTime(const GameplayClock& clock); // Section 10

void updateDesignantSpecialScene(
    SpecialSceneDesignantChallenge& special,
    GameScene& scene)
{
    const int32_t nowMs =
        effectiveGameplayTime(scene.gameplayClock());

    updateDesignantVideoTimeline(special, scene, nowMs);
    updateDesignantPresentationTimeline(special, scene, nowMs);
}

// -----------------------------------------------------------------------------
// 6. A dedicated video/timeline object is synchronised from gameplay time
// -----------------------------------------------------------------------------

/*
 * CONFIRMED in the Designant per-frame update:
 *
 *   - a video-like runtime object is stored around Designant state +0x300
 *   - effective gameplay milliseconds are converted to seconds
 *   - a +10.0 second offset participates in comparison/seek/playback handling
 *   - the object is queried and controlled through virtual play/time/seek-like
 *     operations
 *   - surviving event/string names used in this path include:
 *
 *       "VideoStart"
 *       "PriorVideoStart"
 *
 *   - later presentation code writes a surviving shader/uniform name:
 *
 *       "u_progress"
 *
 * Generic video-loading code in the same build also contains exact resources:
 *
 *       video_720.mp4
 *       video_1080.mp4
 *
 * The exact original class name of the +0x300 object and the semantic meaning of
 * every VideoStart/PriorVideoStart event are not proved strongly enough here.
 *
 * Durable conclusion:
 * SpecialSceneDesignantChallenge owns a gameplay-clock-synchronised video/
 * material presentation timeline. Exact cinematographic choreography is outside
 * this focused gameplay-special slice.
 */

// -----------------------------------------------------------------------------
// 7. DESIGNANT Arc body and ArcTap presentation use TWO global factors
// -----------------------------------------------------------------------------

/*
 * Section 14 established a global float at approximately:
 *
 *   ~0x1BABEE0
 *
 * which multiplies presentation/alpha for runtime Arc body mode 2 (DESIGNANT).
 * Its setter is around ~0x086D018.
 *
 * Section 17 identifies a second global float at approximately:
 *
 *   ~0x1BE40050
 *
 * with setter around ~0x18C7A40.
 *
 * CONFIRMED RenderArcTapNote use:
 * The ArcTap renderer reads this second global and selects it only when the
 * supplied parent-Arc presentation mode is 2. Other modes use factor 1.0.
 *
 * Therefore the two globals can safely be distinguished behaviourally as:
 *
 *   DESIGNANT Arc-body presentation factor
 *   DESIGNANT ArcTap presentation factor
 *
 * These are RECONSTRUCTED descriptive names, not recovered source identifiers.
 */
float gDesignantArcBodyPresentationFactor; // behaviourally identified
float gDesignantArcTapPresentationFactor;  // behaviourally identified

float arcTapPresentationFactorForMode(int parentArcBodyMode)
{
    if (parentArcBodyMode == 2) {
        return gDesignantArcTapPresentationFactor;
    }

    return 1.0f;
}

// -----------------------------------------------------------------------------
// 8. Designant normally drives the two DESIGNANT factors together
// -----------------------------------------------------------------------------

/*
 * CONFIRMED in the per-frame Designant special update around ~0x10E58A4.
 *
 * One byte around SpecialSceneDesignantChallenge +0x329 arms a transition. Its
 * timestamp is stored at +0x32C.
 *
 * While unarmed:
 *
 *   transitionFactor = 1
 *
 * Once armed:
 *
 *   elapsed = now - transitionStart
 *   elapsed is first constrained through a native 0..800 interval
 *
 *   transitionFactor = clamp(1 - elapsed/500, 0, 1)
 *
 * Final value supplied to BOTH global setters:
 *
 *   designantRenderFactor =
 *       0.3 + 0.7 * transitionFactor
 *
 * Therefore the effective visible multiplier is:
 *
 *   at transition start : 1.0
 *   after 500 ms         : 0.3
 *   later                : 0.3
 *
 * This particular transition does not fade DESIGNANT bodies/ArcTaps all the way
 * to zero; its floor is exactly 30 percent of the ordinary presentation value.
 */
static float clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

float designantRenderFactorForTransition(
    bool transitionArmed,
    int32_t nowMs,
    int32_t transitionStartMs)
{
    float transitionFactor = 1.0f;

    if (transitionArmed) {
        const float elapsed = static_cast<float>(
            std::clamp(nowMs - transitionStartMs, 0, 800));

        transitionFactor =
            clamp01(1.0f - elapsed / 500.0f);
    }

    return 0.3f + 0.7f * transitionFactor;
}

void applyDesignantRenderFactors(float factor)
{
    setDesignantArcTapPresentationFactor(factor); // ~0x18C7A40
    setDesignantArcBodyPresentationFactor(factor); // ~0x086D018
}

// -----------------------------------------------------------------------------
// 9. A DESIGNANT-parent ArcTap event arms that 500 ms transition
// -----------------------------------------------------------------------------

/*
 * CONFIRMED Designant-class virtual callback around ~0x0D526FC:
 *
 *   1. reject null argument
 *   2. dynamically identify the argument as LogicArcTapNote
 *   3. follow ArcTap +0x90 to its parent-Arc context
 *   4. require parent Arc +0xA4 == 2
 *
 * Thus this callback explicitly reacts only to ArcTaps whose parent is runtime
 * DESIGNANT body mode 2.
 *
 * The callback maintains Designant-specific vectors/counters around +0x338 and
 * +0x368. When its relevant sequence reaches the observed boundary and the
 * transition has not already been armed, native code performs:
 *
 *   special.+0x329 = 1
 *   special.+0x32C = arcTap.+0x18
 *
 * ArcTap +0x18 is the ordinary point-note/chart timestamp established in the
 * earlier note/ArcTap sections.
 *
 * Therefore the 500 ms fade described above is synchronised from a DESIGNANT
 * ArcTap's own chart timestamp.
 *
 * IMPORTANT LIMIT:
 * The exact high-level name/contract of this SpecialSceneModifier virtual callback
 * is not recovered. This section does NOT claim it is specifically a "successful
 * judgement callback" without stronger evidence. It is safely described as a
 * LogicNote/ArcTap event callback whose argument is dynamically checked here.
 */
struct DesignantSelectedState {
    bool transitionArmed;          // conceptual +0x329
    int32_t transitionStartMs;     // +0x32C
};

void onDesignantRelevantNoteEvent(
    SpecialSceneDesignantChallenge& special,
    LogicNote* note)
{
    auto* tap = dynamicCast<LogicArcTapNote>(note);
    if (!tap || !tap->parentArc) {
        return;
    }

    if (tap->parentArc->bodyMode != ArcBodyMode::Designant) {
        return;
    }

    updateDesignantArcTapSequenceState(special, *tap);

    if (sequenceReachedTransitionBoundary(special) &&
        !special.transitionArmed) {
        special.transitionArmed = true;
        special.transitionStartMs = tap->startTimeMs;
    }
}

/*
 * Function names and the simple boundary predicate above are RECONSTRUCTED.
 * The native vector/counter traversal which decides the boundary is confirmed,
 * but its original semantic labels are not available.
 */

// -----------------------------------------------------------------------------
// 10. Reset/revert boundary: global factors are reset before special-scene select
// -----------------------------------------------------------------------------

/*
 * CONFIRMED generic gameplay-scene setup around ~0x0E2EAC8:
 * Before calling the special-scene song/content selector, the engine writes:
 *
 *   DESIGNANT ArcTap factor = 1.0
 *   DESIGNANT Arc-body factor = 1.0
 *
 * and then constructs/selects the appropriate SpecialSceneModifier.
 *
 * Within the focused Designant update traced here, once +0x329 is armed the
 * transition remains at its 0.3 endpoint; no in-song reset of +0x329 was proved
 * in this slice.
 *
 * Therefore the safe lifecycle statement is:
 *
 *   new gameplay scene -> both global factors reset to 1
 *   Designant ArcTap boundary -> timestamped transition becomes armed
 *   next 500 ms -> both factors move 1 -> 0.3
 *   afterward -> this transition remains at 0.3
 *   later/new gameplay setup -> globals are reset to 1 before modifier selection
 */
void resetDesignantPresentationForNewGameplayScene()
{
    setDesignantArcTapPresentationFactor(1.0f);
    setDesignantArcBodyPresentationFactor(1.0f);
}

// -----------------------------------------------------------------------------
// 11. Do not merge song-ID `designant` with Tokens::DESIGNANT
// -----------------------------------------------------------------------------

/*
 * CONFIRMED distinct layers:
 *
 * A. chart Arc arctype syntax (Section 14)
 *
 *      false      -> mode 0
 *      true       -> mode 1
 *      designant  -> mode 2
 *
 * B. song/content selection (this section)
 *
 *      currentSongId == "designant"
 *          -> SpecialSceneDesignantChallenge
 *
 * C. interaction between them
 *
 *      SpecialSceneDesignantChallenge ArcTap callback
 *          -> requires parent Arc runtime mode 2
 *          -> drives presentation factors used by DESIGNANT body/ArcTap renderers
 *
 * Therefore the song-specific class deliberately interacts with the chart mode,
 * but the existence of one does not prove universal dependence on the other.
 * The global mode-2 factor setters are also called by other special-content paths
 * elsewhere in the binary, so they are not exclusive globals owned only by the
 * song named `designant`.
 */

// -----------------------------------------------------------------------------
// 12. Gameplay versus presentation consequences
// -----------------------------------------------------------------------------

/*
 * RIVEN PILGRIMAGE
 * ----------------
 * CONFIRMED gameplay/input consequence:
 *   changing lanesDurationMs alters +0x74 laneWidenProgress over gameplay time;
 *   +0x74 controls whether outer floor lanes 1/6 are independently selectable.
 *
 * Therefore the rivenpilgrim branch changes gameplay-space input state timing.
 *
 * DESIGNANT SPECIAL SCENE
 * -----------------------
 * The mechanisms traced in this focused section are presentation-side:
 *   - video/timeline synchronisation
 *   - shader/material progress
 *   - DESIGNANT Arc-body render factor
 *   - DESIGNANT ArcTap render factor
 *
 * The ArcTap note event is used as a trigger, but no Tap timing window, ArcTap
 * judgement result, Arc body contact geometry, lane mapping or ScoreState path is
 * modified by the paired factor transition reconstructed here.
 *
 * SpecialSceneDesignantChallenge may contain additional challenge/result state
 * outside this focused slice. This file deliberately does not drag progression
 * or challenge-requirement systems into the gameplay/render conclusion above.
 */

// -----------------------------------------------------------------------------
// 13. Compact readable model
// -----------------------------------------------------------------------------

/*
 * Riven Pilgrimage:
 *
 *   ordinary widening state
 *          |
 *          +-- song == rivenpilgrim
 *          +-- collapse target <= 0
 *          +-- previous progress < 1
 *                    |
 *                    v
 *          lanes duration = 500 ms
 *                    |
 *                    v
 *          generic +0x74 collapse update
 *                    |
 *                    v
 *          outer-lane input timing changes
 *
 *
 * Designant:
 *
 *   current song/content ID == designant
 *                    |
 *                    v
 *      SpecialSceneDesignantChallenge
 *                    |
 *          shared effective gameplay clock
 *                    |
 *          +---------+----------+
 *          |                    |
 *          v                    v
 *      video/material     LogicArcTapNote event
 *        timeline                |
 *                                v
 *                     parent Arc mode == 2
 *                                |
 *                                v
 *                     store ArcTap timestamp
 *                     arm transition +0x329
 *                                |
 *                                v
 *                         500 ms ramp
 *                                |
 *                         1.0 ------> 0.3
 *                           /          \
 *                          v            v
 *                    DESIGNANT      DESIGNANT
 *                    Arc bodies       ArcTaps
 */

// -----------------------------------------------------------------------------
// 14. CONFIRMED / RECONSTRUCTED / UNRESOLVED summary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED
 * ---------
 * - the widening updater contains an exact `rivenpilgrim` comparison
 * - under laneTarget<=0 and progress<1 it overwrites lane duration with 500.0
 * - the subsequent generic widening formula consumes that overwritten duration
 * - the adjacent fifth-power helper's return is discarded in this call path
 * - this Riven branch changes timing of a gameplay/input-affecting +0x74 state
 * - native RTTI includes SpecialSceneModifier and SpecialSceneDesignantChallenge
 * - a song/content selector tests exact ID `designant` and constructs that class
 * - the Designant special update uses the shared effective gameplay clock,
 *   including the fallback conditional -3000 ms branch
 * - its timeline controls a video-like object and uses VideoStart,
 *   PriorVideoStart and u_progress presentation strings
 * - there are separate global mode-2 factors for Arc bodies and ArcTaps
 * - RenderArcTapNote uses the new second global specifically for parent mode 2
 * - the Designant update writes the same 0.3+0.7*f value to both mode-2 factors
 * - once armed, f falls from 1 to 0 over 500 ms, so both factors fall 1 -> 0.3
 * - a Designant-class note callback dynamically identifies LogicArcTapNote,
 *   requires parent Arc +0xA4==2 and stores the ArcTap +0x18 timestamp at +0x32C
 *   when arming the transition byte +0x329
 * - generic gameplay-scene setup resets both global factors to 1.0 before
 *   selecting/constructing the special-scene modifier for the next scene
 *
 * RECONSTRUCTED
 * -------------
 * - readable names such as currentSongId, lanesDurationMs,
 *   gDesignantArcBodyPresentationFactor and gDesignantArcTapPresentationFactor
 * - describing the vtable slot at ~0x10E5124 as the Designant equivalent of
 *   performSpecialSongUpdate(GameScene*), based on class ownership and sibling
 *   surviving method/lambda names
 * - description of the Riven condition as compatible with interrupted widening
 * - compact pseudocode for the Designant sequence-boundary predicate
 *
 * UNRESOLVED
 * ----------
 * - original design reason for Riven's exact progress<1 / 500 ms exception
 * - why the side-effect-free fifth-power helper call remains in that native path
 * - original member names of SpecialSceneDesignantChallenge state fields
 * - exact original contract/name of the LogicNote/ArcTap special-scene callback
 * - exact meaning of every Designant timeline vector/counter
 * - exact semantic purpose of VideoStart/PriorVideoStart and every u_progress
 *   phase, beyond their confirmed video/material presentation use
 * - additional special-song challenge/progression behaviour outside this focused
 *   in-play gameplay/render slice
 */
