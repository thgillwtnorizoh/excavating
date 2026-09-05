/*
 * Arcaea excavation notebook
 * Section 09: enwidenlanes and six-lane activation
 *
 * STATUS: enwidenlanes gameplay slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native code/data/control flow.
 *   RECONSTRUCTED = readable representation assembled from confirmed behaviour.
 *   UNRESOLVED    = exact semantic name/purpose is not yet proved.
 *
 * Scope of this section:
 *   - surviving scenecontrol type parser and enum mapping
 *   - exact `enwidenlanes` runtime transition payload
 *   - per-frame lane-widen progress calculation
 *   - relationship between lane-widen progress and touch-to-lane mapping
 *   - activation/restoration of internal outer lane IDs 1 and 6
 *   - proof that floor-note lane centres themselves remain fixed
 *   - the closely related `enwidencamera` transition where needed to explain
 *     the four-lane -> six-lane spatial change
 *
 * Deliberately out of scope:
 *   - full camera matrix/projection reconstruction
 *   - lane/track rendering meshes and textures
 *   - unrelated scenecontrol types and their visual effects
 *   - song-specific special-effect behaviour beyond recording one observed
 *     exception as UNRESOLVED/deferred
 *
 * This file builds directly on:
 *   08_lane_geometry.cpp
 *
 * Useful native anchors from the investigated ARM64 build:
 *   scenecontrol string -> enum parser             ~0x1090060
 *   chart SceneControl -> LogicSceneControl        ~0x1864110
 *   common LogicSceneControl gameplay scheduler    ~0x0F8056C
 *   scene-control gameplay/visual dispatcher       ~0x1514C64
 *   enwidencamera descriptor setter                ~0x0D59C24
 *   gameplay-state transition updater              ~0x10A7550
 *   touch -> floor-lane mapper                      ~0x130733C
 *
 * WARNING ABOUT SYMBOLS:
 * The binary is stripped. Readable names below are reconstruction names unless
 * explicitly described as surviving chart strings, RTTI/type names, or
 * CONFIRMED field behaviour.
 */

#include <algorithm>
#include <cstdint>

// -----------------------------------------------------------------------------
// 1. `enwidenlanes` is a real scenecontrol type
// -----------------------------------------------------------------------------

/*
 * CONFIRMED surviving chart/control strings include:
 *
 *   "trackdisplay"
 *   "redline"
 *   "arcahvdistort"
 *   "arcahvdebris"
 *   "hidegroup"
 *   "enwidencamera"
 *   "enwidenlanes"
 *
 * One native parser maps these exact strings to compact integer values:
 *
 *   trackdisplay   -> 0
 *   redline        -> 1
 *   arcahvdistort  -> 2
 *   arcahvdebris   -> 3
 *   hidegroup      -> 4
 *   enwidencamera  -> 5
 *   enwidenlanes   -> 6
 *   unknown        -> 7
 *
 * Surviving RTTI also includes:
 *   SceneControl
 *   LogicSceneControl
 *   GameSceneVisualControlHandler
 *
 * Therefore `enwidenlanes` is not a song-name special case or renderer-only
 * token. It is a first-class timed SceneControl type understood by the runtime.
 */
enum class SceneControlType : int32_t {
    TrackDisplay   = 0,
    RedLine        = 1,
    ArcHVDistort   = 2,
    ArcHVDebris    = 3,
    HideGroup      = 4,
    EnwidenCamera  = 5,
    EnwidenLanes   = 6,
    Unknown        = 7,
};

// -----------------------------------------------------------------------------
// 2. Chart SceneControl -> LogicSceneControl payload
// -----------------------------------------------------------------------------

/*
 * CONFIRMED selected chart-side SceneControl fields:
 *
 *   +0x18 : timestamp
 *   +0x20 : scenecontrol type string
 *   +0x38 : floating-point parameter
 *   +0x3C : integer parameter
 *
 * The chart-to-runtime conversion:
 *   1. parses the string through the enum routine above
 *   2. allocates a LogicSceneControl
 *   3. supplies timestamp, enum, float parameter, and integer parameter
 *
 * Selected runtime LogicSceneControl fields observed by the dispatcher:
 *
 *   +0x18 : timestamp
 *   +0x64 : SceneControlType enum
 *   +0x68 : float parameter
 *   +0x6C : integer parameter
 *
 * The loader also has explicit accepted paths for enum 5 and enum 6, further
 * confirming both widening controls as intentional chart/runtime features.
 */
