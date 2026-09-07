/*
 * Arcaea excavation notebook
 * Section 22: rendering refinements tied to gameplay state
 *
 * STATUS: rendering refinement slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native control flow, constants,
 *                   strings, resources, object layout, or independent consumers.
 *   RECONSTRUCTED = readable structure assembled from confirmed behaviour;
 *                   names may differ from original source.
 *   UNRESOLVED    = exact original semantic name or unused/hidden consumer is
 *                   not proved.
 *
 * Scope of this section is deliberately narrow and mechanic-facing:
 *   1. TimingGroup `tracecol` parsing and trace-Arc rendering propagation
 *   2. exact `fadingholds` opacity behaviour
 *   3. identification of the common render opacity virtuals
 *   4. Designant's two opacity layers
 *   5. RenderArcNote child/container anatomy, Arc cap and approach arrow
 *   6. Arc particle resources as separate presentation nodes
 *   7. `enwidencamera` opacity compensation on RenderTapNote auxiliaries
 *   8. widened/custom track presentation and the custom-mask shader
 *
 * This file refines:
 *   03_long_notes.cpp
 *   09_enwidenlanes.cpp
 *   11_gameplay_space.cpp
 *   14_arc_mode_designant.cpp
 *   15_rendering_fundamentals.cpp
 *   16_scenecontrols.cpp
 *
 * It intentionally does NOT attempt a pixel-perfect renderer reconstruction,
 * full material system, or unrelated special-scene cinematics.
 */

#include <algorithm>
#include <cstdint>
#include <vector>

// -----------------------------------------------------------------------------
// 1. `tracecol` is a native TimingGroup rendering attribute
// -----------------------------------------------------------------------------

/*
 * CONFIRMED parser behaviour:
 *
 * The TimingGroup attribute parser recognises the literal eight-character
 * prefix:
 *
 *     "tracecol"
 *
 * The following six characters are parsed as three hexadecimal byte values with
 * an operation equivalent to:
 *
 *     "%02x%02x%02x"
 *
 * Therefore the accepted syntax shape is:
 *
 *     tracecolRRGGBB
 *
 * The parsed RGB object is retained in chart timing/group-derived metadata and
 * propagated specifically into Arc data.
 */
struct Rgb8 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct TraceColourMetadata {
    Rgb8 rgb;
};

/*
 * CONFIRMED propagation chain:
 *
 *   TimingGroup attribute `tracecolRRGGBB`
 *          -> chart timing/group colour object
 *          -> chart ArcNote +0x90
 *          -> only for runtime Arc body mode == 1
 *          -> LogicArcNote +0x178
 *          -> Arc renderer / segment presentation
 *
 * This is not a generic TimingGroup tint for floor taps, holds, judged Arc body
 * mode 0, or Designant mode 2.
 */
struct LogicArcTraceSelectedFields {
    int32_t bodyMode;                    // conceptual LogicArcNote +0xA4
    TraceColourMetadata* traceColour;    // conceptual LogicArcNote +0x178
};

void propagateTraceColourToRuntimeArc(
    LogicArcTraceSelectedFields& arc,
    TraceColourMetadata* chartTraceColour)
{
    if (arc.bodyMode == 1) {
        arc.traceColour = chartTraceColour;
    }
    else {
        arc.traceColour = nullptr;
    }
}

// -----------------------------------------------------------------------------
// 2. This build's confirmed trace-colour render consumer is metadata-presence
//    driven, with a dedicated gold path
// -----------------------------------------------------------------------------

