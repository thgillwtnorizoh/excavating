# Arcaea Deep Dive AI Context

This file is the continuity handoff for the current `deepdive/` excavation.

## Working rules

- Investigated source bundle: stripped Arcaea APK/native engine represented by `arccopy.zip`; main native gameplay code is in ARM64 `libcocos2dcpp.so`.
- Durable reconstruction repository: `thgillwtnorizoh/excavating`.
- Current consolidated chapters live in `deepdive/`.
- Chat is for investigation/explanation. Only after a section is genuinely complete should a durable C++-style pseudocode chapter be written.
- Never present reconstructed pseudocode as recovered original source.
- Evidence labels:
  - **CONFIRMED** = directly supported by native control flow, constants, RTTI/typeinfo, data layout, strings or independent consumers.
  - **RECONSTRUCTED** = readable semantic structure assembled from confirmed facts; names may differ from original source.
  - **UNRESOLVED** = exact original name, rationale or behaviour is not sufficiently proved.
- User-requested scope restriction: **main gameplay and gameplay rendering only**. Do not expand into unlocks, story, menus, purchases, account/network systems, progression or unrelated app infrastructure unless the user explicitly widens scope.
- Explanations should remain readable to someone without formal programming study. Prefer plain-language descriptions beside pseudocode rather than unnecessary compiler jargon.

## Important repository archaeology note

The repository root already contains the earlier detailed excavation notebook, currently `01_recollection_rate.cpp` through `23_ybn_green_event_construction.cpp`, plus `README.md`. These are durable prior findings and should be consulted/cross-checked before re-solving a mechanic from scratch. The current `deepdive/` chapters are a newer consolidated pass over the engine/source architecture.

Particularly relevant root chapters for the runtime layer include:

- `01_recollection_rate.cpp`
- `02_note_fundamentals.cpp`
- `03_long_notes.cpp`
- `04_arc_contact.cpp`
- `06_arctaps.cpp`
- `08_lane_geometry.cpp`
- `10_timinggroups.cpp`
- `12_arc_contact_refinements.cpp`
- `14_arc_mode_designant.cpp`
- `18_flick_runtime_disconnection.cpp`
- `19_logiccolor_arc_tracking.cpp`
- `21_lane_timing_refinements.cpp`

The native anchors in those files were rechecked against the current investigated binary during Deep Dive Section 03 and line up with the same functions/behaviour.

---

# Completed Section 01 — gameplay architecture

Durable file: `deepdive/01_gameplay_architecture.cpp`.

## CONFIRMED high-level class families

Chart/source data:

- `Note`
- `SimpleNote : Note`
- `ArcNote : Note`
- `HoldNote : Note`
- `FlickNote : Note`
- `Timing : Note`
- `SceneControl : Note`
- `CameraControl : Note`

Runtime gameplay logic:

- `LogicEvent : cocos2d::Ref`
- `LogicTimingEvent : LogicEvent`
- `LogicNote : cocos2d::Ref`
- `LogicTapNote : LogicNote`
- `LogicArcTapNote : LogicTapNote`
- `LogicLongNoteBase : LogicNote`
- `LogicHoldNote : LogicLongNoteBase`
- `LogicArcNote : LogicLongNoteBase`
- dormant `LogicFlickNote : LogicNote`
- `LogicSceneControl : LogicNote`
- `LogicCameraControl : LogicNote`

Rendering:

- `RenderNote : cocos2d::Node`
- `RenderTapNote : RenderNote`
- `RenderArcTapNote : RenderTapNote`
- `RenderHoldNote : RenderNote`
- `RenderArcNote : RenderNote`
- `RenderFlickNote : RenderNote`

Other confirmed gameplay/render objects include `LogicChart`, `GameTimeline`, `GameModel`, `GameScene`, `TrackLayer`, `TrackBase`, `CameraController`, `GameSceneVisualControlHandler`, `LogicArcGroup`, `ScoreState`, `LifeBarState`, `HPBar`, `ArcSegment`, and `NoteBurstRenderer`.

Retained method names include:

