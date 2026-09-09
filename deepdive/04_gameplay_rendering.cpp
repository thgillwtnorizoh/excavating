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
//   UNRESOLVED    = exact original name/design reason is not proved.
//
// Scope: main gameplay rendering only. Gameplay HUD/UI is intentionally split
// into Deep Dive Section 05 because core gameplay and note rendering can operate
// without that overlay layer.

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
struct CameraController;

static float clamp01(float x)
{
    return std::max(0.0f, std::min(1.0f, x));
}

// ============================================================================
// 1. CONFIRMED: Logic* and Render* are separate reciprocal objects
// ============================================================================

// Central bridge:
//
//     LogicNote  +0x40  = RenderNote*
//     RenderNote +0x2A8 = LogicNote*
//
// Logic owns judgement/input/timing/path state.
// Render objects own Cocos nodes, sprites, models, textures, geometry and opacity.

struct LogicNoteRenderLink {
    RenderNote* renderer; // conceptual +0x40
};

struct RenderNoteLogicLink {
    LogicNote* logic;     // conceptual +0x2A8
};

// Approximate allocations in this build:
//     RenderTapNote     0x2D0
//     RenderHoldNote    0x2E0
//     RenderArcTapNote  0x2E0
//     RenderFlickNote   0x2D0
// RenderArcNote is larger and owns additional child containers/vectors.

// ============================================================================
// 2. CONFIRMED: central renderer factory and camera-mask partition
// ============================================================================

// The factory dispatches by LogicNote runtime type. Flick handling survives only
// as dormant downstream scaffolding because this build has no live LogicFlickNote
// producer/vtable.
//
// Important refinement from the later pass:
//     ordinary RenderTapNote uses camera mask 0x04
//     RenderArcTapNote uses camera mask 0x10
//
// Track/presentation setup also assigns selected children to masks 4 and 16.
// Therefore global gameplay presentation is first partitioned by camera mask,
// then ordered by the Cocos scene/render machinery inside each camera pass.

RenderNote* makeRenderer(LogicNote& note)
{
    if (auto* tap = dynamicCast<LogicTapNote>(&note)) {
        RenderTapNote* r = createRenderTapNote(*tap);
        r->setCameraMask(0x04, true);
        return r;
    }

    if (auto* hold = dynamicCast<LogicHoldNote>(&note)) {
        return createRenderHoldNote(*hold);
    }

    if (auto* flick = dynamicCast<LogicFlickNote>(&note)) {
        return createRenderFlickNote(*flick); // dormant producer side
    }

    if (auto* arc = dynamicCast<LogicArcNote>(&note)) {
        std::vector<RenderArcTapNote*> children;

        for (LogicArcTapNote* child : arc->arcTaps) {
            RenderArcTapNote* r = createRenderArcTapNote(*child, *arc);
            r->setCameraMask(0x10, true);
            child->renderer = r;
            children.push_back(r);
        }

        return createRenderArcNote(*arc, children);
    }

    return nullptr;
}

// CONFIRMED architecture:
//
//                    gameplay scene
//                          |
//              +-----------+-----------+
//              |                       |
//         camera mask 4           camera mask 16
//              |                       |
//        one render group         another render group
//
// Exact same-priority camera traversal order is not encoded as a separate
// Arcaea-specific depth/priority value in the recovered CameraController setup.

// ============================================================================
// 3. CONFIRMED: RenderNote consumes already-computed logic state
// ============================================================================

// Important LogicNote inputs consumed by renderers include:
//     +0x20 NotePosition* / horizontal descriptor
//     +0x30 current approach/depth state
//     +0x34 related end/depth state (important for Holds)
//     +0x48 active LogicTimingEvent*
//     +0x55 hidegroup presentation flag
//     +0x58 angleX radians
//     +0x5C angleY radians
//
// Renderers do not re-run judgement and do not replace ScoreState.

// ============================================================================
// 4. CONFIRMED: floor Tap rendering and horizon fade
// ============================================================================

// Assets:
//     img/note.png
//     img/note_dark.png
//     img/note_tomato.png
//
// Discrete lane X:
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

