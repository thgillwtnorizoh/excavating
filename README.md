# excavating

Readable pseudocode notes reconstructed from the Arcaea reverse-engineering excavation.

This repository is **not recovered source code**. It is a field notebook: after a mechanic is sufficiently understood, the behaviour is rewritten as compact C++-style pseudocode so it remains useful after the chat that produced it is gone.

## Evidence labels

- `CONFIRMED` — directly supported by native control flow, constants, RTTI/type names, strings, or data files.
- `RECONSTRUCTED` — readable pseudocode combining several confirmed operations; names and structure may differ from the original source.
- `UNRESOLVED` — behaviour exists in the binary, but its semantic name or exact purpose has not yet been proved.

## Current scope

1. Recollection Rate / gauge fundamentals.
2. Special in-play effects triggered by special conditions. Unlock requirements and challenge progression are out of scope unless directly needed.
3. Core gameplay fundamentals: notes, long-note ticks, arcs, rendering, judgement/input flow, lane geometry, `enwidenlanes`, timinggroups, gameplay-space/camera behaviour, and closely related chart mechanics.

Anything outside those goals is deferred until deliberately promoted into scope.

## Sections

- [`01_recollection_rate.cpp`](01_recollection_rate.cpp) — fundamental Recollection Rate model, ordinary gauges, judgement gain/loss, and clear classification.
- [`02_note_fundamentals.cpp`](02_note_fundamentals.cpp) — runtime note hierarchy, point-note timing windows and resolution, ScoreState judgement flow, and common `LogicLongNoteBase` tick/event machinery.
- [`03_long_notes.cpp`](03_long_notes.cpp) — `LogicHoldNote` touch/contact state, long-event success/LOST processing, and release/re-press behaviour.
- [`04_arc_contact.cpp`](04_arc_contact.cpp) — `LogicArcNote` body contact validity, touch ownership/continuity, trace/non-judged body behaviour, and the bridge into common long-note events.
- [`05_arc_path.cpp`](05_arc_path.cpp) — native Arc easing enum and formulas, cubic `b` construction, sampled path representation, and gameplay path consumption.
- [`06_arctaps.cpp`](06_arctaps.cpp) — ArcTap placement on the parent Arc, spatial candidate filtering, inherited point-note judgement, and parent Arc judgement modes.
- [`07_flick_notes.cpp`](07_flick_notes.cpp) — surviving Flick gameplay handlers: spatial gate, directional displacement gesture, timing gates, and success/LOST flow; Section 18 refines runtime-instantiation status for this build.
- [`08_lane_geometry.cpp`](08_lane_geometry.cpp) — `NotePosition` modes, chart-lane/internal-ID conversion, six reserved lane positions, lane-centre spacing, touch-to-lane mapping, and shared Tap/Hold lane filtering.
- [`09_enwidenlanes.cpp`](09_enwidenlanes.cpp) — `scenecontrol` widening types, timed six-lane activation, fixed outer lane slots, lane-widen progress, and the related 1.0→1.5 `enwidencamera` transition.
- [`10_timinggroups.cpp`](10_timinggroups.cpp) — `LogicTimingEvent`, per-note active timing context, timinggroup gameplay flags, the shared judgement clock, and the conditional fallback `-3000 ms` pre-roll path.
- [`11_gameplay_space.cpp`](11_gameplay_space.cpp) — screen-to-world touch unprojection, the Y=0 floor plane, camera-aware floor/sky input geometry, concrete Arc/ArcTap hit extents, and spatial effects of `enwidencamera`.
- [`12_arc_contact_refinements.cpp`](12_arc_contact_refinements.cpp) — Arc touch-ID ownership, release re-acquisition lockout, nearby-Arc ownership relaxation, special tracker bypass, `LogicArcGroup`, connected-segment grouping, and direction-changing seam behaviour.
- [`13_arc_path_refinements.cpp`](13_arc_path_refinements.cpp) — separate gameplay/render Arc tessellations, render sampling multiplier, directional connected-Arc graph, tiny seam-gap normalisation, ownership carryover, and connected-seam tick merging.
- [`14_arc_mode_designant.cpp`](14_arc_mode_designant.cpp) — native `false`/`true`/`designant` Arc arctype mapping, runtime `+0xA4` body modes, ArcTap-driven mode-1 promotion, and Designant-specific nonjudged/red presentation behaviour.
- [`15_rendering_fundamentals.cpp`](15_rendering_fundamentals.cpp) — bidirectional LogicNote/RenderNote pairing, Tap/Hold/Flick/ArcTap presentation models, Arc render-path tessellation into ribbon segments, and exact timinggroup `anglex`/`angley` render transforms.
- [`16_scenecontrols.cpp`](16_scenecontrols.cpp) — `LogicSceneControl` runtime identity/group state, visual-handler dispatch architecture, `trackdisplay`, `redline`, `arcahvdistort`, `arcahvdebris`, `hidegroup`, and the boundary between presentation-only controls and gameplay-affecting widening controls.
- [`17_song_specific_specials.cpp`](17_song_specific_specials.cpp) — the `rivenpilgrim` 500 ms lane-collapse exception and native `SpecialSceneDesignantChallenge`, including gameplay-clock video/timeline synchronisation, DESIGNANT-parent ArcTap triggering, paired body/ArcTap presentation factors, and the gameplay-versus-presentation boundary.
- [`18_flick_runtime_disconnection.cpp`](18_flick_runtime_disconnection.cpp) — build-specific proof that Flick chart/parser and downstream gameplay/render handlers survive while `LogicFlickNote` runtime construction is disconnected: RTTI exists, its class vtable does not, all RTTI code references are consumers, and no Flick-specific release/reset path survives.
- [`19_logiccolor_arc_tracking.cpp`](19_logiccolor_arc_tracking.cpp) — identifies native `LogicColor` as the shared Arc colour/touch-ownership channel; resolves channel-3 ownership bypass and red rejection feedback; and reconstructs the `yourbestnightmare` ratingClass-3 green-Arc subsystem, where colour-2 Arcs receive `+0x170`, suppress ordinary success/LOST accounting and finger ownership, and use a dedicated base +5 RR SpecialScene path.
- [`20_special_gameplay_space.cpp`](20_special_gameplay_space.cpp) — resolves the old special camera/input predicate as chart-level `LogicChart +0x110`, separates it from native `CameraController`, and reconstructs its gameplay-space compatibility branches: camera-dependent floor-depth validity, projected-touch adaptation, ArcTap vertical interpretation, and related spatial consumers.

## Next excavation area

Remaining focused gameplay semantic leftovers after resolving the special gameplay-space compatibility layer. Next close the exact `x == +425` lane-classifier seam and the remaining `LogicTimingEvent` / global-clock identities, including the conversion-context multiplier around `+0xF4`, only where they materially improve the gameplay model. After that, keep rendering refinements separate: timinggroup trace colour, `fadingholds`, Designant secondary rendering, Arc caps/particles/auxiliary nodes, and widening presentation. The YBN one-event green-Arc construction reason remains a small gameplay loose end. Unlocks, progression, challenge requirements and account/content-access systems remain out of scope unless deliberately promoted.