/*
 * CONFIRMED resource / render behaviour:
 *
 * A mode-1 trace Arc with retained +0x178 trace-colour metadata takes a dedicated
 * presentation branch associated with:
 *
 *     RGB(244, 185, 66)
 *     img/trace_body_gold.png
 *
 * and with a lower Arc-root opacity magnitude than the ordinary mode-1 trace
 * presentation (observed 125 on the special branch).
 *
 * IMPORTANT LIMIT:
 * In the consumers traced for this section, the renderer checks that the
 * +0x178 metadata object exists but does not read its stored r/g/b members.
 *
 * Therefore the strongest durable statement for THIS BUILD is:
 *
 *     parsed arbitrary RRGGBB data exists and survives into runtime,
 *     but the confirmed render consumer uses metadata presence to choose the
 *     dedicated gold trace path.
 *
 * Do NOT claim arbitrary custom RGB trace tinting from this build without a
 * separate consumer which actually reads those channels.
 */
static constexpr Rgb8 kConfirmedSpecialTraceGold = {244, 185, 66};
static constexpr int kSpecialTraceRootOpacity = 125;

bool usesConfirmedGoldTracePresentation(
    const LogicArcTraceSelectedFields& arc)
{
    return arc.bodyMode == 1 && arc.traceColour != nullptr;
}

// -----------------------------------------------------------------------------
// 3. The common renderer virtual pair is opacity get/set
// -----------------------------------------------------------------------------

/*
 * CONFIRMED by multiple independent render consumers.
 *
 * A common virtual around the observed +0x460 slot behaves as an 8-bit opacity
 * getter, while the neighbouring +0x470 virtual behaves as an opacity setter.
 *
 * Evidence convergence:
 *
 *   - hidegroup reads current opacity and forces the same property to zero
 *   - Arc segment update reads the getter, masks the result to 0xff, compares
 *     with a desired byte value, then calls the setter
 *   - Hold rendering feeds calculated intensity directly into the setter
 *
 * Readable interface:
 */
struct RenderNodeOpacityInterface {
    virtual ~RenderNodeOpacityInterface() = default;

    virtual int getOpacity() const = 0;   // conceptual vslot near +0x460
    virtual void setOpacity(int value) = 0; // conceptual vslot near +0x470
};

static int clampOpacityByte(int value)
{
    return std::clamp(value, 0, 255);
}

// -----------------------------------------------------------------------------
// 4. `fadingholds` is long-event-state feedback, not a judgement modifier
// -----------------------------------------------------------------------------

/*
 * Established gameplay state from Section 03:
 *
 *   LogicLongNoteBase +0x66
 *       = 1 after a successful collected long-event batch
 *       = 0 after a LOST collected long-event batch
 *
 * Established TimingGroup propagation from Section 10:
 *
 *   timinggroup `fadingholds`
 *       -> LogicHoldNote +0xA9
 *
 * CONFIRMED rendering boundary:
 * The special fading calculation is used only by the Hold renderer's
 * presentation path. It does NOT change:
 *
 *   - tick generation
 *   - tick timing
 *   - Hold contact qualification
 *   - success/LOST collection
 */
struct LongTickRenderEvent {
    int32_t timeMs;
    int32_t scoreUnits;
    uint8_t processedFlags;
};

struct HoldRenderInputs {
    int32_t startTimeMs;
    float tickIntervalMs;

    bool currentlyContacted;      // established Hold +0x64 meaning
    bool lastLongBatchSuccessful; // readable meaning of long-note +0x66
    bool fadingHolds;             // conceptual LogicHoldNote +0xA9

    std::vector<LongTickRenderEvent> tickEvents;
};

static bool tickIsProcessed(const LongTickRenderEvent& e)
{
    return (e.processedFlags & 1) != 0;
}

static const LongTickRenderEvent* firstUnprocessedTick(
    const std::vector<LongTickRenderEvent>& events)
{
    for (const LongTickRenderEvent& e : events) {
        if (!tickIsProcessed(e)) {
            return &e;
        }
    }
    return nullptr;
}

