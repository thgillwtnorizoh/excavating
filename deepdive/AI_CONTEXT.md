# Arcaea Deep Dive AI Context

## Most recent completed task: Section 01 — gameplay architecture

Section 01 is done: we now have the **gameplay skeleton**.

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

Retained mangled lambda RTTI also gives us real method names/signatures:

- `GameModel::initializeTouchEvents()`
- `GameScene::render(Renderer*, const Mat4&, const Mat4*)`
- `GameScene::swapModel(GameModel*, TrackLayer*)`
- `GameSceneVisualControlHandler::handleLogicSceneControl(LogicSceneControl*)`
- `TrackBase::performTrackSplitAnimation()`
- `CameraController::animateMovingCameraTo(Vec3, float)`

`GameScene` inherits `cocos2d::Scene`, `GameModelDelegate`, and `PauseLayerDelegate`. `GameModel` inherits `cocos2d::Ref` and `ManualPadDelegate`. The render-note classes are Cocos nodes, while the logic-note classes are plain reference-counted gameplay objects. That is strong evidence that judgement/simulation and drawing are deliberately separated.

The AFF/parser side is also still visible through token RTTI: `TIMINGGROUP`, `SCENECONTROL`, `ARC`, `HOLD`, `FLICK`, `ARCTAP`, `CAMERA`, and `TIMING`. Native strings also contain `enwidenlanes` and `enwidencamera`.

### RECONSTRUCTED

The strongest current model is:

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

The arrows are architectural reconstruction, not yet a recovered exact call chain. We have not identified the precise factory functions that perform each conversion.

A useful detail is that **arctaps inherit tap behaviour on both sides**: `LogicArcTapNote` derives from `LogicTapNote`, and `RenderArcTapNote` derives from `RenderTapNote`. So an arctap is not implemented as a completely unrelated note type. It reuses the tap foundation and adds arc-specific context elsewhere.

`SimpleNote` is very likely the chart-level representation of an ordinary floor tap. There is no chart-level `TapNote` RTTI class, but there are explicit `LogicTapNote` and `RenderTapNote` runtime classes. I am keeping that as reconstructed rather than confirmed until we trace the parser/factory path.

### UNRESOLVED

We still need to recover:

- exact ownership between `LogicChart`, `GameTimeline`, `GameModel`, `TrackLayer`, logic notes, and render notes;
- the factory/conversion functions that create `Logic*` and `Render*` objects;
- the exact per-frame gameplay update order;
- object field layouts;
- note lifetime/removal rules;
- detailed scene-control and camera command handling;
- track/camera transformation mathematics.

The next sensible excavation is the **main gameplay frame cycle**: trace `GameScene` and `GameModel` initialization/update paths until we can say, in order, what happens during one frame from song time/input through judgement to visual update.

The durable pseudocode for this section is saved as `deepdive/01_gameplay_architecture.cpp`.

---

## Continuity summary

**Completed:** Section 01, high-level gameplay architecture and confirmed RTTI inheritance families for chart data, runtime logic, and rendering.

**Incomplete:** Exact constructors/factories, ownership, per-frame order, field layouts, lifetime rules, and detailed visual transformation behaviour.

**Recent task:** Establish the gameplay/rendering spine before deeper note or rendering investigations.

**Scope restriction:** Investigation is limited to main gameplay and gameplay rendering only. Do not expand into unlocks, story, menus, purchases, network/account systems, progression, or other unrelated app systems unless the user explicitly expands scope.
