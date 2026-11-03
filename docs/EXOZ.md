# Exoz: what is left and how to finish it

## Modules waiting for game hooks
Logic is written and unit-tested in `app/src/main/cpp/exozmod/src/perf` (CaveCulling, EntityCulling, RebuildThrottle,
ParticleLimiter, DynamicResolution). Each needs a game function found by byte pattern:

| Function | Feeds |
|---|---|
| actor / block-entity render | EntityCulling::shouldCull |
| particle spawn or render | ParticleLimiter::allow |
| chunk mesh build submit | RebuildThrottle::allow |
| sub-chunk data ready / changed | CaveCulling::setSection / removeSection |
| sub-chunk draw submit | CaveCulling::isVisible (+ setCamera once per frame) |
| GUI screen render | GUI cache (render once, reuse until input / state changes; items stay live) |

How: extract `lib/arm64-v8a/libminecraftpe.so` from the Minecraft you play, find the functions in Ghidra/IDA, turn
stable instruction runs into patterns (`??` for moving bytes). The launcher already resolves named signatures per
game version from `resources/preloader/preloader_signature_rules_source.json` (see `docs/api/signature.md`), so
new patterns can be added there without changing app code. Register the matching modules in
`exozmod/src/main.cpp` only after their hooks resolve.

## Render resolution
Changing only the native window size makes the game render into a corner, because the game is never told the surface
changed. It needs the Java-side `SurfaceHolder.setFixedSize` (so the game gets its resize callback) plus touch
coordinate scaling. Plan: implement it in the launcher's Java layer and test on a device.
