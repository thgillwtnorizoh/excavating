/*
 * Arcaea excavation notebook
 * Section 20: special gameplay-space / camera-input compatibility
 *
 * STATUS: special gameplay-space compatibility slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native control flow, constants,
 *                   RTTI/type names, strings, or chart/content data.
 *   RECONSTRUCTED = readable structure assembled from confirmed behaviour;
 *                   names may differ from original source.
 *   UNRESOLVED    = behaviour is visible but exact original semantic name or
 *                   higher-level design purpose is not proved.
 *
 * Scope:
 *   - resolve the Section 11 "special camera/input mode" as LogicChart +0x110
 *   - identify it as a chart-construction capability rather than camera state
 *   - separate that capability from actual CameraController animation
 *   - resolve the ordinary versus special floor-depth gates
 *   - resolve the exact roles of -500, +500, -1000, and camera-depth 0/900
 *   - resolve the special ArcTap vertical-input branch
 *   - bound surviving Flick and auxiliary spatial consumers
 *
 * Deliberately out of scope:
 *   - full CameraController animation mathematics
 *   - artistic meaning of every chart placed on the +0x110 allowlist
 *   - mapping every legacy 32-character allowlist identifier to a song/chart
 *   - renderer polish that does not affect input interpretation
 *
 * This file builds directly on:
 *   08_lane_geometry.cpp
 *   09_enwidenlanes.cpp
 *   10_timinggroups.cpp
 *   11_gameplay_space.cpp
 *   18_flick_runtime_disconnection.cpp
 *   19_logiccolor_arc_tracking.cpp
 *
 * Investigated binary:
 *   libcocos2dcpp.so SHA-256
 *   3eaca4e6dabb3395f276f8915698d57675757d0df0970e716b23a3dc201c79be
 *
 * Useful native anchors from this build:
 *   LogicChart +0x110 getter                         ~0x16A3A6C
 *   LogicChart initialiser / allowlist               ~0x173B358
 *   common touch-begin projection path               ~0x0EEC024
 *   special touch projection branch                  ~0x0EEC1F0
 *   floor touch -> lane mapper                       ~0x130733C
 *   Arc / ArcTap sky hit helper                      ~0x0927384
 *   auxiliary camera-derived spatial updater         ~0x10FD1C0
 *   surviving Flick special-space consumer           ~0x1485AB8
 *
 * WARNING ABOUT NAMES:
 * The binary gives us native RTTI/type-name evidence for LogicChart and
 * CameraController, but not the original member name of LogicChart +0x110.
 * `specialGameplaySpace` below is therefore a RECONSTRUCTED behavioural name.
 */

#include <cstdint>

struct Vec2 {
    float x;
    float y;
};

struct Vec3 {
    float x;
    float y;
    float z;
};

struct Ray {
    Vec3 origin;
    Vec3 direction;
};

// -----------------------------------------------------------------------------
// 1. The old "special camera/input mode" is LogicChart +0x110
// -----------------------------------------------------------------------------

/*
 * CONFIRMED.
 *
 * The predicate repeatedly seen by floor, sky and surviving Flick input code is
 * a trivial getter:
 *
 *     ldrb w0, [x0, #0x110]
 *     ret
 *
 * Its input object is the same native LogicChart whose neighbouring +0x111 was
 * resolved in Section 19 as the separate Your Best Nightmare ratingClass-3
 * capability.
 *
 * Therefore:
 *
 *     LogicChart +0x110 = one chart-level boolean capability
 *     LogicChart +0x111 = separate YBN green-Arc capability
 *
 * They must not be conflated.
 */
struct LogicChartSelectedFlags {
    // Earlier members omitted.

    bool specialGameplaySpace;  // conceptual +0x110, RECONSTRUCTED name
    bool ybnGreenArcContext;     // conceptual +0x111, Section 19
};

bool usesSpecialGameplaySpace(const LogicChartSelectedFlags& chart)
{
    return chart.specialGameplaySpace;
}

// -----------------------------------------------------------------------------
// 2. +0x110 is fixed during LogicChart construction, not animated per frame
// -----------------------------------------------------------------------------

