# Exoz Client core mod: HUD + frame-rate hint. Built into the app and auto-installed as a native mod
# (see core/mods/ExozBundledMod.java). The src/perf sources are finished, tested logic that is not
# registered yet because it needs game-function signatures (docs/EXOZ.md).
add_library(exozmod SHARED
    src/main.cpp
    src/hud/FrameTimeline.cpp
    src/hud/HudLayout.cpp
    src/hud/GlOverlay.cpp
    src/perf/module.cpp
    src/perf/modules.cpp
    src/perf/cave_culling.cpp
    src/perf/sigscan.cpp)

target_include_directories(exozmod PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../preloader/include)
target_compile_features(exozmod PRIVATE cxx_std_20)
target_link_libraries(exozmod PRIVATE preloader log GLESv3 EGL android dl)