- `GameModel::initializeTouchEvents()`
- `GameScene::render(Renderer*, const Mat4&, const Mat4*)`
- `GameScene::swapModel(GameModel*, TrackLayer*)`
- `GameSceneVisualControlHandler::handleLogicSceneControl(LogicSceneControl*)`
- `TrackBase::performTrackSplitAnimation()`
- `CameraController::animateMovingCameraTo(Vec3, float)`

Main architectural result:

```text
AFF/source records
      ↓
runtime Logic* gameplay objects
      ↓
GameModel gameplay state/input
      ↓
visual bridge / Track / Camera
      ↓
Render* Cocos nodes
      ↓
GameScene / cocos2d renderer
```

Chart data, judgement logic, and visible note objects are separate layers.

---

# Completed Section 02 — chart / source data

Durable file: `deepdive/02_chart_source_data.cpp`.

## CONFIRMED AFF organisation

AFF is split at the `-` separator into generic header metadata and chart body.

Parsed source container:

```cpp
struct ParsedChart {
    std::map<std::string, std::string> metadata;       // +0x00
    std::vector<Note*> allNotes;                       // +0x18
    std::vector<std::vector<Note*>*> timingGroups;     // +0x30
}; // sizeof = 0x48
```

Header values are generic strings. `AudioOffset` is later interpreted with `atoi()`. `TimingPointDensityFactor` defaults to `1.0f` and is replaced with `atof()` when present.

## CONFIRMED common source Note metadata

```cpp
class Note {
    int timingGroupId;     // +0x08
    bool inputEnabled;     // +0x0C, friendly name from noinput behaviour
    bool fadingHolds;      // +0x0D
    int angleX;            // +0x10
    int angleY;            // +0x14
};
```

Timing-group modifiers confirmed include `noinput`, `fadingholds`, `anglex`, `angley`, and Arc-only `tracecol`.

Default timing group is ID 0. Explicit groups are numbered 1,2,3... Notes are stored once in `allNotes`; timing-group vectors hold additional pointers to the same objects.

## CONFIRMED NotePosition

`NotePosition : cocos2d::Ref` represents either a discrete lane or a free horizontal coordinate.

Integer source 0..5 maps to internal lane IDs 1..6 and stored horizontal values `-0.5, 0, 0.5, 1, 1.5, 2`. Mirror reverses discrete mapping. Float positions mirror as `x = 1 - x`.

## CONFIRMED source note layouts

`SimpleNote`:

```cpp
int time;
NotePosition* position;
```

`HoldNote`:

```cpp
int startTime;
int endTime;
NotePosition* position;
```

`Timing`:

```cpp
int time;
float bpm;
float beatsPerMeasure;
```

The second timing float was refined by later runtime consumers as beats-per-measure/measure beat count.

`SceneControl` is one generic source object:

```cpp
int time;
std::string command;
float floatParameter;
int intParameter;
```

Parser normalisation includes:

```text
trackhide -> trackdisplay, float=0, int=0
trackshow -> trackdisplay, float=0, int=255
```

`CameraControl` stores six floats + easing + duration. Runtime groups the first three as movement and the second three as a separate orientation/rotation Vec3. Mirror negates first movement component and sixth component.

`ArcNote` source layout includes start/end time, X endpoints, easing, Y endpoints, colour, effect, body-mode state, `vector<int>` ArcTap timestamps, optional sampling-density float defaulting to 1, and optional trace-colour RGB pointer.

Arc arctype is ternary in this build:

- false -> 0
- true -> 1
- native `DESIGNANT` token -> 2

ArcTaps are not source objects. Their timestamps live inside the parent Arc source record and become runtime children later.

The optional Arc float controls path/render subdivision sampling density and is clamped to at least `1.0f` at runtime.

## Dormant Flick source path

`FlickNote` parser/source layout exists and carries one integer timestamp plus four floats. `LogicFlickNote` / `RenderFlickNote` names and downstream handlers survive, but the active source-to-runtime producer does not. The user supplied the implementation-history fact that Arcaea never actually implemented Flick gameplay. Later binary archaeology strengthens this: this target build contains no `LogicFlickNote` class vtable, so live most-derived LogicFlickNote construction is disconnected. Treat Flick as dormant scaffolding and do not invent the missing float semantics.

