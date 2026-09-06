/*
 * Arcaea excavation notebook
 * Section 14: LogicArcNote body mode and the native DESIGNANT arctype
 *
 * STATUS: Arc body-mode / DESIGNANT slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native code/data/control flow.
 *   RECONSTRUCTED = readable representation assembled from confirmed behaviour.
 *   UNRESOLVED    = exact semantic name/purpose is not yet proved.
 *
 * Scope of this section:
 *   - native lexer token identity for false / true / designant
 *   - exact parser mapping of those Arc arctype tokens to integer 0 / 1 / 2
 *   - chart ArcNote integer field +0x68
 *   - propagation into LogicArcNote +0xA4
 *   - runtime auto-promotion of mode 0 -> mode 1 for Arcs carrying ArcTaps
 *   - judgement/contact consequences of modes 0 versus nonzero modes
 *   - mode-2-specific rendering colour and dynamic presentation factor
 *   - distinction between DESIGNANT chart syntax and unrelated song-ID checks
 *
 * Deliberately out of scope:
 *   - full RenderArcNote mesh/shader construction
 *   - exact semantic name of the global mode-2 presentation multiplier
 *   - every mode-2-specific presentation/range predicate
 *   - song-specific Designant anomaly/gameplay implementation
 *
 * This file builds directly on:
 *   04_arc_contact.cpp
 *   06_arctaps.cpp
 *   12_arc_contact_refinements.cpp
 *   13_arc_path_refinements.cpp
 *
 * Useful native anchors from the investigated ARM64 build:
 *   chart token lexer / `designant` recognition       ~0x0814A3C
 *   Arc arctype parser action                          ~0x0C2CE68
 *   chart ArcNote constructor                          ~0x0924B50
 *   chart ArcNote serializer                           ~0x0D06D58
 *   chart/runtime Arc factory                          ~0x186514C / ~0x1865418
 *   LogicArcNote runtime initializer                   ~0x0C664F8
 *   mode-colour presentation setup                     ~0x16A7F34
 *   mode-2 alpha/presentation multiplier               ~0x15750F4
 *   global mode-2 factor getter                        ~0x10AC8AC
 *   global mode-2 factor setter                        ~0x086D018
 *
 * WARNING ABOUT SYMBOLS:
 * The binary is stripped. Readable names below are reconstruction names unless
 * explicitly described as surviving RTTI/token strings or confirmed behaviour.
 */

#include <cstdint>
#include <vector>

// -----------------------------------------------------------------------------
// 1. The mystery integer is a first-class chart arctype, not a runtime invention
// -----------------------------------------------------------------------------

/*
 * CONFIRMED surviving native token strings and RTTI:
 *
 *   "false"      -> Tokens::TFALSE
 *   "true"       -> Tokens::TTRUE
 *   "designant"  -> Tokens::DESIGNANT
 *
 * RTTI names recovered from the token singleton vtables/typeinfo:
 *
 *   N6Tokens6TFALSEE
 *   N6Tokens5TTRUEE
 *   N6Tokens9DESIGNANTE
 *
 * The lexer recognises `designant` beside ordinary AFF/chart syntax including
 * camera, scenecontrol, timing, timinggroup, arctap and flick. Therefore this is
 * native chart syntax, not merely a song identifier or community-tool extension.
 */

struct ParsedArcTypeToken;

/*
 * CONFIRMED Arc parser action around ~0x0C2CE68.
 *
 * The parser invokes the token's native type-test virtual twice:
 *
 *   first descriptor  = Tokens::TTRUE
 *   second descriptor = Tokens::DESIGNANT
 *
 * and emits exactly:
 *
 *   DESIGNANT -> 2
 *   TTRUE      -> 1
 *   otherwise  -> 0
 *
 * The remaining accepted boolean arctype token supplied by the lexer is
 * Tokens::TFALSE, giving the ordinary 0 case.
 */
static int32_t parseArcBodyMode(const ParsedArcTypeToken& token)
{
    if (token.isType<Tokens::DESIGNANT>()) {
        return 2;
    }

    if (token.isType<Tokens::TTRUE>()) {
        return 1;
    }

    // Tokens::TFALSE in the normal grammar path.
    return 0;
}

