# Arcaea Deep Dive AI Context

## Completed Section 01 — gameplay architecture

Section 01 established the high-level gameplay skeleton.

### CONFIRMED

The native RTTI shows three separate families rather than one note object doing everything:

1. **Chart/source data**
   - `Note`
   - `SimpleNote : Note`
   - `ArcNote : Note`
   - `HoldNote : Note`
   - `FlickNote : Note`
   - `Timing : Note`
   - `SceneControl : Note`
   - `CameraControl : Note`

2. **Runtime gameplay logic**
   - `LogicNote : cocos2d::Ref`
   - `LogicTapNote : LogicNote`
   - `LogicFlickNote : LogicNote`
   - `LogicLongNoteBase : LogicNote`
   - `LogicHoldNote : LogicLongNoteBase`
   - `LogicArcNote : LogicLongNoteBase`
   - `LogicArcTapNote : LogicTapNote`
   - `LogicSceneControl : LogicNote`
   - `LogicCameraControl : LogicNote`
   - `LogicEvent : cocos2d::Ref`
   - `LogicTimingEvent : LogicEvent`

3. **Visible/render objects**
   - `RenderNote : cocos2d::Node`
   - `RenderTapNote : RenderNote`
   - `RenderFlickNote : RenderNote`
   - `RenderHoldNote : RenderNote`
   - `RenderArcNote : RenderNote`
   - `RenderArcTapNote : RenderTapNote`

Other confirmed gameplay/render classes include `LogicChart`, `GameTimeline`, `GameModel`, `GameScene`, `TrackLayer`, `TrackBase`, `CameraController`, `GameSceneVisualControlHandler`, `NoteBurstRenderer`, `ArcSegment`, `LogicArcGroup`, `ScoreState`, `LifeBarState`, and `HPBar`.

Retained mangled lambda RTTI also gives real method names/signatures:

- `GameModel::initializeTouchEvents()`
- `GameScene::render(Renderer*, const Mat4&, const Mat4*)`
- `GameScene::swapModel(GameModel*, TrackLayer*)`
- `GameSceneVisualControlHandler::handleLogicSceneControl(LogicSceneControl*)`
- `TrackBase::performTrackSplitAnimation()`
- `CameraController::animateMovingCameraTo(Vec3, float)`

`GameScene` inherits `cocos2d::Scene`, `GameModelDelegate`, and `PauseLayerDelegate`. `GameModel` inherits `cocos2d::Ref` and `ManualPadDelegate`. Render-note classes are Cocos nodes, while logic-note classes are reference-counted gameplay objects. Judgement/simulation and drawing are deliberately separated.

### RECONSTRUCTED architecture

```text
.aff chart
   ↓
Note-family source records
   ↓
LogicChart / GameTimeline
   ↓
LogicNote + LogicEvent runtime objects
   ↓
GameModel
   ↓
visual bridge / track / camera
   ↓
RenderNote-family Cocos nodes
   ↓
GameScene / cocos2d::Renderer
```

The exact conversion/factory functions were not yet recovered in Section 01. `LogicArcTapNote : LogicTapNote` and `RenderArcTapNote : RenderTapNote` confirm that arctaps reuse the tap foundation on logic and rendering sides.

Durable file: `deepdive/01_gameplay_architecture.cpp`.

---

# Most recent completed task: Section 02 — chart / source data

Section 02 is complete for the AFF/source-data layer within the current gameplay scope.

## CONFIRMED: AFF file organisation

The chart loader splits an AFF file at either `\r\n-\r\n` or `\n-\n` into:

```text
header metadata
-
chart body
```

The parsed source container is approximately:

```cpp
struct ParsedChart {
    std::map<std::string, std::string> metadata;       // +0x00
    std::vector<Note*> allNotes;                       // +0x18
    std::vector<std::vector<Note*>*> timingGroups;     // +0x30
}; // sizeof = 0x48
```

Header lines are parsed generically as `Key:Value` strings. `AudioOffset` and `TimingPointDensityFactor` are not special lexer syntax; they are ordinary metadata keys interpreted later by `LogicChart`.

### `AudioOffset`

`LogicChart` looks up `AudioOffset`, converts it with `atoi()`, and stores an integer at `LogicChart +0xC4`. Missing/empty input resolves through this path as zero.

### `TimingPointDensityFactor`

`LogicChart +0xC8` starts at `1.0f`. If a non-empty `TimingPointDensityFactor` value exists, the code replaces the default using `atof()`.

## CONFIRMED: lexer/parser vocabulary

Dedicated parser tokens include:

- `ARC`
- `HOLD`
- `FLICK`
- `TIMING`
- `TIMINGGROUP`
- `CAMERA`
- `SCENECONTROL`
- `ARCTAP`
- `TINT`
- `TFLOAT`
- `TTRUE`
- `DESIGNANT`
- punctuation tokens for parentheses, brackets, braces, and comma

