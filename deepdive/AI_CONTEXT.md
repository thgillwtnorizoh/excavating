# Arcaea Deep Dive AI Context

This file is the continuity handoff for the current `deepdive/` excavation.

## Working rules

- Investigated source bundle: stripped Arcaea APK/native engine represented by `arccopy.zip`; main native gameplay code is ARM64 `libcocos2dcpp.so`.
- Investigated binary SHA-256: `3eaca4e6dabb3395f276f8915698d57675757d0df0970e716b23a3dc201c79be`.
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
- Prefer blind excavation first. The user may reveal a tag/effect meaning after the native behaviour is characterised so that expectations do not bias reconstruction.

## Important repository archaeology note

The repository root already contains an earlier detailed excavation notebook, currently `01_recollection_rate.cpp` through `23_ybn_green_event_construction.cpp`, plus `README.md`. These are durable prior findings and should be consulted/cross-checked before re-solving a mechanic from scratch. The current `deepdive/` chapters are a newer consolidated pass over source architecture, runtime logic and rendering.

Particularly useful earlier chapters for Sections 03/04 include:

- `01_recollection_rate.cpp`
- `02_note_fundamentals.cpp`
- `03_long_notes.cpp`
- `04_arc_contact.cpp`
- `05_arc_path.cpp`
- `06_arctaps.cpp`
- `08_lane_geometry.cpp`
- `09_enwidenlanes.cpp`
- `10_timinggroups.cpp`
- `11_gameplay_space.cpp`
- `12_arc_contact_refinements.cpp`
- `13_arc_path_refinements.cpp`
- `14_arc_mode_designant.cpp`
- `15_rendering_fundamentals.cpp`
- `16_scenecontrols.cpp`
- `18_flick_runtime_disconnection.cpp`
- `19_logiccolor_arc_tracking.cpp`
- `21_lane_timing_refinements.cpp`
- `22_rendering_refinements.cpp`

Those root chapters use the same investigated binary SHA and were rechecked against current native anchors during the consolidated pass.

---

# Completed Section 01 — gameplay architecture

Durable file: `deepdive/01_gameplay_architecture.cpp`.

## Main result

Arcaea separates three class families:

```text
AFF/source Note objects
        ↓
Logic* gameplay objects
        ↓
Render* Cocos nodes
```

Confirmed source types include `SimpleNote`, `HoldNote`, `ArcNote`, `FlickNote`, `Timing`, `SceneControl`, and `CameraControl`.

Confirmed runtime families include `LogicTapNote`, `LogicArcTapNote`, `LogicLongNoteBase`, `LogicHoldNote`, `LogicArcNote`, `LogicSceneControl`, `LogicCameraControl`, and `LogicTimingEvent`.

Confirmed render families include `RenderTapNote`, `RenderArcTapNote`, `RenderHoldNote`, `RenderArcNote`, and dormant `RenderFlickNote` support.

Other confirmed gameplay/render objects include `LogicChart`, `GameTimeline`, `GameModel`, `GameScene`, `TrackLayer`, `TrackBase`, `CameraController`, `GameSceneVisualControlHandler`, `LogicArcGroup`, `ScoreState`, `LifeBarState`, `HPBar`, `ArcSegment`, and `NoteBurstRenderer`.

Retained C++ names include:

- `GameModel::initializeTouchEvents()`
- `GameScene::render(Renderer*, const Mat4&, const Mat4*)`
- `GameScene::swapModel(GameModel*, TrackLayer*)`
- `GameSceneVisualControlHandler::handleLogicSceneControl(LogicSceneControl*)`
- `TrackBase::performTrackSplitAnimation()`
- `CameraController::animateMovingCameraTo(Vec3, float)`

---

# Completed Section 02 — chart / source data

Durable file: `deepdive/02_chart_source_data.cpp`.

## AFF organisation

AFF is split at the `-` separator into generic header metadata and chart body.

```cpp
struct ParsedChart {
    std::map<std::string, std::string> metadata;       // +0x00
    std::vector<Note*> allNotes;                       // +0x18
    std::vector<std::vector<Note*>*> timingGroups;     // +0x30
}; // sizeof = 0x48
```