---

# Most recent completed task: Section 03 — runtime gameplay logic

Durable file: `deepdive/03_runtime_gameplay_logic.cpp`.

Section 03 closes the fundamental runtime note state machine from source conversion through input, judgement, long-note processing, ScoreState/LifeBarState fan-out, and logic-critical frame ordering.

## 1. CONFIRMED source -> runtime conversion

`LogicChart` actively converts source objects in the family:

```text
SimpleNote
CameraControl
SceneControl
HoldNote
ArcNote
```

`Timing` is converted separately to per-group `LogicTimingEvent` streams. No live Flick conversion branch exists.

Each runtime note receives the latest `LogicTimingEvent` in its timing group whose timestamp is `<= noteTime`.

## 2. CONFIRMED shared judgement clock

Judgement does not use a private timing-group clock. A shared gameplay clock computes effective time from a synchronized/live path or a fallback path. The fallback path subtracts an additional 3000 ms while its source value is non-positive.

Conceptually:

```cpp
if (useSynchronizedPath)
    now = synchronizedSource - commonOffset;
else {
    now = fallbackSource - commonOffset;
    if (fallbackSource <= 0)
        now -= 3000;
}
```

Point judgement, automatic misses, long-note expiry/contact and scene scheduling share this effective time family.

## 3. CONFIRMED LogicTimingEvent

Runtime timing stores both musical and spatial timing concepts:

```cpp
struct LogicTimingEvent {
    int startTime;
    int endTime;
    float effectiveSpatialBpm;      // rawBpm * scrollScale
    float beatsPerMeasure;
    float rawBpm;
    float unknown24;
    float secondsPerEffectiveBeat;  // 60 / effectiveSpatialBpm
};
```

`LogicChart +0xF0` is selected highspeed/note speed. `LogicChart +0xF4` is the confirmed scroll scale:

```cpp
scrollScale = highspeed * 180 / chartBaseBpm;
```

The important split is:

- raw BPM -> Hold/Arc internal tick cadence
- effective spatial BPM / beat duration -> path/note movement
- global gameplay clock -> judgement and expiry

## 4. CONFIRMED common runtime Note state

Important common `LogicNote` fields include:

- `+0x0C` point-note LOST state
- `+0x0D` point-note successful-hit state
- `+0x10` input/hit payload, not judgement enum
- `+0x18/+0x1C` start/end timestamp
- `+0x20/+0x28` position pointers
- `+0x48` active LogicTimingEvent pointer
- `+0x50` timing-group ID
- `+0x54` inputEnabled from `noinput`
- `+0x60` monotonic runtime serial/index

Point notes resolve by hit/lost flags. Core gameplay passes skip resolved objects; immediate object deletion is not required for resolution.

## 5. CONFIRMED floor touch/lane selection

Raw screen pixels are transformed into gameplay/floor space before lane matching.

Internal discrete lane IDs are 1..6; the normal four lanes are 2..5. Primary gameplay-X boundaries include `-850, -425, 0, +425, +850`, with a real native seam where exactly `x == +425` yields no primary lane. A touch-width scale can add an adjacent lane candidate.

Ordinary floor taps and Holds match their `NotePosition.laneId` against primary or adjacent lane candidates.

## 6. CONFIRMED point-note timing judgement

Touch-begin builds a temporary spatially eligible candidate list and sorts it by note timestamp. `LogicTapNote` and `LogicArcTapNote` then use the same point timing routine.

Inclusive candidate bands:

```text
|error| <= 25 ms   -> MAX PURE
|error| <= 50 ms   -> PURE
|error| <= 100 ms  -> FAR
|error| <= 120 ms  -> LOST via the real ScoreState miss path
> 120 ms           -> candidate routine rejects the attempt
```

For Pure/Far, current time before note time maps to reconstructed Early side value 1, and current time at/after note time maps to reconstructed Late value 2. MaxPure uses side 0.

The `<=120` branch describes this candidate routine. Automatic scheduling can independently resolve overdue notes, so it should not be overinterpreted as a universally reachable symmetric late window.

## 7. CONFIRMED ScoreState flow

Successful judgement ordering:

