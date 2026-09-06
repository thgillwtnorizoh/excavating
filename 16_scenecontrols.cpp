/*
 * Arcaea excavation notebook
 * Section 16: SceneControl framework and generic in-play visual controls
 *
 * STATUS: generic SceneControl / visual-control slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native code/data/control flow.
 *   RECONSTRUCTED = readable representation assembled from confirmed behaviour.
 *   UNRESOLVED    = exact semantic name/purpose is not yet proved.
 *
 * Scope of this section:
 *   - common LogicSceneControl runtime payload refinements
 *   - shared SceneControl scheduler / dispatcher split
 *   - GameSceneVisualControlHandler preparation and visual dispatch gating
 *   - trackdisplay transition and darkening-layer relationship
 *   - redline sprite creation, pulse, keyed lifetime and cleanup
 *   - arcahvdistort prepared-node fade control
 *   - arcahvdebris prepared-node fade control
 *   - hidegroup per-group note visibility propagation
 *   - classification of SceneControl families as presentation-only versus
 *     gameplay/input-affecting
 *
 * Deliberately out of scope:
 *   - full shader/material internals for Arc HV effects
 *   - exact artistic animation equations for every child node
 *   - song-specific anomaly behaviour beyond identifying it as the next target
 *   - exact high-level meaning of the visual-dispatch enable byte
 *   - unlocks, progression, challenges and account/content-access logic
 *
 * This file builds directly on:
 *   08_lane_geometry.cpp
 *   09_enwidenlanes.cpp
 *   10_timinggroups.cpp
 *   11_gameplay_space.cpp
 *   14_arc_mode_designant.cpp
 *   15_rendering_fundamentals.cpp
 *
 * Useful native anchors from the investigated ARM64 build:
 *   SceneControl string -> enum parser                 ~0x1090060
 *   chart SceneControl -> LogicSceneControl            ~0x1864110
 *   common gameplay scheduler                          ~0x0F8056C
 *   SceneControl dispatcher                            ~0x1514C64
 *   trackdisplay branch                                ~0x1514D3C
 *   arcahvdistort branch                               ~0x1514E10
 *   redline branch                                     ~0x1514E50
 *   arcahvdebris branch                                ~0x1515088
 *   GameSceneVisualControlHandler setup                ~0x0A3937C
 *   hidegroup group/note-state helper                  ~0x160A828
 *
 * WARNING ABOUT SYMBOLS:
 * The binary is stripped. Readable names below are reconstruction names unless
 * explicitly described as surviving RTTI, strings, resource names, or confirmed
 * field behaviour.
 */

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// 1. SceneControl type mapping remains the seven-value native family
// -----------------------------------------------------------------------------

/*
 * CONFIRMED surviving scenecontrol strings and parser mapping:
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
 * Sections 09/11 already completed the two widening controls. Section 16 closes
 * the generic 0..4 families and the common dispatcher structure around them.
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
// 2. LogicSceneControl carries group identity and a runtime instance identity
// -----------------------------------------------------------------------------

/*
 * Section 09 established the widening-relevant payload:
 *
 *   +0x18 : timestamp
 *   +0x64 : SceneControlType
 *   +0x68 : float parameter
 *   +0x6C : integer parameter
 *
 * Section 16 refines two additional runtime fields used by the generic effects:
 *
 *   +0x50 : group/context identity associated with this control
 *   +0x60 : runtime SceneControl instance identity
 *
 * CONFIRMED +0x60 construction:
 * The constructor assigns this field from a monotonically increasing global
 * counter. It is therefore runtime identity, not an additional chart parameter.
 * Redline uses it to generate an event-unique scheduler key.
 *
 * CONFIRMED +0x50 behaviour:
 * It is filled from the current group construction context and is consumed by
 * hidegroup to identify which note group receives the visibility state.
 */
struct LogicSceneControl {
    int32_t timeMs;                // conceptual reference to common +0x18

    // Other common LogicEvent/LogicNote-like state omitted.