`AudioOffset` is later interpreted with `atoi()`. `TimingPointDensityFactor` defaults to `1.0f` and is replaced with `atof()` when present.

## Common source Note metadata

```cpp
class Note {
    int timingGroupId;     // +0x08
    bool inputEnabled;     // +0x0C, from noinput behaviour
    bool fadingHolds;      // +0x0D
    int angleX;            // +0x10
    int angleY;            // +0x14
};
```

Timing-group modifiers include `noinput`, `fadingholds`, `anglex`, `angley`, and Arc-only `tracecol`.

Default timing group is ID 0. Explicit groups are numbered 1,2,3... The same source note pointer is stored in `allNotes` and in its group vector.

## NotePosition

`NotePosition : cocos2d::Ref` represents either a discrete lane or a free horizontal coordinate.

Integer source 0..5 maps to internal lane IDs 1..6 and stored horizontal values `-0.5,0,0.5,1,1.5,2`. Mirror reverses discrete mapping. Float positions mirror as `x = 1-x`.

## Source note models

`SimpleNote` stores point time + `NotePosition*`.

`HoldNote` stores start/end time + `NotePosition*`.

`Timing` stores time, BPM, and the later-confirmed beats-per-measure value.

`SceneControl` is generic: timestamp, command string, float parameter and integer parameter. Parser normalisation includes `trackhide`/`trackshow` becoming `trackdisplay` with target values 0/255.

`CameraControl` stores time, six floats, easing and duration. Runtime preserves order; the first three form a movement Vec3 and the second three form a strongly reconstructed orientation/rotation Vec3. Mirror negates the first movement component and sixth component.

`ArcNote` stores times, X/Y endpoints, easing, colour, effect, body mode, ArcTap timestamp vector, optional sampling float and optional trace-colour metadata.

Arc body type is ternary in this build:

- false -> 0
- true -> 1
- native `DESIGNANT` -> 2

ArcTaps are source timestamps inside the parent Arc, not separate source objects.

The optional Arc float defaults to 1.0 and later controls denser render tessellation; runtime clamps it to at least 1.0.

## Dormant Flick

Chart `FlickNote` parsing and downstream runtime/render handlers survive, but the investigated binary contains no `LogicFlickNote` class vtable and no producer. Treat Flick as dormant historical scaffolding in this build. Do not invent the missing chart-float semantics.

---

# Completed Section 03 — runtime gameplay logic

Durable file: `deepdive/03_runtime_gameplay_logic.cpp`.

## Source -> runtime

Active conversion handles `SimpleNote`, `CameraControl`, `SceneControl`, `HoldNote`, and `ArcNote`. `Timing` separately becomes per-group `LogicTimingEvent`. No live Flick producer exists.

Each note receives the latest timing event in its group with `event.time <= note.time`.

## Timing model

Runtime timing carries raw musical BPM and a separate spatial speed form.

`LogicChart +0xF0` is selected highspeed. `LogicChart +0xF4` is:

```cpp
scrollScale = highspeed * 180 / chartBaseBpm;
```

Each `LogicTimingEvent` stores roughly:

```cpp
int time;
float effectiveSpatialBpm;      // raw BPM * scrollScale
float beatsPerMeasure;
float rawBpm;
float secondsPerEffectiveBeat;  // 60 / effectiveSpatialBpm
```

Important split:

- raw BPM -> Hold/Arc long-event cadence
- effective spatial BPM -> note/path travel
- shared gameplay clock -> judgement/expiry

Point judgement does not use a private timing-group clock.

## Point-note judgement

Touch-begin builds spatially eligible candidates and sorts them by timestamp.

Inclusive point candidate windows:

```text
|error| <= 25 ms   -> MAX PURE
|error| <= 50 ms   -> PURE
|error| <= 100 ms  -> FAR
|error| <= 120 ms  -> LOST through ScoreState
>120 ms            -> candidate routine rejects this attempt
```

Pure/Far carry Early/Late side information. Automatic scheduling can independently miss overdue notes, so the candidate window is not claimed as a universal symmetric play window.

## ScoreState / LifeBarState

Successful judgement order:

