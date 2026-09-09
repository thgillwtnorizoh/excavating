// Arcaea native-engine deep dive
// Section 05: Gameplay UI / HUD
//
// IMPORTANT:
// This is reconstructed reference pseudocode, NOT recovered original source.
// Evidence comes from the investigated ARM64 libcocos2dcpp.so, retained RTTI
// and lambda names, Cocos Studio node/resource strings, direct native consumers,
// settings load/save paths, and special-scene callbacks.
//
// Evidence labels:
//   CONFIRMED     = directly supported by native data/control flow/constants.
//   RECONSTRUCTED = readable semantic structure assembled from confirmed facts.
//   UNRESOLVED    = exact original name/design reason is not proved.
//
// Scope: visible gameplay UI/HUD and external systems that alter that HUD.
// Core note judgement, gauge arithmetic and world rendering belong to Sections
// 03/04. The game model can mechanically run without this presentation layer.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace reconstructed {

struct Song;
struct GameModel;
struct ScoreState;
struct LifeBarState;
struct CharacterAbility;
struct PauseLayerDelegate;
struct SpinCountLabel;
struct HPBar;

// ============================================================================
// 1. CONFIRMED: UILayer is a context-sensitive gameplay overlay
// ============================================================================

// Surviving native signature:
//
//   UILayer::init(
//       PauseLayerDelegate*, Song*, DifficultyClass, int,
//       PlayModifier, GameMode, CharacterAbility*, GameModel*, PlayParameters)
//
// Therefore the HUD is built with song/difficulty/modifier/mode/ability/model
// context. It is not a static overlay whose only changing field is score.

struct UILayerSelectedFields {
    void* recallLabel;           // around +0x360
    bool viewNoteResults;        // +0x368
    HPBar* hpBar;                // +0x378
    void* rightRoot;             // +0x380
    SpinCountLabel* scoreLabel;  // +0x390
    int32_t durationMs;          // +0x3AC
    void* progressBar;           // +0x3B8
    void* progressGlow;          // +0x3C0
    void* hardWarningNode;       // around +0x3C8
};

// Base layouts/resources establish visible shell pieces including:
//
//   IngameUIRight.csb
//     jacket / title / artist / difficulty
//     progress_bar / progress_glow
//     pacemakerLabel / pacemakerPlusMinus
//     noteInfo -> PURE / FAR / LOST / EARLY / LATE
//
//   IngameUILeft.csb
//     pauseButton
//
//   HpBar.csb
//     hpLabel / hpBar / hpBar2 / hpGlow / insightOverlay / hp_top
//     beyond_marker and related gauge presentation nodes.

// ============================================================================
// 2. CONFIRMED: HUD-facing ScoreState fields
// ============================================================================

// Independent UI consumers refine the common ScoreState layout:
struct ScoreStateHudView {
    int32_t numericalScore;   // +0x14
    int32_t recall;           // +0x1C
    int32_t maxPure;          // +0x20
    int32_t pure;             // +0x24
    int32_t far;              // +0x28
    int32_t lost;             // +0x2C

    // Timing-side/statistical families used by noteInfo:
    int32_t latePrimary;      // +0x40, reconstructed member name
    int32_t earlyPrimary;     // +0x44
    int32_t lateSecondary;    // +0x48
    int32_t earlySecondary;   // +0x4C
};

// These are presentation consumers of ScoreState. They do not own judgement.

// ============================================================================
// 3. CONFIRMED: normal numerical score uses SpinCountLabel
// ============================================================================

// Native RTTI:
//   SpinCountLabel : cocos2d::CCLabelCustomRenderSize
//
// Normal gameplay score uses this class at UILayer +0x390. The separate
// SpinCountSpacedLabel : SpacedScoreText family is not the ordinary score path;
// its traced factory builds a specialised formatted progress-like number display.

// Score transitions roll toward the requested target over approximately 500 ms.
int32_t interpolateDisplayedScore(
    int32_t start,
    int32_t target,
    float elapsedMs)
{
    const float p = std::clamp(elapsedMs / 500.0f, 0.0f, 1.0f);
    return static_cast<int32_t>(
        start + (target - start) * p);
}

// SpinCountLabel then formats/pads the integer to its configured minimum digit
// count. One presentation mode uses apostrophe-style thousands grouping.