The parser constructs `SimpleNote`, `HoldNote`, `FlickNote`, `Timing`, `ArcNote`, `CameraControl`, and `SceneControl`. `TimingGroup` is a container construct rather than a normal `Note` subclass.

## CONFIRMED: common `Note` source layout

```cpp
class Note {
    void* vtable;          // +0x00
    int timingGroupId;     // +0x08
    bool inputEnabled;     // +0x0C, reconstructed friendly name
    bool fadingHolds;      // +0x0D
    int angleX;            // +0x10
    int angleY;            // +0x14
};
```

Field effects are confirmed. Friendly names are based on the timing-group modifiers that write them.

Default notes use timing group ID 0. Explicit `timinggroup(...)` blocks are numbered 1, 2, 3, ... sequentially. A note inside an explicit group is stored once and the same pointer is placed both in `allNotes` and the timing-group vector.

Confirmed timing-group modifiers include:

- `noinput` → clears byte `+0x0C`
- `fadingholds` → sets byte `+0x0D`
- `anglex...` → parses signed integer with `stoi()` into `+0x10`
- `angley...` → parses signed integer with `stoi()` into `+0x14`
- `tracecol...` → applies only to `ArcNote`

## CONFIRMED: `NotePosition`

RTTI identifies the source position wrapper as `NotePosition : cocos2d::Ref`.

Approximate layout:

```cpp
class NotePosition : public cocos2d::Ref {
    int discrete;       // +0x0C
    int laneId;         // +0x10
    float x;            // +0x14
};
```

Integer source values `0..5` map to:

| source | laneId | x |
|---:|---:|---:|
| 0 | 1 | -0.5 |
| 1 | 2 | 0.0 |
| 2 | 3 | 0.5 |
| 3 | 4 | 1.0 |
| 4 | 5 | 1.5 |
| 5 | 6 | 2.0 |

Mirror reverses the discrete mapping. Float source positions are stored as continuous coordinates with `discrete = 0`, `laneId = 0`, `x = sourceX`; mirror becomes `x = 1.0f - sourceX`.

## CONFIRMED: `SimpleNote`

```cpp
class SimpleNote : public Note {
    int time;                 // +0x18
    NotePosition* position;   // +0x20
}; // sizeof = 0x28
```

`SimpleNote` is strongly reconstructed as the source record for an ordinary floor tap. There is no source-level `TapNote`, while `LogicTapNote` and `RenderTapNote` exist later.

## CONFIRMED: `HoldNote`

```cpp
class HoldNote : public Note {
    int startTime;            // +0x18
    int endTime;              // +0x1C
    NotePosition* position;   // +0x20
}; // sizeof = 0x28
```

The position supports the same discrete and continuous `NotePosition` forms.

## CONFIRMED: `Timing`

```cpp
class Timing : public Note {
    int time;                 // +0x18
    float bpm;                // +0x1C
    float beatsPerLine;       // +0x20
}; // sizeof = 0x28
```

The semantic names are confirmed downstream. The first float participates in `60.0f / bpm`; timing-line spacing uses the equivalent of `60000.0f / bpm * beatsPerLine`.

## CONFIRMED: `SceneControl`

```cpp
class SceneControl : public Note {
    int time;                 // +0x18
    std::string command;      // +0x20
    float floatParameter;     // +0x38
    int intParameter;         // +0x3C
}; // sizeof = 0x40
```

Source scene controls are generic command records rather than separate C++ subclasses per command.

Confirmed parser normalisation:

```text
trackhide -> command "trackdisplay", floatParameter 0.0, intParameter 0
trackshow -> command "trackdisplay", floatParameter 0.0, intParameter 255
```

The exact renderer-side semantic name of the 0/255 value is deferred to a later scene-control/render investigation.

## CONFIRMED / RECONSTRUCTED: `CameraControl`

Physical layout:

```cpp
class CameraControl : public Note {
    int time;                 // +0x18
    float p1;                 // +0x1C
    float p2;                 // +0x20
    float p3;                 // +0x24
    float p4;                 // +0x28
    float p5;                 // +0x2C
    float p6;                 // +0x30
    std::string easing;       // +0x38
    int duration;             // +0x50
}; // sizeof = 0x58
```

Downstream code preserves order and groups the floats into two `Vec3`s:

```cpp
Vec3 first  = { p1, p2, p3 };
Vec3 second = { p4, p5, p6 };
```

The first vector goes through the camera movement animation path, including retained `CameraController::animateMovingCameraTo(Vec3, float)`. The second vector is strongly reconstructed as rotation/orientation.

Mirror behaviour is confirmed:

```cpp
p1 = -p1;
p6 = -p6;
```

while the other four components remain unchanged.

## CONFIRMED: `ArcNote`