/*
 * CONFIRMED in the LogicChart initialiser.
 *
 * +0x110 is enabled by a hard-coded compatibility/allowlist path. Observed exact
 * song/content IDs include:
 *
 *     "lfdyrmx"
 *     "unknownrmx"
 *     "cataclysmcry"
 *     "deinosphainein"
 *     "ember"
 *     "sacrosanct"
 *
 * The same construction path also recognises several 32-character hexadecimal
 * identifiers associated with chart/resource metadata:
 *
 *     9821191e310d21438ae6b3ce29662720
 *     1c1a5c128973527be725f44a22623ab0
 *     30432b6e85427282e41213c75b32afd0
 *     2417276e3192388c131eb4cdfa273f60
 *     183b790b01838bb42532f75fb5aaa540
 *     3b442c161f8811836b62729614081b20
 *     2d808a6861e944bc9e46eb4d4a2aa4d0
 *
 * Their exact source-field name and mapping to historical charts are UNRESOLVED.
 * Do not automatically call them MD5 hashes merely because they are 32 hex
 * characters.
 *
 * Behavioural conclusion:
 * +0x110 is not inferred dynamically from camera position. The chart is marked
 * as needing this compatibility behaviour when LogicChart is built, then
 * consumers additionally inspect live camera-derived coordinates.
 */
bool deriveSpecialGameplaySpaceCapability(
    const char* songId,
    const char* associatedResourceIdentifier)
{
    // RECONSTRUCTED compact form of several native comparisons.
    if (equalsAny(songId, {
            "lfdyrmx",
            "unknownrmx",
            "cataclysmcry",
            "deinosphainein",
            "ember",
            "sacrosanct",
        })) {
        return true;
    }

    if (equalsAny(associatedResourceIdentifier, {
            "9821191e310d21438ae6b3ce29662720",
            "1c1a5c128973527be725f44a22623ab0",
            "30432b6e85427282e41213c75b32afd0",
            "2417276e3192388c131eb4cdfa273f60",
            "183b790b01838bb42532f75fb5aaa540",
            "3b442c161f8811836b62729614081b20",
            "2d808a6861e944bc9e46eb4d4a2aa4d0",
        })) {
        return true;
    }

    return false;
}

// -----------------------------------------------------------------------------
// 3. This capability is not itself the camera animator
// -----------------------------------------------------------------------------

/*
 * CONFIRMED native type/symbol evidence separately contains:
 *
 *     CameraController
 *
 * and a surviving lambda symbol originating from approximately:
 *
 *     CameraController::animateMovingCameraTo(cocos2d::Vec3, float)
 *
 * Thus the binary has a distinct native camera-motion subsystem.
 *
 * LogicChart +0x110 is consumed by gameplay/input/spatial code so those systems
 * remain meaningful while nonstandard camera motion is occurring; it is not the
 * position/animation value that moves the camera.
 *
 * This is why `specialGameplaySpace` is a safer behavioural name than
 * `specialCameraMode`.
 */

// -----------------------------------------------------------------------------
// 4. Ordinary floor-depth validity from Section 11
// -----------------------------------------------------------------------------

/*
 * CONFIRMED.
 *
 * Floor touch input first intersects the common world-space touch ray with Y=0.
 * In ordinary gameplay-space mode, the result is accepted only when:
 *
 *     -500 < ground.z < +500
 *
 * These comparisons are strict.
 */
bool ordinaryFloorDepthValid(float groundZ)
{
    return groundZ > -500.0f && groundZ < 500.0f;
}

// -----------------------------------------------------------------------------
// 5. The 0 / 900 camera-depth branch is now resolved
// -----------------------------------------------------------------------------

/*
 * CONFIRMED control flow in the floor mapper around ~0x13073F0.
 *
 * Only when LogicChart +0x110 is true does the mapper read a camera-related Vec3
 * through an object reached from gameplay state.
 *
 * Its third component is tested as:
 *
 *     cameraDerivedDepth < 0
 *         OR
 *     cameraDerivedDepth > 900
 *
 * Because the native expression invokes the Vec3-returning getter separately
 * for the two sides of the OR, the disassembly contains two virtual calls.
 * The readable reconstruction below treats them as one conceptual live camera
 * depth value; the exact original accessor name is UNRESOLVED.
 *
 * IMPORTANT correction to earlier scratch interpretation:
 * the expanded branch is used OUTSIDE the nominal 0..900 camera-depth span,
 * not inside it.
 */
bool cameraOutsideNominalDepthSpan(float cameraDerivedDepth)
{
    return
        cameraDerivedDepth < 0.0f ||
        cameraDerivedDepth > 900.0f;
}

// -----------------------------------------------------------------------------
// 6. Special floor mode widens the depth gate toward the camera
// -----------------------------------------------------------------------------

/*
 * CONFIRMED.
 *
 * If +0x110 is enabled AND camera depth is outside 0..900:
 *
 *     ground.z > -1000
 *
 * is the relevant depth-validity gate before lane classification.
 *
 * There is no corresponding +500 upper bound in this special branch of this
 * function.
 *
 * Otherwise, including flagged charts while camera depth remains within 0..900,
 * the mapper uses the ordinary -500..+500 gate.
 */