// ============================================================================
// 4. CONFIRMED: recall/combo display is separate from numerical score
// ============================================================================

// ScoreState +0x1C is displayed through a separate custom-size label path.
// Change detection triggers position/scale/opacity animation; zero and nonzero
// recall have distinct presentation states.

void updateRecallPresentation(
    UILayerSelectedFields& ui,
    int32_t previousRecall,
    int32_t currentRecall)
{
    if (previousRecall == currentRecall)
        return;

    setLabelInteger(ui.recallLabel, currentRecall);
    animateRecallChange(ui.recallLabel, currentRecall != 0);
}

// ============================================================================
// 5. CONFIRMED: noteInfo and the CURRENT Early/Late setting
// ============================================================================

// CharacterAbilityViewNoteResults enables the noteInfo panel. The recurring HUD
// update rewrites PURE/FAR/LOST and EARLY/LATE from ScoreState.
//
// Critical settings closure:
//
//   current key: "lateearly_showall"
//
// Loader around ~0x1A02CF0 reads this bool (default false) and stores it directly
// into the settings object at +0x18.
// Setter around ~0x0F6B360 writes bool(value) to +0x18 and persists the SAME key.
// noteInfo around ~0x1609720 reads exactly that runtime +0x18 byte.
//
// Older keys:
//   "arcaea_early_all"
//   "arcaea_late_all"
// survive in a settings migration/compatibility path. They are NOT read by the
// live noteInfo updater and should not be described as its current per-frame
// configuration source.

struct GameplayDisplaySettings {
    bool lateEarlyShowAll; // +0x18, behaviour CONFIRMED
};

void updateNoteInfo(
    const GameplayDisplaySettings& settings,
    const ScoreStateHudView& score,
    NoteInfoWidgets& ui)
{
    ui.pure  = score.pure;
    ui.far   = score.far;
    ui.lost  = score.lost;

    if (settings.lateEarlyShowAll) {
        ui.early = score.earlyPrimary + score.earlySecondary;
        ui.late  = score.latePrimary  + score.lateSecondary;
    }
    else {
        ui.early = score.earlyPrimary;
        ui.late  = score.latePrimary;
    }
}

// Exact original semantic distinction between primary/secondary timing counters
// is not promoted here; only their confirmed display combination is documented.

// ============================================================================
// 6. CONFIRMED: pacemaker is a projected-final-score grade pacemaker
// ============================================================================

// The live pacemaker estimates final score from judged progress, then compares
// that projection against ordered grade thresholds. It is not merely a rival
// score label in this traced branch.

struct PacemakerProjection {
    int32_t projectedScore;
    const char* grade;
    int32_t targetScore;
    int32_t signedDifference;
};

static const char* gradeName(int id)
{
    switch (id) {
        case 1: return "C";
        case 2: return "B";
        case 3: return "A";
        case 4: return "AA";
        case 5: return "EX";
        case 6: return "EX+";
        default: return "D";
    }
}

static int32_t gradeThreshold(int id)
{
    switch (id) {
        case 1: return 8600000;
        case 2: return 8900000;
        case 3: return 9200000;
        case 4: return 9500000;
        case 5: return 9800000;
        case 6: return 9900000;
        default: return 0;
    }
}

int32_t projectFinalScore(
    const ScoreStateHudView& score,
    int32_t totalUnits)
{
    const int32_t judged = score.pure + score.far + score.lost;
    if (judged <= 0 || totalUnits <= 0)
        return 0;

    if (judged >= totalUnits)
        return score.numericalScore;

    const float progress =
        static_cast<float>(judged) /
        static_cast<float>(totalUnits);

    const float accuracyBasis =
        ((static_cast<float>(score.pure)
          + static_cast<float>(score.far) * 0.5f)
         / static_cast<float>(totalUnits))
        * 10000000.0f
        + static_cast<float>(score.maxPure);

    return static_cast<int32_t>(accuracyBasis / progress);
}

// Native code chooses the relevant threshold from the ordered C..EX+ family and
// shows projectedScore-targetScore through pacemakerPlusMinus, with explicit
// positive/negative presentation. A separate special "MAX" path also exists.

// ============================================================================
// 7. CONFIRMED: progress bar is gameplay time / supplied duration
// ============================================================================