// Ordinary far-distance fade:
//
//     horizon = clamp((depth + 9000) / 1000, 0, 1)
//
//     depth <= -9000   -> transparent
//     -9000..-8000     -> fade in
//     depth >= -8000   -> full ordinary opacity

int tapHorizonOpacity(float depth)
{
    return static_cast<int>(
        clamp01((depth + 9000.0f) / 1000.0f) * 255.0f);
}

// ============================================================================
// 5. CONFIRMED: RenderTapNote +0x2B4 = special approach fade behaviour
// ============================================================================

// Later excavation resolves the old anonymous byte around RenderTapNote +0x2B4.
// The factory builds precomputed chart-time ranges. A Tap receives the byte when:
//
//     range.start <= tap.timestamp <= range.end
//
// In the observed special mode 6, the range helper contains hard-coded song-ID
// tables including:
//
//     arghena
//     cataclysmcry
//     rivenpilgrim
//     un
//
// Another observed mode value 1 creates a broad range beginning at zero and
// ending at a session/chart-derived endpoint.
//
// The exact original name of the higher-level mode enum remains UNRESOLVED, but
// the render-byte effect itself is CONFIRMED.
//
// When enabled, final Tap opacity receives an additional factor:
//
//     nearFactor = clamp((-depth) / 8700, 0, 1)
//
// This makes the Tap progressively disappear as it approaches judgement depth.

struct TimeRange {
    int32_t startMs;
    int32_t endMs;
};

bool tapFallsInsideSpecialPresentationRange(
    int32_t tapTimeMs,
    const std::vector<TimeRange>& ranges)
{
    for (const TimeRange& range : ranges) {
        if (range.startMs <= tapTimeMs && tapTimeMs <= range.endMs)
            return true;
    }
    return false;
}

float tapApproachVanishFactor(float depth)
{
    return clamp01((-depth) / 8700.0f);
}

int specialTapOpacity(float depth, bool fadeOutOnApproach)
{
    float alpha = clamp01((depth + 9000.0f) / 1000.0f);

    if (fadeOutOnApproach)
        alpha *= tapApproachVanishFactor(depth);

    return static_cast<int>(alpha * 255.0f);
}

// `fadeOutOnApproach` is a RECONSTRUCTED friendly name for the CONFIRMED effect.

// hidegroup forces presentation opacity to zero. It is distinct from
// LogicNote +0x54/noinput, which is an input gate.

int applyHideGroupToOpacity(int opacity, bool hiddenByGroup)
{
    return hiddenByGroup ? 0 : opacity;
}

// ============================================================================
// 6. CONFIRMED: Tap textured quad deforms with approach depth
// ============================================================================

// The Tap child is not merely translated. Its textured-quad geometry changes:
//
//   speedFactor = clamp(effectiveSpatialBpm / 400, 1, 1.5)
//   approach    = (-depth / 10000) * speedFactor
//   longitudinalExtent ~= int(200 + 340 * approach)
//   local half-width = 191

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
// 7. CONFIRMED: Hold rendering is one stretched body
// ============================================================================

// Normal:
//     img/note_hold.png
//     img/note_hold_dark.png
//     img/note_hold_tomato.png
//
// Contact/highlight:
//     img/note_hold_hi.png
//     img/note_hold_dark_hi.png
//     img/note_hold_hi_tomato.png
//
// The Hold renderer stretches one body between current/start and end depth.
// LogicHoldNote +0x64 current-contact selects highlight presentation.

const char* chooseHoldTexture(bool contact, HoldStyle style)
{
    return contact
        ? highlightedHoldTextureFor(style)
        : normalHoldTextureFor(style);
}

// ============================================================================
// 8. CONFIRMED: fadingholds is visual feedback, not judgement timing
// ============================================================================

float fadingHoldIntensity(const LogicHoldNote& hold, int nowMs)
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
// 9. CONFIRMED: ArcTap is a separate 3D-model renderer
// ============================================================================

// Resources include:
//     models/tap_l.obj
//     models/tap_d.obj
//     models/tap_tomato.obj
//     models/sfx_l.obj
//     models/sfx_d.obj
//
// Parent Arc path construction precomputes the child point. RenderArcTapNote
// consumes that point and uses camera mask 16.

