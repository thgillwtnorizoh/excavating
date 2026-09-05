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
3. Core gameplay fundamentals: notes, long-note ticks, arcs, rendering, judgement/input flow, lane geometry, `enwidenlanes`, and closely related chart/gameplay mechanics.

Anything outside those goals is deferred until deliberately promoted into scope.

## Sections

- [`01_recollection_rate.cpp`](01_recollection_rate.cpp) — fundamental Recollection Rate model, ordinary gauges, judgement gain/loss, and clear classification.
- [`02_note_fundamentals.cpp`](02_note_fundamentals.cpp) — runtime note hierarchy, point-note timing windows and resolution, ScoreState judgement flow, and common `LogicLongNoteBase` tick/event machinery.
- [`03_long_notes.cpp`](03_long_notes.cpp) — `LogicHoldNote` touch/contact state, long-event success/LOST processing, and release/re-press behaviour.
- [`04_arc_contact.cpp`](04_arc_contact.cpp) — `LogicArcNote` body contact validity, touch ownership/continuity, trace/non-judged body behaviour, and the bridge into common long-note events.
- [`05_arc_path.cpp`](05_arc_path.cpp) — native Arc easing enum and formulas, cubic `b` construction, sampled path representation, and gameplay path consumption.
- [`06_arctaps.cpp`](06_arctaps.cpp) — ArcTap placement on the parent Arc, spatial candidate filtering, inherited point-note judgement, and parent Arc judgement modes.
- [`07_flick_notes.cpp`](07_flick_notes.cpp) — live Flick processing, spatial gate, directional displacement gesture, timing gates, and success/LOST flow.
- [`08_lane_geometry.cpp`](08_lane_geometry.cpp) — `NotePosition` modes, chart-lane/internal-ID conversion, six reserved lane positions, lane-centre spacing, touch-to-lane mapping, and shared Tap/Hold lane filtering.
- [`09_enwidenlanes.cpp`](09_enwidenlanes.cpp) — `scenecontrol` widening types, timed six-lane activation, fixed outer lane slots, lane-widen progress, and the related 1.0→1.5 `enwidencamera` transition.

## Next excavation area

Gameplay timing and timinggroups, kept deliberately narrow: identify the timing state that produces effective gameplay time, separate judgement time from visual/scroll time, resolve the recurring conditional ±3000 ms correction, and identify only timinggroup flags that materially alter gameplay. Avoid mapping the entire chart parser or unrelated visual flags.
