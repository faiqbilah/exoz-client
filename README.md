# Exoz Client

A Minecraft Bedrock launcher for Android with built-in performance tools. Exoz Client is a fork of
[LeviLaunchroid](https://github.com/LiteLDev/LeviLaunchroid) (Apache-2.0): it keeps LeviLaunchroid's loader, which runs
Minecraft inside the app and loads native mods first, and adds Exoz branding and the **Exoz core mod**.
You need the Google Play version of Minecraft installed (LeviLaunchroid's minimum is 1.21.80).

## What Exoz adds
- **Exoz HUD**: FPS (green / yellow / red), frame time, 1% low and a 60-frame frame-time graph, drawn into the game
  frame. Size, corner and opacity are sliders in the in-game Mod Menu.
- **Frame Rate Hint**: asks Android for a fixed refresh rate (30 / 45 / 60 / 90 / 120) for the game window. Off by default.
- The core mod installs itself into the current game version's mods folder on every launch (`ExozBundledMod.java`)
  and appears in the in-game Mod Menu. If you switch it off, it stays off.
- Upstream update checks and the upstream news feed are disabled; crash/analytics collection is switched off.

## Get an APK (no PC needed)
1. Put this folder's contents in your own GitHub repo (fork of LeviLaunchroid, or a new repo via web upload).
2. The workflow **Exoz Client APK** runs on every push (Actions tab, or "Run workflow"). If submodules are missing it
   fetches them itself.
3. Download `ExozClient-debug` from the run page, unzip, install the APK.

## Not in this build, and why
These need either game-function signatures or a Java-side resize path that I could not verify without a device and
your `libminecraftpe.so`, so they are **not** in the menu (no dead switches). The tested logic is already in
`app/src/main/cpp/exozmod/src/perf`. See `docs/EXOZ.md`.
- Cave culling, entity / block-entity culling, chunk-rebuild throttle, particle limiter
- **GUI cache** (stop redrawing the GUI every frame): needs the GUI render function
- Render resolution

## Known limits
- If the game runs on Vulkan instead of OpenGL ES, the HUD has nothing to hook (same as the built-in FPS display).
- Not built or run by me: the Gradle/NDK build and everything on a device. Native logic is covered by host tests.

## License
Apache-2.0, see `LICENSE` and `NOTICE`. Exoz Client is not affiliated with Mojang, Microsoft or LeviMC.
