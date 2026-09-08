// Arcaea native-engine deep dive
// Section 04: Gameplay rendering
//
// IMPORTANT:
// This is reconstructed reference pseudocode, NOT recovered original source.
// Evidence comes from the investigated ARM64 libcocos2dcpp.so, surviving RTTI,
// retained lambda type names, native assets/shader strings, renderer factories,
// direct disassembly, and earlier root-level rendering excavations rechecked
// against the same binary SHA-256.
//
// Evidence labels:
//   CONFIRMED     = directly supported by native data/control flow/constants.
//   RECONSTRUCTED = readable semantic structure assembled from confirmed facts.
//   UNRESOLVED    = exact original name/order/design reason is not proved.
//
// Scope: main gameplay rendering only.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace reconstructed {

struct Vec3 { float x, y, z; };
struct LogicNote;
struct LogicTapNote;
struct LogicHoldNote;
struct LogicArcNote;
struct LogicArcTapNote;
struct LogicFlickNote;
struct LogicTimingEvent;
struct RenderNote;
struct RenderTapNote;
struct RenderHoldNote;
struct RenderArcNote;
struct RenderArcTapNote;
struct RenderFlickNote;

// ============================================================================
// 1. CONFIRMED: logic and rendering are reciprocal, separate objects
// ============================================================================

// The central renderer factory stores both directions:
//
//     LogicNote  +0x40 = RenderNote*
//     RenderNote +0x2A8 = LogicNote*
//
// Gameplay judgement/path state therefore remains on Logic* objects while the
// Render* side owns Cocos nodes, sprites, models, textures, meshes and opacity.

struct LogicNoteRenderLink {
    RenderNote* renderer;            // conceptual common +0x40
};

struct RenderNoteLogicLink {
    LogicNote* logic;                // conceptual common +0x2A8
};

// Approximate allocation sizes confirmed in this build:
//     RenderTapNote     0x2D0
//     RenderHoldNote    0x2E0
//     RenderArcTapNote  0x2E0
//     RenderFlickNote   0x2D0
// RenderArcNote is larger and owns additional child containers/vectors.

// ============================================================================
// 2. CONFIRMED: central LogicNote -> RenderNote factory
// ============================================================================

RenderNote* makeRenderer(LogicNote& note)
{
    if (auto* tap = dynamicCast<LogicTapNote>(&note)) {
        return createRenderTapNote(*tap);
    }

    if (auto* hold = dynamicCast<LogicHoldNote>(&note)) {
        return createRenderHoldNote(*hold);
    }

    // This consumer survives although the investigated build has no live
    // LogicFlickNote producer/vtable. It is dormant downstream scaffolding.
    if (auto* flick = dynamicCast<LogicFlickNote>(&note)) {
        return createRenderFlickNote(*flick);
    }

    if (auto* arc = dynamicCast<LogicArcNote>(&note)) {
        std::vector<RenderArcTapNote*> children;

        for (LogicArcTapNote* child : arc->arcTaps) {
            RenderArcTapNote* r = createRenderArcTapNote(*child, *arc);
            child->renderer = r;
            children.push_back(r);
        }

        return createRenderArcNote(*arc, children);
    }

    return nullptr;
}

// The factory also registers each top-level RenderNote into a common renderer
// collection. Exact final cross-family z-order is intentionally left unresolved;
// internal Arc child structure is better proved below.

// ============================================================================
// 3. CONFIRMED: RenderNote consumes logic-side spatial state
// ============================================================================

// Important established LogicNote inputs consumed by renderers:
//     +0x20 NotePosition* / horizontal descriptor
//     +0x30 current approach/depth state
//     +0x34 related end/depth state (important for Holds)
//     +0x48 active LogicTimingEvent*
//     +0x55 hidegroup presentation flag
//     +0x58 angleX radians
//     +0x5C angleY radians
//
// Renderers do not recompute judgement and do not replace ScoreState.

// ============================================================================
// 4. CONFIRMED: floor Tap rendering
// ============================================================================