// -----------------------------------------------------------------------------
// 2. Chart ArcNote stores the parsed integer verbatim
// -----------------------------------------------------------------------------

/*
 * CONFIRMED chart ArcNote constructor around ~0x0924B50.
 *
 * Selected constructor argument/field mapping:
 *
 *   startTime       -> +0x18
 *   endTime         -> +0x1C
 *   xStart/xEnd     -> +0x20/+0x24
 *   easing string   -> +0x28
 *   yStart/yEnd     -> +0x40/+0x44
 *   colour/index    -> +0x48
 *   effect string   -> +0x50
 *   arctype integer -> +0x68
 *   ArcTap data     -> around +0x70
 *   smoothness      -> +0x88
 *
 * The parser-produced 0/1/2 integer is passed as constructor argument w6 and is
 * stored directly at chart ArcNote +0x68.
 *
 * The native chart serializer later reads +0x68 as an integer. It does not give
 * us a second textual-name mapping, so this section deliberately relies on the
 * stronger parser/token evidence above rather than inventing one.
 */
struct ChartArcNote {
    int32_t startTimeMs;
    int32_t endTimeMs;
    // Other chart fields omitted.
    int32_t arcBodyMode; // conceptual reference to +0x68
};

// -----------------------------------------------------------------------------
// 3. Chart mode becomes LogicArcNote +0xA4
// -----------------------------------------------------------------------------

/*
 * CONFIRMED chart -> runtime construction:
 *
 * The Arc factory carries chart ArcNote +0x68 through the runtime creation path.
 * LogicArcNote's initializer receives that integer and stores it at:
 *
 *     LogicArcNote +0xA4
 *
 * before any later mode-specific processing.
 *
 * Therefore the long-standing +0xA4 values originate from the chart arctype:
 *
 *     false      -> 0
 *     true       -> 1
 *     designant  -> 2
 */
enum class ArcBodyMode : int32_t {
    JudgedBody = 0,
    SkylineOrTrace = 1, // RECONSTRUCTED friendly name; native token is TTRUE
    Designant = 2,      // native token name is CONFIRMED
};

struct LogicArcNote {
    ArcBodyMode bodyMode; // conceptual reference to +0xA4
    // Other Arc state omitted.
};

// -----------------------------------------------------------------------------
// 4. Runtime may auto-promote a false/0 Arc carrying ArcTaps to mode 1
// -----------------------------------------------------------------------------

/*
 * CONFIRMED in LogicArcNote initialization around ~0x0C6663C:
 *
 *   - store supplied mode at +0xA4
 *   - copy ArcTap children into the runtime ArcTap vector around +0x120
 *   - if supplied mode == 0 and that ArcTap vector is non-empty:
 *         write 1 to +0xA4
 *
 * Thus runtime mode 1 has TWO creation routes:
 *
 *   1. explicit chart `true`
 *   2. automatic promotion of chart `false` when ArcTap children are present
 *
 * Runtime mode 2 is not produced by this promotion. The observed mode-2 source
 * is the native DESIGNANT chart token.
 */
void applyArcTapBodyPromotion(
    LogicArcNote& arc,
    std::vector<void*>& arcTapChildren)
{
    if (arc.bodyMode == ArcBodyMode::JudgedBody &&
        !arcTapChildren.empty()) {
        arc.bodyMode = ArcBodyMode::SkylineOrTrace;
    }
}

/*
 * IMPORTANT:
 * This proves the native transformation but not the original design rationale.
 * A safe gameplay description is simply that an Arc carrying ArcTaps cannot
 * remain ordinary body mode 0 through this initializer.
 */

// -----------------------------------------------------------------------------
// 5. Mode 0 versus nonzero modes: body judgement boundary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from Sections 04, 12 and 13:
 *
 * Mode 0:
 *   - participates in ordinary Arc-body geometric touch qualification
 *   - can refresh +0x64/+0x65 current body contact
 *   - participates in common long-note body success/LOST processing
 *   - judged connected continuations can carry ownership across seams
 *   - judged seam tick-merging logic explicitly searches for mode 0 successors
 *
 * Any nonzero mode (1 or 2):
 *   - ordinary Arc-body contact predicate rejects it
 *   - ordinary Arc-body LOST processing is suppressed
 *
 * Therefore mode 2 is NOT a second judged Arc-body mechanic. Like mode 1, its
 * body is nonjudged by the ordinary Arc contact/long-note machinery.
 *
 * ArcTaps remain separate LogicArcTapNote point notes and are not erased merely
 * because the parent body is mode 1 or mode 2.
 */
