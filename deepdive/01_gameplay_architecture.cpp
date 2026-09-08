// Arcaea native-engine deep dive
// Section 01: Gameplay architecture
//
// IMPORTANT:
// This is reconstructed reference pseudocode, NOT recovered original source.
// Evidence comes from RTTI/typeinfo, retained mangled lambda type names,
// parser token names, and native strings in libcocos2dcpp.so.

namespace reconstructed {

// ============================================================================
// CONFIRMED: chart/source-data hierarchy from C++ RTTI
// ============================================================================

class Note {};

class SimpleNote    : public Note {};   // likely bare ground tap source record
class ArcNote       : public Note {};
class HoldNote      : public Note {};
class FlickNote     : public Note {};
class Timing        : public Note {};
class SceneControl  : public Note {};
class CameraControl : public Note {};

// Parser token types also confirmed in the native binary:
// Tokens::TIMINGGROUP
// Tokens::SCENECONTROL
// Tokens::ARC
// Tokens::HOLD
// Tokens::FLICK
// Tokens::ARCTAP
// Tokens::CAMERA
// Tokens::TIMING
//
// Native chart-command strings include:
// "enwidenlanes"
// "enwidencamera"

// ============================================================================
// CONFIRMED: runtime gameplay-logic hierarchy from C++ RTTI
// ============================================================================

class LogicEvent : public cocos2d::Ref {};
class LogicTimingEvent : public LogicEvent {};

class LogicNote : public cocos2d::Ref {};

class LogicTapNote   : public LogicNote {};
class LogicFlickNote : public LogicNote {};

class LogicLongNoteBase : public LogicNote {};
class LogicHoldNote      : public LogicLongNoteBase {};
class LogicArcNote       : public LogicLongNoteBase {};

// Arc taps reuse the tap-note logic hierarchy.
class LogicArcTapNote : public LogicTapNote {};

class LogicSceneControl  : public LogicNote {};
class LogicCameraControl : public LogicNote {};

class LogicArcGroup : public cocos2d::Ref {};
class LogicChart    : public cocos2d::Ref {};
class GameTimeline  : public cocos2d::Ref {};

// ============================================================================
// CONFIRMED: gameplay-render hierarchy from C++ RTTI
// ============================================================================

class RenderNote : public cocos2d::Node {};

class RenderTapNote   : public RenderNote {};
class RenderFlickNote : public RenderNote {};
class RenderHoldNote  : public RenderNote {};
class RenderArcNote   : public RenderNote {};

// Arc taps also reuse the tap renderer hierarchy.
class RenderArcTapNote : public RenderTapNote {};

class ArcSegment        : public cocos2d::Node {};
class NoteBurstRenderer : public cocos2d::Node {};
class TrackBase         : public cocos2d::Node {};
class TrackLayer        : public cocos2d::Layer {};
class CameraController  : public cocos2d::Node {};
class GameSceneVisualControlHandler : public cocos2d::Node {};

// ============================================================================
// CONFIRMED: top-level gameplay objects and retained method signatures
// ============================================================================

class GameModel : public cocos2d::Ref,
                  public ManualPadDelegate {
public:
    // Retained lambda RTTI proves this method exists and installs touch handlers.
    void initializeTouchEvents();
};

class GameScene : public cocos2d::Scene,
                  public GameModelDelegate,
                  public PauseLayerDelegate {
public:
    // Retained lambda RTTI proves these method signatures exist.
    void render(cocos2d::Renderer*,
                const cocos2d::Mat4&,
                const cocos2d::Mat4*);

    void swapModel(GameModel*, TrackLayer*);
};

class GameSceneVisualControlHandler : public cocos2d::Node {
public:
    void handleLogicSceneControl(LogicSceneControl*);
};

class TrackBase : public cocos2d::Node {
public:
    void performTrackSplitAnimation();
};

class CameraController : public cocos2d::Node {
public:
    void animateMovingCameraTo(cocos2d::Vec3 target, float duration);
};

// ============================================================================
// RECONSTRUCTED: high-level gameplay pipeline
// ============================================================================

// The exact factory functions and ownership fields are not recovered yet.
// This pipeline is the strongest structure supported by the parallel class
// families and the confirmed bridge methods above.
//
//     .aff chart text
//          |
//          v
//   Note-derived source records
//   (SimpleNote / ArcNote / HoldNote / FlickNote / Timing / ...)
//          |
//          v
//   LogicChart + GameTimeline
//          |
//          v
//   LogicNote / LogicEvent runtime objects
//          |
//          v
//       GameModel
//   (time, touch input, gameplay state)
//          |
//          +------------------------------+
//          |                              |
//          v                              v
//   gameplay/delegate state      visual-control bridge
//                                 e.g. LogicSceneControl
//                                          |
//                                          v
//                             GameSceneVisualControlHandler
//                                          |
//                                          v
//                 TrackLayer / TrackBase / CameraController
//                          + RenderNote-derived nodes
//                                          |
//                                          v
//                              cocos2d::Renderer / GameScene
//
// In plain language: chart data, gameplay logic, and visible note nodes are
// separate layers. A note is not simply one object that both judges and draws
// itself.

// ============================================================================
// RECONSTRUCTED details
// ============================================================================

// SimpleNote is very likely the source representation of an ordinary floor tap:
// there is no separate chart-level TapNote RTTI type, while runtime/render types
// are explicitly named LogicTapNote and RenderTapNote.
//
// LogicArcTapNote : LogicTapNote and RenderArcTapNote : RenderTapNote strongly
// indicate that arctaps reuse the generic tap behaviour/visual base while being
// associated with arc-specific state elsewhere.

// ============================================================================
// UNRESOLVED
// ============================================================================

// 1. Exact ownership pointers between LogicChart, GameTimeline, GameModel,
//    TrackLayer, and the individual logic/render objects.
// 2. Exact factory functions converting source Note records into Logic* objects
//    and Logic* objects into Render* nodes.
// 3. Exact per-frame update order.
// 4. Exact object field layouts and offsets.
// 5. Lifetime/removal rules for logic and render notes.
// 6. Detailed mapping of scenecontrol/camera commands to visual effects.
// 7. Exact track/camera transformation mathematics.

} // namespace reconstructed
