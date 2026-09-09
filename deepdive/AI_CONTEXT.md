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
- Scope is main gameplay, gameplay rendering, and gameplay HUD/UI. Menus/story/account/unlocks/network/progression remain outside scope unless the user expands it.
- Prefer blind excavation before the user reveals tag/effect names so expected behaviour does not bias reconstruction.
- Explain plain-English behaviour beside low-level evidence.

## Repository archaeology note

The repository root contains the earlier detailed notebook `01_recollection_rate.cpp` through `23_ybn_green_event_construction.cpp`. Consult and cross-check those before re-solving a mechanic. The current `deepdive/` chapters are a consolidated architecture pass over the same investigated binary.

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
        ↓
UILayer / gameplay HUD consumers
```

Confirmed source families include `SimpleNote`, `HoldNote`, `ArcNote`, `FlickNote`, `Timing`, `SceneControl`, and `CameraControl`.

Confirmed runtime families include `LogicTapNote`, `LogicArcTapNote`, `LogicLongNoteBase`, `LogicHoldNote`, `LogicArcNote`, `LogicSceneControl`, `LogicCameraControl`, and `LogicTimingEvent`.

Confirmed rendering families include `RenderTapNote`, `RenderArcTapNote`, `RenderHoldNote`, `RenderArcNote`, and dormant `RenderFlickNote` support.

Other important classes include `LogicChart`, `GameTimeline`, `GameModel`, `GameScene`, `TrackLayer`, `TrackBase`, `CameraController`, `GameSceneVisualControlHandler`, `LogicArcGroup`, `ScoreState`, `LifeBarState`, `HPBar`, `ArcSegment`, `NoteBurstRenderer`, `UILayer`, and `SpinCountLabel`.

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

Section 05 independently confirms useful HUD-facing ScoreState offsets:

```text
+0x14 numerical score
+0x1C recall/combo
+0x20 Max Pure
+0x24 Pure
+0x28 Far
+0x2C Lost
+0x40/+0x48 late-side counter family
+0x44/+0x4C early-side counter family
```

The exact original semantic distinction between the two early/late counter subfamilies remains unnamed, but their display use is confirmed.

---

# Completed Section 04 — gameplay rendering

Durable file: `deepdive/04_gameplay_rendering.cpp`.

Section 04 includes the refined renderer internals rather than only the earlier mechanic-facing pass.

## Logic/render bridge

```text
LogicNote  +0x40  = RenderNote*
RenderNote +0x2A8 = LogicNote*
```

Logic owns gameplay state; Render owns Cocos presentation.

## Camera-mask partition

Gameplay presentation is split across two camera masks rather than one flat global z-order list.

Confirmed examples:

```text
ordinary RenderTapNote -> camera mask 4
RenderArcTapNote        -> camera mask 16
```

Track/auxiliary presentation also assigns selected children to masks 4 and 16.

## Tap/Hold/Arc rendering

- floor Tap uses fixed lane X, Y≈4 and runtime approach depth as Z;
- far fade is `clamp((depth+9000)/1000,0,1)`;
- Tap textured-quad geometry deforms with depth and effective spatial BPM;
- `RenderTapNote +0x2B4` is set for precomputed chart-time ranges and multiplies `clamp((-depth)/8700,0,1)`, causing the Tap to disappear while approaching judgement depth;
- observed special range tables include song IDs `arghena`, `cataclysmcry`, `rivenpilgrim`, and `un` under one unresolved higher-level mode enum;
- Hold is one stretched body with normal/highlight textures; fadingholds is presentation-only intensity feedback;
- ArcTap uses separate 3D model presentation and camera mask 16;
- LogicArcNote has gameplay samples at +0xE8 and denser render samples at +0x100;
- optional Arc source float becomes render-only tessellation multiplier +0x118;
- visible Arc body is a chain of four-corner ribbon/quad segments;
- anglex/angley become X/Y render rotations;
- traced tracecol consumers select a fixed gold path on metadata presence; arbitrary RGB rendering remains unproved;
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

## GameScene render-command boundary resolved

`GameScene::render(...)::$_6` is the GL-state cleanup callback:

```cpp
glDisable(GL_DEPTH_TEST);
glDisable(GL_STENCIL_TEST);
glDepthMask(false);
glDisable(GL_BLEND);
```

The owning `cocos2d::CustomCommand` is initialized with global order 0 and non-3D flags, then queued after normal scene traversal has emitted gameplay commands. This is a queued graphics-state boundary, not random cleanup at function return.

Remaining Section 04 unknowns are bounded implementation details such as the original special-Tap mode enum name, CameraControl second-Vec3 method name, arbitrary tracecol RGB usage, and pixel-perfect Cocos action/camera ordering.

---

# Completed Section 05 — gameplay UI / HUD

Durable file: `deepdive/05_gameplay_ui.cpp`.

Section 05 is deliberately separate from Section 04: gameplay logic and world rendering can operate without this overlay. `UILayer`/`HPBar` consume gameplay state and can be replaced or mutated without becoming judgement truth.

## UILayer construction context

Surviving signature:

```cpp
UILayer::init(
    PauseLayerDelegate*, Song*, DifficultyClass, int,
    PlayModifier, GameMode, CharacterAbility*, GameModel*, PlayParameters);
