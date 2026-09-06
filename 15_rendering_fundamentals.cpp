/*
 * Arcaea excavation notebook
 * Section 15: RenderNote architecture and note rendering fundamentals
 *
 * STATUS: note rendering fundamentals slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native code/data/control flow.
 *   RECONSTRUCTED = readable representation assembled from confirmed behaviour.
 *   UNRESOLVED    = exact semantic name/purpose is not yet proved.
 *
 * Scope of this section:
 *   - RenderNote class family and LogicNote <-> RenderNote pairing
 *   - central factory which constructs note-specific render objects
 *   - basic Tap / Hold / Flick presentation models
 *   - ArcTap 3D-model presentation and parent-Arc context
 *   - Arc render tessellation from LogicArcNote +0x100
 *   - Arc segment/ribbon construction at a structural level
 *   - exact timinggroup anglex/angley unit conversion and render-axis use
 *   - where already-computed gameplay position/depth enters rendering
 *
 * Deliberately out of scope:
 *   - full shader/material implementation
 *   - every alpha/fade curve and visibility option
 *   - detailed Arc particle/cap effects
 *   - exact LogicFlickNote input AABB construction
 *   - special song/anomaly rendering systems
 *
 * This file builds directly on:
 *   05_arc_path.cpp
 *   08_lane_geometry.cpp
 *   10_timinggroups.cpp
 *   11_gameplay_space.cpp
 *   13_arc_path_refinements.cpp
 *   14_arc_mode_designant.cpp
 *
 * Useful native anchors from the investigated ARM64 build:
 *   central LogicNote -> RenderNote factory             ~0x0AE68A0
 *   common RenderNote logic getter                      ~0x0D5DC48
 *   RenderTapNote init / update                         ~0x0D7E290 / 0x0F20C28
 *   RenderHoldNote init / body update                   ~0x1720D18 / 0x13EA188
 *   RenderFlickNote init / update                       ~0x1315A90 / 0x0CFC560
 *   RenderArcTapNote init / update                      ~0x15B82BC / 0x1073160
 *   RenderArcNote init / update                         ~0x13B5FF8 / 0x11B7D78
 *   Arc render-segment factory                          ~0x14F3F40
 *   Arc render-segment initializer                      ~0x16A7E5C
 *   Arc segment geometry updater                        ~0x1873CFC
 *   Arc timinggroup-angle transform                     ~0x1893F3C
 *   Y-axis rotation helper                              ~0x134FC8C
 *   X-axis rotation helper                              ~0x1653A54
 *
 * WARNING ABOUT SYMBOLS:
 * The binary is stripped. Readable class/member/function names below are
 * reconstruction names unless explicitly described as surviving RTTI/type data
 * or as confirmed field behaviour.
 */

#include <cmath>
#include <cstdint>
#include <vector>

// -----------------------------------------------------------------------------
// 1. Logic and rendering are two separate object families
// -----------------------------------------------------------------------------

/*
 * CONFIRMED surviving RTTI:
 *
 *   RenderNote
 *      |
 *      +-- RenderTapNote
 *      +-- RenderHoldNote
 *      +-- RenderArcNote
 *      +-- RenderArcTapNote
 *      +-- RenderFlickNote
 *
 * This mirrors, but is separate from, the LogicNote hierarchy established in
 * Section 02.
 *
 * The central render factory stores a reciprocal pair:
 *
 *   LogicNote  +0x40 = RenderNote*
 *   RenderNote +0x2A8 = LogicNote*
 *
 * A common virtual method around ~0x0D5DC48 simply returns RenderNote +0x2A8.
 *
 * Therefore gameplay state and presentation are deliberately split. The logic
 * object owns judgement/input/path state; the render object consumes that state
 * and owns Cocos scene nodes, sprites, models, textures and mesh pieces.
 */
struct LogicNote;
struct RenderNote;

struct LogicNoteSelectedRenderFields {
    RenderNote* renderer; // actual common field +0x40

    // ...

    float angleXRad;      // +0x58, refined in section 11 below
    float angleYRad;      // +0x5C
};

struct RenderNoteSelectedFields {
    // Cocos/scene-node base state omitted.

    int32_t renderedX;    // common render-side working field around +0x2A0
    int32_t renderedY;    // common render-side working field around +0x2A4
    LogicNote* logic;     // +0x2A8, CONFIRMED
};

// -----------------------------------------------------------------------------
// 2. Central render factory
// -----------------------------------------------------------------------------