bool floorDepthValid(
    bool specialGameplaySpace,
    float cameraDerivedDepth,
    float groundZ)
{
    const bool expandedDepth =
        specialGameplaySpace &&
        cameraOutsideNominalDepthSpan(cameraDerivedDepth);

    if (expandedDepth) {
        return groundZ > -1000.0f;
    }

    return ordinaryFloorDepthValid(groundZ);
}

/*
 * RECONSTRUCTED geometric interpretation:
 * The fixed ordinary slab is suitable while the camera occupies its normal
 * depth span. When an allowlisted chart moves the camera beyond that regime,
 * input retains a larger region behind/toward the camera instead of rejecting
 * the projected ground point at -500.
 *
 * The control flow and thresholds are CONFIRMED. This design explanation is
 * RECONSTRUCTED, not an original source comment.
 */

// -----------------------------------------------------------------------------
// 7. Special floor mode does NOT replace lane X geometry
// -----------------------------------------------------------------------------

/*
 * CONFIRMED.
 *
 * After passing either depth gate, the same floor-X classifier continues to use
 * the established lane boundaries:
 *
 *     -850, -425, 0, +425, +850
 *
 * and the same Section 09 widening state:
 *
 *     +0x60 = enwidencamera scale
 *     +0x74 = enwidenlanes progress
 *
 * Thus +0x110 changes whether a projected floor point is spatially valid, but
 * does not create a second set of lane-centre or lane-boundary X coordinates.
 */
struct LaneCandidates {
    int primary;
    int adjacent;
};

LaneCandidates mapSpecialAwareFloorTouch(
    GameplayState& state,
    const Ray& touchRay)
{
    const Vec3 ground = intersectGroundY0(touchRay);

    const bool special =
        usesSpecialGameplaySpace(*state.logicChart);

    const float cameraDepth =
        getCameraRelatedDepth(state); // RECONSTRUCTED accessor name

    if (!floorDepthValid(special, cameraDepth, ground.z)) {
        return {0, 0};
    }

    return classifyFloorXWithWidening(
        static_cast<int>(ground.x),
        state.cameraWidenScale,
        state.laneWidenProgress);
}

// -----------------------------------------------------------------------------
// 8. Touch-begin has an additional camera-behind-zero compatibility branch
// -----------------------------------------------------------------------------

/*
 * CONFIRMED around ~0x0EEC1F0.
 *
 * The common touch-begin path has already built the ordinary near/far world ray.
 * When +0x110 is true it obtains a camera-related Vec3 and checks its third
 * component against zero.
 *
 * If that component is negative, the path rebuilds and overwrites a projected
 * touch-reference point using another Y=0 ray/plane intersection before note
 * candidate processing continues.
 *
 * Readable shape:
 */
void adaptProjectedTouchReferenceForSpecialSpace(
    GameplayState& state,
    const Ray& touchRay,
    Vec3& projectedReference)
{
    if (!usesSpecialGameplaySpace(*state.logicChart)) {
        return;
    }

    const float cameraDepth = getCameraRelatedDepth(state);

    if (cameraDepth < 0.0f) {
        projectedReference = intersectGroundY0(touchRay);
    }
}

/*
 * The extra intersection and the <0 condition are CONFIRMED.
 * `adaptProjectedTouchReferenceForSpecialSpace` and the idea of stabilising input
 * after the camera passes the ordinary track origin are RECONSTRUCTED names/
 * interpretation.
 */

// -----------------------------------------------------------------------------
// 9. Sky input keeps its universal -1000 Z gate
// -----------------------------------------------------------------------------

/*
 * CONFIRMED refinement of Section 11.
 *
 * Arc/ArcTap input does not use the floor depth test. Its shared sky-hit helper
 * ends with:
 *
 *     touchRelatedPoint.z > -1000
 *
 * for BOTH ordinary and +0x110 charts.
 *
 * Therefore the -1000 constant has two distinct usages:
 *
 *   floor mapper:
 *     conditional special-space lower bound
 *
 *   Arc/ArcTap sky helper:
 *     unconditional final Z gate
 *
 * Do not describe the sky -1000 comparison as evidence that sky input itself
 * enters a separate -1000 mode. It is always present.
 */
bool commonSkyDepthValid(const Vec3& touchRelatedPoint)
{
    return touchRelatedPoint.z > -1000.0f;
}

// -----------------------------------------------------------------------------
// 10. +0x110 changes ArcTap vertical coordinate interpretation
// -----------------------------------------------------------------------------