/*
 * CONFIRMED `fadingholds` non-contact behaviour:
 *
 *   before Hold start:
 *       intensity = 1.0
 *
 *   after start, previous collected batch was LOST:
 *       intensity = 0.5
 *
 *   after start, previous collected batch was successful:
 *       find first unprocessed long event
 *       horizon = nextTick.time + 2*tickInterval
 *
 *       intensity = clamp(
 *           0.5 + (horizon - now) / 500,
 *           0.5,
 *           1.0)
 *
 * The renderer then multiplies this factor into its ordinary opacity magnitude.
 *
 * Gameplay interpretation:
 * `fadingholds` provides visual feedback about long-event health. A Hold can
 * sink toward half intensity as its next pending event approaches the loss
 * horizon, while a LOST batch leaves the non-contact presentation at half
 * intensity until successful state is restored.
 */
float calculateFadingHoldIntensity(
    const HoldRenderInputs& hold,
    int32_t nowMs)
{
    if (!hold.fadingHolds || hold.currentlyContacted) {
        return 1.0f;
    }

    if (nowMs < hold.startTimeMs) {
        return 1.0f;
    }

    if (!hold.lastLongBatchSuccessful) {
        return 0.5f;
    }

    const LongTickRenderEvent* next =
        firstUnprocessedTick(hold.tickEvents);

    if (next == nullptr) {
        return 1.0f;
    }

    const float horizonMs =
        static_cast<float>(next->timeMs)
        + 2.0f * hold.tickIntervalMs;

    const float factor =
        0.5f
        + (horizonMs - static_cast<float>(nowMs)) / 500.0f;

    return std::clamp(factor, 0.5f, 1.0f);
}

// -----------------------------------------------------------------------------
// 5. Designant's global presentation factor controls two opacity layers
// -----------------------------------------------------------------------------

/*
 * Sections 14/17 established a global Designant presentation factor which is
 * dynamically changed by the Designant special-scene timeline.
 *
 * Section 22 resolves two previously anonymous render consumers as opacity.
 *
 * CONFIRMED:
 *
 *   RenderArcNote/root layer, mode 2:
 *       opacity = int(globalDesignantFactor * 125)
 *
 *   individual Arc ribbon segment, mode 2:
 *       opacity = int(globalDesignantFactor * 225)
 *
 * Thus the factor fades the parent Arc presentation and the visible ribbon
 * segments independently, with different base strengths.
 */
float getDesignantPresentationFactor(); // readable reconstructed name

int designantRootOpacity(float factor)
{
    return clampOpacityByte(
        static_cast<int>(factor * 125.0f));
}

int designantSegmentOpacity(float factor)
{
    return clampOpacityByte(
        static_cast<int>(factor * 225.0f));
}

void applyDesignantOpacityLayers(
    RenderNodeOpacityInterface& root,
    std::vector<RenderNodeOpacityInterface*>& ribbonSegments,
    int32_t arcBodyMode)
{
    if (arcBodyMode != 2) {
        return;
    }

    const float factor = getDesignantPresentationFactor();

    root.setOpacity(designantRootOpacity(factor));

    for (RenderNodeOpacityInterface* segment : ribbonSegments) {
        segment->setOpacity(designantSegmentOpacity(factor));
    }
}

/*
 * Combined with Section 14's confirmed RGB(240,41,97), Designant's Arc body is
 * not merely a differently coloured trace. It is a dedicated colour family plus
 * a dedicated multi-layer opacity controller.
 */

// -----------------------------------------------------------------------------
// 6. RenderArcNote is a small scene graph, not one monolithic mesh
// -----------------------------------------------------------------------------

/*
 * CONFIRMED selected RenderArcNote child/container identity:
 *
 *   +0x2B8  Arc-body segment container
 *            - every tessellated ribbon segment is attached here
 *
 *   +0x2C0  ArcTap-render container
 *            - child RenderArcTapNote nodes are attached here
 *
 *   +0x2D0  Arc-cap sprite
 *            - optional approach-arrow child can be attached under the cap
 *
 * This refines the generic "auxiliary Arc nodes" language from Section 15.
 */
struct SpriteNode;
struct SceneNode;
struct RenderArcTapNote;
struct ArcRenderSegment;