static bool hasOrdinaryJudgedArcBody(const LogicArcNote& arc)
{
    return arc.bodyMode == ArcBodyMode::JudgedBody;
}

// -----------------------------------------------------------------------------
// 6. Mode 2 is visually distinct: native hot red/pink colour
// -----------------------------------------------------------------------------

struct RGB8 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

/*
 * CONFIRMED presentation setup around ~0x16A7F34:
 *
 * The render-side object copies LogicArcNote +0xA4 into its own mode field and
 * explicitly tests 2 before 1.
 *
 * Mode 2 constructs the colour:
 *
 *     RGB(240, 41, 97)
 *
 * This is a dedicated hot red/pink presentation path.
 *
 * A mode-1 branch under one presentation condition constructs:
 *
 *     RGB(192, 105, 155)
 *
 * but mode 1 has additional presentation branches, so this file does NOT call
 * that triplet the universal mode-1 colour.
 */
static RGB8 specialArcModeColour(ArcBodyMode mode)
{
    if (mode == ArcBodyMode::Designant) {
        return {240, 41, 97};
    }

    if (mode == ArcBodyMode::SkylineOrTrace) {
        // One confirmed mode-1 presentation branch only.
        return {192, 105, 155};
    }

    return ordinaryArcColourFromOtherState();
}

// -----------------------------------------------------------------------------
// 7. Designant has its own global presentation multiplier
// -----------------------------------------------------------------------------

/*
 * CONFIRMED:
 * A global float at native address ~0x1BABEE0:
 *
 *   - defaults to 1.0
 *   - has a dedicated getter around ~0x10AC8AC
 *   - has a dedicated setter around ~0x086D018
 *   - is changed by several runtime state-transition paths
 *
 * Render/update code around ~0x15750F4 does:
 *
 *     alphaLikeValue = ordinaryArcAlphaCalculation(...)
 *
 *     if (arc.bodyMode == 2)
 *         alphaLikeValue *= globalMode2Factor;
 *
 * before comparing/updating the render node's 8-bit alpha-style property.
 *
 * Another RenderArcNote path around ~0x13B6558 also distinguishes mode 2 and
 * derives an integer parameter proportional to:
 *
 *     globalMode2Factor * 125
 *
 * The exact semantic name of that second render parameter is UNRESOLVED because
 * observed values include negative values on other branches and should not be
 * mislabeled as opacity/z-order/etc. without proof.
 */
float getDesignantPresentationFactor(); // RECONSTRUCTED name

uint8_t applyDesignantAlphaFactor(
    const LogicArcNote& arc,
    float ordinaryAlpha)
{
    float value = ordinaryAlpha;

    if (arc.bodyMode == ArcBodyMode::Designant) {
        value *= getDesignantPresentationFactor();
    }

    return clampToByte(value);
}

/*
 * Gameplay/presentation conclusion:
 * Mode 2 is not merely `true` with a different integer label. It has dedicated
 * colour and dynamically controlled presentation behavior.
 */

// -----------------------------------------------------------------------------
// 8. Mode 2 is intentionally excluded from several ordinary Arc calculations
// -----------------------------------------------------------------------------

/*
 * CONFIRMED examples:
 *
 * - During one chart/runtime range scan around ~0x1865658, LogicArcNote objects
 *   with +0xA4 == 2 are skipped from a bound calculation which other notes can
 *   extend.
 *
 * - A later related bound update only counts mode 0 Arcs.
 *
 * - Several Arc-associated predicates around ~0x0C78900 and other locations
 *   explicitly return a different result when the associated Arc is mode 2.
 *
 * - Ordinary connected-seam judged logic from Section 13 looks specifically for
 *   mode 0 continuations, excluding both mode 1 and mode 2.
 *
 * These prove that Designant has dedicated nonordinary treatment outside simple
 * colouring. The exact user-facing purpose of every one of these exclusions is
 * outside this focused slice and remains UNRESOLVED rather than guessed.
 */

