/*
 * Arcaea excavation notebook
 * Section 05: LogicArcNote path interpolation and easing
 *
 * STATUS: arc-path mathematics slice complete
 * STYLE: C++-like pseudocode, NOT recovered source code
 *
 * Evidence terminology:
 *   CONFIRMED     = directly supported by native code/data/control flow.
 *   RECONSTRUCTED = readable representation assembled from confirmed behaviour.
 *   UNRESOLVED    = exact semantic name/purpose is not yet proved.
 *
 * Scope of this section:
 *   - native easing-type parser and runtime enum
 *   - mapping of AFF arc endpoint fields into LogicArcNote
 *   - exact normalized X/Y easing formula for all eight classic arc types
 *   - the special cubic construction used by `b`
 *   - how the analytic path is pre-sampled into Vec3-like polyline data
 *   - how gameplay later consumes that sampled path to refresh the cached
 *     expected Arc position used by Section 04's contact test
 *
 * Deliberately out of scope:
 *   - arc rendering meshes/textures
 *   - full camera/timinggroup architecture
 *   - complete meaning of the two sampled-path vectors and density multiplier
 *   - ArcTap judgement/contact behaviour beyond noting that ArcTap placement
 *     reuses the parent Arc path evaluator
 *
 * This file builds directly on:
 *   02_note_fundamentals.cpp
 *   03_long_notes.cpp
 *   04_arc_contact.cpp
 *
 * Useful native anchors from the investigated ARM64 build:
 *   easing string -> enum parser                  ~0x0D51010
 *   LogicArcNote runtime initialiser              ~0x0C664F8
 *   non-b sinusoidal path evaluator               ~0x180A190
 *   b cubic Bezier evaluator                      ~0x14F3078
 *   Arc sampled-path builder                      ~0x1925084
 *   gameplay sampled-path consumer/cache update   ~0x19F3D4C
 *
 * WARNING ABOUT SYMBOLS:
 * The binary is stripped. Readable names below are reconstruction names unless
 * explicitly described as surviving chart strings, RTTI names, or confirmed
 * field behaviour.
 */

#include <cmath>
#include <cstdint>
#include <vector>

// -----------------------------------------------------------------------------
// 1. Native easing enum
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from surviving chart strings and one parser routine.
 *
 * The parser compares the exact strings used by AFF and returns these compact
 * integer values:
 *
 *     "s"    -> 0
 *     "b"    -> 1
 *     "si"   -> 2
 *     "so"   -> 3
 *     "sisi" -> 4
 *     "sosi" -> 5
 *     "siso" -> 6
 *     "soso" -> 7
 *
 * The resulting integer is stored at LogicArcNote +0x15C by the runtime arc
 * initialiser. Therefore +0x15C is CONFIRMED as the Arc easing-type field.
 */
enum class ArcEasingType : int32_t {
    S    = 0,
    B    = 1,
    SI   = 2,
    SO   = 3,
    SISI = 4,
    SOSI = 5,
    SISO = 6,
    SOSO = 7,
};

// -----------------------------------------------------------------------------
// 2. Endpoint fields entering LogicArcNote
// -----------------------------------------------------------------------------

/*
 * CONFIRMED by following the native AFF serializer and the chart-object to
 * LogicArcNote creation path together.
 *
 * The serializer exposes the familiar ordering:
 *
 *   arc(startTime, endTime,
 *       xStart, xEnd,
 *       easing,
 *       yStart, yEnd,
 *       color,
 *       effect,
 *       trace, ...)
 *
 * Selected chart-object fields observed in native code:
 *   +0x18 startTime
 *   +0x1C endTime
 *   +0x20 xStart
 *   +0x24 xEnd
 *   +0x28 easing string
 *   +0x40 yStart
 *   +0x44 yEnd
 *   +0x48 colour/index field
 *   +0x50 effect string
 *   +0x68 trace-like field established in Section 04
 *
 * During runtime creation:
 *   - the easing string is parsed and becomes LogicArcNote +0x15C
 *   - yStart/yEnd are passed as floats and copied into +0xA8/+0xAC
 *   - xStart/xEnd travel through endpoint/position objects later read by the
 *     path builder; their float member is converted into gameplay horizontal
 *     coordinates with arithmetic equivalent to x * 850 - 425.
 *
 * Exact original class/member names of those endpoint objects are UNRESOLVED.
 */