```text
note accepts success
  -> score counters
  -> every active LifeBarState
  -> timing/statistics
```

LOST uses a separate note-acceptance path, increments LOST and fans out to all life bars.

Baseline Recollection:

```cpp
if (N < 400)      RF = 0.2 + 80/N;
else if (N < 600) RF = 0.2 + 32/N;
else              RF = 0.08 + 96/N;
```

MAX PURE/PURE gain RF; FAR gains half RF. Long-note LOST events receive half ordinary-note damage before further modifiers. Detailed special partner/gauge systems remain outside the baseline section.

## Long-note event model

`LogicLongNoteBase` owns repeated 12-byte events:

```cpp
struct LongTickEvent {
    int time;
    int scoreUnits;      // common builder writes 1
    byte processedFlags; // bit0 = processed
};
```

Tick interval:

```cpp
beatMs = 60000 / abs(rawBpm);
subdivision = abs(rawBpm) >= 255 ? 1 : 2;
tickInterval = beatMs / subdivision / TimingPointDensityFactor;
```

Success horizon is `now + 0.5*tickInterval`.

Ordinary LOST horizon is `now - min(2*tickInterval,500ms)`.

Successful long units enter ScoreState as judgement class 0/side 0 rather than point timing bands.

## Hold

Hold touch-begin establishes persistent engagement; per-frame current contact at `+0x64/+0x65` is cleared and refreshed from active touches immediately before long-event judgement. Releasing does not instantly lose a pending event; it ages toward the long-event LOST cutoff. Re-press before expiry can recover still-pending events.

## Arc

Judged Arc bodies share the long-note event system but require per-frame geometric contact plus `LogicColor` touch ownership.

Relevant state includes:

- Arc body mode `+0xA4`
- `LogicColor* +0xB0`
- active/current path state around `+0xD0/+0xD4/+0xD8`
- `LogicArcGroup* +0xE0`
- ArcTap child vector `+0x120`

Only body mode 0 uses ordinary Arc-body judgement. Mode 1/2 bodies are nonjudged through this common path.

`LogicColor` combines colour/channel identity with touch-ID ownership. Ordinary release can start a separate re-acquisition lockout:

```text
min(4*tickInterval,1000ms)
```

This is mechanically distinct from long-event LOST grace `min(2*tickInterval,500ms)`.

Connected Arc pieces use `LogicArcGroup`. Continuations are recognised around <=9 ms time separation, <0.1 X separation and matching Y endpoint. Continuation flag `+0xA0` is set; if X or Y motion direction changes, `+0x6C` is set and controls the special first-event expiry branch.

## ArcTap

`LogicArcTapNote : LogicTapNote`. Parent Arc path construction caches its spatial point. Touch-begin discovers it through the parent Arc and performs sky/gameplay-space filtering; then it uses the ordinary point timing routine. Parent Arc current body contact is not required.

## Core frame ordering

Two judgement entrances exist:

```text
touch begin -> candidate selection -> immediate point judgement
```

and:

```text
update gameplay/spatial state
 -> reset current contact/ownership-frame state
 -> active-touch refresh
 -> common scheduler / auto-miss / long-event judgement
```

The important proven order is active-touch refresh immediately before long-note scheduling, so Hold/Arc ticks see current-frame contact.

---

# Most recent completed task: Section 04 — gameplay rendering

Durable file: `deepdive/04_gameplay_rendering.cpp`.

Section 04 closes the mechanic-facing renderer from LogicNote/RenderNote pairing through note presentation, Arc tessellation, Hold/contact feedback, track/scene visual controls, camera movement, and the GameScene GL-state boundary.

## 1. CONFIRMED LogicNote <-> RenderNote pairing

The central render factory stores reciprocal links:

```text
LogicNote  +0x40 = RenderNote*
RenderNote +0x2A8 = LogicNote*
```

Gameplay logic owns judgement/input/path state. Render objects own Cocos scene nodes, sprites, models, textures, mesh pieces and opacity.

Factory dispatch:

```text
LogicTapNote   -> RenderTapNote
LogicHoldNote  -> RenderHoldNote
LogicFlickNote -> RenderFlickNote   (dormant consumer)
LogicArcNote   -> RenderArcTapNote children + RenderArcNote parent
```