struct RenderArcSelectedChildren {
    SceneNode* bodySegmentContainer; // conceptual +0x2B8
    SceneNode* arcTapContainer;      // conceptual +0x2C0
    SpriteNode* arcCap;              // conceptual +0x2D0
};

/*
 * CONFIRMED cap/arrow resources:
 *
 *     img/1080/arc_cap.png
 *     img/1080/approach_arrow.png
 *
 * The optional approach arrow is attached under the cap, scaled to about 1.5,
 * and receives an integer presentation/order parameter of 10.
 *
 * Designant-style construction tints the cap into the same hot pink/red family
 * established for mode 2:
 *
 *     RGB(240,41,97)
 */
static constexpr Rgb8 kDesignantArcColour = {240, 41, 97};
static constexpr float kApproachArrowScale = 1.5f;
static constexpr int kApproachArrowOrderLike = 10;

void attachArcPresentationChildren(
    RenderArcSelectedChildren& render,
    const std::vector<ArcRenderSegment*>& segments,
    const std::vector<RenderArcTapNote*>& arcTaps,
    bool showApproachArrow,
    int32_t arcBodyMode)
{
    for (ArcRenderSegment* segment : segments) {
        addChild(render.bodySegmentContainer, segment);
    }

    for (RenderArcTapNote* arcTap : arcTaps) {
        addChild(render.arcTapContainer, arcTap);
    }

    if (arcBodyMode == 2) {
        setNodeColour(render.arcCap, kDesignantArcColour);
    }

    if (showApproachArrow) {
        SpriteNode* arrow =
            createSprite("img/1080/approach_arrow.png");

        setNodeScale(arrow, kApproachArrowScale);
        setOrderLikeParameter(arrow, kApproachArrowOrderLike);
        addChild(render.arcCap, arrow);
    }
}

// -----------------------------------------------------------------------------
// 7. Arc particles are separate emitter nodes, not Arc ribbon geometry
// -----------------------------------------------------------------------------

/*
 * CONFIRMED particle resources include:
 *
 *     particle/particle_arc.plist
 *     particle/particle_arc_mirai_light.plist
 *     particle/particle_arc_mirai_conflict.plist
 *
 * The generic emitter is created with scale about 2.1.
 * Mirai-specific variants are created with scale about 1.9.
 *
 * Created emitters are enabled, assigned an observed internal state/mode value 2,
 * and retained in a particle-node collection.
 *
 * The exact content-selector wrapper for the Mirai variants is deliberately not
 * promoted here because it is cosmetic/context-specific and not needed to
 * explain Arc judgement or geometry.
 */
enum class ArcParticleVariant {
    Generic,
    MiraiLight,
    MiraiConflict,
};

const char* arcParticleResource(ArcParticleVariant variant)
{
    switch (variant) {
        case ArcParticleVariant::Generic:
            return "particle/particle_arc.plist";

        case ArcParticleVariant::MiraiLight:
            return "particle/particle_arc_mirai_light.plist";

        case ArcParticleVariant::MiraiConflict:
            return "particle/particle_arc_mirai_conflict.plist";
    }

    return "particle/particle_arc.plist";
}

float arcParticleScale(ArcParticleVariant variant)
{
    return variant == ArcParticleVariant::Generic
        ? 2.1f
        : 1.9f;
}

// -----------------------------------------------------------------------------
// 8. `enwidencamera` compensation on Tap auxiliaries is opacity, not geometry
// -----------------------------------------------------------------------------

/*
 * Section 11 found a compensating factor when camera widening changes:
 *
 *     factor =
 *         1 - (cameraWidenScale - 1) * (2/3)
 *
 * giving:
 *
 *     camera scale 1.0 -> factor 1.0
 *     camera scale 1.5 -> factor 2/3
 *
 * Section 22 identifies the downstream consumer.
 *
 * CONFIRMED update sequence:
 *
 *   live LogicTapNote
 *       -> LogicNote +0x40
 *       -> dynamic-cast to native RenderTapNote
 *       -> iterate RenderTapNote child presentation nodes starting at child 1
 *       -> set opacity to int(factor * 130)
 *
 * The main child at index 0 is intentionally skipped.
 *
 * Therefore the old provisional idea that factor*130 represented position or
 * scale compensation is incorrect. It is opacity compensation for auxiliary
 * Tap render children during camera widening.
 */
