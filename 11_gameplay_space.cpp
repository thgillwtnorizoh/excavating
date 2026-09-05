/*
 * Arcaea excavation notebook
 * Section 11: gameplay-space projection and camera-aware input geometry
 *
 * STATUS: gameplay-space / camera-input fundamentals slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native code/data/control flow.
 *   RECONSTRUCTED = readable representation assembled from confirmed behaviour.
 *   UNRESOLVED    = exact semantic name/purpose is not yet proved.
 *
 * Scope of this section:
 *   - screen touch -> world ray unprojection
 *   - gameplay axis orientation and ground-plane projection
 *   - how the floor lane mapper consumes that ray
 *   - how Arc/ArcTap input differs from floor projection
 *   - native Arc/ArcTap camera-scaled hit-region dimensions
 *   - direct gameplay effects of `enwidencamera`
 *   - relationship between the common spatial updater and the global -3000 ms
 *     fallback clock established in Section 10
 *   - minimum statement about timinggroup anglex/angley relative to common input
 *
 * Deliberately out of scope:
 *   - complete camera matrix construction / animation system
 *   - note rendering meshes, textures, shaders, particles
 *   - exact LogicFlickNote AABB construction formula
 *   - exact downstream visual transform driven by timinggroup anglex/angley
 *
 * This file builds directly on:
 *   04_arc_contact.cpp
 *   06_arctaps.cpp
 *   07_flick_notes.cpp
 *   08_lane_geometry.cpp
 *   09_enwidenlanes.cpp
 *   10_timinggroups.cpp
 *
 * Useful native anchors from the investigated ARM64 build:
 *   touch-begin common projection/candidate path        ~0x0EEC024
 *   touch location getter                               ~0x13E0324
 *   screen/NDC/inverse-VP unprojection core             ~0x16D3DCC
 *   ray normalisation helper                            ~0x1571280
 *   ray/plane intersection helper                       ~0x129DEE0
 *   touch -> floor-lane mapper                          ~0x130733C
 *   Arc/ArcTap camera-scaled hit helper                 ~0x0927384
 *   active-touch Flick branch                           ~0x1485CD4
 *   widening/spatial state updater                      ~0x10A7550
 *   observed input-size default initialisation          ~0x1930AA4
 *
 * WARNING ABOUT SYMBOLS:
 * The binary is stripped. Nearby library symbol labels printed by objdump are
 * frequently unrelated. Readable names below are reconstruction names unless
 * explicitly described as confirmed field behaviour or surviving chart/type
 * identifiers.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>

// -----------------------------------------------------------------------------
// 1. Gameplay coordinate axes
// -----------------------------------------------------------------------------

/*
 * CONFIRMED by the floor touch mapper.
 *
 * Ground input is obtained by intersecting the touch ray with a plane whose:
 *
 *     normal = (0, 1, 0)
 *     point  = (0, 0, 0)
 *
 * Therefore the gameplay ground plane is Y=0.
 *
 * Combined with established lane/Arc consumers:
 *
 *     X = horizontal across the playfield
 *     Y = sky height
 *     Z = track depth / forward-back direction
 *
 * The lane boundaries from Section 08:
 *
 *     -850, -425, 0, +425, +850
 *
 * are therefore X coordinates on the gameplay Y=0 plane, not screen pixels.
 */
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
    Vec3 direction; // normalised
};

// -----------------------------------------------------------------------------
// 2. Screen touch -> world ray
// -----------------------------------------------------------------------------

/*
 * CONFIRMED call shape from touch-begin processing.
 *
 * The game obtains the touch's 2D screen location, then constructs two 3D
 * screen-space samples at the SAME x/y:
 *
 *     nearSample = (screenX, screenY, -1)
 *     farSample  = (screenX, screenY, +1)
 *
 * Both are unprojected through the current camera.
 *
 * The resulting world points form a ray:
 *
 *     origin    = nearWorld
 *     direction = normalize(farWorld - nearWorld)
 *
 * This is performed before floor-lane, Arc/ArcTap, Hold and other spatial
 * candidate logic branches apart.
 */
Ray buildTouchWorldRay(
    const Camera& camera,
    float screenX,
    float screenY)
{
    const Vec3 nearWorld =
        unproject(camera, {screenX, screenY, -1.0f});

    const Vec3 farWorld =
        unproject(camera, {screenX, screenY, +1.0f});

    return {
        nearWorld,
        normalize(farWorld - nearWorld),
    };
}