// ============================================================================
// 10. CONFIRMED: Arc has separate gameplay and render sampled paths
// ============================================================================

struct LogicArcSelectedRenderFields {
    std::vector<Vec3> gameplaySamples; // actual vector around +0xE8
    std::vector<Vec3> renderSamples;   // actual vector around +0x100
    float renderSamplingMultiplier;    // +0x118, friendly name
};

// Both sample the same analytic Arc.
//
//     +0xE8  -> gameplay/contact expected-position solver
//     +0x100 -> RenderArcNote tessellation
//
// Base density:
//     nominal duration < 1000ms -> 14
//     otherwise                 -> 7
//
// With D=effective duration seconds, B=base density,
// M=max(chart optional Arc float,1):
//
//     gameplayStep ~= 1 / (D*B)
//     renderStep   ~= 1 / (D*B*M)
//
// M increases visual smoothness without changing gameplay contact sampling.

float baseArcSamplingDensity(int durationMs)
{
    return durationMs < 1000 ? 14.0f : 7.0f;
}

// ============================================================================
// 11. CONFIRMED: Arc visible body is a ribbon assembled from segments
// ============================================================================

// RenderArcNote walks adjacent +0x100 samples. Each pair creates one ArcSegment
// (~0x360 bytes) whose geometry forms four corners around the endpoints.
//
// Resources include:
//     img/arc_body.png
//     img/arc_body_hi.png

void rebuildArcRibbon(const LogicArcNote& arc)
{
    for (size_t i = 0; i + 1 < arc.renderSamples.size(); ++i) {
        createArcRibbonSegment(
            arc.renderSamples[i],
            arc.renderSamples[i + 1]);
    }
}

// ============================================================================
// 12. CONFIRMED: RenderArcNote is a small scene graph
// ============================================================================

// Selected fields:
//
//     +0x2B8 body-segment container
//     +0x2C0 ArcTap renderer container
//     +0x2D0 Arc cap sprite
//                 + optional approach arrow
//
// Assets:
//     img/1080/arc_cap.png
//     img/1080/approach_arrow.png
//
// Approach arrow scale ~1.5. An observed order-like integer 10 remains without
// a recovered original semantic name.
//
// Arc particles are separate emitter nodes:
//     particle/particle_arc.plist                     scale ~2.1
//     particle_arc_mirai_light/conflict variants     scale ~1.9

// ============================================================================
// 13. CONFIRMED: timinggroup anglex/angley rotate visual Arc geometry
// ============================================================================

// Timinggroup integer units are tenths of a degree:
//
//     radians = raw * PI / 180 / 10
//
// Runtime:
//     LogicNote +0x58 = angleX radians -> X-axis rotation matrix
//     LogicNote +0x5C = angleY radians -> Y-axis rotation matrix
//
// These matrices transform Arc render geometry downstream of ordinary touch-ray
// construction. They do not rotate the common screen->world input ray.

// ============================================================================
// 14. CONFIRMED: Arc colour/opacity presentation
// ============================================================================

struct RGB8 { uint8_t r, g, b; };

static constexpr RGB8 kGoldTrace     = {244, 185, 66};
static constexpr RGB8 kDesignantPink = {240,  41, 97};
static constexpr RGB8 kRejectRed     = {230,  50, 50};

// tracecolRRGGBB is parsed and propagated into mode-1 Arc metadata. In the traced
// render consumer for this build, metadata presence selects a dedicated gold
// trace branch using img/trace_body_gold.png / RGB(244,185,66). No traced render
// consumer here reads arbitrary stored R/G/B to produce arbitrary custom tint.

int designantRootOpacity(float factor)
{
    return std::clamp(static_cast<int>(factor * 125.0f), 0, 255);
}

int designantSegmentOpacity(float factor)
{
    return std::clamp(static_cast<int>(factor * 225.0f), 0, 255);
}