static float cameraWidenTapAuxFactor(float cameraWidenScale)
{
    return
        1.0f
        - (cameraWidenScale - 1.0f)
          * (2.0f / 3.0f);
}

static int tapAuxOpacityForCameraWiden(float cameraWidenScale)
{
    const float factor =
        cameraWidenTapAuxFactor(cameraWidenScale);

    return clampOpacityByte(
        static_cast<int>(factor * 130.0f));
}

void updateTapAuxiliaryOpacityForCameraWiden(
    std::vector<RenderNodeOpacityInterface*>& renderChildren,
    float cameraWidenScale)
{
    const int opacity =
        tapAuxOpacityForCameraWiden(cameraWidenScale);

    // CONFIRMED: child index 0 is excluded by the native helper.
    for (size_t i = 1; i < renderChildren.size(); ++i) {
        renderChildren[i]->setOpacity(opacity);
    }
}

/*
 * Endpoint examples:
 *
 *     camera scale 1.0 -> opacity ~= 130
 *     camera scale 1.5 -> opacity ~= 86
 */

// -----------------------------------------------------------------------------
// 9. Extra-lane presentation is separate from fixed gameplay lane geometry
// -----------------------------------------------------------------------------

/*
 * CONFIRMED track resources include dedicated extra-lane art:
 *
 *     img/track_extralane_light.png
 *     img/track_extralane_dark.png
 *
 * These presentation sprites are constructed separately from the gameplay lane
 * coordinates established in Sections 08/09.
 *
 * Thus `enwidenlanes` does not need to move lane centres to make the visible
 * track appear wider. The gameplay engine already owns six fixed world-space
 * slots, while the renderer can reveal/add extra-lane artwork independently.
 */

// -----------------------------------------------------------------------------
// 10. Custom track masking can reshape presentation without changing lanes
// -----------------------------------------------------------------------------

/*
 * CONFIRMED embedded shader identity:
 *
 *     track_custom_mask_shader
 *
 * Surviving shader inputs include:
 *
 *     z_clip
 *     texture_mask
 *     mask_inverse
 *     texture_height
 *     track_width
 *     fixed_x_pos
 *     model_width
 *
 * The fragment logic maps model/world X into track-relative mask X, maps depth/Z
 * relative to `z_clip` into the mask's vertical/repeating coordinate, samples
 * `texture_mask`, and discards masked fragments to transparency.
 *
 * Readable equivalent:
 */
struct TrackMaskUniforms {
    float zClip;
    float textureHeight;
    float trackWidth;
    float fixedXPos;
    float modelWidth;
    bool maskInverse;
};

bool customTrackFragmentIsVisible(
    const TrackMaskUniforms& u,
    float modelOrWorldX,
    float worldZ,
    float sampledMaskValue)
{
    const float relativeX =
        (modelOrWorldX - u.fixedXPos)
        / u.modelWidth;

    const float trackMaskX =
        relativeX * u.trackWidth;

    const float relativeZ =
        worldZ - u.zClip;

    const float trackMaskY =
        relativeZ / u.textureHeight;

    (void)trackMaskX;
    (void)trackMaskY;

    const bool maskSaysVisible =
        sampledMaskValue > 0.0f;

    return u.maskInverse
        ? !maskSaysVisible
        : maskSaysVisible;
}

/*
 * The exact shader arithmetic above is intentionally source-ish pseudocode, not
 * a byte-for-byte GLSL transcription. The durable architectural result is:
 *
 *   gameplay:
 *       six fixed world-space lane slots
 *
 *   presentation:
 *       extra-lane sprites
 *       + custom track pieces
 *       + shader mask clipping
 *
 * Visual track shapes can therefore widen, split, disappear or be carved into
 * nonstandard silhouettes without rewriting the underlying floor-lane geometry.
 */

