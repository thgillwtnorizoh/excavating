# Arcaea Deep Dive AI Context

This file is the continuity handoff for the current `deepdive/` excavation.

## Working rules

- Investigated source bundle: stripped Arcaea APK/native engine represented by `arccopy.zip`; main native gameplay code is ARM64 `libcocos2dcpp.so`.
- Investigated binary SHA-256: `3eaca4e6dabb3395f276f8915698d57675757d0df0970e716b23a3dc201c79be`.
- Durable reconstruction repository: `thgillwtnorizoh/excavating`.
- Current consolidated chapters live in `deepdive/`.
- Chat is for excavation/explanation. Only after a section is genuinely complete should a durable C++-style pseudocode chapter be written.
- Never present reconstructed pseudocode as recovered original source.
- Evidence labels: **CONFIRMED**, **RECONSTRUCTED**, **UNRESOLVED**.
- Scope remains main gameplay and gameplay rendering. Gameplay HUD/UI is now explicitly included as its own presentation layer because it is directly tied to play, but menus/story/account/unlocks/network/progression remain outside scope unless the user expands it.
- Prefer blind excavation before the user reveals tag/effect names so expected behaviour does not bias interpretation.
- Explanations should keep plain-language summaries beside low-level evidence.

## Repository archaeology note

The repository root contains the earlier detailed notebook `01_recollection_rate.cpp` through `23_ybn_green_event_construction.cpp`. Consult and cross-check those files before re-solving a mechanic. The current `deepdive/` chapters are a consolidated architecture pass over the same investigated binary.

Particularly relevant earlier chapters include `01_recollection_rate.cpp`, `02_note_fundamentals.cpp`, `03_long_notes.cpp`, `04_arc_contact.cpp`, `05_arc_path.cpp`, `06_arctaps.cpp`, `08_lane_geometry.cpp`, `09_enwidenlanes.cpp`, `10_timinggroups.cpp`, `11_gameplay_space.cpp`, `12_arc_contact_refinements.cpp`, `13_arc_path_refinements.cpp`, `14_arc_mode_designant.cpp`, `15_rendering_fundamentals.cpp`, `16_scenecontrols.cpp`, `18_flick_runtime_disconnection.cpp`, `19_logiccolor_arc_tracking.cpp`, `21_lane_timing_refinements.cpp`, and `22_rendering_refinements.cpp`.

---

# Completed Section 01 — gameplay architecture

Durable file: `deepdive/01_gameplay_architecture.cpp`.

Main architecture:

```text
AFF/source Note objects
        ↓
Logic* gameplay objects
        ↓
Render* Cocos nodes
```

Confirmed source families include `SimpleNote`, `HoldNote`, `ArcNote`, `FlickNote`, `Timing`, `SceneControl`, and `CameraControl`.

Confirmed runtime families include `LogicTapNote`, `LogicArcTapNote`, `LogicLongNoteBase`, `LogicHoldNote`, `LogicArcNote`, `LogicSceneControl`, `LogicCameraControl`, and `LogicTimingEvent`.

Confirmed rendering families include `RenderTapNote`, `RenderArcTapNote`, `RenderHoldNote`, `RenderArcNote`, and dormant `RenderFlickNote` support.

Other important classes include `LogicChart`, `GameTimeline`, `GameModel`, `GameScene`, `TrackLayer`, `TrackBase`, `CameraController`, `GameSceneVisualControlHandler`, `LogicArcGroup`, `ScoreState`, `LifeBarState`, `HPBar`, `ArcSegment`, and `NoteBurstRenderer`.

---

# Completed Section 02 — chart / source data

Durable file: `deepdive/02_chart_source_data.cpp`.

Key results:

- AFF is split into generic header metadata plus chart body.
- Parsed container is a metadata map + all-notes vector + timing-group vectors.
- `AudioOffset` is interpreted with `atoi`; `TimingPointDensityFactor` defaults to `1.0f` and uses `atof` when present.
- Common source-note metadata includes timing-group ID, input-enabled/noinput state, fadingholds, anglex and angley.
- `NotePosition` represents either a discrete lane or free horizontal coordinate; mirror is resolved during construction.
- `SimpleNote`, `HoldNote`, `Timing`, `SceneControl`, `CameraControl`, and `ArcNote` layouts are recovered.
- Timing is time + BPM + beats-per-measure.
- Camera stores two Vec3 groups plus easing/duration; first is movement, second is a strongly reconstructed orientation/rotation family.
- Arc source stores ArcTap timestamps directly, ternary body mode false/true/DESIGNANT, optional render sampling multiplier, and optional trace-colour metadata.
- Flick source parsing survives but runtime production is disconnected. Treat Flick as dormant historical scaffolding in this build.