    int32_t groupId;               // +0x50, behaviour CONFIRMED
    int32_t runtimeInstanceId;     // +0x60, behaviour CONFIRMED
    SceneControlType type;         // +0x64, CONFIRMED
    float floatParameter;          // +0x68, CONFIRMED
    int32_t intParameter;          // +0x6C, CONFIRMED
};

// -----------------------------------------------------------------------------
// 3. SceneControls fire on the ordinary effective gameplay clock
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from the same common scheduler established in Sections 09/10:
 *
 *   - LogicSceneControl objects are recognised by runtime type
 *   - the scheduler waits until the control timestamp is reached
 *   - it dispatches the control through the common SceneControl handler
 *
 * Therefore SceneControls do not own an independent visual clock.
 * They inherit the same effective gameplay-time machinery used by note expiry,
 * touch processing, widening and other timed gameplay state, including the
 * already-confirmed fallback branch which conditionally subtracts 3000 ms.
 */

// -----------------------------------------------------------------------------
// 4. The dispatcher itself separates three kinds of SceneControl behaviour
// -----------------------------------------------------------------------------

/*
 * CONFIRMED control-flow structure around ~0x1514C64.
 *
 * Types 0..3 are routed into the generic visual-control handler only when a byte
 * in GameSceneVisualControlHandler around +0x2C8 is nonzero.
 *
 * Type 4 (`hidegroup`) is peeled off into its own group/note-state path rather
 * than entering that visual jump table.
 *
 * Types 5 and 6 install the widening transition state reconstructed in Section
 * 09 and materially affect gameplay-space/input behaviour.
 *
 * This gives a native architectural split:
 *
 *   0..3 : optional prepared visual controls
 *      4 : group-wide note visibility state
 *    5/6 : gameplay spatial/lane transition state
 *
 * The readable name `visualControlDispatchEnabled` below is RECONSTRUCTED. The
 * exact original meaning of handler +0x2C8 is UNRESOLVED.
 */
struct GameSceneVisualControlHandlerSelectedFields {
    bool visualControlDispatchEnabled; // conceptual +0x2C8
    int32_t trackDisplayValue;          // conceptual +0x2CC, see below
};

void dispatchSceneControl(
    GameSceneVisualControlHandler& visual,
    GameplayState& gameplay,
    const LogicSceneControl& control)
{
    switch (control.type) {
    case SceneControlType::TrackDisplay:
    case SceneControlType::RedLine:
    case SceneControlType::ArcHVDistort:
    case SceneControlType::ArcHVDebris:
        if (visual.visualControlDispatchEnabled) {
            handlePreparedVisualControl(visual, control);
        }
        break;

    case SceneControlType::HideGroup:
        applyHideGroupState(
            control.groupId,
            control.intParameter != 0);
        break;

    case SceneControlType::EnwidenCamera:
        installCameraWidenTransition(gameplay, control);
        break;

    case SceneControlType::EnwidenLanes:
        installLaneWidenTransition(gameplay, control);
        break;

    default:
        break;
    }
}

// -----------------------------------------------------------------------------
// 5. GameSceneVisualControlHandler lazily prepares only used visual families
// -----------------------------------------------------------------------------

/*
 * CONFIRMED high-level setup around ~0x0A3937C.
 *
 * During visual-control preparation the handler scans gameplay logic objects,
 * identifies LogicSceneControl instances, and records which visual control types
 * 0..3 are actually present. Each required family is prepared once.
 *
 * This is not evidence that every scene always owns every effect node. The setup
 * path is demand-driven by the chart controls that exist for that play session.
 */
void prepareVisualControls(
    GameSceneVisualControlHandler& handler,
    const std::vector<LogicObject*>& objects)
{
    bool prepared[4] = {false, false, false, false};

    for (LogicObject* object : objects) {
        auto* control = dynamicCast<LogicSceneControl>(object);
        if (!control) {
            continue;
        }

        const int type = static_cast<int>(control->type);
        if (type < 0 || type > 3 || prepared[type]) {
            continue;
        }

        prepared[type] = true;

        switch (control->type) {
        case SceneControlType::TrackDisplay:
            prepareTrackDisplay(handler);
            break;
        case SceneControlType::RedLine:
            prepareRedLineResources(handler);
            break;
        case SceneControlType::ArcHVDistort:
            prepareArcHVDistort(handler);
            break;
        case SceneControlType::ArcHVDebris:
            prepareArcHVDebris(handler);
            break;
        default:
            break;
        }
    }
}