```

Base HUD resources cover song/jacket/difficulty, pause control, rolling score, recall/combo, progress bar/glow, pacemaker, optional noteInfo, and independent HPBar gauge presentation.

## Score and recall display

Normal numerical score uses `SpinCountLabel : cocos2d::CCLabelCustomRenderSize` at roughly `UILayer +0x390`.

The visible score rolls from old to new target over approximately 500 ms rather than jumping instantly.

Recall/combo uses a separate custom-size label fed from `ScoreState +0x1C`, with zero/nonzero and change animations.

`SpinCountSpacedLabel : SpacedScoreText` was traced to a separate specialised formatted presentation and is not the ordinary gameplay score path.

## noteInfo and Early/Late setting closure

`CharacterAbilityViewNoteResults` enables live labels:

```text
PURE
FAR
LOST
EARLY
LATE
```

Current runtime key is exactly:

```text
lateearly_showall
```

Loader around ~0x1A02CF0 reads it directly into settings `+0x18`; setter around ~0x0F6B360 writes that byte and persists the same key; noteInfo around ~0x1609720 reads exactly that byte.

Legacy keys:

```text
arcaea_early_all
arcaea_late_all
```

survive in migration/compatibility code but are not the live per-frame noteInfo source.

When `lateearly_showall` is false:

```text
EARLY = +0x44
LATE  = +0x40
```

When true:

```text
EARLY = +0x44 + +0x4C
LATE  = +0x40 + +0x48
```

## Pacemaker

The traced pacemaker is a projected-final-score grade pacemaker.

It estimates final score from judged progress and compares against ordered grade thresholds:

```text
C    8,600,000
B    8,900,000
A    9,200,000
AA   9,500,000
EX   9,800,000
EX+  9,900,000
```

Native grade text mapping is D/default, C, B, A, AA, EX, EX+. The HUD shows signed projectedScore-targetScore through `pacemakerPlusMinus`; a special MAX branch also exists.

## Progress

Progress bar is gameplay time divided by the duration supplied to UILayer:

```cpp
position = max(nowMs / durationMs * 380, 0);
```

The glow follows the current endpoint with an observed offset around -405.

## HPBar

`LifeBarState` remains authoritative gauge state. `HPBar` is presentation.

Important HPBar fields include root, hpBar, hpBar2, insightOverlay, hpGlow, hpTop, hpLabel, previous/display gauge state, PlayModifier, CharacterAbility pointer, and the Nell hide-lifebar flag around `+0x3AB`.

Ordinary fill uses texture-rectangle cropping rather than stretching one whole bitmap.

The live common HPBar updater around ~0x178D4FC has a fully resolved CharacterAbility RTTI set:

```text
CharacterAbilityGaugeChunithm
CharacterAbilityGaugeFixedValueCondition
CharacterAbilityClearBasedOnScore
CharacterAbilityClearBasedOnBestGrade
CharacterAbilityBonusesBasedOnPeakHealth
CharacterAbilityMaxHealthReducesBasedOnCurrentHealth
CharacterAbilitySafe
```

There is no anonymous cast left in that common chain.

Other external HUD paths are separate:

- `CharacterAbilityHideLifebarNell` suppresses normal lifebar text/presentation;
- `HPBar::performInsightHardSwitch()` is a distinct one-shot runtime UI morph;
- recurring hard-like 30-RR crossing warning is a third separate presentation, suppressed by Nell hiding.

## Ability-specific live HUD extensions

Confirmed live callback/presentation families include:

- `CharacterAbilityModifyFragmentOnResultOngeki` -> reusable animated indicator + related number;
- `CharacterAbilityBonusesBasedOnOngeki` -> `CharacterAbilityBonusType` selects STEP/OVER/FRAG gain textures;
- `CharacterAbilityLunaIlot` -> live Rotaeno backing/medal/bar progress presentation;
- `CharacterAbilityDjmaxFever` -> live multiplier/bar/effect presentation, 1x/2x/5x families, `animateFeverDecreasing`;
- `CharacterAbilityBonusesBasedOnPeakHealth` -> live C2-like HARD/OVER/STEP/FRAG presentation;
- `CharacterAbilityViewNoteResults` -> recurring noteInfo update;
- `CharacterAbilityClearBasedOnBestGrade` -> dynamic grade/gauge presentation through HPBar.

Thus CharacterAbility can affect gameplay HUD at construction time and through live callbacks without moving core judgement into UILayer.

## Special-scene live HUD mutation

`SpecialSceneTempestissimoChallenge` retained methods:

```text
doPreTriggerUiChanges()
doTriggerUiChanges()
```

operate on existing Node, SpinCountLabel, HPBar and Sprite objects. The special scene repositions/scales/fades/actions ordinary HUD pieces and aligns special visuals to existing gauge geometry.

`SpecialSceneAegleseekerChallenge::performSpecialSongUpdate(GameScene*)` is gameplay-clock driven. Confirmed thresholds include 81,066 ms and >94,500 ms; staged texture reveal widths at 95,460/95,690/95,880/96,090/96,300 ms are 181/304/450/489/609 at height 76, followed by another phase around 96,750 ms. A nested callback directly accepts the ordinary SpinCountLabel.

## Special-scene lifetime closure

Tempestissimo destructor around ~0x1260AB0 releases retained special nodes/resources before the shared base destructor.

Aegleseeker destructor around ~0x0E6BC8C similarly releases its retained node/resource fields before base destruction.

No dedicated normal-HUD restore/reset method family was found, and these destructors do not manually restore ordinary UILayer position/scale/opacity.

Durable model:

- individual actions can remove/fade special nodes earlier;
- retained special nodes are released by special-scene destruction;
- ordinary HUD mutations are not unwound by a separate restore pass;
- normal scene/UI ownership teardown ends their lifetime.

## Pause boundary

Two widget-touch callbacks plus one long-press callback in UILayer all converge on the same `PauseLayerDelegate` operation after eligibility checks. One touch route adds about 750 ms debounce. UILayer owns pause input presentation; actual pausing is delegated outside UILayer.

## Recurring HUD update order

The traced live flow is:

```text
ScoreState/context
  -> recall/combo
  -> optional noteInfo
  -> projected-grade pacemaker
  -> rolling numerical score target
  -> active LifeBarState -> HPBar
  -> one-shot Insight switch if newly required
  -> gameplay time -> progress bar/glow
  -> hard-like 30-RR warning