struct LogicSceneControl {
    int32_t timeMs;             // conceptual reference to common +0x18
    SceneControlType type;      // +0x64, CONFIRMED
    float floatParameter;       // +0x68, CONFIRMED payload
    int32_t intParameter;       // +0x6C, CONFIRMED payload
};

// -----------------------------------------------------------------------------
// 3. LogicSceneControl fires through the ordinary gameplay scheduler
// -----------------------------------------------------------------------------

/*
 * CONFIRMED:
 * The common gameplay update dynamically identifies LogicSceneControl objects,
 * waits until their chart/gameplay time has been reached, resolves the event,
 * and dispatches it to the scene-control handler.
 *
 * The dispatcher switches directly on LogicSceneControl +0x64. Enum 6 takes a
 * dedicated `enwidenlanes` branch. Therefore the transition begins because a
 * chart-timed gameplay event fires, not because input code notices an outer-lane
 * note or because rendering heuristics infer that widening is needed.
 */

// -----------------------------------------------------------------------------
// 4. Runtime widening state: two independent transition descriptors
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from the enum-5/enum-6 branches and the per-frame state updater.
 *
 * One gameplay state object contains two adjacent timed transition descriptors.
 * Readable names below describe behaviour; original member names are unknown.
 *
 *   enwidencamera:
 *     +0x54 : transition start time
 *     +0x58 : integer target/mode
 *     +0x5C : duration-like float
 *     +0x60 : current camera-width scale
 *     +0x64 : previous camera-width scale copied each update
 *
 *   enwidenlanes:
 *     +0x68 : transition start time
 *     +0x6C : integer target/mode
 *     +0x70 : duration-like float
 *     +0x74 : current lane-widen progress
 *
 * `duration-like` can be promoted to transition duration in gameplay units/ms:
 * the updater divides (effectiveTime - startTime) by the float. Effective time
 * is integer gameplay time measured in the same millisecond-like domain already
 * used by note timestamps.
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

    // Many unrelated gameplay fields omitted.
};

// -----------------------------------------------------------------------------
// 5. `enwidenlanes` event handling is just descriptor installation
// -----------------------------------------------------------------------------

/*
 * CONFIRMED enum-6 dispatcher branch:
 *
 *   state.+0x68 = sceneControl.time
 *   state.+0x6C = sceneControl.intParameter
 *   state.+0x70 = sceneControl.floatParameter
 *
 * No lane centre coordinates are rewritten here.
 * No NotePosition objects are rewritten here.
 * No note list is rebuilt here.
 *
 * The event simply installs a timed target transition; the ordinary gameplay
 * update derives current progress afterward.
 */
void handleEnwidenLanes(
    WideningGameplayState& state,
    const LogicSceneControl& control)
{
    state.lanesStartTimeMs = control.timeMs;
    state.lanesTarget = control.intParameter;
    state.lanesDurationMs = control.floatParameter;
}

// -----------------------------------------------------------------------------
// 6. Exact per-frame `enwidenlanes` transition
// -----------------------------------------------------------------------------

static float clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

/*
 * CONFIRMED native arithmetic around ~0x10A76AC.
 *
 *   elapsed = effectiveGameplayTime - lanesStartTime
 *   t       = elapsed / lanesDuration
 *
 * The integer parameter is tested against 1:
 *
 *   target >= 1 -> widening   : progress = t
 *   target <  1 -> collapsing : progress = 1 - t
 *
 * The result is clamped into [0,1] and stored at +0x74.
 *
 * The common chart convention is naturally represented by target 1 for open and
 * target 0 for closed, but the durable statement is the native comparison:
 * every integer >=1 uses the widening branch; every integer <1 uses collapse.
 */
void updateLaneWidenProgress(
    WideningGameplayState& state,
    int effectiveGameplayTimeMs)
{
    const float t =
        static_cast<float>(
            effectiveGameplayTimeMs - state.lanesStartTimeMs)
        / state.lanesDurationMs;

    float progress;

    if (state.lanesTarget >= 1) {
        progress = t;
    }
    else {
        progress = 1.0f - t;
    }

    state.laneWidenProgress = clamp01(progress);
}

/*
 * Gameplay interpretation:
 *
 *   target >= 1:
 *       0 ---------------------------> 1
 *          transitionDuration
 *
 *   target < 1:
 *       1 ---------------------------> 0
 *          transitionDuration
 *
 * A zero/near-zero duration naturally reaches a clamped endpoint immediately
 * through floating-point division/clamping; no separate special instant branch
 * was required in the observed updater.
 */

// -----------------------------------------------------------------------------
// 7. This +0x74 is exactly the outer-lane gate from Section 08
// -----------------------------------------------------------------------------