// Surviving assets:
//     img/note.png
//     img/note_dark.png
//     img/note_tomato.png
//
// Discrete lane X uses the fixed six-slot geometry:
//     x = (internalLaneId - 1) * 425 - 1063
//
// Root Y is 4; Z/depth comes from LogicNote +0x30.

Vec3 tapWorldPosition(const LogicTapNote& tap)
{
    return {
        resolveNotePositionX(*tap.position),
        4.0f,
        static_cast<float>(tap.currentDepth),
    };
}

// Newly rechecked directly in this pass: ordinary Tap horizon opacity.
// Let d = LogicNote +0x30 depth. The native arithmetic is equivalent to:
//
//     alpha = clamp((d + 9000) / 1000, 0, 1) * 255
//
// Therefore:
//     d <= -9000  -> transparent
//     -9000..-8000 -> fade in
//     d >= -8000  -> full ordinary opacity

static float clamp01(float x)
{
    return std::max(0.0f, std::min(1.0f, x));
}

int tapHorizonOpacity(float depth)
{
    return static_cast<int>(
        clamp01((depth + 9000.0f) / 1000.0f) * 255.0f);
}

// A render-side byte around +0x2B4 selects an additional multiplier:
//
//     clamp((-depth) / 8700, 0, 1)
//
// Exact semantic trigger/name of this byte is UNRESOLVED. Preserve it as a
// conditional presentation factor rather than inventing a game-design label.

float optionalTapNearFactor(float depth)
{
    return clamp01((-depth) / 8700.0f);
}

// hidegroup ultimately forces presentation opacity to zero. It is distinct from
// LogicNote +0x54/noinput, which is an input gate.

int applyHideGroupToOpacity(int opacity, bool hiddenByGroup)
{
    return hiddenByGroup ? 0 : opacity;
}

// ============================================================================
// 5. CONFIRMED: Tap textured quad deforms with approach depth
// ============================================================================

// The Tap child is not merely translated. Its local textured-quad geometry is
// rewritten while approaching.
//
// Directly rechecked arithmetic:
//
//   speedFactor = clamp(effectiveSpatialBpm / 400, 1, 1.5)
//   approach    = (-depth / 10000) * speedFactor
//   longitudinalExtent ~= int(200 + 340 * approach)
//   local half-width = 191
//
// The exact original member/vertex names are unavailable. The safe statement is
// that the floor-note quad's local longitudinal geometry changes with approach
// depth and effective spatial BPM; this is presentation, not judgement.

float tapApproachFactor(float depth, float effectiveSpatialBpm)
{
    const float speed = std::clamp(
        effectiveSpatialBpm / 400.0f,
        1.0f,
        1.5f);

    return (-depth / 10000.0f) * speed;
}

int tapQuadLongitudinalExtent(float depth, float effectiveSpatialBpm)
{
    return static_cast<int>(
        200.0f + 340.0f * tapApproachFactor(depth, effectiveSpatialBpm));
}

static constexpr float kTapQuadHalfWidth = 191.0f;

// ============================================================================
// 6. CONFIRMED: Hold rendering is a stretched body, not tick sprites
// ============================================================================

// Normal body assets:
//     img/note_hold.png
//     img/note_hold_dark.png
//     img/note_hold_tomato.png
//
// Contact/highlight assets:
//     img/note_hold_hi.png
//     img/note_hold_dark_hi.png
//     img/note_hold_hi_tomato.png
//
// The Hold renderer stretches one body between current/start and end depth.
// Horizontal placement uses the same NotePosition lane model as Tap.
// The transient LogicHoldNote +0x64 current-contact latch selects highlight
// presentation. It does not manufacture a separate sprite for every tick.

const char* chooseHoldTexture(bool contact, HoldStyle style)
{
    if (contact)
        return highlightedHoldTextureFor(style);
    return normalHoldTextureFor(style);
}

// ============================================================================
// 7. CONFIRMED: fadingholds is render feedback, not judgement timing
// ============================================================================