/*
 * RECONSTRUCTED function names above wrap CONFIRMED setup control flow.
 */

// -----------------------------------------------------------------------------
// 6. trackdisplay: 100-step quadratic track-presentation transition
// -----------------------------------------------------------------------------

/*
 * CONFIRMED:
 * The visual handler stores a current track-display value around +0x2CC.
 * Its ordinary initial value is 255.
 *
 * When a trackdisplay control fires, the branch captures:
 *
 *   start  = current track-display value
 *   target = control.intParameter
 *
 * and installs a repeating callback with 100 total transition steps.
 *
 * Scheduler interval is derived from:
 *
 *   control.floatParameter / 100
 *
 * while the callback computes a normalised step p and applies a quadratic ramp:
 *
 *   p = min(step / 100, 1)
 *
 *   value =
 *       start
 *       + (target - start) * p^2
 *
 * The resulting integer is written back as current state and applied through the
 * same Cocos opacity-style setter used by ordinary rendered nodes.
 *
 * Thus the float is a transition-span/duration value in the scheduler's time
 * domain and the integer is the requested final track presentation value.
 */
static float clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static int quadraticTrackDisplayValue(
    int start,
    int target,
    int step)
{
    const float p =
        clamp01(static_cast<float>(step) / 100.0f);

    return static_cast<int>(
        static_cast<float>(start)
        + static_cast<float>(target - start)
          * p * p);
}

void beginTrackDisplayTransition(
    GameSceneVisualControlHandler& handler,
    const LogicSceneControl& control)
{
    const int start = handler.trackDisplayValue;
    const int target = control.intParameter;

    scheduleRepeated(
        /* interval */ control.floatParameter / 100.0f,
        /* repeat */   99,
        [&, start, target](int step) {
            const int value =
                quadraticTrackDisplayValue(
                    start,
                    target,
                    step + 1);

            handler.trackDisplayValue = value;
            setTrackPresentationOpacity(value);
        });
}

/*
 * CONFIRMED classification:
 * No note judgement, lane, Arc path, touch ownership or gameplay-space state is
 * modified in this branch. trackdisplay is presentation-only.
 */

// -----------------------------------------------------------------------------
// 7. trackdisplay also coordinates an auxiliary background-darkening layer
// -----------------------------------------------------------------------------

/*
 * CONFIRMED prepared resource:
 *
 *   img/bg/bg_darken.png
 *
 * The trackdisplay branch performs additional endpoint-dependent visual actions
 * on this node. The target is compared around full byte opacity:
 *
 *   target <= 254 : enter/show the darkening-side transition
 *   target >  254 : take the opposite/fade-out-side transition
 *
 * The below-full branch includes a FadeTo target of approximately 240 plus a
 * short scale-style presentation action.
 *
 * The exact artistic scale choreography is deliberately not reconstructed here.
 * What matters architecturally is confirmed: trackdisplay is a coordinated track
 * fade + background-darken presentation, not a lane/camera geometry mutation.
 */
void updateTrackDisplayAuxiliaryPresentation(int target)
{
    if (target <= 254) {
        showBackgroundDarkenLayer();
        runBackgroundDarkenEnterAnimation(/* fade target ~= 240 */);
    }
    else {
        runBackgroundDarkenExitAnimation();
    }
}

// -----------------------------------------------------------------------------
// 8. redline: create a unique sprite, pulse it, remove it after a timed lifetime
// -----------------------------------------------------------------------------

/*
 * CONFIRMED resource:
 *
 *   img/redline.png
 *
 * Unlike Arc HV effects, a redline event creates a fresh sprite rather than
 * merely fading a pre-existing shared named node.
 *
 * The sprite begins transparent, rapidly fades to full opacity, then runs a
 * repeated pulse using short FadeTo/Delay/FadeIn-style actions.
 *
 * Observed action values include approximately:
 *
 *   FadeTo(0.05, 255)
 *
 * followed by repeated:
 *
 *   FadeTo(0.006, 200)
 *   DelayTime(0.05)
 *   FadeIn(0.006)
 *
 * The exact Cocos action-object nesting is presentation detail; the pulse
 * lifecycle itself is confirmed.
 */