Approximate sizes:

- RenderTapNote 0x2D0
- RenderHoldNote 0x2E0
- RenderArcTapNote 0x2E0
- RenderFlickNote 0x2D0

The factory registers top-level renderers in a common renderer collection. Exact global cross-family z-order remains unresolved.

## 2. CONFIRMED common spatial handoff

Renderers consume already-computed runtime state, including `NotePosition`, current depth/path state, active TimingEvent spatial values, hidegroup flag and Arc path samples. They do not rerun ScoreState judgement.

This is the core responsibility boundary:

```text
logic decides where/state
renderer decides how it looks
```

## 3. CONFIRMED Tap rendering

Assets include:

- `img/note.png`
- `img/note_dark.png`
- `img/note_tomato.png`

Discrete lane X uses:

```cpp
x = (laneId - 1) * 425 - 1063;
```

Tap root uses Y=4 and runtime depth as scene Z.

Direct disassembly newly rechecked in Section 04 proves ordinary horizon opacity:

```cpp
alpha = clamp((depth + 9000) / 1000, 0, 1) * 255;
```

Thus Tap is transparent at/before depth -9000, fades through approximately -9000..-8000, and reaches full ordinary opacity by about -8000.

A render byte around `+0x2B4` optionally multiplies an additional:

```cpp
clamp((-depth) / 8700, 0, 1)
```

Its exact semantic trigger/name remains unresolved.

Tap textured-quad geometry is also rewritten with approach depth. Directly rechecked native arithmetic is approximately:

```cpp
speedFactor = clamp(effectiveSpatialBpm / 400, 1, 1.5);
approach = (-depth / 10000) * speedFactor;
longitudinalExtent = int(200 + 340 * approach);
localHalfWidth = 191;
```

This is presentation geometry, not judgement.

`hidegroup` ultimately forces presentation opacity to zero while `noinput` remains the separate input gate.

## 4. CONFIRMED Hold rendering and fadingholds

Hold uses one stretched body rather than one sprite per internal tick.

Normal assets include:

- `img/note_hold.png`
- `img/note_hold_dark.png`
- `img/note_hold_tomato.png`

Highlighted contact variants include corresponding `_hi` textures.

Current Hold contact chooses highlight presentation. Body length is derived from current/start/end depth.

`fadingholds` is presentation feedback. With no current contact after start:

- latest LOST batch -> 0.5 intensity
- after a successful batch, next unprocessed event defines:

```cpp
horizon = nextTick.time + 2*tickInterval;
intensity = clamp(0.5 + (horizon-now)/500, 0.5, 1.0);
```

This multiplies ordinary opacity and does not alter judgement timestamps.

## 5. CONFIRMED ArcTap rendering

ArcTap rendering is a small 3D model, not the ordinary floor sprite. Surviving models include:

- `models/tap_l.obj`
- `models/tap_d.obj`
- `models/tap_tomato.obj`
- `models/sfx_l.obj`
- `models/sfx_d.obj`

Parent Arc path construction supplies the cached spatial point consumed by RenderArcTapNote.

## 6. CONFIRMED dual Arc tessellations

LogicArcNote owns two Vec3 sample vectors of the same analytic Arc:

```text
+0xE8  -> gameplay/contact polyline
+0x100 -> denser renderer polyline
```

Base density uses:

```text
nominal duration < 1000ms -> 14
otherwise                 -> 7
```

Chart Arc optional float becomes runtime `+0x118`, clamped to at least 1. It multiplies **only the render tessellation**:

```text
gameplayStep ~= 1/(D*B)
renderStep   ~= 1/(D*B*M)
```

So increasing this field visually smooths the Arc without increasing contact/hit-polyline resolution.

Connected continuations may extend effective sampling end only within the already-proved tiny <=9ms seam tolerance.

## 7. CONFIRMED Arc ribbon scene graph

RenderArcNote walks consecutive +0x100 samples and creates one ribbon segment per adjacent pair. Each segment generates four corners around two endpoints, so the visible Arc is a quad/ribbon strip, not a line primitive.

Arc assets include `img/arc_body.png` and `img/arc_body_hi.png`.