// The source timinggroup flag reaches LogicHoldNote. When disabled or current
// contact exists, intensity is 1.0.
//
// For a fading Hold after start while contact is absent:
//   - if the most recent long-event batch was LOST, intensity = 0.5
//   - after a successful batch, locate the next unprocessed timing point:
//       horizon = nextTick.time + 2*tickInterval
//       intensity = clamp(0.5 + (horizon-now)/500, 0.5, 1.0)
//
// This factor multiplies ordinary opacity. It visualises long-note contact/event
// health but does not change the event timestamps or ScoreState rules.

float fadingHoldIntensity(
    const LogicHoldNote& hold,
    int nowMs)
{
    if (!hold.fadingHolds || hold.currentContact)
        return 1.0f;

    if (nowMs < hold.startTime)
        return 1.0f;

    if (!hold.lastLongEventBatchSucceeded)
        return 0.5f;

    const LongTickEvent* next = firstUnprocessedTick(hold);
    if (!next)
        return 1.0f;

    const float horizon =
        static_cast<float>(next->timeMs)
        + 2.0f * hold.tickIntervalMs;

    return std::clamp(
        0.5f + (horizon - nowMs) / 500.0f,
        0.5f,
        1.0f);
}

// ============================================================================
// 8. CONFIRMED: ArcTap is rendered as a small 3D model
// ============================================================================

// Surviving model assets include:
//     models/tap_l.obj
//     models/tap_d.obj
//     models/tap_tomato.obj
//     models/sfx_l.obj
//     models/sfx_d.obj
//
// Parent Arc path construction precomputes the child's gameplay/path point.
// RenderArcTapNote consumes that point. It is not the same floor-note sprite as
// RenderTapNote even though LogicArcTapNote inherits LogicTapNote.

// ============================================================================
// 9. CONFIRMED: Arc has TWO sampled polylines
// ============================================================================

struct LogicArcSelectedRenderFields {
    std::vector<Vec3> gameplaySamples; // actual vector begins around +0xE8
    std::vector<Vec3> renderSamples;   // actual vector begins around +0x100
    float renderSamplingMultiplier;    // +0x118, friendly name
};

// Both vectors tessellate the same analytic Arc/easing curve.
//
// +0xE8 is consumed by gameplay/contact expected-position solving.
// +0x100 is consumed by RenderArcNote.
//
// Base sampling density:
//     nominal duration < 1000ms -> 14
//     otherwise                 -> 7
//
// Let:
//     D = effective path duration in seconds
//     B = base density
//     M = max(chart Arc optional float, 1.0)
//
// Approximate native step families:
//     gameplayStep ~= 1 / (D * B)
//     renderStep   ~= 1 / (D * B * M)
//
// M therefore increases visible tessellation smoothness without retessellating
// the gameplay contact polyline.

float baseArcSamplingDensity(int durationMs)
{
    return durationMs < 1000 ? 14.0f : 7.0f;
}

// Connected continuations can extend the predecessor's effective sampling end,
// but membership already requires the seam gap to be <=9 ms; this is a tiny seam
// normalisation, not arbitrary path concatenation.

// ============================================================================
// 10. CONFIRMED: visible Arc body = ribbon segments
// ============================================================================

// RenderArcNote walks adjacent points from LogicArcNote +0x100 and creates one
// render segment per neighbouring sample pair.
//
// Each segment owns two endpoints and generates four corners around the pair.
// The visible Arc is therefore a ribbon/quad strip rather than a mathematical
// line primitive.
//
// Surviving assets include:
//     img/arc_body.png
//     img/arc_body_hi.png
//
// ArcSegment allocation is roughly 0x360 bytes in this build.

void rebuildArcRibbon(const LogicArcNote& arc)
{
    for (size_t i = 0; i + 1 < arc.renderSamples.size(); ++i) {
        createArcRibbonSegment(
            arc.renderSamples[i],
            arc.renderSamples[i + 1]);
    }
}

// ============================================================================
// 11. CONFIRMED: RenderArcNote child anatomy
// ============================================================================