/*
 * CONFIRMED by combining the transition updater with the touch-to-lane mapper.
 *
 * Section 08 observed a float at gameplay state +0x74 which determines whether
 * the two outer internal lane IDs can be returned independently. Section 09 now
 * proves where that field comes from: it is the current `enwidenlanes`
 * transition progress calculated above.
 *
 * Primary lane classification uses the fixed gameplay-space boundaries:
 *
 *   -850, -425, 0, +425, +850
 *
 * and behaves structurally as follows (the exact x==425 oddity from Section 08
 * is omitted here because it is unrelated to widening itself):
 */
struct LaneCandidates {
    int primary;
    int adjacent;
};

int classifyPrimaryLaneForWidenState(
    int groundX,
    float laneWidenProgress)
{
    const bool outerLanesActive = laneWidenProgress > 0.0f;

    if (groundX < -850) {
        return outerLanesActive ? 1 : 2;
    }
    if (groundX < -425) {
        return 2;
    }
    if (groundX < 0) {
        return 3;
    }
    if (groundX < 425) {
        return 4;
    }
    if (groundX <= 850) {
        return 5;
    }

    return outerLanesActive ? 6 : 5;
}

/*
 * Critical confirmed consequence:
 * The input system uses a strict positive test, not `progress == 1`.
 *
 * Therefore during OPENING:
 *   as soon as +0x74 becomes >0, the far-left and far-right input regions can
 *   resolve to internal lanes 1 and 6.
 *
 * During CLOSING:
 *   lanes 1 and 6 remain independently selectable while progress remains >0;
 *   only when progress reaches 0 do those regions clamp back to lanes 2 and 5.
 *
 * Thus note/input eligibility changes at the transition endpoints in terms of
 * positive-vs-zero state, while +0x74 still carries continuous 0..1 progress for
 * other consumers such as visual/gameplay transition presentation.
 */

// -----------------------------------------------------------------------------
// 8. Floor lane centres do NOT move during `enwidenlanes`
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from Section 08's NotePosition consumer plus Section 09's handler.
 *
 * Discrete NotePosition lane centres are calculated from the internal lane ID:
 *
 *   x ~= (internalLaneId - 1) * 425 - 1063
 *
 * producing fixed centres approximately:
 *
 *   lane 1 : -1062.5
 *   lane 2 :  -637.5
 *   lane 3 :  -212.5
 *   lane 4 :  +212.5
 *   lane 5 :  +637.5
 *   lane 6 : +1062.5
 *
 * `enwidenlanes` does not modify NotePosition, lane IDs, or this conversion.
 * Its runtime state controls whether the already-existing outer IDs 1 and 6 are
 * treated as independently selectable lanes by input.
 *
 * Therefore the gameplay model is NOT:
 *
 *   four existing lane centres slide sideways and two new centres are created
 *
 * It is:
 *
 *   the engine already has six fixed lane slots;
 *   ordinary play uses internal 2..5;
 *   widening activates the pre-existing outer slots 1 and 6.
 */

// -----------------------------------------------------------------------------
// 9. Notes in chart lanes 0 and 5 fit the widened system directly
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from Section 08:
 *
 * Integer chart NotePosition values map to internal IDs using +1 in ordinary
 * orientation:
 *
 *   chart 0 -> internal 1
 *   chart 1 -> internal 2
 *   chart 2 -> internal 3
 *   chart 3 -> internal 4
 *   chart 4 -> internal 5
 *   chart 5 -> internal 6
 *
 * The note objects themselves can therefore exist in the outer positions even
 * while ordinary four-lane input is active.
 *
 * During `enwidenlanes` progress >0, touches beyond +/-850 map to IDs 1/6 and
 * can match such Tap/Hold NotePositions through the same ordinary candidate
 * comparison established in Section 08.
 *
 * Once collapse reaches progress ==0, those same far-side touches clamp to
 * IDs 2/5 again, so an outer-lane note would no longer receive a matching floor
 * lane candidate through the ordinary mapper.
 */

// -----------------------------------------------------------------------------
// 10. `enwidencamera` is the separate spatial widening sibling
// -----------------------------------------------------------------------------