```cpp
struct RGB { int r, g, b; };

enum ArcSpecialState {
    ArcNormal    = 0,
    ArcTrue      = 1,
    ArcDesignant = 2
};

class ArcNote : public Note {
    int startTime;                         // +0x18
    int endTime;                           // +0x1C
    float startX;                          // +0x20
    float endX;                            // +0x24
    std::string easing;                    // +0x28
    float startY;                          // +0x40
    float endY;                            // +0x44
    int colour;                            // +0x48
    std::string effect;                    // +0x50
    int specialState;                      // +0x68
    std::vector<int> arcTapTimes;          // +0x70
    float samplingDensityMultiplier;       // +0x88, reconstructed name
    RGB* traceColourOverride;              // +0x90
}; // sizeof = 0x98
```

The arc parser order is confirmed as two integers, two floats, string, two floats, integer, string, special token, optional float, optional arctap list.

### Arc special state

The field often treated externally as boolean is actually ternary in this build:

- normal/false → 0
- `true` token → 1
- dedicated `DESIGNANT` token → 2

If state 2 is encountered while the parser's relevant mode flag is disabled, the arc is discarded/skipped instead of inserted into the chart.

### Arctaps

There is no source-level `ArcTapNote` object. `[arctap(t), ...]` becomes a `std::vector<int>` of timestamps inside `ArcNote`. Runtime `LogicArcTapNote` / `RenderArcTapNote` objects are therefore created later from those timestamps.

### Arc optional float

The optional source float at `+0x88` defaults to `1.0f`. Runtime clamps it to at least 1.0 and uses it in arc geometry sample/subdivision generation. Simplified effect:

```cpp
sampleRate = baseRate * max(sourceValue, 1.0f);
step = 1.0f / (arcDurationSeconds * sampleRate);
```

Increasing the field increases arc sampling/subdivision density. `samplingDensityMultiplier` is a reconstructed descriptive name; the effect is confirmed.

### `tracecol`

The timing-group trace-colour modifier only applies to `ArcNote`. A six-digit RGB string is parsed into a separately allocated 12-byte `{int r, int g, int b}` structure stored at `ArcNote +0x90`. It is distinct from the normal integer arc colour at `+0x48`.

## CONFIRMED + USER CONTEXT: dormant Flick scaffolding

Physical source layout:

```cpp
class FlickNote : public Note {
    int time;                 // +0x18
    float parameter1;         // +0x1C
    float parameter2;         // +0x20
    float parameter3;         // +0x24
    float parameter4;         // +0x28
}; // sizeof = 0x30
```

The parser definitely recognises `flick` and constructs `FlickNote`. RTTI also confirms `LogicFlickNote` and `RenderFlickNote` exist.

However, the principal source-to-logic dispatcher has active paths for `SimpleNote`, `CameraControl`, `SceneControl`, `HoldNote`, and `ArcNote`, while no equivalent active `FlickNote` conversion path was found. No reliable active consumer of all four source floats was recovered.

The user provided the implementation-history fact that Arcaea never actually implemented flick gameplay even though flick-related code exists. This matches the binary evidence: parser/source/runtime/render scaffolding exists, but the expected live source-to-logic bridge is absent. Treat Flick as **dormant scaffolding**. Do not invent names such as x/y/dx/dy for the four floats without new evidence.

## Completed source pipeline

```text
AFF text
  |
  +--> generic header map (string -> string)
  |
  +--> chart-body lexer/parser
          |
          +--> Note-derived source objects
          +--> timing-group cross references
                    |
                    v
                 LogicChart
                    |
                    v
              runtime logic layer
```

Source notes are stored once in `allNotes` and may additionally be referenced by a timing-group vector. Header metadata remains separate until `LogicChart` interprets gameplay-relevant keys.

Durable file: `deepdive/02_chart_source_data.cpp`.

---

# Continuity summary

**Completed:**
- Section 01: high-level gameplay architecture and confirmed chart/runtime/render class families.
- Section 02: AFF/chart source representation, metadata, timing groups, source object layouts, NotePosition, Timing semantics, SceneControl source normalisation, Camera source parameters/vector grouping, Arc source representation/arctaps/special state/trace colour/sampling density, and dormant Flick scaffolding.

**Known unresolved details that belong to later sections, not unfinished Section 02:**
- exact renderer effect/semantic name of SceneControl `trackdisplay` integer values 0 and 255;
- exact semantic/original name of CameraControl's second `Vec3` (strongly reconstructed as rotation/orientation);
- dormant Flick float semantics, intentionally left unnamed;
- exact source->logic factory/conversion implementation and ownership;
- per-frame gameplay update order;
- runtime note judgement/state fields;
- note lifetime/removal rules;
- detailed rendering geometry, track transformations, camera maths, and scene-control visual effects.

**Most recent task:** Finish and close the chart/source-data layer. This is now complete.

**Suggested next boundary-respecting direction:** move from source records into the runtime gameplay-logic layer, starting with the exact `ParsedChart/Note -> LogicChart/LogicNote` conversion path, before investigating judgement behaviour or rendering.

**Scope restriction:** Investigation is limited to main gameplay and gameplay rendering only. Do not expand into unlocks, story, menus, purchases, network/account systems, progression, or other unrelated app systems unless the user explicitly expands scope.