// Selected scene-graph members established by independent consumers:
//
//     RenderArcNote
//       +0x2B8 : body-segment container
//       +0x2C0 : ArcTap renderer container
//       +0x2D0 : Arc cap sprite
//                  + optional approach arrow
//
// Dedicated assets:
//     img/1080/arc_cap.png
//     img/1080/approach_arrow.png
//
// The approach arrow uses scale around 1.5 and an observed order-like value 10.
// Exact original purpose/name of that order value is UNRESOLVED.
//
// Arc particles are separate emitter nodes/resources rather than ribbon pieces:
//     particle/particle_arc.plist         scale ~2.1
//     Mirai light/conflict variants       scale ~1.9

// ============================================================================
// 12. CONFIRMED: Arc angle metadata is a render transform
// ============================================================================

// Timinggroup integer angles are converted from tenths of a degree:
//
//     radians = raw * PI / 180 / 10
//
// Runtime fields:
//     LogicNote +0x58 -> angleX radians
//     LogicNote +0x5C -> angleY radians
//
// Arc rendering applies the Y-angle as a Y-axis matrix rotation and X-angle as
// an X-axis matrix rotation to the visual geometry.
//
// The common touch unprojection path does NOT apply these same note-angle
// matrices. Therefore anglex/angley are visual/spatial Arc transforms, not a
// rotation of the player's raw screen-touch ray.

// ============================================================================
// 13. CONFIRMED: Arc colour/opacity presentation modes
// ============================================================================

struct RGB8 { uint8_t r, g, b; };

static constexpr RGB8 kGoldTrace      = {244, 185, 66};
static constexpr RGB8 kDesignantPink  = {240,  41, 97};
static constexpr RGB8 kRejectRed      = {230,  50, 50};

// tracecol:
// The TimingGroup parser stores six hexadecimal RGB digits. In the render
// consumers traced in this build, presence of that metadata selects a dedicated
// gold trace branch and texture:
//     img/trace_body_gold.png
//     RGB(244,185,66)
//
// IMPORTANT LIMITATION:
// No traced consumer in this slice actually reads arbitrary stored R/G/B values
// for arbitrary custom colour output. Do not claim general RRGGBB rendering from
// the parser merely because the source data stores three channels.

// DESIGNANT / Arc body mode 2:
//     visual colour family RGB(240,41,97)
//     root opacity    ~= clamp(globalFactor * 125)
//     segment opacity ~= clamp(globalFactor * 225)
//     cap uses the same colour family

int designantRootOpacity(float factor)
{
    return std::clamp(static_cast<int>(factor * 125.0f), 0, 255);
}

int designantSegmentOpacity(float factor)
{
    return std::clamp(static_cast<int>(factor * 225.0f), 0, 255);
}

// LogicColor rejection feedback is presentation state separate from the actual
// touch-ownership lockout. While active, judged Arc presentation blends toward
// RGB(230,50,50).

// ============================================================================
// 14. CONFIRMED: common opacity interface and hidegroup
// ============================================================================

// Render-note virtuals around the established +0x460/+0x470 slots behave as
// opacity get/set across independent consumers.
//
// `hidegroup` writes LogicNote +0x55 and ArcTap children as well. Render updates
// consume this flag and suppress presentation. Input paths continue to use
// LogicNote +0x54 (`noinput`) instead.
//
// Thus:
//     noinput   -> player input eligibility
//     hidegroup -> visual visibility

// ============================================================================
// 15. CONFIRMED: track widening presentation does not move lane coordinates
// ============================================================================

// `enwidenlanes` activates pre-existing outer gameplay lane IDs 1 and 6. Lane
// centres remain fixed. Renderer resources include:
//     img/track_extralane_light.png
//     img/track_extralane_dark.png
//
// Track presentation can be reshaped/cut by native `track_custom_mask_shader`.
// Its uniforms include:
//     z_clip, texture_mask, mask_inverse, texture_height,
//     track_width, fixed_x_pos, model_width
//
// The shader calculates fragment position in track space and discards/makes
// transparent pixels based on the mask. This changes visible track artwork, not
// the already-confirmed floor-lane centres.

// ============================================================================
// 16. CONFIRMED: enwidencamera has renderer compensation
// ============================================================================

