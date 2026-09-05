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
3. Core gameplay fundamentals: notes, long-note ticks, arcs, rendering, judgement/input flow, lane geometry, `enwidenlane`, and closely related chart/gameplay mechanics.

Anything outside those goals is deferred until deliberately promoted into scope.

## Sections

- [`01_recollection_rate.cpp`](01_recollection_rate.cpp) — fundamental Recollection Rate model, ordinary gauges, judgement gain/loss, clear classification.
