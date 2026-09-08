// Arcaea native-engine deep dive
// Section 02: Chart / source data
//
// IMPORTANT:
// This is reconstructed reference pseudocode, NOT recovered original source.
// Evidence comes from the native AFF lexer/parser, C++ RTTI, constructors,
// serializers/debug-string builders, field use sites, and source-to-runtime
// conversion paths in libcocos2dcpp.so.
//
// Evidence labels used below:
//   CONFIRMED     = directly supported by the binary/disassembly.
//   RECONSTRUCTED = semantic name/structure inferred from confirmed behaviour.
//   UNRESOLVED    = binary evidence exists but does not justify a stronger name.
//
// Scope: main gameplay and gameplay rendering only.

namespace reconstructed {

// ============================================================================
// CONFIRMED: AFF file is split into generic header metadata + chart body
// ============================================================================

// The loader searches for the AFF separator using either CRLF or LF form:
//     "\r\n-\r\n"
//     "\n-\n"
//
// Header lines are parsed as generic Key:Value pairs and stored in a
// std::map<std::string, std::string>. The parser itself does not give
// AudioOffset or TimingPointDensityFactor dedicated syntax.

struct ParsedChart {
    std::map<std::string, std::string> metadata;       // +0x00, size 0x18
    std::vector<class Note*> allNotes;                 // +0x18, size 0x18
    std::vector<std::vector<class Note*>*> groups;     // +0x30, size 0x18
}; // sizeof = 0x48

// Conceptual header parser:
void parseHeader(ParsedChart& chart, const std::vector<std::string>& lines) {
    for (const std::string& line : lines) {
        auto pieces = splitAtColon(line);
        if (!pieces.key.empty() && !pieces.value.empty()) {
            chart.metadata[pieces.key] = removeCarriageReturn(pieces.value);
        }
    }
}

// ============================================================================
// CONFIRMED: metadata interpreted later by LogicChart
// ============================================================================

struct LogicChartMetadataView {
    int audioOffsetMs;                    // LogicChart +0xC4
    float timingPointDensityFactor;       // LogicChart +0xC8
};

LogicChartMetadataView interpretMetadata(
    const std::map<std::string, std::string>& metadata)
{
    LogicChartMetadataView out{};

    // AudioOffset is converted with atoi(). Missing/empty input therefore
    // resolves through this path as zero.
    auto audioIt = metadata.find("AudioOffset");
    out.audioOffsetMs =
        (audioIt != metadata.end()) ? std::atoi(audioIt->second.c_str()) : 0;

    // CONFIRMED default.
    out.timingPointDensityFactor = 1.0f;

    auto densityIt = metadata.find("TimingPointDensityFactor");
    if (densityIt != metadata.end() && !densityIt->second.empty()) {
        out.timingPointDensityFactor =
            static_cast<float>(std::atof(densityIt->second.c_str()));
    }

    return out;
}

// ============================================================================
// CONFIRMED: lexer/parser vocabulary
// ============================================================================

// Dedicated lexer token classes/identifiers include:
//   ARC
//   HOLD
//   FLICK
//   TIMING
//   TIMINGGROUP
//   CAMERA
//   SCENECONTROL
//   ARCTAP
//   TINT
//   TFLOAT
//   TTRUE
//   DESIGNANT
//   LPAREN / RPAREN
//   LBRACK / RBRACK
//   LBRACE / RBRACE
//   COMMA
//
// The parser creates source objects for:
//   SimpleNote
//   HoldNote
//   FlickNote
//   Timing
//   ArcNote
//   CameraControl
//   SceneControl
//
// TimingGroup is a container construct rather than another Note subclass.

// ============================================================================
// CONFIRMED: common source Note layout
// ============================================================================

class Note {
public:
    void* vtable;                 // +0x00
    int timingGroupId;            // +0x08

    // Binary effects of these bytes are confirmed. Friendly names are
    // reconstructed from the timing-group modifiers that write them.
    bool inputEnabled;            // +0x0C, default true; "noinput" sets false
    bool fadingHolds;             // +0x0D, default false; "fadingholds" sets true

    // padding at +0x0E..+0x0F