```text
note virtual accept-success hook
        ↓ if accepted
increment judgement counters
        ↓
fan judgement to EVERY active LifeBarState
        ↓
record timing/statistics
```

Counter behaviour:

- MaxPure increments both MaxPure count and broad Pure count.
- Pure increments broad Pure count.
- Far increments Far count.

LOST is a separate ScoreState entry path:

```text
note virtual accept-LOST hook
        ↓ if accepted
increment LOST count
        ↓
fan LOST to every LifeBarState
```

## 8. CONFIRMED baseline Recollection propagation

Important `LifeBarState` fields include current Recollection Rate at `+0x10`, Recollection Factor at `+0x8C`, LOST scale percent at `+0x98`, gauge mode at `+0xA0`, and `CharacterAbility*` at `+0xC8`.

Recollection Factor by chart note count `N`:

```cpp
if (N < 400)      RF = 0.2 + 80/N;
else if (N < 600) RF = 0.2 + 32/N;
else              RF = 0.08 + 96/N;
```

An observed flag can multiply RF by `0.8`.

Successful baseline gain:

```text
MAX PURE / PURE -> +RF
FAR             -> +0.5 RF
```

Standalone LOST baseline:

```text
Normal -> 2.0
Easy   -> 1.2
Hard   -> 9.0 above 30 RR, 5.0 at/below 30 RR
```

A LOST event from `LogicLongNoteBase` receives half the standalone-note damage before further scaling. `lossScalePercent` applies `/100`. CharacterAbility hooks can modify gain/loss. Hard gauge has a confirmed special correction when one loss crosses from above 30 to below 30. Common application clamps to the permitted gauge range.

This section intentionally does not expand every special partner/gauge ability.

## 9. CONFIRMED common long-note event system

Holds and judged Arc bodies share `LogicLongNoteBase`.

A long note owns 12-byte internal events:

```cpp
struct LongTickEvent {
    int time;
    int scoreUnits;      // reconstructed name; common builder writes 1
    byte processedFlags; // bit 0 = processed
};
```

Tick interval:

```cpp
beatMs = 60000 / abs(rawBpm);
subdivision = abs(rawBpm) >= 255 ? 1 : 2;
tickInterval = beatMs / subdivision / TimingPointDensityFactor;
```

So below 255 BPM the common cadence is half-beat before density scaling; at/above 255 BPM it is full-beat before density scaling.

Ordinary generated candidate timestamps exclude the exact end time. A non-zero long note that would otherwise produce no event receives a midpoint event.

Successful long-event eligibility uses:

```text
now + 0.5 * tickInterval
```

Overdue LOST normally uses:

```text
now - min(2 * tickInterval, 500 ms)
```

Each processed scoring unit calls ScoreState independently. Successful long units enter as judgement 0 / side 0 / payload -1, rather than using point-note timing bands.

## 10. CONFIRMED Hold state machine

Touch-begin can engage a matching Hold when lane identity matches and:

```text
now < hold.end
hold.start < now + 100 ms
```

This means Hold acquisition is not restricted to one exact start-time hit.

A persistent Hold engagement flag around `+0xA8` is distinct from the transient current-contact bytes `+0x64/+0x65`.

Every update clears transient contact. Active matching touches re-latch it immediately before long-event judgement.

Consequences:

- continuous contact -> eligible ticks succeed;
- release -> contact stops refreshing, but pending ticks do not instantly become LOST;
- re-press before pending tick expiry can recover still-unprocessed events;
- once an event is processed as LOST it cannot be recovered;
- mid-Hold pickup is possible while earlier already-expired ticks stay LOST.

`fadingholds` survives in runtime Hold state but belongs to presentation feedback rather than timing arithmetic.

## 11. CONFIRMED Arc-body runtime logic

`LogicArcNote` shares the long-note event system but has a different definition of valid contact.

Important runtime fields/state include:

- `+0xA0` connected-continuation flag (reconstructed name)
- `+0xA4` Arc body mode
- `+0xB0` `LogicColor*`, native RTTI identity confirmed
- `+0xD0` active/playable Arc state
- `+0xD4/+0xD8` cached current expected Arc gameplay position
- `+0xE0` `LogicArcGroup*`
- `+0x120` attached LogicArcTapNote vector
- `+0x170` special runtime capability flag; special-system details are deferred from the baseline model