/*
 * CONFIRMED factory structure around ~0x0AE68A0.
 *
 * It iterates the top-level LogicNote list and dispatches by native RTTI/type:
 *
 *   LogicTapNote   -> RenderTapNote
 *   LogicHoldNote  -> RenderHoldNote
 *   LogicFlickNote -> RenderFlickNote
 *   LogicArcNote   -> RenderArcNote plus child RenderArcTapNotes
 *
 * A separate LogicBar -> RenderBar branch also exists, but bars are not part of
 * this note-family slice.
 *
 * After successful construction:
 *   - the renderer is stored into LogicNote +0x40
 *   - the render object already contains the reciprocal LogicNote pointer
 *   - the renderer is registered/attached to the gameplay scene controller
 */
RenderNote* createRendererForLogicNote(LogicNote& note)
{
    if (auto* tap = dynamicCast<LogicTapNote>(&note)) {
        return createRenderTapNote(*tap);
    }

    if (auto* hold = dynamicCast<LogicHoldNote>(&note)) {
        return createRenderHoldNote(*hold);
    }

    if (auto* flick = dynamicCast<LogicFlickNote>(&note)) {
        return createRenderFlickNote(*flick);
    }

    if (auto* arc = dynamicCast<LogicArcNote>(&note)) {
        std::vector<RenderArcTapNote*> renderedArcTaps;

        for (LogicArcTapNote* child : arc->arcTaps) {
            RenderArcTapNote* renderedChild =
                createRenderArcTapNote(*child, *arc);

            child->renderer = renderedChild;
            renderedArcTaps.push_back(renderedChild);
        }

        return createRenderArcNote(*arc, renderedArcTaps);
    }

    return nullptr;
}

/*
 * RECONSTRUCTED function/type names above describe confirmed dispatch/control
 * flow. Exact allocation sizes in this build are approximately:
 *
 *   RenderTapNote     0x2D0
 *   RenderHoldNote    0x2E0
 *   RenderArcTapNote  0x2E0
 *   RenderFlickNote   0x2D0
 *
 * RenderArcNote has additional vectors/nodes and is constructed through its own
 * helper rather than the simple direct branch used by point/floor notes.
 */

// -----------------------------------------------------------------------------
// 3. Renderers consume gameplay position; they do not re-run judgement
// -----------------------------------------------------------------------------

/*
 * CONFIRMED common pattern across Tap/Hold/Flick/Arc-family updates:
 *
 *   RenderNote -> get LogicNote
 *              -> read already-computed spatial state
 *              -> update scene-node position/scale/opacity/geometry
 *
 * Important established logic-side spatial inputs include:
 *
 *   LogicNote +0x20 : NotePosition* horizontal descriptor
 *   LogicNote +0x30 : current track-depth / approach-depth state used as Z
 *
 * Holds additionally use a second depth/end state around +0x34 to determine the
 * visible body span.
 *
 * Arcs additionally use:
 *
 *   LogicArcNote +0xD4/+0xD8 : current expected X/Y
 *   LogicArcNote +0x100       : dense render tessellation path
 *
 * None of these renderer paths replaces ScoreState judgement or the input
 * qualification logic reconstructed in earlier sections.
 */

// -----------------------------------------------------------------------------
// 4. Floor Tap: one sprite at NotePosition X and current depth Z
// -----------------------------------------------------------------------------

/*
 * CONFIRMED RenderTapNote assets include:
 *
 *   img/note.png
 *   img/note_dark.png
 *   img/note_tomato.png
 *
 * The initializer creates a sprite variant chosen from the supplied style.
 *
 * The per-frame update resolves X through the same NotePosition model proven in
 * Section 08. For discrete lanes this is the fixed six-slot geometry:
 *
 *   X ~= (internalLaneId - 1) * 425 - 1063
 *
 * The render object uses a small constant ground-height offset of 4 and reads
 * LogicNote +0x30 as the scene Z/depth component.
 */
struct Vec3 {
    float x;
    float y;
    float z;
};

Vec3 renderTapPosition(const LogicTapNote& tap)
{
    return {
        resolveNotePositionX(*tap.position),
        4.0f,
        static_cast<float>(tap.currentDepth30),
    };
}

/*
 * Timing/approach state also drives ordinary visibility/opacity/scale behavior.
 * Those curves are presentation details and are deliberately not promoted into
 * this fundamental slice.
 */

// -----------------------------------------------------------------------------
// 5. Hold: one stretchable body sprite with normal/highlight texture swapping
// -----------------------------------------------------------------------------