    int angleX;                   // +0x10, from anglex<N>
    int angleY;                   // +0x14, from angley<N>
}; // subclass data begins at +0x18

// ============================================================================
// CONFIRMED: timing-group organisation
// ============================================================================

// Group 0 is the default/global group.
// Explicit timinggroup(...) blocks are assigned sequential IDs 1, 2, 3, ...
//
// Notes inside explicit groups are NOT copied. The exact same Note* is inserted
// into both ParsedChart::allNotes and that timing group's vector.

void registerNoteInGroup(
    ParsedChart& chart,
    Note* note,
    int explicitGroupId,
    std::vector<Note*>& explicitGroup)
{
    note->timingGroupId = explicitGroupId;
    chart.allNotes.push_back(note);
    explicitGroup.push_back(note);
}

// Confirmed timing-group modifier strings:
//   noinput
//   fadingholds
//   anglex...
//   angley...
//   tracecol...
//
// anglex/angley remove the textual prefix and parse the remainder with stoi().

void applyBasicTimingGroupModifiers(Note* note, const GroupModifiers& m) {
    if (m.noinput)
        note->inputEnabled = false;

    if (m.fadingholds)
        note->fadingHolds = true;

    if (m.hasAngleX)
        note->angleX = std::stoi(m.angleXText);

    if (m.hasAngleY)
        note->angleY = std::stoi(m.angleYText);
}

// ============================================================================
// CONFIRMED: NotePosition source abstraction
// ============================================================================

// RTTI identifies this class as NotePosition : cocos2d::Ref.
// It stores both a discrete-lane identity and a resolved horizontal coordinate.

class NotePosition : public cocos2d::Ref {
public:
    int discrete;       // +0x0C: 1 for integer lane form, 0 for float form
    int laneId;         // +0x10: 1..6 for integer form, 0 for float form
    float x;            // +0x14: resolved horizontal coordinate
};

// Integer source positions 0..5 map as follows when not mirrored:
//   source 0 -> laneId 1 -> x = -0.5
//   source 1 -> laneId 2 -> x =  0.0
//   source 2 -> laneId 3 -> x =  0.5
//   source 3 -> laneId 4 -> x =  1.0
//   source 4 -> laneId 5 -> x =  1.5
//   source 5 -> laneId 6 -> x =  2.0
//
// With mirror enabled, the discrete mapping reverses:
//   0->6, 1->5, 2->4, 3->3, 4->2, 5->1
// and therefore the resolved x values reverse too.
//
// Float source positions are continuous:
//   discrete = 0
//   laneId   = 0
//   x        = sourceX
// and mirror as:
//   x = 1.0f - sourceX

NotePosition* makeIntegerPosition(int sourceLane, bool mirror);
NotePosition* makeFloatPosition(float sourceX, bool mirror);

// ============================================================================
// CONFIRMED: SimpleNote
// ============================================================================

class SimpleNote : public Note {
public:
    int time;                    // +0x18
    NotePosition* position;      // +0x20
}; // sizeof = 0x28

// Parser form is conceptually:
//   (time, number);
// where number may be integer or float and becomes NotePosition.
//
// RECONSTRUCTED semantic role:
// SimpleNote is the source record for an ordinary floor tap. This is strongly
// supported by the absence of source-level TapNote RTTI and the later existence
// of LogicTapNote / RenderTapNote.

// ============================================================================
// CONFIRMED: HoldNote
// ============================================================================

class HoldNote : public Note {
public:
    int startTime;               // +0x18
    int endTime;                 // +0x1C
    NotePosition* position;      // +0x20
}; // sizeof = 0x28

// Parser form:
//   hold(startTime, endTime, number);
// number may use the same integer/float NotePosition forms as SimpleNote.

// ============================================================================
// CONFIRMED: Timing
// ============================================================================

class Timing : public Note {
public:
    int time;                    // +0x18
    float bpm;                   // +0x1C
    float beatsPerLine;          // +0x20
}; // sizeof = 0x28

// Semantic names are confirmed from downstream mathematics:
//   secondsPerBeat = 60.0f / bpm
// and timing-line spacing uses the equivalent of:
//   60000.0f / bpm * beatsPerLine
//
// A chart-wide timing scale is applied later during runtime construction.

// ============================================================================
// CONFIRMED: SceneControl generic source record
// ============================================================================

class SceneControl : public Note {
public:
    int time;                    // +0x18
    std::string command;         // +0x20
    float floatParameter;        // +0x38
    int intParameter;            // +0x3C
}; // sizeof = 0x40

// Scene controls are represented generically as timestamp + command string +
// one float + one integer, rather than one C++ subclass per command.
//
// CONFIRMED parser normalisation:
//   trackhide -> command "trackdisplay", floatParameter 0.0f, intParameter 0
//   trackshow -> command "trackdisplay", floatParameter 0.0f, intParameter 255
//
// RECONSTRUCTED:
// The 0/255 pair strongly resembles a visibility/opacity target, but the exact
// consumer-side semantic name belongs to the later scene-control investigation.

// ============================================================================
// CONFIRMED: CameraControl physical layout
// ============================================================================

class CameraControl : public Note {
public:
    int time;                    // +0x18