/*
 * CONFIRMED around ~0x092752C.
 *
 * The sky helper distinguishes Arc body from ArcTap.
 *
 * Arc body:
 *     use touchRelatedPoint.y directly
 *
 * ArcTap, ordinary chart:
 *     use an additional camera/enwidencamera-aware vertical remapping/clamping
 *
 * ArcTap, +0x110 chart:
 *     use touchRelatedPoint.y directly, like Arc body
 *
 * This is the clearest sky-input effect of +0x110.
 */
enum class SkyCandidateKind {
    ArcBody,
    ArcTap,
};

float deriveSkyTouchVertical(
    const GameplayState& state,
    SkyCandidateKind kind,
    const Vec3& touchRelatedPoint)
{
    if (kind == SkyCandidateKind::ArcBody) {
        return touchRelatedPoint.y;
    }

    if (usesSpecialGameplaySpace(*state.logicChart)) {
        return touchRelatedPoint.y;
    }

    return ordinaryArcTapVerticalRemap(
        touchRelatedPoint.y,
        state.cameraWidenScale);
}

/*
 * The ordinary ArcTap remap contains camera/input scaling and a 100-unit clamp/
 * reference term. This section intentionally preserves its behavioural role
 * rather than pretending an exact original source formula/name has been proved.
 */

// -----------------------------------------------------------------------------
// 11. Horizontal sky-hit behaviour: a latent mode-dependent choice
// -----------------------------------------------------------------------------

/*
 * CONFIRMED native structure, but no material difference at current callers.
 *
 * The sky helper has a mode-dependent nominal-width selection for ArcTap:
 *
 *   ordinary chart -> hard-coded ArcTap nominal 212
 *   +0x110 chart    -> use the nominal float supplied by the caller
 *
 * However BOTH identified callers in this exact build supply 212.0f.
 *
 * Therefore the branch exists, but we do NOT claim that +0x110 currently makes
 * ordinary ArcTap horizontal hitboxes wider/narrower by itself.
 *
 * This is also insufficient by itself to explain the archived chart-colour-3
 * zero-duration Arc -> ArcTap width behaviour. That is a separate subsystem and
 * should be connected only if its producer path is independently traced.
 */

// -----------------------------------------------------------------------------
// 12. Sky-hit geometry after the special vertical choice
// -----------------------------------------------------------------------------

/*
 * CONFIRMED relation to Section 11.
 *
 * Once scale S and the candidate vertical coordinate have been selected, the
 * familiar rectangular/slab gates remain:
 *
 * Arc body default:
 *     horizontal ~ expected.x +/- 190*S
 *     vertical   ~ expected.y +/- 250*S
 *
 * ArcTap default:
 *     horizontal ~ expected.x +/- 302*S
 *     vertical   ~ expected.y - 310*S .. expected.y + 250*S
 *
 * plus:
 *     touchRelatedPoint.z > -1000
 *
 * +0x110 changes how ArcTap's touch-side vertical coordinate is obtained, not
 * the existence of this final rectangular/slab test.
 */

// -----------------------------------------------------------------------------
// 13. 900 is also a camera-space reference in sky scaling
// -----------------------------------------------------------------------------

/*
 * CONFIRMED inside the common Arc/ArcTap hit helper.
 *
 * The helper's camera/aspect/input scale uses exact reference dimensions and
 * constants including:
 *
 *     720
 *     960
 *     240
 *     900
 *
 * and current camera-derived depth.
 *
 * The same 900 value also forms the positive edge of the floor mapper's nominal
 * camera-depth span. This strongly ties 900 to the game's normal camera/track
 * depth reference system.
 *
 * `normal camera/track depth reference` is a RECONSTRUCTED semantic description;
 * the original constant/member name is not recovered.
 */

// -----------------------------------------------------------------------------
// 14. Auxiliary spatial updater replaces fixed 300/700 bounds in special mode
// -----------------------------------------------------------------------------

/*
 * CONFIRMED around ~0x10FD1C0.
 *
 * Ordinary mode seeds an integer pair:
 *
 *     300, 700
 *
 * When +0x110 is enabled, the routine instead asks the same camera-related Vec3
 * provider twice and converts its third components to integers before continuing
 * through chart/timing/spatial processing.
 *
 * This proves +0x110 affects more than one touch predicate: additional gameplay-
 * spatial state is made camera-derived instead of fixed.
 *
 * UNRESOLVED:
 * The exact original meaning of the 300/700 pair. It may be a visible/effective
 * track-depth range or another spatial window, but this section does not promote
 * that guess to fact.
 */
struct SpatialRangePair {
    int first;
    int second;
};