Arc body mode:

- 0 = ordinary judged body
- 1 = true/trace/skyline-like nonjudged body
- 2 = native DESIGNANT mode

Runtime can auto-promote mode 0 to mode 1 when ArcTap children are present.

Only mode 0 participates in ordinary Arc-body contact and ordinary long-note success/LOST accounting.

## 12. CONFIRMED Arc geometry + LogicColor ownership

Arc contact is rebuilt every frame.

Normal flow:

```text
current Arc path point / active phase
        ↓
touch transformed into gameplay/sky space
        ↓
camera/screen-scaled Arc hit-region test
        ↓
LogicColor touch ownership acceptance
        ↓
Arc +0x64/+0x65 current-contact latch
        ↓
shared long-note tick success/LOST machinery
```

The spatial helper is not a simple `distance <= 212` circle. Native code constructs camera/screen-scaled axis bounds using the nominal 212 value plus other transforms.

Touch-begin Arc acquisition uses the same geometric family with approximately +120 ms lookahead toward Arc start.

`LogicColor` combines colour-channel identity with Arc touch ownership. Same-channel Arcs can share a LogicColor object. Ordinary channels coordinate through a global claimed-touch-ID set.

Ordinary LogicColor ownership rules include:

- active release lockout rejects touches;
- same assigned touch ID has a direct acceptance path;
- a different touch does not simply replace an ordinary still-assigned touch;
- an unassigned channel can freshly claim only a globally unclaimed touch ID;
- channel ID 3 bypasses ordinary exact-ID ownership checks after geometry;
- nearby Arc conditions can temporarily relax exact-ID ownership.

On ordinary release, assignment and global claim are removed immediately, but re-acquisition can be locked out for:

```text
min(4 * tickInterval, 1000 ms)
```

This is separate from the long-event LOST grace `min(2*tickInterval,500 ms)`.

## 13. CONFIRMED connected Arc continuity

Connected Arc pieces are postprocessed using near-contiguous endpoints, including:

```text
abs(next.start - previous.end) <= 9 ms
abs(next.xStart - previous.xEnd) < 0.1
next.yStart == previous.yEnd
```

Accepted continuation pieces receive `+0xA0 = 1`.

If horizontal or vertical movement direction changes across the seam, the continuation receives `+0x6C = 1`. This same flag controls the previously mysterious special first-event overdue branch in the common long-note event scanner.

`LogicArcGroup` carries per-frame shared contact/timing state across connected pieces. Its contact bytes are reset each frame and refreshed by qualifying member Arc contact.

## 14. CONFIRMED ArcTap runtime logic

ArcTap remains a point note:

```text
LogicArcNote body -> LogicLongNoteBase repeated ticks
LogicArcTapNote   -> LogicTapNote one-shot timing judgement
```

Parent Arc path building evaluates each ArcTap timestamp through the same Arc easing/path machinery and caches a gameplay-space point in the child.

Touch-begin discovers ArcTap children through their parent Arc, spatially filters them in sky/gameplay space, and inserts qualifying children into the same timestamp-sorted point candidate vector used by other point notes.

ArcTap then uses the exact common point timing windows.

Parent Arc current body contact is not required for ArcTap judgement.

ArcTap hit/LOST overrides resolve local state first. If parent Arc mode == 2, they then veto normal ScoreState accounting; other modes use ordinary point-note score/gauge flow.

Automatic ArcTap miss processing is parent-owned and contains an overdue `> childTime + 100 ms` branch.

## 15. CONFIRMED touch lifetime

`GameModel::initializeTouchEvents()` installs four Cocos touch callbacks.

Touch-begin retains/registers `Touch*` in GameModel's active collection, performs gameplay-space candidate work, can immediately judge point notes, and can establish Hold/Arc acquisition state.

Touch-end/cancel removes and releases the Touch object and propagates release into Hold/LogicColor/Arc ownership state.

Gameplay therefore works from GameModel-managed active Cocos touches, not by having every note poll raw Android touch state.

## 16. CONFIRMED logic-critical update order