    float p1;                    // +0x1C
    float p2;                    // +0x20
    float p3;                    // +0x24
    float p4;                    // +0x28
    float p5;                    // +0x2C
    float p6;                    // +0x30

    std::string easing;          // +0x38
    int duration;                // +0x50
}; // sizeof = 0x58

// CONFIRMED downstream grouping:
//   Vec3 first  = { p1, p2, p3 }
//   Vec3 second = { p4, p5, p6 }
//
// The first Vec3 is passed through the camera movement animation path, including
// the retained method:
//   CameraController::animateMovingCameraTo(cocos2d::Vec3, float)
//
// RECONSTRUCTED, very strong:
//   first  = movement vector
//   second = rotation/orientation vector
//
// CONFIRMED mirror behaviour:
//   p1 = -p1
//   p6 = -p6
// while p2, p3, p4, p5 remain unchanged.
//
// Readable semantic reconstruction:
struct CameraControlView {
    int time;
    cocos2d::Vec3 movement;      // confirmed role for first vector
    cocos2d::Vec3 rotation;      // reconstructed name for second vector
    std::string easing;
    int duration;
};

// ============================================================================
// CONFIRMED: ArcNote source layout
// ============================================================================

struct RGB {
    int r;
    int g;
    int b;
}; // allocated as 12 bytes

enum ArcSpecialState {
    ArcNormal    = 0,
    ArcTrue      = 1,
    ArcDesignant = 2
};

class ArcNote : public Note {
public:
    int startTime;                       // +0x18
    int endTime;                         // +0x1C

    float startX;                        // +0x20
    float endX;                          // +0x24

    std::string easing;                  // +0x28

    float startY;                        // +0x40
    float endY;                          // +0x44

    int colour;                          // +0x48

    std::string effect;                  // +0x50

    int specialState;                    // +0x68, 0/1/2 rather than bool

    std::vector<int> arcTapTimes;        // +0x70

    float samplingDensityMultiplier;     // +0x88; semantic name reconstructed