Selected RenderArcNote children:

```text
+0x2B8 body segment container
+0x2C0 ArcTap render container
+0x2D0 Arc cap sprite
         + optional approach arrow
```

Assets include `img/1080/arc_cap.png` and `img/1080/approach_arrow.png`. Arc particles are separate emitter nodes/resources rather than ribbon pieces.

## 8. CONFIRMED timinggroup angle render transforms

Source angle integers are tenths of a degree:

```cpp
radians = raw * PI / 180 / 10;
```

Runtime uses roughly `LogicNote +0x58` for X angle and `+0x5C` for Y angle. Arc renderer applies corresponding X/Y rotation matrices to visual geometry.

The common touch unprojection path does not rotate the screen-touch ray by these same matrices. Therefore these angle modifiers are visual/spatial note transforms, not a rewrite of raw input projection.

## 9. CONFIRMED Arc colour/opacity presentation

Traced `tracecol` render consumers treat metadata presence as a dedicated gold trace branch:

```text
RGB(244,185,66)
img/trace_body_gold.png
```

Important limitation: although parser source stores arbitrary R/G/B integers, this excavation did not trace a consumer that renders arbitrary custom RGB. Do not claim arbitrary `tracecolRRGGBB` output without new evidence.

DESIGNANT/body mode 2 uses hot pink/red family RGB(240,41,97). Root and segment opacities scale separately from a global factor, approximately `factor*125` and `factor*225`.

`LogicColor` rejection-feedback presentation can blend judged Arcs toward RGB(230,50,50). The warning colour state is separate from the mechanical ownership/re-acquisition lockout.

## 10. CONFIRMED common opacity and hidegroup

Render-note virtuals around the established opacity slots behave consistently as opacity get/set methods.

`hidegroup` writes runtime visibility state to matching notes and ArcTap children. Renderer paths consume it and suppress presentation. Input paths continue to use `noinput`/`+0x54` separately.

## 11. CONFIRMED track/widen presentation

`enwidenlanes` activates pre-existing outer gameplay lane IDs 1 and 6; it does not move lane centres.

Track resources include:

- `img/track_extralane_light.png`
- `img/track_extralane_dark.png`

A native `track_custom_mask_shader` uses uniforms such as `z_clip`, `texture_mask`, `mask_inverse`, `texture_height`, `track_width`, `fixed_x_pos`, and `model_width` to carve/mask visible track artwork in track space. This changes presentation without rewriting lane geometry.

`enwidencamera` has a presentation compensation:

```cpp
factor = 1 - (cameraWidenScale - 1) * 2/3;
```

A traced Tap branch applies approximately `factor*130` opacity to auxiliary child nodes while leaving the main child separate. This consumer is opacity compensation, not note geometry scaling.

## 12. CONFIRMED SceneControl visual effects

Runtime mapping:

```text
trackdisplay  -> 0
redline       -> 1
arcahvdistort -> 2
arcahvdebris  -> 3
hidegroup     -> 4
enwidencamera -> 5
enwidenlanes  -> 6
```

`trackdisplay` uses 100-step quadratic interpolation:

```cpp
p = step/100;
value = start + (target-start)*p*p;
```

The scheduler interval is `floatParameter/100`. Background darkening uses `img/bg/bg_darken.png`.

`redline` creates `img/redline.png`, animates it and removes it after the float-parameter lifetime.

`arcahvdistort` fades prepared node `ARCAHV_DISTORT` / `img/bg/arcahv-srt.png` to the low 8 bits of intParameter over floatParameter duration.

`arcahvdebris` similarly fades prepared animated container `ARCAHV_DEBRIS` / `img/bg/arcahv-debris.png`.

These branches are presentation-side; widening types also have gameplay-space effects already documented in Section 03/root chapters.

## 13. CONFIRMED CameraController movement

Retained lambda type proves `CameraController::animateMovingCameraTo(Vec3,float)`.

Direct disassembly in Section 04 additionally confirms:

- CameraController owns two scene/camera-like nodes around `+0x2A8/+0x2B0`.
- duration 0 sets both immediately to target Vec3.
- nonzero duration uses scheduler key `moveCamera`.
- step count is approximately `int(duration*60)`.
- interval is `duration/stepCount`, approximately 60 Hz.
- callback reads current position, uses quadratic progress and applies the same Vec3 to both nodes.