// Gameplay camera-widen state runs from 1.0 to 1.5. A presentation compensation
// factor is:
//
//     factor = 1 - (cameraWidenScale - 1) * (2/3)
//
// A traced Tap branch applies approximately factor*130 as opacity to auxiliary
// child nodes while leaving child index 0/main note presentation separate.
// This was previously easy to misread as geometry scaling; the consumer proves
// it is opacity compensation.

float cameraWidenPresentationFactor(float cameraWidenScale)
{
    return 1.0f
        - (cameraWidenScale - 1.0f) * (2.0f / 3.0f);
}

int cameraWidenTapAuxOpacity(float cameraWidenScale)
{
    return static_cast<int>(
        cameraWidenPresentationFactor(cameraWidenScale) * 130.0f);
}

// ============================================================================
// 17. CONFIRMED: generic gameplay SceneControl visual branches
// ============================================================================

enum class SceneControlType : int32_t {
    TrackDisplay  = 0,
    RedLine       = 1,
    ArcHVDistort  = 2,
    ArcHVDebris   = 3,
    HideGroup     = 4,
    EnwidenCamera = 5,
    EnwidenLanes  = 6,
    Unknown       = 7,
};

// Types 0..3 are prepared visual effects. hidegroup is group presentation state.
// Types 5/6 also affect gameplay-space/input state and were reconstructed in the
// runtime layer.

// trackdisplay:
//     current value starts at 255
//     100-step quadratic interpolation
//     p = step/100
//     value = start + (target-start)*p^2
//     scheduler interval = floatParameter/100
//     coordinated background darkening uses img/bg/bg_darken.png

int trackDisplayStep(int start, int target, int step)
{
    const float p = clamp01(static_cast<float>(step) / 100.0f);
    return static_cast<int>(
        start + (target - start) * p * p);
}

// redline:
//     creates a fresh img/redline.png sprite
//     fast fade-in/pulse animation
//     floatParameter controls lifetime before cleanup
//     runtime SceneControl instance ID gives unique scheduler key

// arcahvdistort:
//     prepared node name ARCAHV_DISTORT
//     resource img/bg/arcahv-srt.png
//     FadeTo(duration=floatParameter, opacity=low8(intParameter))

// arcahvdebris:
//     prepared node name ARCAHV_DEBRIS
//     resource img/bg/arcahv-debris.png
//     prepared animated child/container is faded as a whole

// ============================================================================
// 18. CONFIRMED: CameraController movement animation
// ============================================================================

// Retained type name:
//   CameraController::animateMovingCameraTo(cocos2d::Vec3, float)::$_0
//
// Direct disassembly resolves the method's core behaviour.
//
// CameraController owns two scene/camera-like nodes at approximately +0x2A8 and
// +0x2B0. Their exact high-level identities are UNRESOLVED, but the method moves
// BOTH to the same target Vec3.
//
// duration == 0:
//     set target position on both immediately.
//
// duration != 0:
//     scheduler key = "moveCamera"
//     stepCount ~= int(duration * 60)
//     interval  = duration / stepCount   (~1/60 second)
//
// The callback repeatedly reads each coordinate of the current position, then
// uses a quadratic progress value. Readable equivalent for each coordinate:
//
//     p = currentStep / totalSteps
//     next = current + (target - current) * p^2
//
// It applies the resulting Vec3 to both controller-owned nodes and increments
// the step counter.

float moveCameraCoordinate(float current, float target, float progress)
{
    return current + (target - current) * progress * progress;
}

void animateMovingCameraTo(
    CameraController& controller,
    Vec3 target,
    float duration)
{
    if (duration == 0.0f) {
        setPosition(controller.nodeA_2A8, target);
        setPosition(controller.nodeB_2B0, target);
        return;
    }

    const int totalSteps = static_cast<int>(duration * 60.0f);
    const float interval = duration / static_cast<float>(totalSteps);

    scheduleRepeated("moveCamera", interval, totalSteps,
        [&controller, target, step = 0, totalSteps](float /*dt*/) mutable {
            const float p =
                static_cast<float>(step) /
                static_cast<float>(totalSteps);

            const Vec3 current = positionOf(controller.nodeA_2A8);
            const Vec3 next {
                moveCameraCoordinate(current.x, target.x, p),
                moveCameraCoordinate(current.y, target.y, p),
                moveCameraCoordinate(current.z, target.z, p),
            };

            setPosition(controller.nodeA_2A8, next);
            setPosition(controller.nodeB_2B0, next);
            ++step;
        });
}