The core runtime has **two judgement entrances**.

### Event-driven touch-begin path

```text
finger begins
  ↓
project touch / build candidates
  ↓
sort point candidates by timestamp
  ↓
try point judgement immediately
  ↓
ScoreState immediately
  ↓
LifeBarState fan-out immediately
```

### Frame/scheduler path

The investigated main GameModel update contains this confirmed core ordering:

```text
1. derive/update effective gameplay time and spatial/timing state
2. update/reset per-frame Arc/contact/LogicColor/ArcGroup state
3. active-touch refresh                  (~0x14858D4)
     - Hold contact re-latched
     - Arc geometry + LogicColor contact re-latched
4. common scheduler / auto-miss / long-event judgement (~0x0F8056C)
     - resolved notes skipped
     - overdue point-like notes -> LOST
     - long notes: current contact selects success vs overdue LOST collector
     - ArcTap automatic-miss checks
     - ScoreState calls happen synchronously
5. later session/special-scene state continues outside this baseline chapter
```

The key proven ordering is **active-touch refresh before long-note scheduler/judgement**. Long-note ticks therefore see this update's contact state, not stale contact from the prior update.

## 17. Dormant Flick boundary strengthened

This binary contains:

- live chart Flick parser/source class;
- `LogicFlickNote` RTTI;
- surviving downstream gesture/expiry handlers;
- `RenderFlickNote` support;

but no `LogicFlickNote` class vtable and no runtime producer. Every LogicFlickNote RTTI code reference is a consumer/type test. Therefore the build cannot instantiate a valid most-derived LogicFlickNote object through the normal C++ polymorphic route. Treat Flick as dormant historical scaffolding in this target build.

Do not guess the absent chart-float -> runtime Flick hit-region producer formula.

## Section 03 unresolved/deferred details

These do **not** block the fundamental runtime model:

- exact original semantic names for several common contact/event bytes such as `+0x64/+0x65/+0x66`;
- original name of LogicNote `+0x10` input/hit payload;
- exact original names of several LogicArcGroup fields;
- developer rationale for the direction-change `+0x6C` first-event treatment;
- full special-system meaning of Arc `+0x170`, although baseline interactions are known in prior root chapters;
- exact final Arc/ArcTap camera-scaled hit-region dimensions belong to gameplay-space/render investigation;
- exact original semantic name for CameraControl's second Vec3;
- final displayed numerical-score formula was not reconstructed in this section; judgement counters, resolution and gauge propagation are complete.

---

# Continuity summary

**Completed:**

1. `deepdive/01_gameplay_architecture.cpp` — high-level source/runtime/render architecture and class families.
2. `deepdive/02_chart_source_data.cpp` — AFF/header/parser/source object model, timing groups, NotePosition, Timing, SceneControl, CameraControl, Arc source representation, ArcTap timestamps, sampling density, DESIGNANT source state, dormant Flick source scaffolding.
3. `deepdive/03_runtime_gameplay_logic.cpp` — source-to-runtime construction, shared gameplay clock, LogicTimingEvent/highspeed split, common LogicNote state, floor lane candidate selection, point judgement windows, ScoreState and baseline LifeBar/Recollection fan-out, long-note tick construction and expiry, Hold state machine, Arc body contact/LogicColor ownership/connected groups, ArcTap judgement, touch lifetime, and logic-critical update order.

**Most recent task:** Finish the remaining runtime gameplay logic. This is now complete.

**What is not unfinished Section 03:** rendering implementation, exact path/mesh formulas, scene-control visual effects, camera rendering transforms, note presentation/opacity/effects, and other renderer-facing state. Those belong to the next gameplay-rendering sections.

**Natural next direction inside the user's border:** move into gameplay rendering from the runtime side, preferably starting with the common `LogicNote <-> RenderNote` pairing and render lifecycle, then individual Tap/Hold/Arc/ArcTap rendering. The repo root already contains `15_rendering_fundamentals.cpp` and `22_rendering_refinements.cpp`; consult them first and use the current binary to consolidate/verify rather than restarting from zero.

**Scope restriction:** main gameplay and gameplay rendering only until the user explicitly expands it.