/*
 * Section 08 observed gameplay-state +0x60 being used when constructing the
 * secondary/adjacent lane candidate near boundaries. It was provisionally
 * described as a horizontal touch-width scale.
 *
 * Section 09 upgrades its source/identity:
 *   +0x60 is the current transition value produced by `enwidencamera`.
 *
 * CONFIRMED enum-5 dispatcher:
 *
 *   state.+0x54 = sceneControl.time
 *   state.+0x58 = sceneControl.intParameter
 *   state.+0x5C = sceneControl.floatParameter
 *
 * CONFIRMED per-frame arithmetic:
 *
 *   elapsed = effectiveTime - cameraStartTime
 *   q       = elapsed / cameraDuration
 *
 *   target >=1:
 *       cameraWidthScale = clamp(1 + 0.5*q, 1, 1.5)
 *
 *   target <1:
 *       cameraWidthScale = clamp(1.5 - 0.5*q, 1, 1.5)
 *
 * The previous +0x60 value is copied to +0x64 before the new value is stored.
 */
static float clampCameraScale(float value)
{
    if (value < 1.0f) return 1.0f;
    if (value > 1.5f) return 1.5f;
    return value;
}

void updateCameraWidenScale(
    WideningGameplayState& state,
    int effectiveGameplayTimeMs)
{
    state.previousCameraWidthScale = state.cameraWidthScale;

    const float q =
        static_cast<float>(
            effectiveGameplayTimeMs - state.cameraStartTimeMs)
        / state.cameraDurationMs;

    float scale;

    if (state.cameraTarget >= 1) {
        scale = 1.0f + 0.5f * q;
    }
    else {
        scale = 1.5f - 0.5f * q;
    }

    state.cameraWidthScale = clampCameraScale(scale);
}

/*
 * Gameplay interpretation:
 *
 * Four lane slots occupy 4 * 425 units of pitch while six occupy 6 * 425.
 * The ratio is exactly:
 *
 *   6 / 4 = 1.5
 *
 * which matches `enwidencamera`'s confirmed 1.0..1.5 transition range.
 *
 * This is powerful architectural evidence for why the game separates the two
 * scene controls:
 *
 *   enwidencamera -> spatial/camera width transition
 *   enwidenlanes  -> lane-availability transition
 *
 * They are closely related but not the same state variable.
 */

// -----------------------------------------------------------------------------
// 11. Camera widening also refines boundary-overlap behaviour
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from the Section 08 lane mapper:
 *
 * When state.cameraWidthScale > 1, the mapper checks extra left/right positions
 * around the transformed touch and may return an adjacent lane candidate.
 * The half-width term is:
 *
 *   halfExtra = (cameraWidthScale - 1) * 425 * 0.5
 *
 * At full camera widening (1.5):
 *
 *   halfExtra = 0.5 * 425 * 0.5 = 106.25 gameplay units
 *
 * Therefore Section 08's behavioural observation was correct, but its source is
 * now stronger: this width comes from the live `enwidencamera` transition,
 * rather than an unidentified generic touch-size setting.
 */
float adjacentLaneHalfExtra(float cameraWidthScale)
{
    if (cameraWidthScale <= 1.0f) {
        return 0.0f;
    }

    return (cameraWidthScale - 1.0f) * 425.0f * 0.5f;
}

// -----------------------------------------------------------------------------
// 12. Combined readable widened-lane state machine
// -----------------------------------------------------------------------------

/*
 * RECONSTRUCTED from the confirmed pieces above.
 *
 * A chart can drive camera width and lane availability independently. A normal
 * six-lane widening presentation is naturally represented by corresponding
 * enum-5 and enum-6 events, but this section does not require that every chart
 * always supplies them as an inseparable pair.
 */
void gameplayUpdateWidening(
    WideningGameplayState& state,
    int effectiveGameplayTimeMs)
{
    updateCameraWidenScale(state, effectiveGameplayTimeMs);
    updateLaneWidenProgress(state, effectiveGameplayTimeMs);
}

/*
 * Compact gameplay model:
 *
 *   NORMAL
 *
 *       internal lanes:    1 [ 2 3 4 5 ] 6
 *       selectable:          [ X X X X ]
 *       lane progress:             0
 *       camera scale:              1.0
 *
 *                         scenecontrol events
 *                                |
 *                                v
 *   OPENING
 *
 *       enwidencamera:  1.0 ------------> 1.5
 *       enwidenlanes:   0.0 ------------> 1.0
 *
 *       once lane progress >0:
 *           IDs 1 and 6 are independently selectable
 *
 *                                |
 *                                v
 *   WIDE
 *
 *       internal lanes:      [ 1 2 3 4 5 6 ]
 *       lane progress:              1
 *       camera scale:               1.5
 *
 *                                |
 *                                v
 *   CLOSING
 *
 *       enwidencamera:  1.5 ------------> 1.0
 *       enwidenlanes:   1.0 ------------> 0.0
 *
 *       IDs 1/6 remain selectable while lane progress >0
 *
 *                                |
 *                                v
 *   NORMAL again
 *
 *       lane progress ==0 -> far-side input clamps to IDs 2/5
 */