// The source CameraControl's second Vec3 remains strongly reconstructed as the
// separate orientation/rotation family, but this section does not invent an
// original method name for that path without equivalent retained-symbol proof.

// ============================================================================
// 19. CONFIRMED: GameScene render owns an explicit GL-state boundary
// ============================================================================

// A std::function/lambda whose retained C++ type is scoped directly inside:
//
//     GameScene::render(Renderer*, const Mat4&, const Mat4*)::$_6
//
// has a callback body that performs exactly:
//
//     glDisable(GL_DEPTH_TEST);     // 0x0B71
//     glDisable(GL_STENCIL_TEST);   // 0x0B90
//     glDepthMask(false);
//     glDisable(GL_BLEND);          // 0x0BE2
//
// Therefore the gameplay scene explicitly brackets/resets low-level GL state
// rather than allowing its 3D/depth/blend state to leak indefinitely into later
// Cocos rendering.
//
// UNRESOLVED:
// The exact position of this custom command relative to every other GameScene
// command has not been completely reconstructed, so this file does not claim a
// pixel-perfect global command ordering from that callback alone.

void gameSceneRenderStateResetCallback()
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDepthMask(false);
    glDisable(GL_BLEND);
}

// ============================================================================
// 20. RECONSTRUCTED complete gameplay-render pipeline
// ============================================================================

/*
 * Logic update has already calculated:
 *   - current note depth/path state
 *   - Hold/Arc contact state
 *   - Arc sampled paths/current point
 *   - hidden/group flags
 *   - LogicColor feedback state
 *   - SceneControl/widening state
 *
 *                         |
 *                         v
 *              LogicNote <-> RenderNote
 *                         |
 *          +--------------+------------------+
 *          |              |                  |
 *          v              v                  v
 *        Tap/Hold       Arc/ArcTap        Track/scene FX
 *          |              |                  |
 *   position/quad/    dense render      masks/fades/
 *   texture/alpha      samples ->        extra lanes/
 *                       ribbons          visual controls
 *          \              |                  /
 *           \             |                 /
 *                         v
 *                    Cocos scene graph
 *                         |
 *                         v
 *              CameraController transforms
 *                         |
 *                         v
 *                  cocos2d::Renderer
 *                         |
 *                         v
 *          GameScene custom render-state boundary
 */

// The critical rule is one-way responsibility:
//   logic decides where/what state the note is in;
//   renderer decides how that state is drawn.
//
// Renderer-side visual feedback never replaces ScoreState judgement.

// ============================================================================
// 21. UNRESOLVED / intentionally deferred
// ============================================================================

// These do NOT block the fundamental gameplay-render model:
//
// 1. Exact global z-order between every top-level Tap/Hold/Arc/track/effect node.
//    Internal Arc body/ArcTap/cap containers are confirmed, but a complete global
//    draw-order table was not recovered here.
// 2. Exact original identity/name of CameraController +0x2A8 and +0x2B0 nodes.
// 3. Exact original method/name for CameraControl's second Vec3 orientation path.
// 4. Exact original meaning of RenderTapNote byte +0x2B4 selecting the extra
//    near-depth opacity multiplier.
// 5. Whether any untraced native consumer uses arbitrary tracecol RGB values;
//    traced consumers select a fixed gold branch on metadata presence.
// 6. Pixel-perfect artistic action nesting for every scenecontrol/particle.
// 7. Full generic Cocos renderer internals unrelated to Arcaea gameplay.
//
// Those are implementation/polish details, not missing core rendering mechanics.

} // namespace reconstructed