/*
 * CONFIRMED RenderHoldNote assets:
 *
 *   normal body:
 *     img/note_hold.png
 *     img/note_hold_dark.png
 *     img/note_hold_tomato.png
 *
 *   contacted/highlight texture:
 *     img/note_hold_hi.png
 *     img/note_hold_dark_hi.png
 *     img/note_hold_hi_tomato.png
 *
 * Important correction over an early visual guess:
 * The renderer does NOT need two permanently stacked body sprites.
 *
 * Native initialization creates the body sprite at approximately +0x2B8, then
 * caches texture objects for the ordinary and highlighted variants around
 * +0x2C8/+0x2D0. The update switches the body sprite's texture according to the
 * Hold's current contact state.
 *
 * LogicHoldNote +0x64, already established as current qualified contact, selects
 * the highlighted contact presentation.
 */
struct RenderHoldState {
    Sprite* bodySprite;        // approx +0x2B8
    Texture* normalTexture;    // approx +0x2C8
    Texture* contactTexture;   // approx +0x2D0
};

void selectHoldBodyTexture(
    RenderHoldState& render,
    const LogicHoldNote& hold)
{
    render.bodySprite->setTexture(
        hold.currentContact64
            ? render.contactTexture
            : render.normalTexture);
}

/*
 * CONFIRMED geometry/update structure:
 *
 *   - horizontal position comes from the Hold's NotePosition
 *   - renderer ground-height offset is approximately 3
 *   - current/start/end depth state determines a longitudinal span
 *   - the body sprite is scaled along its long axis to fill that span
 *   - the renderer root is positioned at the chosen visible span endpoint
 *
 * Thus a Hold is fundamentally a stretchable floor strip, not a sequence of
 * independently rendered tick sprites.
 */

// -----------------------------------------------------------------------------
// 6. `fadingholds` enters rendering, not judgement
// -----------------------------------------------------------------------------

/*
 * Section 10 established:
 *
 *   timinggroup `fadingholds`
 *       -> LogicHoldNote +0xA9
 *
 * CONFIRMED in RenderHoldNote's presentation update around ~0x13EA188:
 * +0xA9 changes the branch used when the Hold is not currently contacted.
 * The fading path derives a changing intensity/opacity factor from current time
 * and long-note state, including 500 ms-scale interpolation in this routine,
 * whereas the ordinary path uses simpler fixed/full-or-reduced intensity states.
 *
 * Exact aesthetic fade curves and the semantic names of the nearby +0x99/+0x66
 * state are outside this section. The architectural conclusion is firm:
 * `fadingholds` is a renderer/presentation behavior and does not modify Hold
 * judgement timestamps or tick spacing.
 */

// -----------------------------------------------------------------------------
// 7. Flick: directional arrow sprite
// -----------------------------------------------------------------------------

/*
 * CONFIRMED RenderFlickNote asset:
 *
 *   img/arr.png
 *
 * The initializer reads the Flick's required gesture direction:
 *
 *   LogicFlickNote +0x74/+0x78
 *
 * and calculates the visible arrow orientation from that vector using vector
 * length/dot/acos-style angle arithmetic plus a sign selected from horizontal
 * direction.
 *
 * This is presentation of the same required direction used by the Section 07
 * gesture recognizer; it does not perform gesture recognition itself.
 *
 * Its per-frame position uses:
 *   - horizontal NotePosition/free-X state
 *   - Flick vertical position state around LogicFlickNote +0x70
 *   - LogicNote +0x30 as current depth Z
 */
void orientFlickArrow(RenderFlickNote& render, const LogicFlickNote& flick)
{
    const float angle =
        angleOfDirectionVector(flick.requiredDirection);

    render.arrowSprite->setRotation(angle);
}

// -----------------------------------------------------------------------------
// 8. ArcTap: a separate 3D-model renderer created with parent Arc context
// -----------------------------------------------------------------------------

/*
 * CONFIRMED Arc construction order:
 * Parent RenderArcNote creation first walks LogicArcNote +0x120, creates a
 * RenderArcTapNote for every LogicArcTapNote child, stores that child renderer at
 * LogicArcTapNote +0x40, and collects the renderers into a vector supplied to the
 * parent RenderArcNote constructor.
 *
 * Therefore ArcTap presentation can depend on parent-Arc mode/style without
 * duplicating that presentation state into the LogicArcTapNote judgement model.
 *
 * CONFIRMED model resources include:
 *
 *   models/tap_l.obj
 *   models/tap_d.obj
 *   models/tap_tomato.obj
 *   models/sfx_l.obj
 *   models/sfx_d.obj
 *
 * Different parent/child style flags choose between these model variants and
 * their scales. ArcTap is therefore rendered as a small 3D model, not as the
 * same 2D floor sprite used by RenderTapNote.
 */

// -----------------------------------------------------------------------------
// 9. Arc: dense render path -> one render-segment object per adjacent pair
// -----------------------------------------------------------------------------