// -----------------------------------------------------------------------------
// 3. Native unprojection is ordinary inverse view-projection unprojection
// -----------------------------------------------------------------------------

/*
 * CONFIRMED high-level arithmetic of the helper around ~0x16D3DCC.
 *
 * Inputs include viewport/frame dimensions and origin plus the camera's
 * inverse view-projection state.
 *
 * The helper:
 *   1. converts screen x/y/z into viewport-normalised coordinates
 *   2. flips screen Y into the camera convention
 *   3. maps normalised coordinates into NDC [-1,+1]
 *   4. multiplies by the inverse view-projection matrix
 *   5. performs the homogeneous divide by W
 *
 * Readable equivalent:
 */
Vec3 unproject(const Camera& camera, Vec3 screen)
{
    const Viewport vp = camera.viewport();

    const float nx =
        (screen.x - vp.originX) / vp.width;

    const float ny =
        (vp.originY + vp.height - screen.y) / vp.height;

    const float nz =
        normaliseScreenDepth(screen.z, vp);

    const Vec4 ndc {
        2.0f * nx - 1.0f,
        2.0f * ny - 1.0f,
        2.0f * nz - 1.0f,
        1.0f,
    };

    Vec4 world =
        camera.inverseViewProjection() * ndc;

    if (world.w != 0.0f) {
        world.x /= world.w;
        world.y /= world.w;
        world.z /= world.w;
    }

    return {world.x, world.y, world.z};
}

/*
 * RECONSTRUCTED naming only:
 * "Viewport", "inverseViewProjection", and "normaliseScreenDepth" are readable
 * descriptions of confirmed operations, not recovered original member names.
 */

// -----------------------------------------------------------------------------
// 4. Floor input: intersect the world ray with Y=0
// -----------------------------------------------------------------------------

/*
 * CONFIRMED:
 * The floor-lane mapper builds the plane Y=0 and calls a generic ray/plane
 * intersection helper.
 *
 * Readable algebra:
 *
 *     denom = dot(N, ray.direction)
 *     t     = (planeD - dot(N, ray.origin)) / denom
 *     hit   = ray.origin + ray.direction * t
 *
 * The observed helper falls back to t=0 for the parallel/degenerate path.
 */
Vec3 intersectGround(const Ray& ray)
{
    const Vec3 normal {0.0f, 1.0f, 0.0f};
    const float planeD = 0.0f;

    const float denom = dot(normal, ray.direction);

    float t = 0.0f;

    if (denom != 0.0f) {
        t =
            (planeD - dot(normal, ray.origin))
            / denom;
    }

    return
        ray.origin + ray.direction * t;
}

/*
 * CONFIRMED ordinary depth validity:
 * The normal floor-input branch checks the ground intersection's Z against
 * approximately:
 *
 *     -500 < ground.z < +500
 *
 * A separate camera/input mode has a wider/dependent branch involving -1000,
 * 900, and camera-derived state. The exact higher-level semantic identity of
 * that mode is UNRESOLVED and is not required for the ordinary lane model.
 */

// -----------------------------------------------------------------------------
// 5. Floor lane selection is a consumer of projected world space
// -----------------------------------------------------------------------------

/*
 * CONFIRMED relationship to Section 08.
 *
 * Once the Y=0 intersection exists:
 *
 *     ground.x -> lane boundary classifier
 *     ground.z -> depth validity
 *
 * No raw-screen X division is used to choose the lane.
 *
 * Ordinary horizontal boundaries:
 *
 *     -850  -425   0   +425  +850
 *
 * The mapper then applies the Section 08/09 widening rules:
 *   - +0x74 (`enwidenlanes` progress) controls whether outer IDs 1/6 can be
 *     independently selected.
 *   - +0x60 (`enwidencamera` scale) affects adjacent-lane overlap width.
 */
struct LaneCandidates {
    int primary;
    int adjacent;
};

LaneCandidates projectTouchToFloorLanes(
    GameplayState& state,
    const Ray& ray)
{
    const Vec3 ground = intersectGround(ray);

    if (!groundDepthIsValid(state, ground.z)) {
        return {0, 0};
    }

    return classifyFloorXWithWidening(
        static_cast<int>(ground.x),
        state.cameraWidenScale, // +0x60
        state.laneWidenProgress); // +0x74
}