// DESIGNANT mode-2 uses RGB(240,41,97), with separate root and ribbon opacity.
// LogicColor rejection feedback is separate presentation state and blends judged
// Arc presentation toward RGB(230,50,50).

// ============================================================================
// 15. CONFIRMED: hidegroup/noinput separation
// ============================================================================

// Common render virtuals around the observed +0x460/+0x470 slots behave as
// opacity get/set.
//
//     LogicNote +0x54 / noinput   -> player-input eligibility
//     LogicNote +0x55 / hidegroup -> presentation visibility

// ============================================================================
// 16. CONFIRMED: track widening/masking are presentation over fixed lanes
// ============================================================================

// `enwidenlanes` activates pre-existing gameplay lanes 1 and 6. Lane centres do
// not move. Renderer assets include:
//     img/track_extralane_light.png
//     img/track_extralane_dark.png
//
// `track_custom_mask_shader` includes uniforms:
//     z_clip, texture_mask, mask_inverse, texture_height,
//     track_width, fixed_x_pos, model_width
//
// It clips visible track fragments without rewriting gameplay lane geometry.

// ============================================================================
// 17. CONFIRMED: enwidencamera presentation compensation
// ============================================================================

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

// This factor is applied as opacity to auxiliary Tap children (starting at child
// index 1); it is not a lane-coordinate or note-position rescale.

// ============================================================================
// 18. CONFIRMED: generic SceneControl presentation branches
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

int trackDisplayStep(int start, int target, int step)
{
    const float p = clamp01(static_cast<float>(step) / 100.0f);
    return static_cast<int>(start + (target - start) * p * p);
}

// TrackDisplay: 100-step quadratic opacity/presentation transition; background
// darkening uses img/bg/bg_darken.png.
// RedLine: fresh img/redline.png sprite, pulse, timed removal.
// ArcHVDistort: prepared ARCAHV_DISTORT / img/bg/arcahv-srt.png FadeTo.
// ArcHVDebris: prepared ARCAHV_DEBRIS / img/bg/arcahv-debris.png FadeTo.

// ============================================================================
// 19. CONFIRMED: CameraController contains two synchronized cocos2d::Camera's
// ============================================================================

// Later disassembly resolves the old anonymous +0x2A8/+0x2B0 nodes.
// CameraController derives from cocos2d::Node and creates two actual Camera
// children.
//
//     +0x2A8 Camera*, camera flag 4,  near=1, far=10000
//     +0x2B0 Camera*, camera flag 16, near=1, far=9000
//
//     +0x2B8 shared look-at target Vec3
//     +0x2C4 shared camera-position Vec3
//
// Both cameras receive the same position and look-at target/up vector. They are
// synchronized viewpoints serving different camera-mask render groups.

struct CameraControllerSelected {
    cocos2d::Camera* camera4;   // +0x2A8
    cocos2d::Camera* camera16;  // +0x2B0
    Vec3 lookAtTarget;          // +0x2B8
    Vec3 cameraPosition;        // +0x2C4
};

float moveCameraCoordinate(float current, float target, float progress)
{
    return current + (target - current) * progress * progress;
}

void animateMovingCameraTo(
    CameraControllerSelected& controller,
    Vec3 target,
    float duration)
{
    if (duration == 0.0f) {
        controller.camera4->setPosition3D(target);
        controller.camera16->setPosition3D(target);
        controller.cameraPosition = target;
        return;
    }

    const int totalSteps = static_cast<int>(duration * 60.0f);
    const float interval = duration / static_cast<float>(totalSteps);

    scheduleRepeated("moveCamera", interval, totalSteps,
        [&controller, target, step = 0, totalSteps](float /*dt*/) mutable {
            const float p =
                static_cast<float>(step) /
                static_cast<float>(totalSteps);

            const Vec3 current = controller.cameraPosition;
            const Vec3 next {
                moveCameraCoordinate(current.x, target.x, p),
                moveCameraCoordinate(current.y, target.y, p),
                moveCameraCoordinate(current.z, target.z, p),
            };

            controller.cameraPosition = next;
            controller.camera4->setPosition3D(next);
            controller.camera16->setPosition3D(next);
            ++step;
        });
}