/*
 * Section 13 proved:
 *
 *   LogicArcNote +0xE8  = gameplay/contact sampled path
 *   LogicArcNote +0x100 = denser render sampled path
 *
 * Section 15 closes the presentation consumer.
 *
 * RenderArcNote initialization walks consecutive 12-byte Vec3 samples from
 * LogicArcNote +0x100. For every adjacent pair it calls a dedicated factory
 * around ~0x14F3F40 which allocates an approximately 0x360-byte render-segment
 * object and initializes it with:
 *
 *   - sample A
 *   - sample B
 *   - parent LogicArcNote
 *   - style/body-mode flags
 *   - endpoint/first/last-segment flags
 *
 * Those objects are stored in a RenderArcNote vector around +0x2E8/+0x2F0.
 */
struct ArcRenderSegment;

void buildVisibleArcSegments(
    RenderArcNote& render,
    LogicArcNote& arc)
{
    const auto& path = arc.renderPath100;

    for (size_t i = 0; i + 1 < path.size(); ++i) {
        render.segments.push_back(
            createArcRenderSegment(
                path[i],
                path[i + 1],
                arc,
                i == 0,
                i + 2 == path.size()));
    }
}

/*
 * This is the physical destination of Arc smoothness (+0x118): more render-path
 * samples create more, shorter visible segment objects, while the gameplay
 * contact path remains at its independent density.
 */

// -----------------------------------------------------------------------------
// 10. Arc segment presentation is ribbon/quad geometry, not a line primitive
// -----------------------------------------------------------------------------

/*
 * CONFIRMED segment state/geometry observations:
 *
 *   - segment initializer stores the two endpoints in render-side Vec3 state
 *     around +0x2F4 and +0x300
 *   - it stores parent Arc body mode around +0x2EC
 *   - it constructs body/highlight presentation nodes/textures
 *   - surviving Arc textures include:
 *
 *       img/arc_body.png
 *       img/arc_body_hi.png
 *
 *   - mode-specific color selection includes the DESIGNANT #F02961 path already
 *     established in Section 14
 *
 * The geometry updater around ~0x1873CFC takes a transformed endpoint pair,
 * derives perpendicular/width offsets, constructs four corner positions and
 * updates the visible segment geometry.
 *
 * Therefore the readable primitive is a ribbon/quad segment:
 *
 *        corner A+ ---------------- corner B+
 *             |                          |
 *             |       Arc segment        |
 *             |                          |
 *        corner A- ---------------- corner B-
 *
 * Consecutive segment objects form the visible Arc ribbon/strip.
 *
 * Exact GPU vertex-buffer/material/shader ownership is deliberately left for a
 * deeper rendering slice if it ever becomes necessary.
 */

// -----------------------------------------------------------------------------
// 11. Arc root positioning still consumes logic-space state
// -----------------------------------------------------------------------------

/*
 * CONFIRMED in the RenderArcNote update:
 *
 *   - parent Arc current expected X/Y comes from LogicArcNote +0xD4/+0xD8
 *   - current depth comes from the common LogicNote spatial state
 *   - render-segment child objects are updated each frame
 *   - a dedicated transform pass then applies timinggroup angle metadata before
 *     final segment geometry is refreshed
 *
 * Thus the Arc renderer does not independently evaluate AFF easing each frame.
 * The analytic/sampled path work belongs to LogicArcNote; RenderArcNote consumes
 * the already-built render tessellation and current logic-space state.
 */

// -----------------------------------------------------------------------------
// 12. TimingGroup angle units are now exact
// -----------------------------------------------------------------------------

/*
 * Section 10 left the exact runtime angle transform unresolved.
 * Section 15 closes it.
 *
 * Chart TimingGroup processing propagates integer:
 *
 *   anglex -> child chart note +0x10
 *   angley -> child chart note +0x14
 *
 * During chart -> runtime note construction, each is converted using:
 *
 *   radians = rawInteger * PI / 180 / 10
 *
 * Therefore chart angle units are tenths of a degree:
 *
 *   10   ->  1 degree
 *   100  -> 10 degrees
 *   900  -> 90 degrees
 *
 * The converted values are stored as:
 *
 *   LogicNote +0x58 = anglex radians
 *   LogicNote +0x5C = angley radians
 */
static float timingGroupAngleToRadians(int rawTenthsOfDegree)
{
    return
        static_cast<float>(rawTenthsOfDegree)
        * 3.14159265358979323846f
        / 180.0f
        / 10.0f;
}

// -----------------------------------------------------------------------------
// 13. anglex/angley rotate Arc visible geometry downstream of touch projection
// -----------------------------------------------------------------------------