// -----------------------------------------------------------------------------
// 11. Full mechanic-facing rendering model
// -----------------------------------------------------------------------------

/*
 * TimingGroup trace presentation
 * ------------------------------
 *
 *   tracecolRRGGBB
 *         |
 *         v
 *   parsed RGB metadata
 *         |
 *         +--> mode-1 Arc only
 *                    |
 *                    v
 *              LogicArcNote +0x178
 *                    |
 *                    v
 *          confirmed special gold trace path
 *
 *
 * Hold presentation
 * -----------------
 *
 *   long-event success/LOST state (+0x66)
 *                 |
 *                 v
 *          fadingholds renderer
 *                 |
 *       0.5 .. 1.0 intensity
 *                 |
 *                 v
 *               opacity
 *
 *
 * Designant Arc presentation
 * --------------------------
 *
 *   global Designant factor
 *          /         \
 *         v           v
 *   root opacity   segment opacity
 *      x125             x225
 *         \           /
 *          \         /
 *        RGB(240,41,97)
 *
 *
 * Arc scene graph
 * ---------------
 *
 *   RenderArcNote
 *      |- body-segment container
 *      |- ArcTap container
 *      '- Arc cap
 *           '- optional approach arrow
 *
 *   Arc particle emitters are separate nodes/collections.
 *
 *
 * Widening presentation
 * ---------------------
 *
 *   enwidencamera
 *       -> Tap auxiliary child opacity compensation
 *
 *   enwidenlanes
 *       -> fixed gameplay lanes remain fixed
 *       -> extra-lane artwork / custom track masking changes presentation
 */

// -----------------------------------------------------------------------------
// 12. CONFIRMED / RECONSTRUCTED / UNRESOLVED summary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED
 * ---------
 * - `tracecol` is a native TimingGroup attribute prefix.
 * - Six following hex digits are parsed as three RGB bytes.
 * - Trace-colour metadata propagates into Arc data and runtime mode-1 Arcs.
 * - In traced consumers from this build, metadata presence selects a dedicated
 *   gold trace presentation using RGB(244,185,66) and trace_body_gold.png.
 * - No traced consumer in this slice reads arbitrary stored R/G/B channels.
 * - Common render virtuals near +0x460/+0x470 function as opacity get/set.
 * - `fadingholds` uses long-event state and a 500 ms, 0.5..1.0 non-contact
 *   intensity calculation; it does not change Hold judgement.
 * - Designant root opacity uses global factor*125.
 * - Designant segment opacity uses global factor*225.
 * - RenderArcNote contains separate body-segment, ArcTap and cap branches.
 * - arc_cap.png and approach_arrow.png are dedicated Arc presentation assets.
 * - Designant cap presentation uses the same RGB(240,41,97) family.
 * - Arc particles are separate emitter nodes/resources from ribbon segments.
 * - `enwidencamera` factor*130 is Tap auxiliary opacity compensation, not
 *   spatial scaling/position compensation.
 * - dedicated extra-lane track textures exist.
 * - track_custom_mask_shader reshapes visible track fragments with a mask.
 *
 * RECONSTRUCTED
 * -------------
 * - helper/function names in this file.
 * - scene-graph field names for +0x2B8/+0x2C0/+0x2D0.
 * - the high-level phrase "long-event health feedback" for fadingholds.
 * - pseudocode form of the custom track mask shader.
 *
 * UNRESOLVED
 * ----------
 * - whether an untraced native consumer renders arbitrary tracecol RRGGBB.
 * - exact original names of the opacity virtual methods.
 * - exact original name/purpose of the Arc approach-arrow order-like integer.
 * - high-level semantic wrapper selecting Mirai Arc particle variants.
 *
 * None of those unresolved names alter the mechanic-facing rendering model.
 */