```

Ability-specific callbacks such as Ongeki/DJMAX/Rotaeno are installed separately and fire through their event/state hooks rather than all being polled by the common UILayer loop.

## Remaining bounded Section 05 unknowns

The gameplay-HUD architecture is complete. Only nonblocking details remain:

- exact original semantic names for the two early/late counter subfamilies;
- pixel-perfect Action/easing nesting for every custom ability/special-song animation;
- separate specialised SpinCountSpacedLabel presentation outside ordinary HUD;
- pause-menu internals outside gameplay HUD scope.

---

# Continuity summary

**Completed:**

1. `deepdive/01_gameplay_architecture.cpp`
2. `deepdive/02_chart_source_data.cpp`
3. `deepdive/03_runtime_gameplay_logic.cpp`
4. `deepdive/04_gameplay_rendering.cpp`
5. `deepdive/05_gameplay_ui.cpp`

**Most recent completed task:** Section 05 gameplay UI/HUD, including base score/recall/progress/pacemaker, noteInfo settings, HPBar live presentation, CharacterAbility HUD extensions, special-scene live HUD mutation, pause input boundary, special-scene cleanup model, and recurring UILayer update order.

**Natural next direction:** start a new container of sections rather than extending the current source/runtime/render/UI foundation. The first five deepdive chapters now provide a complete vertical stack from AFF source through runtime gameplay, world rendering, and gameplay HUD presentation.

**Scope rule:** keep core gameplay truth in Logic/ScoreState/LifeBarState. RenderNote/UILayer/HPBar consume and present that state unless direct native evidence proves a gameplay-side mutation.