/*
 * CONFIRMED transform pass around ~0x1893F3C.
 *
 * While iterating visible Arc render segments, the renderer retrieves the
 * underlying logic context and applies two native rotation-matrix helpers:
 *
 *   LogicNote +0x5C -> helper ~0x134FC8C
 *   LogicNote +0x58 -> helper ~0x1653A54
 *
 * Decoding the matrices gives:
 *
 *   +0x5C / angley -> Y-axis rotation matrix
 *
 *       [ cos  0  -sin  0 ]
 *       [  0   1    0   0 ]
 *       [ sin  0   cos  0 ]
 *       [  0   0    0   1 ]
 *
 *   +0x58 / anglex -> X-axis rotation matrix
 *
 *       [ 1   0    0   0 ]
 *       [ 0  cos  sin  0 ]
 *       [ 0 -sin  cos  0 ]
 *       [ 0   0    0   1 ]
 *
 * The signs above are the engine's exact matrix convention. Calling them
 * positive/negative user-facing rotations would depend on row/column and handed
 * conventions, so this notebook preserves the native matrices instead.
 *
 * The transformed segment endpoints are then passed into the Arc geometry
 * updater before the visible ribbon piece is refreshed.
 *
 * This proves the architectural boundary predicted in Section 11:
 *
 *   screen touch -> common camera unprojection          (angles NOT applied)
 *
 *   Logic Arc render segment -> note-specific rotations (angles applied here)
 */

// -----------------------------------------------------------------------------
// 14. Compact render architecture
// -----------------------------------------------------------------------------

/*
 *                         LogicNote
 *                            |
 *                    reciprocal pointer
 *                            |
 *                         RenderNote
 *          __________________|__________________
 *         /          /        |        \         \
 *        v          v         v         v         v
 *      Tap        Hold       Arc      ArcTap     Flick
 *       |           |         |         |          |
 *    sprite      stretch   +0x100    3D model    arrow
 *                sprite       |                    sprite
 *                  |          v
 *            texture swap  segment objects
 *                          + quad/ribbon geometry
 *                                  |
 *                                  v
 *                       anglex / angley transforms
 *
 * Logic owns gameplay state and sampled paths.
 * Render objects own scene presentation and consume that state.
 */

// -----------------------------------------------------------------------------
// 15. CONFIRMED / RECONSTRUCTED / UNRESOLVED summary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED
 * ---------
 * - RenderTapNote, RenderHoldNote, RenderArcNote, RenderArcTapNote and
 *   RenderFlickNote survive as RenderNote-derived RTTI types.
 * - LogicNote +0x40 stores its render object.
 * - RenderNote +0x2A8 stores its logic object.
 * - A central factory creates renderers by LogicNote runtime type.
 * - ArcTap renderers are created as children before the parent RenderArcNote.
 * - Tap uses note sprite variants and NotePosition/current-depth state.
 * - Hold uses one stretchable body sprite with cached normal/highlight textures.
 * - Hold current contact selects highlight presentation.
 * - timinggroup fadingholds reaches a distinct Hold presentation branch.
 * - Flick uses img/arr.png and derives arrow orientation from gesture direction.
 * - ArcTap uses 3D model resources and parent Arc presentation context.
 * - Arc rendering consumes LogicArcNote +0x100, not gameplay path +0xE8.
 * - One render-segment object is created for each adjacent +0x100 sample pair.
 * - Arc segment geometry is built from four offset corners around two endpoints.
 * - Arc body/highlight textures include img/arc_body.png / img/arc_body_hi.png.
 * - anglex/angley chart integers are tenths of a degree.
 * - runtime +0x58 is anglex radians and feeds an X-axis rotation matrix.
 * - runtime +0x5C is angley radians and feeds a Y-axis rotation matrix.
 * - timinggroup angles affect Arc render geometry downstream of common input ray
 *   construction; they do not rotate the common screen->world touch ray.
 *
 * RECONSTRUCTED
 * -------------
 * - readable names such as `currentDepth30`, `ArcRenderSegment`, and helper
 *   function names in this file
 * - description of Arc pieces as a ribbon/quad strip assembled from segment
 *   objects; the four-corner construction itself is confirmed
 *
 * UNRESOLVED
 * ----------
 * - original source names for most RenderNote member offsets
 * - exact semantic names of every Tap/Hold/Flick style integer
 * - complete approach/fade/opacity equations common to note renderers
 * - exact aesthetic curve selected by fadingholds
 * - complete Arc GPU material/shader/particle implementation
 * - exact ownership of every Arc cap/body auxiliary scene node
 */