void spawnRedLine(
    GameSceneVisualControlHandler& handler,
    const LogicSceneControl& control)
{
    Sprite* line = createSprite("img/redline.png");

    line->setOpacity(0);
    attachRedLineToScene(handler, line);

    line->runAction(
        sequence(
            fadeTo(0.05f, 255),
            repeatForever(
                sequence(
                    fadeTo(0.006f, 200),
                    delay(0.05f),
                    fadeTo(0.006f, 255))))));

    const std::string schedulerKey =
        std::string("scene_redline_")
        + toString(control.runtimeInstanceId);

    scheduleOnce(
        /* delay */ control.floatParameter,
        schedulerKey,
        [line]() {
            line->stopAllActions();
            line->removeFromParent();
        });
}

/*
 * CONFIRMED consequences:
 *
 *   - control.floatParameter is the delay/lifetime before cleanup in this branch
 *   - runtimeInstanceId +0x60 gives each redline cleanup a unique scheduler key
 *   - control.intParameter is not materially used by this redline branch
 *   - redline is presentation-only
 */

// -----------------------------------------------------------------------------
// 9. arcahvdistort: FadeTo on one prepared named node
// -----------------------------------------------------------------------------

/*
 * CONFIRMED prepared node/resource strings:
 *
 *   node name: ARCAHV_DISTORT
 *   resource : img/bg/arcahv-srt.png
 *
 * The prepared node begins hidden/transparent in the observed setup path.
 *
 * When arcahvdistort fires:
 *
 *   1. locate the prepared named node
 *   2. make it visible
 *   3. stop its previous actions
 *   4. run Cocos FadeTo
 *
 * with:
 *
 *   duration      = control.floatParameter
 *   targetOpacity = low 8 bits of control.intParameter
 *
 * No Arc gameplay path, expected point, hit geometry, touch tracker or judgement
 * state is touched by this branch.
 */
void handleArcHVDistort(const LogicSceneControl& control)
{
    Node* node = findSceneNode("ARCAHV_DISTORT");
    if (!node) {
        return;
    }

    node->setVisible(true);
    node->stopAllActions();

    node->runAction(
        fadeTo(
            control.floatParameter,
            static_cast<uint8_t>(control.intParameter)));
}

/*
 * CONFIRMED classification: presentation-only.
 *
 * UNRESOLVED:
 * The original expansion/meaning of the surviving text "HV" and the resource
 * suffix "srt" are not proved. They are preserved rather than guessed.
 */

// -----------------------------------------------------------------------------
// 10. arcahvdebris: fade control over a prepared animated debris container
// -----------------------------------------------------------------------------

/*
 * CONFIRMED prepared node/resource strings:
 *
 *   node name: ARCAHV_DEBRIS
 *   resource : img/bg/arcahv-debris.png
 *
 * The debris preparation path creates a persistent container/child presentation
 * and gives the debris child its own repeating animation.
 *
 * The SceneControl does NOT individually spawn debris pieces. Instead it fades
 * the prepared container as a whole using the same parameter model as distort:
 *
 *   duration      = control.floatParameter
 *   targetOpacity = low 8 bits of control.intParameter
 */
void handleArcHVDebris(const LogicSceneControl& control)
{
    Node* node = findSceneNode("ARCAHV_DEBRIS");
    if (!node) {
        return;
    }

    node->setVisible(true);
    node->stopAllActions();

    node->runAction(
        fadeTo(
            control.floatParameter,
            static_cast<uint8_t>(control.intParameter)));
}

/*
 * CONFIRMED classification: presentation-only.
 *
 * Detailed debris child animation, materials and shaders are outside this slice.
 */

// -----------------------------------------------------------------------------
// 11. hidegroup propagates one visibility byte through one runtime group
// -----------------------------------------------------------------------------