    RGB* traceColourOverride;            // +0x90, nullptr by default
}; // sizeof = 0x98

// Parser order is confirmed as:
//   integer, integer,
//   float, float,
//   identifier/string,
//   float, float,
//   integer,
//   identifier/string,
//   special token,
//   optional float,
//   optional arctap list
//
// Friendly semantic mapping startTime/endTime/startX/endX/easing/startY/endY/
// colour/effect is supported by downstream consumers; these are not arbitrary
// AFF-community labels pasted onto unknown fields.

// ============================================================================
// CONFIRMED: Arc special state is ternary, not bool
// ============================================================================

int parseArcSpecialState(Token token) {
    if (token.type == Tokens::TTRUE)
        return ArcTrue;
    if (token.type == Tokens::DESIGNANT)
        return ArcDesignant;
    return ArcNormal;
}

// DESIGNANT is a dedicated lexer token.
//
// The chart parser receives a mode flag. If an ArcNote has specialState == 2
// and that mode is not enabled, the parser discards/skips the arc rather than
// inserting it into the parsed chart.

// ============================================================================
// CONFIRMED: arctaps are timestamps stored inside ArcNote source data
// ============================================================================

// There is no source-level ArcTapNote class in the parser hierarchy.
// A nested list of:
//   [arctap(1000), arctap(1200), ...]
// becomes simply:
//   std::vector<int>{1000, 1200, ...}
// inside ArcNote::arcTapTimes.
//
// Runtime classes LogicArcTapNote and RenderArcTapNote exist later; therefore
// source arctap timestamps are expanded into runtime objects downstream.

// ============================================================================
// CONFIRMED: arc optional float controls runtime geometry sampling density
// ============================================================================

// Source default:
//   samplingDensityMultiplier = 1.0f
//
// Runtime clamps the value to at least 1.0f, then uses it in arc geometric
// sample/subdivision generation. Simplified effect:
//
//   sampleRate = baseRate * max(sourceValue, 1.0f);
//   step = 1.0f / (arcDurationSeconds * sampleRate);
//
// Increasing the source value therefore increases arc sampling/subdivision
// density. "samplingDensityMultiplier" is a reconstructed descriptive name;
// the effect itself is confirmed.

// ============================================================================
// CONFIRMED: timing-group tracecol applies only to ArcNote
// ============================================================================

// The modifier path dynamic_casts Note* to ArcNote*. If successful, a textual
// 6-digit RGB value is parsed approximately with:
//   sscanf(text, "%02x%02x%02x", &r, &g, &b)
// and a separately allocated RGB structure is stored at ArcNote +0x90.
//
// This is independent of ArcNote::colour at +0x48.

void applyTraceColour(Note* note, const std::string& rrggbb) {
    ArcNote* arc = dynamic_cast<ArcNote*>(note);
    if (!arc)
        return;

    RGB* rgb = new RGB{};
    parseHexRGB(rrggbb, rgb->r, rgb->g, rgb->b);
    arc->traceColourOverride = rgb;
}

// ============================================================================
// CONFIRMED: FlickNote source scaffolding
// ============================================================================

class FlickNote : public Note {
public:
    int time;                    // +0x18
    float parameter1;            // +0x1C
    float parameter2;            // +0x20
    float parameter3;            // +0x24
    float parameter4;            // +0x28
}; // sizeof = 0x30

// The parser definitely recognises flick and constructs FlickNote from:
//   integer + float + float + float + float
//
// RTTI also confirms LogicFlickNote and RenderFlickNote exist.
//
// However, the principal source->logic conversion dispatcher handles
// SimpleNote, CameraControl, SceneControl, HoldNote and ArcNote through explicit
// type paths, while no equivalent active FlickNote conversion path was found.
// No reliable active consumer of all four FlickNote floats was recovered.
//
// USER-PROVIDED IMPLEMENTATION CONTEXT:
// The user states that Arcaea never actually implemented flick gameplay even
// though flick-related code exists. This matches the binary evidence unusually
// well: parser/source/runtime/render scaffolding exists, but the expected live
// source->logic bridge is absent.
//
// Therefore this section treats Flick as DORMANT SCAFFOLDING. The four float
// semantics remain intentionally unnamed rather than being guessed as x/y/dx/dy.

// ============================================================================
// CONFIRMED / RECONSTRUCTED: complete source-data pipeline
// ============================================================================

//       AFF file text
//            |
//            +--------------------+
//            |                    |
//            v                    v
//      generic header map      chart body lexer
//      string -> string            |
//            |                     v
//            |                 parser tokens
//            |                     |
//            |                     v
//            |        Note-derived source records
//            |        + timing-group cross references
//            |                     |
//            +----------+----------+
//                       |
//                       v
//                   LogicChart
//                       |
//                       v
//                 runtime logic layer
//
// Parsed source notes are stored once in allNotes and may additionally be
// referenced by an explicit timing-group vector. Header metadata remains a
// separate generic map until LogicChart interprets keys that matter to gameplay.

// ============================================================================
// SECTION STATUS
// ============================================================================

// COMPLETE for the source/chart-data layer within current gameplay scope.
//
// Confirmed or sufficiently reconstructed:
//   - AFF header/body split
//   - metadata map and two gameplay-consumed metadata keys
//   - lexer/parser note vocabulary
//   - ParsedChart memory layout
//   - base Note layout and timing-group fields/modifiers
//   - timing-group note sharing / group numbering
//   - NotePosition discrete/continuous forms and mirroring
//   - SimpleNote / HoldNote layouts
//   - Timing layout and BPM / beats-per-line semantics
//   - SceneControl generic representation and trackhide/show normalisation
//   - CameraControl six-float layout, vector grouping and mirror behaviour
//   - ArcNote layout, ternary special state, arctap storage, trace colour,
//     and geometry-sampling multiplier
//   - Flick source/runtime/render scaffolding, documented as dormant
//
// Remaining unresolved items are deliberately outside what is needed to close
// this source-data section:
//   - exact live renderer effect of SceneControl intParameter 0 vs 255
//   - exact semantic name of CameraControl's second Vec3
//   - dormant FlickNote float semantics
//
// Those belong either to later rendering/control investigations or to dormant
// code archaeology, not to basic AFF/source representation.

} // namespace reconstructed