// -----------------------------------------------------------------------------
// 13. Timing source: transition update uses effective gameplay time
// -----------------------------------------------------------------------------

/*
 * CONFIRMED placement in the per-frame update:
 * Both widening transitions use an effective gameplay-time value after the same
 * timing-related correction machinery used elsewhere in note processing.
 *
 * The nearby code again exposes the previously observed conditional -3000 ms
 * adjustment branch. Its exact semantic meaning remains UNRESOLVED and is not
 * required to understand widening arithmetic.
 *
 * This is intentionally left as the bridge into the next timing/timinggroup
 * excavation rather than expanding Section 09 sideways.
 */

// -----------------------------------------------------------------------------
// 14. One observed song-specific exception is deferred
// -----------------------------------------------------------------------------

/*
 * CONFIRMED but deliberately NOT excavated here:
 * The widening update contains a branch checking the exact song identifier
 * "rivenpilgrim". Under a collapse-related condition while lane-widen progress
 * has not reached the expected endpoint, it forces the lane transition duration
 * field to 500.0 before continuing.
 *
 * This is evidence that at least one special-content path can intervene in the
 * otherwise generic widening transition.
 *
 * UNRESOLVED in this section:
 *   - why this exception exists
 *   - the higher-level gameplay/anomaly effect it supports
 *   - whether another helper result in the branch is meaningful to presentation
 *
 * It belongs to the later special-gameplay-effects excavation. The generic
 * `enwidenlanes` state machine above does not depend on resolving it.
 */

// -----------------------------------------------------------------------------
// 15. CONFIRMED / RECONSTRUCTED / UNRESOLVED summary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED
 * ---------
 * - `enwidenlanes` survives as a scenecontrol string and maps to enum value 6.
 * - `enwidencamera` maps to enum value 5.
 * - Both are converted into timed LogicSceneControl runtime objects.
 * - LogicSceneControl carries timestamp, type, float parameter, and int parameter.
 * - Enum 6 installs lane transition start/target/duration at +0x68/+0x6C/+0x70.
 * - Per-frame lane progress at +0x74 is:
 *       target>=1: clamp((now-start)/duration, 0, 1)
 *       target< 1: clamp(1-(now-start)/duration, 0, 1)
 * - The floor-lane mapper tests this exact progress against zero.
 * - progress>0 enables independent outer lane IDs 1 and 6.
 * - progress==0 clamps far-left/far-right input back to lanes 2 and 5.
 * - Floor NotePosition lane centres remain the fixed six-slot geometry proved in
 *   Section 08; `enwidenlanes` does not move/rewrite those centres.
 * - Chart lane values 0 and 5 already map to internal outer IDs 1 and 6.
 * - Enum 5 installs an independent camera transition descriptor at +0x54..+0x5C.
 * - Camera width scale +0x60 transitions between 1.0 and 1.5.
 * - The lane mapper uses that camera scale when constructing adjacent-lane input
 *   overlap, with `(scale-1)*425*0.5` half-extra width.
 *
 * RECONSTRUCTED
 * -------------
 * - Member names such as laneWidenProgress, cameraWidthScale, lanesTarget,
 *   cameraTarget, and WideningGameplayState are documentation names.
 * - Calling int parameter 0/1 "closed/open" is readable semantics; native code
 *   itself only proves the comparisons `<1` and `>=1`.
 * - A normal presentation likely coordinates enum5 and enum6, but they remain
 *   independently controllable native scene-control states.
 *
 * UNRESOLVED / DEFERRED
 * ---------------------
 * - full camera projection/matrix changes during enwidencamera
 * - visual lane/track mesh animation driven by the continuous progress values
 * - exact reason for the song-specific `rivenpilgrim` 500 ms exception
 * - the already-known exact x==425 primary-lane classifier oddity
 * - semantic meaning of the nearby conditional -3000 ms effective-time branch
 */

// -----------------------------------------------------------------------------
// 16. Next narrow excavation target
// -----------------------------------------------------------------------------

/*
 * With note families, lane geometry, and widened-lane activation established,
 * the next useful slice is gameplay timing/timinggroups only as far as they
 * affect effective note time and spatial movement:
 *
 *   - identify the timing-state objects supplying effective gameplay time
 *   - separate judgement time from visual/scroll time
 *   - resolve the recurring conditional +/-3000 ms correction
 *   - identify the minimum timinggroup flags that materially alter gameplay
 *   - stop before attempting to map the entire AFF parser or every visual flag
 */