/*
 * hidegroup is structurally different from visual types 0..3.
 *
 * CONFIRMED dispatcher inputs:
 *
 *   selected group = LogicSceneControl +0x50
 *   state          = (LogicSceneControl.intParameter != 0)
 *
 * The dedicated helper around ~0x160A828:
 *
 *   - indexes per-group runtime state
 *   - writes the requested byte into that group record
 *   - walks all LogicNotes
 *   - for every note whose +0x50 group identity matches, writes the same state
 *     to LogicNote +0x55
 *   - when the matching note is a LogicArcNote, also propagates the state through
 *     its LogicArcTapNote child vector around +0x120
 *
 * The names `hidden` / `hiddenByGroup` are RECONSTRUCTED from confirmed renderer
 * consumption and the native scenecontrol string.
 */
struct LogicNoteSelectedVisibilityFields {
    int32_t groupId;           // conceptual +0x50
    uint8_t inputEnabled;      // +0x54, Section 10
    uint8_t hiddenByGroup;     // +0x55, behaviour CONFIRMED here
};

void applyHideGroupState(int groupId, bool hidden)
{
    groupRuntimeState(groupId).hidden = hidden;

    for (LogicNote* note : allLogicNotes()) {
        if (note->groupId != groupId) {
            continue;
        }

        note->hiddenByGroup = hidden;

        if (auto* arc = dynamicCast<LogicArcNote>(note)) {
            for (LogicArcTapNote* tap : arc->arcTaps) {
                tap->hiddenByGroup = hidden;
            }
        }
    }
}

// -----------------------------------------------------------------------------
// 12. hidegroup does not behave like noinput
// -----------------------------------------------------------------------------

/*
 * CONFIRMED by consumer tracing.
 *
 * The live player-input paths use:
 *
 *   LogicNote +0x54
 *
 * as the noinput/input-enabled gate established in Section 10.
 *
 * Those note-processing/input slices do not use +0x55 as an equivalent gate.
 * Renderer update paths do consume +0x55 and suppress/fade note presentation.
 *
 * Therefore the durable behavioural distinction is:
 *
 *   +0x54 / noinput
 *       -> player-input eligibility
 *
 *   +0x55 / hidegroup state
 *       -> presentation visibility
 *
 * A hidden group is not thereby proved removed from timing/judgement/gameplay
 * processing. The observed native evidence supports presentation suppression,
 * not a second input-disable path.
 */
static bool acceptsPlayerInput(const LogicNoteSelectedVisibilityFields& note)
{
    return note.inputEnabled != 0;
}

static bool shouldPresentNote(const LogicNoteSelectedVisibilityFields& note)
{
    return note.hiddenByGroup == 0;
}

/*
 * CONFIRMED classification: hidegroup is presentation-side group visibility.
 */

// -----------------------------------------------------------------------------
// 13. Compact framework model
// -----------------------------------------------------------------------------

/*
 *                         chart scenecontrol(...)
 *                                  |
 *                                  v
 *                         LogicSceneControl
 *                         time / group / id
 *                           type / params
 *                                  |
 *                                  v
 *                       common gameplay scheduler
 *                                  |
 *                                  v
 *                     SceneControl dispatcher
 *                _____________|________________
 *               /              |                \
 *              v               v                 v
 *          types 0..3       hidegroup 4       widening 5/6
 *              |               |                 |
 *       visual-handler         |                 |
 *       enable gate            |                 |
 *              |               |                 |
 *     +--------+------+        |                 |
 *     |        |      |        |                 |
 * trackdisplay |   Arc HV      |                 |
 *          redline             |                 |
 *     |        |      |        |                 |
 * presentation effects   per-group +0x55   gameplay/input state
 *                               |
 *                               v
 *                         render visibility
 *
 * This is the key Section 16 architectural result: a scenecontrol token does not
 * automatically imply a renderer-only effect. The native dispatcher separates
 * prepared visuals, group visibility state and genuine gameplay-space mutation.
 */

// -----------------------------------------------------------------------------
// 14. Parameter model by generic control family
// -----------------------------------------------------------------------------