// -----------------------------------------------------------------------------
// 6. `enwidencamera` directly changes input geometry
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from Sections 08/09 and this section:
 *
 * Gameplay-state +0x60 is the current `enwidencamera` transition value:
 *
 *     normal  = 1.0
 *     widened = 1.5
 *
 * It is read directly by:
 *   - the floor lane mapper when generating its optional adjacent candidate
 *   - the Arc/ArcTap hit-region helper
 *
 * Therefore `enwidencamera` is not renderer-only presentation state.
 *
 * Floor adjacent-lane expansion established in Section 08:
 *
 *     halfExtra =
 *         (cameraWidenScale - 1)
 *         * 425
 *         * 0.5
 *
 * At scale 1.0, halfExtra = 0.
 * At scale 1.5, halfExtra = 106.25 gameplay X units.
 */

// -----------------------------------------------------------------------------
// 7. Camera widening also triggers compensating note/presentation adjustment
// -----------------------------------------------------------------------------

/*
 * CONFIRMED arithmetic in the main widening/spatial updater:
 *
 * When +0x60 changes, live note-associated objects are revisited and a derived
 * factor is calculated:
 *
 *     factor =
 *         1
 *         - (cameraWidenScale - 1) * (2/3)
 *
 * Therefore:
 *
 *     camera scale 1.0 -> factor 1.0
 *     camera scale 1.5 -> factor 2/3
 *
 * A downstream helper applies a value proportional to factor*130 to child
 * scene/presentation objects.
 *
 * RECONSTRUCTED gameplay meaning:
 * This is reciprocal-style horizontal/spatial compensation which keeps note
 * presentation registered while the camera/field widens.
 *
 * UNRESOLVED:
 * The exact native class/member name of the adjusted object is not proved, so
 * this file intentionally does NOT call it a NotePosition setter or claim a
 * complete camera projection-matrix reconstruction.
 */
float presentationCompensationForCameraWiden(float cameraWidenScale)
{
    return
        1.0f
        - (cameraWidenScale - 1.0f)
          * (2.0f / 3.0f);
}

// -----------------------------------------------------------------------------
// 8. Sky input does NOT use the floor-plane intersection
// -----------------------------------------------------------------------------

/*
 * CONFIRMED architecture:
 *
 * Touch-begin still builds the same common world ray.
 *
 * Floor notes:
 *     world ray
 *       -> intersect Y=0
 *       -> lane IDs
 *
 * Arc/ArcTap:
 *     near-world point / ray-origin data
 *       + expected Arc gameplay point
 *       -> camera/aspect-scaled Arc hit helper
 *
 * The Arc helper does NOT simply intersect the ray with a horizontal plane at
 * the Arc's expected Y.
 *
 * Thus floor and sky input share the screen->world unprojection but diverge
 * immediately afterward.
 */

// -----------------------------------------------------------------------------
// 9. Arc/ArcTap hit region: confirmed default geometry
// -----------------------------------------------------------------------------

/*
 * CONFIRMED static defaults used by the hit helper:
 *
 *   generic horizontal width term = 180
 *   generic vertical extent       = 250
 *   Arc-body nominal extent       = 100
 *
 * Surviving settings strings include:
 *
 *   "touchSizeWidth"
 *   "touchSizeHeight"
 *
 * Their exact one-to-one wiring to these particular globals was not proved, so
 * the readable names below describe behaviour rather than claiming recovered
 * configuration names.
 */
static constexpr float kDefaultHorizontalWidthTerm = 180.0f;
static constexpr float kDefaultVerticalExtent = 250.0f;
static constexpr float kDefaultArcBodyNominal = 100.0f;
static constexpr float kArcTapNominal = 212.0f;
static constexpr float kArcTapExtraDownwardExtent = 60.0f;