---

# Completed Section 03 — runtime gameplay logic

Durable file: `deepdive/03_runtime_gameplay_logic.cpp`.

Key results:

- Active source-to-runtime conversion covers Simple/Tap, CameraControl, SceneControl, Hold, Arc; Timing separately becomes `LogicTimingEvent`.
- Runtime timing separates raw musical BPM from highspeed-normalised spatial BPM.
- Point candidate windows are <=25 MAX PURE, <=50 PURE, <=100 FAR, <=120 LOST in the touch candidate routine.
- ScoreState owns judgement counters and fans successful/LOST events to every active LifeBarState.
- Baseline Recollection Factor and Normal/Easy/Hard damage families are reconstructed.
- Holds and judged Arc bodies use shared `LogicLongNoteBase` internal events.
- Long-event cadence is based on raw BPM, the 255 BPM subdivision seam, and `TimingPointDensityFactor`.
- Hold current contact is refreshed from active touches immediately before long-event scheduling.
- Arc body contact combines current path geometry with `LogicColor` touch ownership; ordinary release lockout and long-event LOST grace are separate timers.
- Connected Arc pieces use `LogicArcGroup` plus directional continuation links.
- ArcTap is a point note using ordinary point timing and does not require parent Arc body contact.
- Core update order proves active-touch refresh immediately before common scheduler/auto-miss/long-event judgement.
- Flick runtime handlers survive but no valid most-derived `LogicFlickNote` producer/vtable exists.

UI-derived refinements such as exact ScoreState display-counter offsets may later be added to Section 03 if they clarify runtime data layout, but HUD behaviour itself belongs to Section 05.

---

# Completed Section 04 — gameplay rendering

Durable file: `deepdive/04_gameplay_rendering.cpp`.

Section 04 has now been refined beyond the earlier mechanic-facing pass and includes the previously unresolved renderer internals.

## Logic/render bridge

```text
LogicNote  +0x40  = RenderNote*
RenderNote +0x2A8 = LogicNote*
```

Logic owns gameplay state; Render owns Cocos presentation.

## Camera-mask partition

Later excavation proves gameplay presentation is split across two camera masks rather than one flat global z-order list.

Confirmed examples:

```text
ordinary RenderTapNote -> camera mask 4
RenderArcTapNote        -> camera mask 16
```

Track/auxiliary presentation also assigns selected children to masks 4 and 16.

## Tap rendering

- floor Tap uses fixed lane X, Y≈4 and runtime approach depth as Z;
- far fade is `clamp((depth+9000)/1000,0,1)`;
- Tap textured-quad geometry deforms with depth and effective spatial BPM;
- `RenderTapNote +0x2B4` is now behaviourally resolved: it is set for precomputed chart-time ranges and multiplies `clamp((-depth)/8700,0,1)`, causing the Tap to disappear while approaching judgement depth;
- observed special range tables include song IDs `arghena`, `cataclysmcry`, `rivenpilgrim`, and `un` under one higher-level mode. The original enum/tag name remains unresolved.

## Hold rendering

Hold is one stretched body with normal/highlight texture switching. `fadingholds` is a presentation-only intensity calculation driven by long-event state and next-event horizon.

## Arc/ArcTap rendering

- ArcTap uses separate 3D model presentation and camera mask 16.
- LogicArcNote has gameplay samples at +0xE8 and denser render samples at +0x100.
- The optional Arc source float becomes a render-only tessellation multiplier at +0x118.
- Visible Arc body is a chain of four-corner ribbon/quad segment objects.
- RenderArcNote has separate body-segment, ArcTap, cap/approach-arrow and particle presentation branches.
- anglex/angley are tenths-of-degree source values converted to X/Y rotation matrices on Arc render geometry.
- tracecol arbitrary RGB is parsed, but traced render consumers in this build use metadata presence to select a dedicated gold trace path; arbitrary RGB output remains unproved.
- DESIGNANT uses RGB(240,41,97) with separate root/segment opacity factors.

## Track/SceneControl rendering

- `enwidenlanes` uses fixed six-lane geometry plus extra-lane artwork; lane centres do not move.
- `track_custom_mask_shader` reshapes presentation without rewriting gameplay lanes.
- `enwidencamera` applies Tap auxiliary opacity compensation.
- trackdisplay, redline, arcahvdistort, arcahvdebris and hidegroup presentation paths are recovered.