float progressBarPosition(int32_t nowMs, int32_t durationMs)
{
    if (durationMs == 0)
        return 0.0f;

    return std::max(
        static_cast<float>(nowMs)
            / static_cast<float>(durationMs)
            * 380.0f,
        0.0f);
}

// progress_glow follows the resulting bar endpoint with an observed offset around
// -405. This is clock/duration presentation, not note-count progress.

// ============================================================================
// 8. CONFIRMED: HPBar is the visual consumer of LifeBarState
// ============================================================================

// LifeBarState (Section 03) is authoritative gameplay gauge state. HPBar is a
// stateful Cocos widget which receives that state every HUD update.

struct HPBarSelectedFields {
    void* root;             // +0x2A0
    void* hpBar;            // +0x2B0
    void* hpBar2;           // +0x2B8
    void* insightOverlay;   // +0x2C0
    void* hpGlow;           // +0x2C8
    void* hpTop;            // +0x2D0
    void* hpLabel;          // +0x2F0

    float previousGauge;    // around +0x378
    float thresholdState;   // around +0x37C
    float specialMaximum;   // around +0x380/+0x388 family

    int32_t playModifier;   // +0x398
    CharacterAbility* ability; // +0x3A0

    bool hideLifeBarNell;   // +0x3AB, reconstructed member name
};

// Ordinary fill is implemented by changing the visible texture rectangle rather
// than simply scaling one whole texture:
//
//   fillPixels = displayedRR/100 * fullBarHeight
//   rect = {0, fullBarHeight-fillPixels, width, fillPixels}
//
// hpBar2 is actively used by specialised/multipart gauge presentation paths.

// ============================================================================
// 9. CONFIRMED: complete common HPBar ability RTTI set
// ============================================================================

// The live HPBar updater around ~0x178D4FC performs dynamic_cast checks against
// the following CharacterAbility subclasses. There is no remaining anonymous
// cast in this common update path:
//
//   CharacterAbilityGaugeChunithm
//   CharacterAbilityGaugeFixedValueCondition
//   CharacterAbilityClearBasedOnScore
//   CharacterAbilityClearBasedOnBestGrade
//   CharacterAbilityBonusesBasedOnPeakHealth
//   CharacterAbilityMaxHealthReducesBasedOnCurrentHealth
//   CharacterAbilitySafe
//
// Other HUD-affecting ability paths such as HideLifebarNell and Insight switching
// occur outside this exact common-cast chain.

// Selected confirmed effects:
//
// GaugeChunithm:
//   ability +0xA0 float scales incoming displayed gauge state and participates in
//   specialised hpBar2 presentation.
//
// GaugeFixedValueCondition:
//   condition-dependent value participates in visible gauge-label formatting.
//
// ClearBasedOnScore / ClearBasedOnBestGrade:
//   specialised label/grade presentation; best-grade uses the same C..EX+ text
//   family as pacemaker.
//
// BonusesBasedOnPeakHealth:
//   special/cyclic health presentation and dedicated C2-like HUD module.
//
// MaxHealthReducesBasedOnCurrentHealth:
//   visible maximum/fill geometry changes as the gameplay maximum changes.
//
// Safe:
//   owns its own branch in the live gauge presentation path.

// ============================================================================
// 10. CONFIRMED: Nell, Insight switch and 30-RR warning are three systems
// ============================================================================

// CharacterAbilityHideLifebarNell sets the HPBar +0x3AB hide state. Normal label
// construction selects an empty string and related warning presentation is
// suppressed. This is separate from Insight.

// A real retained method exists:
//   HPBar::performInsightHardSwitch()
//
// UILayer invokes it once when a live gameplay flag enters the required state and
// the HPBar has not switched yet. It installs a 30-ish threshold presentation,
// records previous gauge state, manipulates insightOverlay and normal gauge nodes,
// and performs short fade/move/scale actions. This is a one-shot HUD morph.

// Separately, the recurring UILayer loop tracks previous/current RR and animates
// a hard-like warning node when selected modifier families cross the 30-RR seam.
// Nell hiding suppresses that warning. Therefore:
//
//   HideLifebarNell != Insight switch != 30-RR crossing warning.