/*
 * CONFIRMED useful parameter meanings for the completed controls:
 *
 * trackdisplay:
 *   intParameter   -> target track-display/opacity-style value
 *   floatParameter -> transition span; divided into 100 callback intervals
 *
 * redline:
 *   intParameter   -> not materially used by observed branch
 *   floatParameter -> one-shot scheduler delay/lifetime before cleanup
 *
 * arcahvdistort:
 *   intParameter   -> target opacity byte
 *   floatParameter -> FadeTo duration
 *
 * arcahvdebris:
 *   intParameter   -> target opacity byte
 *   floatParameter -> FadeTo duration
 *
 * hidegroup:
 *   intParameter   -> zero/nonzero visibility state
 *   floatParameter -> not needed by the observed hidegroup state mutation
 *   groupId +0x50  -> selected runtime group
 *
 * enwidencamera/enwidenlanes parameter models remain those completed in Section
 * 09 and are intentionally not duplicated here.
 */

// -----------------------------------------------------------------------------
// 15. CONFIRMED / RECONSTRUCTED / UNRESOLVED summary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED
 * ---------
 * - SceneControl strings map to enum values 0..6 as established in Section 09.
 * - LogicSceneControl +0x50 carries group/context identity used by hidegroup.
 * - LogicSceneControl +0x60 is assigned from a monotonically increasing global
 *   counter and serves as runtime SceneControl instance identity.
 * - LogicSceneControl +0x64/+0x68/+0x6C are type/float/int payload fields.
 * - SceneControls fire through the ordinary effective gameplay scheduler.
 * - Dispatcher types 0..3 pass through a visual-handler enable byte around
 *   +0x2C8, while hidegroup and widening controls take separate paths.
 * - GameSceneVisualControlHandler lazily prepares visual families which occur in
 *   the chart rather than blindly constructing every special effect.
 * - trackdisplay stores a current value initially around 255.
 * - trackdisplay uses a 100-step quadratic interpolation p^2.
 * - trackdisplay divides floatParameter by 100 for its repeating callback span.
 * - trackdisplay applies the interpolated value through Cocos opacity behaviour.
 * - trackdisplay also coordinates img/bg/bg_darken.png presentation.
 * - redline creates img/redline.png as a fresh event-specific sprite.
 * - redline rapidly appears, pulses, and is later removed by a one-shot callback.
 * - redline cleanup key includes LogicSceneControl +0x60 instance identity.
 * - redline floatParameter supplies the cleanup delay/lifetime.
 * - ARCAHV_DISTORT and ARCAHV_DEBRIS are prepared named visual nodes.
 * - arcahvdistort fades its prepared node using float duration / int opacity.
 * - arcahvdebris fades its prepared animated container using the same parameter
 *   model.
 * - hidegroup selects a runtime group using LogicSceneControl +0x50.
 * - hidegroup writes one byte to matching LogicNote +0x55 state.
 * - matching ArcTap children receive the same propagated state.
 * - player-input code uses +0x54 noinput/input-enabled state rather than +0x55.
 * - render update code consumes +0x55 as presentation visibility state.
 * - generic types 0..4 are therefore presentation-side in their observed paths;
 *   enwidencamera/enwidenlanes remain the known gameplay-affecting siblings.
 *
 * RECONSTRUCTED
 * -------------
 * - readable names such as runtimeInstanceId, hiddenByGroup,
 *   visualControlDispatchEnabled and trackDisplayValue
 * - pseudocode helper names and class layouts in this file
 * - description of trackdisplay as a coordinated track fade / background-darken
 *   effect; the opacity operations themselves are confirmed
 *
 * UNRESOLVED
 * ----------
 * - original member names for LogicSceneControl +0x50/+0x60 and handler fields
 * - exact high-level semantic identity of GameSceneVisualControlHandler +0x2C8
 * - exact artistic scale/action sequence for bg_darken.png
 * - original expanded meaning of surviving `ARCAHV_*` / `HV` terminology
 * - internal debris animation/shader/material details
 * - song-specific special-effect branches such as `rivenpilgrim` and song-ID
 *   `designant`, which are now the next excavation area
 */