Readable coordinate update:

```cpp
p = step / totalSteps;
next = current + (target-current) * p*p;
```

Exact high-level identities of the two nodes remain unresolved. The source CameraControl second Vec3 remains strongly reconstructed as a separate orientation/rotation family, but no original method name is claimed without equally strong evidence.

## 14. CONFIRMED GameScene render GL-state boundary

A retained lambda type is explicitly scoped inside:

```text
GameScene::render(Renderer*, const Mat4&, const Mat4*)::$_6
```

Its callback executes:

```cpp
glDisable(GL_DEPTH_TEST);
glDisable(GL_STENCIL_TEST);
glDepthMask(false);
glDisable(GL_BLEND);
```

Therefore the gameplay scene explicitly restores/brackets low-level GL state rather than leaking gameplay 3D/depth/blend state indefinitely into later Cocos drawing.

Exact placement of this custom command relative to every other scene command remains unresolved, so do not claim a pixel-perfect global draw-order table from this callback alone.

## 15. Reconstructed full render pipeline

```text
runtime logic computes position/contact/path/visibility state
                         ↓
               LogicNote <-> RenderNote
                         ↓
        Tap/Hold/Arc/ArcTap presentation
                         ↓
        Track masks / SceneControl visual state
                         ↓
                  Cocos scene graph
                         ↓
              CameraController transforms
                         ↓
                  cocos2d Renderer
                         ↓
        GameScene explicit GL-state boundary/reset
```

The central durable rule is:

> Logic decides where the note is and what gameplay state it is in. Renderer decides how that state is shown.

Renderer-side feedback does not replace judgement.

## Section 04 unresolved/deferred details

These do **not** block the mechanic-facing renderer model:

- exact global z-order among every top-level note/track/effect node;
- exact original identity/name of CameraController `+0x2A8/+0x2B0` child nodes;
- exact original method/name for CameraControl's second orientation Vec3;
- exact semantic name/trigger of RenderTapNote byte `+0x2B4` controlling the additional near-depth alpha factor;
- whether any untraced consumer renders arbitrary parser-stored tracecol RGB rather than the confirmed fixed gold branch;
- pixel-perfect particle/action nesting and generic Cocos internals unrelated to Arcaea mechanics.

---

# Continuity summary

**Completed:**

1. `deepdive/01_gameplay_architecture.cpp` — source/runtime/render class architecture.
2. `deepdive/02_chart_source_data.cpp` — AFF/header/parser/source objects, timing groups, NotePosition, source Timing/SceneControl/Camera/Arc, ArcTap timestamps, DESIGNANT source state, dormant Flick source path.
3. `deepdive/03_runtime_gameplay_logic.cpp` — source-to-runtime construction, timing/highspeed split, point judgement, ScoreState/LifeBar fan-out, long-note events, Hold/Arc/ArcTap state machines, LogicColor ownership, touch lifetime and core frame ordering.
4. `deepdive/04_gameplay_rendering.cpp` — LogicNote/RenderNote bridge, Tap/Hold/ArcTap/Arc presentation, Tap horizon fade and approach quad deformation, fadingholds feedback, dual Arc tessellations and ribbon construction, Arc colour/opacity modes, hidegroup, widening/track masking, SceneControl visuals, CameraController movement, and GameScene GL-state reset boundary.

**Most recent task:** Move from runtime logic into gameplay rendering. This section is now complete at the mechanic-facing level.

**Remaining rendering unknowns are bounded implementation details**, primarily exact global z-order, a few original field names, and pixel-perfect Cocos/GPU command sequencing. They are not treated as missing foundational gameplay-render behaviour.

**Natural next direction inside the user's border:** either drill deeper into one renderer family (for example exact Arc ribbon/segment geometry and materials, exact Tap/Hold mesh vertices, track/camera projection matrices), or return to a gameplay mechanic that depends on the now-complete source/runtime/render stack. Do not leave main gameplay/gameplay rendering scope unless the user explicitly widens it.