// ============================================================================
// 11. CONFIRMED: CharacterAbility can install live HUD callbacks
// ============================================================================

// Several ability-specific HUDs are not static construction-time skins. UILayer
// installs callbacks into the active ability so gameplay events/state can drive
// existing HUD nodes.

// Ongeki fragment-result module:
//   CharacterAbilityModifyFragmentOnResultOngeki
//     -> callback selects an available indicator node, positions/enables it,
//        updates related numeric text and runs short actions.
//
// Ongeki bonus module:
//   CharacterAbilityBonusesBasedOnOngeki
//     -> callback(CharacterAbilityBonusType)
//        type 0 -> hp_item_gain_step.png
//        type 2 -> hp_item_gain_over.png
//        otherwise -> hp_item_gain_frag.png
//        then reuses/animates a gain indicator.
//
// Luna Ilot / Rotaeno:
//   CharacterAbilityLunaIlot owns a registered live callback which updates
//   backing/medal/bar presentation from ability progress/state.
//
// DJMAX Fever:
//   CharacterAbilityDjmaxFever owns a callback registered around ability +0xC0.
//   It reads live Fever fields, updates "<n>x" text and switches/animates the
//   1x/2x/5x families. Surviving action name includes animateFeverDecreasing.
//   Observed visual grouping uses <2, 2..4, >=5.
//
// Peak-health/C2:
//   CharacterAbilityBonusesBasedOnPeakHealth installs a live callback operating
//   on health/progress state and HARD/OVER/STEP/FRAG presentation nodes.
//
// ViewNoteResults:
//   CharacterAbilityViewNoteResults instead uses the normal recurring UILayer
//   update to rewrite noteInfo counters.
//
// ClearBasedOnBestGrade:
//   dynamic gauge presentation is handled through HPBar rather than requiring an
//   equivalent independent UILayer event callback.

// ============================================================================
// 12. CONFIRMED: special songs can mutate already-existing HUD objects
// ============================================================================

// SpecialSceneTempestissimoChallenge has retained methods:
//   doPreTriggerUiChanges()
//   doTriggerUiChanges()
//
// Pre-trigger callbacks directly receive an existing cocos2d::Node* and the
// normal SpinCountLabel*. They reposition/scale/fade/action these existing HUD
// objects.
//
// Trigger callbacks directly receive:
//   HPBar*
//   Sprite*, Sprite*
//   Sprite*
//   Sprite*
//
// They align special visuals to existing gauge geometry and run short movement /
// opacity / shake-like action pieces. Thus the special scene extends the live HUD
// rather than replacing ScoreState/LifeBarState.

// SpecialSceneAegleseekerChallenge::performSpecialSongUpdate(GameScene*) is
// similarly gameplay-clock driven. Confirmed one-shot thresholds include:
//
//   81,066 ms
//   >94,500 ms major presentation insertion
//
// Staged texture reveal widths (height 76) include:
//   95,460 -> 181
//   95,690 -> 304
//   95,880 -> 450
//   96,090 -> 489
//   96,300 -> 609
//
// A later phase around 96,750 ms further reconfigures/fades special nodes.
// A nested callback directly accepts SpinCountLabel*, proving the ordinary score
// widget itself can be animated by the special-scene system.

// ============================================================================
// 13. CONFIRMED cleanup model: scene ownership, not explicit HUD restoration
// ============================================================================

// This closes the special-scene lifetime question.
//
// TempestissimoChallenge destructor (~0x1260AB0) releases retained special nodes /
// resources at fields around +0x300/+0x308/+0x338..+0x368, then falls through to
// the shared special-scene/base destructor.
//
// AegleseekerChallenge destructor (~0x0E6BC8C) similarly releases its retained
// nodes/resources around +0x310..+0x380 before base destruction.
//
// No dedicated Tempestissimo/Aegleseeker "restore normal HUD" method family was
// found, and these destructors do not reset position/scale/opacity on ordinary
// UILayer objects before destruction.
//
// Durable model:
//   - individual actions may remove/fade special nodes during the sequence;
//   - retained special objects are released by special-scene destruction;
//   - ordinary HUD mutations are not manually unwound by a separate restore pass;
//   - normal scene/UI ownership teardown ends the special presentation lifetime.

// ============================================================================
// 14. CONFIRMED: pause input belongs to UILayer, pause transition does not
// ============================================================================