// No separate Arcaea-specific camera-depth/priority write was found after camera
// creation. Their gameplay-facing distinction is camera flag/mask and far plane.

// ============================================================================
// 20. CONFIRMED: GameScene::render queues an explicit GL-state boundary
// ============================================================================

// Retained lambda type:
//
//     GameScene::render(Renderer*, const Mat4&, const Mat4*)::$_6
//
// Callback body:
//
//     glDisable(GL_DEPTH_TEST);     // 0x0B71
//     glDisable(GL_STENCIL_TEST);   // 0x0B90
//     glDepthMask(false);
//     glDisable(GL_BLEND);          // 0x0BE2
//
// The later refinement traces the owning cocos2d::CustomCommand:
//
//     global order = 0
//     non-3D command flags
//
// GameScene first allows normal scene traversal to queue gameplay draw commands,
// then adds this CustomCommand to the renderer. Cocos classifies it into the
// zero-global-order 2D command queue. Thus this is a real queued render boundary,
// not merely cleanup performed after Renderer has already finished.
//
// High-level ordering family:
//
//     negative global-order commands
//     opaque 3D
//     transparent 3D
//     zero global-order commands  <-- GameScene cleanup command lives here
//     positive global-order commands
//
// This cleanly prevents gameplay 3D/depth/stencil/blend state from leaking into
// later positive-order presentation/UI commands.

void gameSceneRenderStateResetCallback()
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDepthMask(false);
    glDisable(GL_BLEND);
}

void queueGameSceneRenderStateBoundary(
    cocos2d::Renderer& renderer,
    const cocos2d::Mat4& transform)
{
    cocos2d::CustomCommand command;
    command.init(0.0f, transform, 0); // reconstructed signature shape
    command.func = gameSceneRenderStateResetCallback;
    renderer.addCommand(&command);
}

// ============================================================================
// 21. RECONSTRUCTED complete gameplay-render pipeline
// ============================================================================

/*
 * Logic update computes positions, contact, paths and visibility.
 *
 *                LogicNote <-> RenderNote
 *                           |
 *              camera-mask assignment
 *                   /               \
 *                  v                 v
 *              mask 4            mask 16
 *                  \                 /
 *                   \               /
 *                    v             v
 *                synchronized CameraController
 *                  Camera4 / Camera16
 *                           |
 *                    Cocos scene traversal
 *                           |
 *                   gameplay draw commands
 *                           |
 *                 zero-order cleanup command
 *                           |
 *            depth/stencil/depth-write/blend disabled
 *                           |
 *              later positive-order presentation/UI
 *
 * Separately inside RenderNote families:
 *   Tap  -> position, deforming quad, horizon/special fade
 *   Hold -> stretched body + contact/fadingholds presentation
 *   Arc  -> dense render samples -> ribbon segment scene graph
 *   ArcTap -> separate 3D child model on camera-mask 16
 *   Track/SceneControl -> extra lanes, masks, fades and effect nodes
 */

// Critical responsibility rule:
//     Logic decides where a gameplay object is and what state it is in.
//     Renderer decides how that state is presented.
//
// Renderer-side effects never replace ScoreState judgement.

// ============================================================================
// 22. Remaining narrow unresolved details
// ============================================================================

// These do NOT block the gameplay-render architecture:
//
// 1. Exact original name of the higher-level render/special mode enum whose
//    observed values 1 and 6 build Tap fade-out time ranges.
// 2. Exact original method/name for CameraControl's second Vec3 orientation path.
// 3. Whether an untraced consumer displays arbitrary tracecol RRGGBB rather than
//    using metadata presence to choose the confirmed gold branch.
// 4. Pixel-perfect artistic Cocos Action nesting and generic renderer internals.
// 5. Same-priority Camera 4 vs Camera 16 traversal details belong to Cocos unless
//    a later Arcaea-specific camera priority write is discovered.
//
// Gameplay HUD/UI is NOT an unresolved part of this section. It is intentionally
// separated into Deep Dive Section 05 because the gameplay simulation and note
// renderer do not require the HUD overlay to function.

} // namespace reconstructed