// -----------------------------------------------------------------------------
// 9. Do not confuse DESIGNANT syntax with song-ID special cases
// -----------------------------------------------------------------------------

/*
 * CONFIRMED separately:
 * The literal string "designant" is ALSO used by song/context-specific native
 * branches elsewhere in the game, alongside IDs such as fractureray and other
 * special charts.
 *
 * These are two distinct facts:
 *
 *   A. Tokens::DESIGNANT is a first-class chart token and maps Arc arctype -> 2.
 *   B. The song/content identifier "designant" is checked by special gameplay or
 *      presentation code in other systems.
 *
 * This section does not infer that every mode-2 Arc requires the Designant song,
 * nor that every Designant-song special branch exists solely to service mode 2.
 */

// -----------------------------------------------------------------------------
// 10. Full readable model
// -----------------------------------------------------------------------------

/*
 * Native chart syntax:
 *
 *       false            true             designant
 *         |                |                  |
 *         v                v                  v
 *         0                1                  2
 *          \               |                 /
 *           \              |                /
 *              Chart ArcNote +0x68
 *                       |
 *                       v
 *              LogicArcNote +0xA4
 *                       |
 *            +----------+-----------+
 *            |                      |
 *          mode 0                mode != 0
 *            |                      |
 *     judged Arc body        no ordinary body
 *     touch / LOST path      contact / LOST path
 *                                   |
 *                          +--------+--------+
 *                          |                 |
 *                        mode 1            mode 2
 *                          |                 |
 *                    trace/skyline       DESIGNANT
 *                    presentation       hot red/pink
 *                                      + global dynamic
 *                                        presentation factor
 *
 * Additional initializer rule:
 *
 *     chart mode 0 + ArcTap children -> runtime mode 1
 */

// -----------------------------------------------------------------------------
// 11. CONFIRMED / RECONSTRUCTED / UNRESOLVED summary
// -----------------------------------------------------------------------------

/*
 * CONFIRMED
 * ---------
 * - The native lexer has Tokens::TFALSE, Tokens::TTRUE and Tokens::DESIGNANT.
 * - `designant` is recognised as chart syntax.
 * - Arc parser action maps TTRUE ->1 and DESIGNANT ->2; the false/default accepted
 *   arctype path becomes 0.
 * - The parsed integer is stored at chart ArcNote +0x68.
 * - The runtime Arc initializer stores the supplied integer at LogicArcNote +0xA4.
 * - If supplied mode is 0 and the Arc has ArcTap children, runtime mode becomes 1.
 * - Mode 0 is the ordinary judged Arc-body mode.
 * - Modes 1 and 2 suppress ordinary Arc-body contact/LOST processing.
 * - Mode 2 has a dedicated RGB(240,41,97) render colour.
 * - Mode 2 multiplies an alpha-style render value by a dedicated global float.
 * - That global defaults to 1.0 and is dynamically changed by runtime state.
 * - Other presentation/range code also distinguishes mode 2 specifically.
 * - Native song-ID `designant` checks exist separately from Tokens::DESIGNANT.
 *
 * RECONSTRUCTED
 * -------------
 * - ArcBodyMode enum and friendly names JudgedBody / SkylineOrTrace / Designant
 * - `getDesignantPresentationFactor` readable global name
 * - interpretation of mode 1 as the ordinary trace/skyline body based on its
 *   established nonjudged behavior and native TTRUE chart token
 *
 * UNRESOLVED
 * ----------
 * - original native member/enum name for chart +0x68 / runtime +0xA4
 * - exact semantic name of global mode-2 float at ~0x1BABEE0
 * - exact meaning of the secondary mode-dependent render parameter near 125
 * - exact meaning of the mode-1-associated pointer at Arc +0x178
 * - design rationale for automatically promoting mode0+ArcTaps to runtime mode1
 * - precise higher-level role of every mode-2-specific range/predicate exclusion
 * - song-specific Designant special-effect implementation
 */