struct ArcChartParameters {
    int32_t startTimeMs;
    int32_t endTimeMs;
    float xStart;
    float xEnd;
    ArcEasingType easing;
    float yStart;
    float yEnd;
};

// -----------------------------------------------------------------------------
// 3. Three normalized scalar functions are enough for the classic arc types
// -----------------------------------------------------------------------------

static constexpr double kPi = 3.14159265358979323846;

/*
 * CONFIRMED native mathematics.
 *
 * L is ordinary linear interpolation.
 *
 * SI comes directly from a sin() call with the native pi/2 constant:
 *
 *     SI(t) = sin(pi*t/2)
 *
 * SO is implemented natively as:
 *
 *     1 - sin(pi*(t + 1)/2)
 *
 * which is exactly:
 *
 *     SO(t) = 1 - cos(pi*t/2)
 *
 * IMPORTANT TERMINOLOGY NOTE:
 * Do not rename SI/SO according to a generic UI animation library's
 * "ease-in/ease-out" convention. Their chart names and exact formulas are the
 * reliable facts. Mathematically SI starts with the larger initial slope and SO
 * starts with zero slope.
 */
static double L(double t)
{
    return t;
}

static double SI(double t)
{
    return std::sin(kPi * t * 0.5);
}

static double SO(double t)
{
    return 1.0 - std::cos(kPi * t * 0.5);
}

/*
 * CONFIRMED construction for `b`, simplified mathematically.
 *
 * The native `b` helper evaluates an ordinary cubic Bezier with weights:
 *
 *   (1-t)^3
 *   3(1-t)^2 t
 *   3(1-t) t^2
 *   t^3
 *
 * The path builder supplies duplicated endpoint control points:
 *
 *   P0 = P1 = start
 *   P2 = P3 = end
 *
 * Therefore progression from start to end reduces exactly to:
 *
 *   B(t) = 3*t^2 - 2*t^3
 *
 * Calling this scalar expression "smoothstep" is a mathematical description,
 * not a recovered Arcaea function/member name.
 */
static double B(double t)
{
    return 3.0 * t * t - 2.0 * t * t * t;
}

// -----------------------------------------------------------------------------
// 4. Complete X/Y easing table
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from native branch masks, sin() arithmetic, the dedicated straight
 * branch, and the dedicated cubic-Bezier branch.
 *
 * For normalized t in [0,1]:
 *
 *   x(t) = x0 + (x1 - x0) * Fx(t)
 *   y(t) = y0 + (y1 - y0) * Fy(t)
 *
 *   chart type   enum     Fx        Fy
 *   ----------   ----     --------  --------
 *   s             0       L         L
 *   b             1       B         B
 *   si            2       SI        L
 *   so            3       SO        L
 *   sisi          4       SI        SI
 *   sosi          5       SO        SI
 *   siso          6       SI        SO
 *   soso          7       SO        SO
 *
 * This table is the main durable result of Section 05.
 */
struct ArcPoint2D {
    double x;
    double y;
};

static double lerp(double a, double b, double factor)
{
    return a + (b - a) * factor;
}

ArcPoint2D evaluateClassicArc(
    ArcEasingType type,
    double x0,
    double x1,
    double y0,
    double y1,
    double t)
{
    double fx = 0.0;
    double fy = 0.0;

    switch (type) {
        case ArcEasingType::S:
            fx = L(t);
            fy = L(t);
            break;

        case ArcEasingType::B:
            fx = B(t);
            fy = B(t);
            break;

        case ArcEasingType::SI:
            fx = SI(t);
            fy = L(t);
            break;

        case ArcEasingType::SO:
            fx = SO(t);
            fy = L(t);
            break;

        case ArcEasingType::SISI:
            fx = SI(t);
            fy = SI(t);
            break;

        case ArcEasingType::SOSI:
            fx = SO(t);
            fy = SI(t);
            break;

        case ArcEasingType::SISO:
            fx = SI(t);
            fy = SO(t);
            break;

        case ArcEasingType::SOSO:
            fx = SO(t);
            fy = SO(t);
            break;
    }

    return {
        lerp(x0, x1, fx),
        lerp(y0, y1, fy),
    };
}