## CameraController resolved

`CameraController : cocos2d::Node` owns two actual `cocos2d::Camera` children:

```text
+0x2A8 Camera* flag 4,  near 1, far 10000
+0x2B0 Camera* flag 16, near 1, far 9000
+0x2B8 shared look-at Vec3
+0x2C4 shared camera-position Vec3
```

Both cameras receive the same position/look-at and move together. `animateMovingCameraTo(Vec3,float)` schedules ~60 Hz quadratic motion under key `moveCamera` for nonzero duration.

No separate Arcaea camera-depth/priority write was found after creation; their gameplay-facing distinction is camera flag/mask and far plane.

## GameScene render-command boundary resolved

`GameScene::render(...)::$_6` is the GL-state cleanup callback:

```cpp
glDisable(GL_DEPTH_TEST);
glDisable(GL_STENCIL_TEST);
glDepthMask(false);
glDisable(GL_BLEND);
```

The owning `cocos2d::CustomCommand` is initialized with global order 0 and non-3D flags, then queued after normal scene traversal has emitted gameplay commands. Cocos therefore places it in the zero-global-order 2D queue, between the 3D command families and later positive-global-order presentation.

This resolves the previous placement uncertainty: it is a queued graphics-state boundary, not random cleanup at function return.

## Remaining narrow Section 04 unknowns

Only bounded implementation details remain:

- original name of the higher-level mode enum controlling special Tap fade ranges;
- original method/name for CameraControl's second Vec3 orientation path;
- whether an untraced consumer ever displays arbitrary parser-stored tracecol RGB;
- pixel-perfect Action nesting and generic same-priority Cocos camera traversal details.

These do not block the gameplay-render architecture.

---

# Current Section 05 — gameplay UI / HUD layer

This section is intentionally separate from Section 04.

Reason: gameplay simulation and note/track rendering can function without the HUD overlay. The HUD consumes gameplay state and can be replaced/mutated externally without becoming the source of judgement truth.

Current confirmed starting architecture:

```cpp
UILayer::init(
    PauseLayerDelegate*,
    Song*,
    DifficultyClass,
    int,
    PlayModifier,
    GameMode,
    CharacterAbility*,
    GameModel*,
    PlayParameters
);
```

`UILayer : cocos2d::Layer` therefore knows song/difficulty/modifier/mode/ability/model/play-parameter context at construction time.

Visible base HUD resources include right-side song/jacket/difficulty/progress/pacemaker/note-info pieces, left-side pause UI, and an independent `HPBar : cocos2d::Node` gauge presentation.

Current confirmed live links include:

```text
GameModel/gameplay clock -> progress bar
ScoreState counters       -> optional noteInfo labels
LifeBarState RR/state     -> HPBar presentation
```

`CharacterAbilityViewNoteResults` enables a live noteInfo panel and UILayer updates PURE/FAR/LOST/EARLY/LATE counters from ScoreState.

Other CharacterAbility subtypes have confirmed custom HUD branches, including Ongeki-, Rotaeno-, DJMAX-, C2-/peak-health-, grade-based/DORO*C-like, and note-results presentation families.

SpecialScene classes can mutate existing HUD objects during gameplay. Confirmed examples include Tempestissimo challenge callbacks acting on HPBar, SpinCountLabel and sprites, and an Aegleseeker update path acting on SpinCountLabel.

Section 05 remains OPEN. Finish before committing a durable `05_*.cpp` chapter. Current targets:

1. ordinary score/combo display and exact SpacedScoreText/SpinCountLabel ownership;
2. pacemaker update arithmetic and semantics;
3. HPBar live transitions, especially Insight Hard switching and special-scene mutations;
4. classify construction-time HUD selection versus runtime external HUD mutation.

---

# Continuity summary

**Completed:**

1. `deepdive/01_gameplay_architecture.cpp`
2. `deepdive/02_chart_source_data.cpp`
3. `deepdive/03_runtime_gameplay_logic.cpp`
4. `deepdive/04_gameplay_rendering.cpp` — now includes refined camera masks, two-camera CameraController layout, special Tap fade ranges, and exact queued GameScene GL-state boundary.

**Current:** Section 05 gameplay UI/HUD and external effects on HUD elements.

**Scope rule:** keep gameplay UI/HUD separate from core gameplay rendering. HUD may consume or present runtime state, but do not move judgement or gauge truth into UI classes unless native evidence proves it.