/*
 * The helper first derives a camera/aspect/input scale. Call it S.
 *
 * The calculation incorporates:
 *   - current camera/viewport measurements
 *   - reference dimensions including 720 and 960
 *   - gameplay state `enwidencamera` scale +0x60
 *   - a special camera/input-mode predicate
 *
 * Its exact original high-level name is UNRESOLVED.
 *
 * Horizontal half extent is:
 *
 *     halfX =
 *         S * (nominal + horizontalWidthTerm/2)
 *
 * Normal defaults:
 *
 * Arc body:
 *     nominal = 100
 *     halfX   = S * (100 + 90)
 *             = S * 190
 *
 * ArcTap:
 *     nominal = 212
 *     halfX   = S * (212 + 90)
 *             = S * 302
 *
 * Horizontal comparisons are strict at the final bounds.
 *
 * Vertical defaults:
 *
 * Arc body:
 *     expectedY - S*250
 *       < touchVertical
 *       < expectedY + S*250
 *
 * ArcTap:
 *     expectedY - S*(250+60)
 *       < touchVertical
 *       < expectedY + S*250
 *
 * Therefore at S=1:
 *
 *     Arc body : ±190 X, -250/+250 Y
 *     ArcTap   : ±302 X, -310/+250 Y
 *
 * The ArcTap window is deliberately larger and extends 60 units farther
 * downward than upward.
 *
 * A third gate additionally requires the supplied touch/ray-origin Z component:
 *
 *     touchRelatedZ > -1000
 *
 * This confirms a rectangular/slab-like 3D acceptance region, not a radius test.
 */
enum class SkyHitMode {
    ArcBody,
    ArcTap,
};

bool skyHitRegionContains(
    float S,
    SkyHitMode mode,
    Vec3 touchRelatedPoint,
    Vec2 expected)
{
    const float nominal =
        mode == SkyHitMode::ArcTap
            ? kArcTapNominal
            : kDefaultArcBodyNominal;

    const float halfX =
        S * (
            nominal
            + kDefaultHorizontalWidthTerm * 0.5f);

    const float upperY =
        expected.y
        + S * kDefaultVerticalExtent;

    const float lowerExtra =
        mode == SkyHitMode::ArcTap
            ? kArcTapExtraDownwardExtent
            : 0.0f;

    const float lowerY =
        expected.y
        - S * (
            kDefaultVerticalExtent
            + lowerExtra);

    const float touchY =
        deriveCameraAwareSkyTouchVertical(
            touchRelatedPoint);

    return
        touchRelatedPoint.x > expected.x - halfX &&
        touchRelatedPoint.x < expected.x + halfX &&
        touchY > lowerY &&
        touchY < upperY &&
        touchRelatedPoint.z > -1000.0f;
}

/*
 * IMPORTANT:
 * `deriveCameraAwareSkyTouchVertical()` abstracts a confirmed but fairly
 * branchy camera/aspect transformation in the native helper. The final bound
 * arithmetic above is confirmed; inventing a prettier name/formula for the
 * intermediate scalar would reduce evidence quality.
 */

// -----------------------------------------------------------------------------
// 10. Design-reference dimensions inside the sky helper
// -----------------------------------------------------------------------------

/*
 * CONFIRMED constants participating in camera/aspect correction include:
 *
 *     720
 *     960
 *     240
 *     100
 *     900
 *
 * The 720/960 pair acts as a reference viewport/aspect threshold in the helper.
 * Exact original semantic names of every intermediate are not proved.
 *
 * The durable result is that Arc/ArcTap input extents are NOT fixed raw-world
 * constants. They are passed through a camera/screen/aspect scale before the
 * final comparisons.
 */

// -----------------------------------------------------------------------------
// 11. Flick uses note-local 2D spatial state, but its constructor remains open
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from Section 07 and the live active-touch branch:
 *
 * LogicFlickNote stores an AABB at:
 *
 *     +0x7C .. +0x88
 *
 * Before the gesture evaluator is called, the live touch branch:
 *   1. obtains a transformed 2D touch point
 *   2. copies the Flick AABB
 *   3. performs an inclusive rectangle-contains test
 *   4. only then evaluates the >106 / <45-degree directional displacement
 *
 * This section establishes the common camera/gameplay-space machinery around
 * that test, but DOES NOT prove the exact constructor formula which produces
 * the four AABB floats.
 *
 * Therefore the following remains UNRESOLVED:
 *
 *     Flick chart position/direction
 *       -> exact runtime +0x7C..+0x88 rectangle
 *
 * It is intentionally deferred rather than inferred from Arc or lane hit sizes.
 */

// -----------------------------------------------------------------------------
// 12. Timinggroup anglex/angley do not alter common touch unprojection
// -----------------------------------------------------------------------------