// -----------------------------------------------------------------------------
// 5. Why `b` has a separate native helper
// -----------------------------------------------------------------------------

/*
 * CONFIRMED native structure:
 *
 * `s` is built directly by a linear branch.
 * `si`/`so`/their four combinations share the sinusoidal evaluator.
 * `b` does NOT call that evaluator. It builds four 2D control points and calls
 * a cubic-Bezier helper.
 *
 * In the path builder, a temporary local coordinate frame is derived from the
 * endpoint span using atan2(), then the cubic point is transformed back into
 * the Arc/gameplay coordinate frame.
 *
 * That rotation is coordinate-frame plumbing, not a different easing law.
 * Because P0=P1=start and P2=P3=end, both chart X and Y still progress with
 * B(t)=3t^2-2t^3 between their respective endpoints.
 */
struct Vec2 {
    float x;
    float y;
};

Vec2 cubicBezier(
    Vec2 p0,
    Vec2 p1,
    Vec2 p2,
    Vec2 p3,
    float t)
{
    const float u = 1.0f - t;

    const float w0 = u * u * u;
    const float w1 = 3.0f * u * u * t;
    const float w2 = 3.0f * u * t * t;
    const float w3 = t * t * t;

    return {
        w0*p0.x + w1*p1.x + w2*p2.x + w3*p3.x,
        w0*p0.y + w1*p1.y + w2*p2.y + w3*p3.y,
    };
}

// -----------------------------------------------------------------------------
// 6. Native path representation: pre-sampled Vec3-like polyline
// -----------------------------------------------------------------------------

/*
 * CONFIRMED architecture from the builder around ~0x1925084.
 *
 * Arcaea does not need to rerun the AFF easing formula from scratch during each
 * touch check. LogicArcNote owns sampled-path vectors containing 12-byte
 * elements. Helper behaviour proves each element contains three floats, i.e. a
 * Vec3-like path point.
 *
 * Selected vector storage:
 *   LogicArcNote +0xE8  -> sampled Vec3-like path vector
 *   LogicArcNote +0x100 -> second sampled Vec3-like path vector
 *
 * The builder chooses the easing branch from +0x15C and repeatedly emits points
 * into these vectors. The third component of the ordinary easing helper advances
 * linearly with t even when X/Y are sinusoidally eased:
 *
 *     z(t) = zSpan * t
 *
 * The exact high-level names of the third coordinate and the two separate path
 * vectors are RECONSTRUCTED/UNRESOLVED. It is enough for this section to prove
 * that they are sampled 3-component path data, not score/tick records.
 */
struct ArcSamplePoint {
    float x;
    float y;
    float z;
};

static_assert(sizeof(ArcSamplePoint) == 12);

struct LogicArcNotePathState {
    std::vector<ArcSamplePoint> gameplayPath;  // +0xE8, readable name
    std::vector<ArcSamplePoint> secondaryPath; // +0x100, exact purpose unresolved

    float samplingMultiplier;                  // +0x118, exact semantic name unresolved
    ArcEasingType easing;                      // +0x15C, CONFIRMED
};

// -----------------------------------------------------------------------------
// 7. Sampling-density policy: mechanically visible, semantics partly unresolved
// -----------------------------------------------------------------------------