SpatialRangePair deriveAuxiliarySpatialRange(
    GameplayState& state)
{
    if (!usesSpecialGameplaySpace(*state.logicChart)) {
        return {300, 700};
    }

    return {
        static_cast<int>(getCameraRelatedDepth(state)),
        static_cast<int>(getCameraRelatedDepth(state)),
    };
}

// -----------------------------------------------------------------------------
// 15. Surviving Flick code uses the same capability
// -----------------------------------------------------------------------------

/*
 * CONFIRMED downstream handler evidence around ~0x1485AB8.
 *
 * The surviving active-touch Flick path tests +0x110 while selecting between:
 *
 *   - an ordinary camera/enwidencamera-adjusted coordinate
 *   - a more direct projected coordinate used in special-space mode
 *
 * It then feeds the result into the common floor mapper.
 *
 * Section 18 proved LogicFlickNote construction is disconnected in this exact
 * binary, so this is architectural residue/consumer evidence rather than proof
 * of a reachable Flick producer path in this build.
 */

// -----------------------------------------------------------------------------
// 16. Presentation-side objects also receive the capability
// -----------------------------------------------------------------------------

/*
 * CONFIRMED at another construction path around ~0x0E2F60C:
 *
 *     special = usesSpecialGameplaySpace(chart)
 *     newlyCreatedObject[+0x2C8] = special
 *
 * The exact class/field semantic of that destination is not required here.
 * It demonstrates that the compatibility bit is propagated beyond one isolated
 * input routine so presentation/spatial consumers can stay aligned.
 *
 * Do NOT equate this +0x2C8 with unrelated objects that happen to use the same
 * numeric offset elsewhere.
 */

// -----------------------------------------------------------------------------
// 17. Reconstructed end-to-end mental model
// -----------------------------------------------------------------------------

/*
 * RECONSTRUCTED from confirmed branches:
 *
 *                      LogicChart construction
 *                              |
 *              song/resource compatibility allowlist
 *                              |
 *                         +0x110 boolean
 *                              |
 *         +--------------------+---------------------+
 *         |                    |                     |
 *         v                    v                     v
 *   common touch begin    floor lane mapper      sky hit helper
 *         |                    |                     |
 *   camera depth <0?      camera depth outside      ArcTap?
 *         |                  0..900?                 |
 *   redo Y=0 projected       /    \           ordinary | special
 *   reference point       yes      no                |
 *                          |        |          remapped Y | raw world Y
 *                    ground.z     -500..500           |
 *                      > -1000       gate             |
 *                          \        /                 |
 *                           same lane X          same rectangular
 *                           classifier           Arc/ArcTap slab
 *                                                   + z > -1000
 *
 * Separate subsystem:
 *
 *     CameraController
 *         -> actual moving-camera animation/state
 *
 * +0x110 does not animate that controller. It tells gameplay-space consumers to
 * use compatibility branches while charts known to exercise unusual camera
 * geometry are active.
 */

// -----------------------------------------------------------------------------
// 18. Compact findings
// -----------------------------------------------------------------------------

/*
 * CONFIRMED:
 *   - the old special-space predicate is LogicChart +0x110.
 *   - it is a chart-construction boolean enabled by hard-coded identifiers.
 *   - it is distinct from YBN LogicChart +0x111.
 *   - native CameraController exists separately from this flag.
 *   - ordinary floor depth is strict -500 < z < +500.
 *   - flagged charts inspect live camera-related depth.
 *   - when that depth is <0 OR >900, floor validity becomes ground.z > -1000.
 *   - while camera depth remains within 0..900, flagged charts still use the
 *     ordinary -500..+500 floor gate.
 *   - lane X boundaries remain the same after the depth gate.
 *   - sky input always requires touchRelatedPoint.z > -1000.
 *   - flagged ArcTaps use raw projected/world Y instead of ordinary ArcTap's
 *     additional vertical remapping.
 *   - the sky helper contains a mode-dependent ArcTap nominal-width branch, but
 *     current callers pass 212 in both cases, so no width change is claimed.
 *   - another updater replaces fixed 300/700 values with camera-derived values
 *     when +0x110 is active.
 *   - surviving Flick code consumes the same capability.
 *
 * RECONSTRUCTED:
 *   - name `specialGameplaySpace`.
 *   - interpretation as a compatibility layer for nonstandard camera geometry.
 *   - `cameraRelatedDepth` naming for the third component returned by the
 *     repeatedly used camera-related Vec3 provider.
 *
 * UNRESOLVED:
 *   - exact original source member/getter name for +0x110.
 *   - exact semantic name of the camera Vec3 provider.
 *   - exact meaning of auxiliary fixed range 300/700.
 *   - historical mapping of every 32-character allowlist identifier.
 */