// UILayer::init contains two widget-touch callbacks plus one long-press callback.
// All three:
//   1. check gameplay/pause eligibility,
//   2. perform common UI feedback,
//   3. converge on the same PauseLayerDelegate operation.
//
// One touch route has approximately 750 ms debounce. Long-press requires the
// recognizer's active/recognised state.
//
// Therefore the pause button/gesture is HUD input, while actual gameplay pausing
// is delegated out of UILayer. Pause-menu internals are outside Section 05.

// ============================================================================
// 15. RECONSTRUCTED from CONFIRMED calls: recurring UILayer update order
// ============================================================================

void updateGameplayHud(
    UILayer& ui,
    GameModel& model,
    float dt)
{
    ScoreStateHudView& score = hudScoreState(model);

    // 1. Recall/combo presentation.
    updateRecallIfChanged(ui, score.recall);

    // 2. Optional note-result panel.
    if (ui.viewNoteResults)
        updateNoteInfo(currentDisplaySettings(), score, ui.noteInfo());

    // 3. Projected-grade pacemaker.
    updatePacemaker(ui, score, totalScoringUnits(model));

    // 4. Rolling numerical score target.
    ui.scoreLabel->setTarget(score.numericalScore);

    // 5. Authoritative gauge model -> HPBar presentation.
    LifeBarState& bar = activeLifeBar(model);
    ui.hpBar->updateFromGameplayState(bar, score, model);

    // 6. One-shot Insight presentation switch if newly required.
    if (shouldPerformInsightSwitch(model, *ui.hpBar))
        ui.hpBar->performInsightHardSwitch();

    // 7. Track-time progress bar and glow.
    const int now = effectiveGameplayTime(model.clock());
    setProgress(ui.progressBar, progressBarPosition(now, ui.durationMs));
    moveGlowToProgressEndpoint(ui.progressGlow, ui.progressBar);

    // 8. Hard-like 30-RR crossing warning, unless presentation is hidden.
    updateThirtyRRWarning(ui, bar);

    // Ability callbacks such as Ongeki/DJMAX/Rotaeno are event/state callbacks
    // installed separately and are not all polled by this common update body.
}

// Ordering names are RECONSTRUCTED; relative call/data-flow ordering above is
// based on the traced live UILayer consumers.

// ============================================================================
// 16. Complete responsibility model
// ============================================================================

/*
 *                              GAMEPLAY CORE
 *                    ScoreState / LifeBarState / clock
 *                           /        |        \
 *                          /         |         \
 *                         v          v          v
 *                 score/recall     HPBar     progress
 *                       \             |          /
 *                        \            |         /
 *                              UILayer
 *                                 |
 *              +------------------+------------------+
 *              |                  |                  |
 *              v                  v                  v
 *       base gameplay HUD   CharacterAbility    SpecialScene
 *       score/progress/etc   live extensions    live mutation
 *              |                  |                  |
 *              +------------------+------------------+
 *                                 |
 *                          Cocos UI scene graph
 *
 * UILayer/HPBar are presentation consumers. Removing them does not redefine note
 * judgement, score counters, RR arithmetic, Arc contact or world rendering.
 */

// ============================================================================
// 17. Bounded unresolved / intentionally excluded details
// ============================================================================

// The fundamental gameplay-HUD model is complete. Remaining details are bounded:
//
// 1. Exact original semantic distinction between ScoreState timing-counter pairs
//    at +0x40/+0x48 and +0x44/+0x4C. Their display use is confirmed.
// 2. Pixel-perfect action/easing nesting for every ability/special-song HUD
//    animation is deliberately not reproduced where it does not change the
//    architecture or live data mapping.
// 3. SpinCountSpacedLabel's separate specialised presentation is not part of the
//    ordinary gameplay HUD consumer traced here.
// 4. Pause menu internals and non-gameplay menus remain outside project scope.
//
// Importantly, the former mysteries are CLOSED:
//   - current Early/Late "show all" source = lateearly_showall -> settings +0x18
//   - common HPBar ability RTTI chain has no anonymous remaining cast
//   - Tempestissimo/Aegleseeker use destruction/scene ownership, not an explicit
//     normal-HUD restoration pass.

} // namespace reconstructed