/*
 * CONFIRMED arithmetic in the path builder:
 *
 * One base sampling branch chooses 14 when the Arc's own duration is below
 * 1000 ms, otherwise 7. The normalized sample step is derived from the effective
 * path duration in seconds and that factor.
 *
 * A second sampling step additionally uses LogicArcNote +0x118. The initialiser
 * clamps that field so values below 1 become 1.
 *
 * A related-pointer vector around +0x138 can extend the builder's effective
 * horizon by taking the maximum of Arc end time and selected related objects'
 * start times.
 *
 * UNRESOLVED:
 *   - original semantic name of +0x118
 *   - exact reason the second path vector uses it
 *   - exact semantic identity of related-vector +0x138
 *
 * Do not call +0x118 "render quality" or "arc density" as an original member
 * name without further evidence. These details alter tessellation/policy, not
 * the easing formula table established above.
 */

// -----------------------------------------------------------------------------
// 8. Gameplay consumes the sampled path, not another easing function
// -----------------------------------------------------------------------------

/*
 * CONFIRMED from the gameplay update around ~0x19F3D4C:
 *
 *   - LogicArcNote's +0xE8 vector is walked as adjacent 12-byte points.
 *   - temporary segment vectors are constructed.
 *   - helper ~0x16DA57C is a plain 3-component dot product:
 *
 *         dot(a,b) = ax*bx + ay*by + az*bz
 *
 *   - the update computes a clamped interpolation/projection parameter on a
 *     sampled segment and obtains a current point from the polyline.
 *   - while within the relevant Arc time range, the first two components of the
 *     resulting point are stored at LogicArcNote +0xD4/+0xD8.
 *   - Section 04's Arc touch qualification then compares the finger against this
 *     cached expected gameplay position.
 *
 * Therefore the clean mental model is:
 *
 *   AFF endpoints + easing type
 *             |
 *             v
 *   exact easing mathematics
 *             |
 *             v
 *   sampled Vec3-like polyline (+0xE8 ...)
 *             |
 *             v
 *   gameplay-space segment interpolation/projection
 *             |
 *             v
 *   cached expected point (+0xD4/+0xD8)
 *             |
 *             v
 *   Arc contact hit-region test (Section 04)
 *
 * No evidence in this path indicates that gameplay applies a second hidden
 * easing curve after the sampled path is built.
 */
static float dot3(const ArcSamplePoint& a, const ArcSamplePoint& b)
{
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

// -----------------------------------------------------------------------------
// 9. ArcTap connection discovered while tracing the builder
// -----------------------------------------------------------------------------

/*
 * CONFIRMED structural observation, intentionally not expanded into Section 06:
 *
 * The Arc path builder also evaluates attached LogicArcTapNote positions using
 * the same parent-Arc path mathematics at the ArcTap's normalized time and
 * stores the resulting path-related position into ArcTap runtime state.
 *
 * This is the ideal next excavation target because Section 02 already proved
 * LogicArcTapNote derives from LogicTapNote, while this section now proves how
 * its spatial position is obtained from its parent Arc.
 *
 * The remaining question is not "what curve is the ArcTap on?" but rather:
 *   - how that stored parent-path position participates in ArcTap touch
 *     selection/judgement,
 *   - whether parent Arc contact matters,
 *   - and which point-note rules are inherited unchanged from LogicTapNote.
 */

// -----------------------------------------------------------------------------
// 10. Durable gameplay model
// -----------------------------------------------------------------------------

/*
 * CONFIRMED / RECONSTRUCTED summary:
 *
 *  1. Chart parser maps the eight AFF easing strings to enum 0..7.
 *  2. Runtime stores that enum at LogicArcNote +0x15C.
 *  3. Arc X/Y endpoints come from the native parsed AFF endpoint fields.
 *  4. Exact normalized easing laws are:
 *
 *       L(t)  = t
 *       B(t)  = 3t^2 - 2t^3
 *       SI(t) = sin(pi*t/2)
 *       SO(t) = 1 - cos(pi*t/2)
 *
 *  5. The eight chart types select X/Y factors as listed in Section 4.
 *  6. Runtime pre-samples the result into Vec3-like path vectors.
 *  7. Gameplay walks/interpolates that sampled path to refresh the expected Arc
 *     point used by contact logic.
 *  8. Rendering remains a separate problem and is not required to understand
 *     the Arc's fundamental gameplay path.
 */