/*
 * CONFIRMED architectural boundary:
 *
 * Common touch ray construction receives camera/game state and the screen touch.
 * It does not receive the candidate LogicNote.
 *
 * Section 10 established anglex/angley as metadata propagated into individual
 * runtime notes.
 *
 * Consequence:
 * timinggroup angle metadata cannot alter the COMMON screen->world touch ray.
 * Any angle effect must enter a note-specific spatial/presentation transform
 * downstream of common unprojection.
 *
 * Exact downstream consumer/formula remains UNRESOLVED and is better excavated
 * with note rendering/presentation rather than by expanding this camera slice.
 */

// -----------------------------------------------------------------------------
// 13. The -3000 ms fallback clock also drives spatial/camera updating
// -----------------------------------------------------------------------------

/*
 * CONFIRMED:
 *
 * The widening/spatial updater around ~0x10A7550 obtains the same effective
 * gameplay time reconstructed in Section 10:
 *
 *   synchronised path:
 *       liveSource - commonOffset
 *
 *   fallback path:
 *       fallbackSource - commonOffset
 *       and, while fallbackSource <= 0, additionally -3000 ms
 *
 * That effective time advances `enwidencamera`, `enwidenlanes`, and associated
 * note/presentation spatial state.
 *
 * Therefore the -3000 ms fallback is NOT judgement-only. Negative gameplay time
 * can participate in the normal spatial/camera update machinery.
 *
 * RECONSTRUCTED DESIGN INTERPRETATION:
 * This is consistent with a three-second pre-roll which allows gameplay state
 * and upcoming geometry to settle before chart time zero / synchronised timing
 * takes over.
 *
 * The mechanism is confirmed; the developers' intended UX rationale is not.
 */

// -----------------------------------------------------------------------------
// 14. Compact gameplay-space model
// -----------------------------------------------------------------------------

/*
 *                          raw screen touch
 *                                |
 *                                v
 *                     near/far unprojection
 *                                |
 *                                v
 *                         normalised world ray
 *                           /             \
 *                          /               \
 *                         v                 v
 *                FLOOR INPUT            SKY INPUT
 *                   |                       |
 *             intersect Y=0          near-world/ray-origin
 *                   |                 + expected Arc point
 *             ground X / Z                  |
 *                   |                       v
 *             lane classifier       camera-scaled box/slab
 *                   |                       |
 *             Tap / Hold             Arc / ArcTap
 *
 * `enwidencamera` +0x60 participates in BOTH sides:
 *   - floor adjacent-lane acceptance width
 *   - sky hit-region scaling
 * and additionally triggers compensating note/presentation updates.
 */

// -----------------------------------------------------------------------------
// 15. CONFIRMED / RECONSTRUCTED / UNRESOLVED summary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED
 * ---------
 * - Touch input is unprojected at near=-1 and far=+1 through current camera state.
 * - The resulting two world points form a normalised ray.
 * - Gameplay ground is plane Y=0.
 * - X is horizontal, Y is sky height, Z is track depth.
 * - Floor lane selection intersects the world ray with Y=0.
 * - Ordinary ground-depth acceptance is roughly -500 < Z < +500.
 * - Floor lane boundaries are gameplay-space X values, not screen pixels.
 * - `enwidencamera` +0x60 directly affects floor candidate overlap.
 * - Arc/ArcTap input shares common unprojection but not floor-plane intersection.
 * - The sky helper reads the live `enwidencamera` scale.
 * - Default Arc body horizontal half extent is S*190.
 * - Default ArcTap horizontal half extent is S*302.
 * - Default Arc body vertical range is S*250 above/below expected Y.
 * - Default ArcTap vertical range is S*250 upward and S*310 downward.
 * - Sky helper has a Z > -1000 gate.
 * - Widening-state updates use the same effective clock, including fallback -3000.
 * - Common touch unprojection is note-independent, so per-note timinggroup angles
 *   cannot modify the common ray itself.
 *
 * RECONSTRUCTED
 * -------------
 * - readable Camera/Viewport/Ray function names used in pseudocode
 * - interpretation of the camera-widen note factor as visual/spatial compensation
 * - UX interpretation of -3000 as gameplay pre-roll
 *
 * UNRESOLVED
 * ----------
 * - exact full camera matrix construction/animation ownership
 * - original semantic names of Arc hit-size globals
 * - exact mapping of `touchSizeWidth` / `touchSizeHeight` strings to those globals
 * - exact special camera/input-mode semantics in floor and sky helpers
 * - exact LogicFlickNote AABB construction
 * - exact downstream anglex/angley transform
 */
