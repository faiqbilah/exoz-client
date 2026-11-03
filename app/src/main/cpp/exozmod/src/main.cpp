// Exoz Client core mod (native mod for the Preloader SDK).
//  * Exoz HUD: FPS (colour-coded), frame time, 1% low and a frame-time graph, drawn into the game frame.
//  * Frame Rate Hint: asks Android for a fixed display refresh rate for the game window.
// Both only need EGL, so they work on every Minecraft version (no game signatures).
#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/memory/Hook.hpp>

#include "hud/FrameTimeline.h"
#include "hud/GlOverlay.h"
#include "hud/HudLayout.h"

#include <EGL/egl.h>
#include <android/native_window.h>
#include <dlfcn.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* kHudId = "exoz.hud";
constexpr const char* kRateId = "exoz.framerate";

// Menu values (written by menu callbacks on any thread, read on the render thread).
std::atomic<bool> gHud{true}, gGraph{true}, gLow{true};
std::atomic<int> gScale{2}, gCorner{1}, gOpacity{70};
std::atomic<bool> gRateOn{false};
std::atomic<int> gRateIndex{0};                       // 0 auto, 1..5 -> 30/45/60/90/120
std::atomic<bool> gRateDirty{false};

// Render-thread state.
FrameTimeline gTimeline;
GlOverlay gOverlay;
std::vector<Vertex> gVerts;
std::chrono::steady_clock::time_point gLastSwap = std::chrono::steady_clock::now();

// Game window (captured when the game creates its EGL surface).
std::mutex gWindowMutex;
ANativeWindow* gWindow = nullptr;

using SwapFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
using CreateSurfaceFn = EGLSurface (*)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*);
SwapFn gOrigSwap = nullptr;
CreateSurfaceFn gOrigCreate = nullptr;

bool isTrue(std::string_view v) { return v == "true" || v == "1"; }

void onToggleHud(std::string_view, bool on) { gHud.store(on); }
void onToggleRate(std::string_view, bool on) { gRateOn.store(on); gRateDirty.store(true); }

void onConfigChanged(std::string_view moduleId, std::string_view key, std::string_view value) {
    const std::string v(value);
    if (moduleId == kHudId) {
        if (key == "graph") gGraph.store(isTrue(value));
        else if (key == "low") gLow.store(isTrue(value));
        else if (key == "scale") gScale.store(std::clamp(std::atoi(v.c_str()), 1, 6));
        else if (key == "corner") gCorner.store(std::clamp(std::atoi(v.c_str()), 0, 3));
        else if (key == "opacity") gOpacity.store(std::clamp(std::atoi(v.c_str()), 0, 100));
    } else if (moduleId == kRateId) {
        if (key == "rate") { gRateIndex.store(std::clamp(std::atoi(v.c_str()), 0, 5)); gRateDirty.store(true); }
    }
}

// ANativeWindow_setFrameRate exists from Android 11; resolve it at runtime (the app supports older).
void applyFrameRate() {
    using Fn = int32_t (*)(ANativeWindow*, float, int8_t);
    static Fn fn = reinterpret_cast<Fn>(dlsym(RTLD_DEFAULT, "ANativeWindow_setFrameRate"));
    if (!fn) return;
    static const float kRates[] = {0.f, 30.f, 45.f, 60.f, 90.f, 120.f};
    const float rate = gRateOn.load() ? kRates[std::clamp(gRateIndex.load(), 0, 5)] : 0.f;   // 0 = no preference
    std::lock_guard<std::mutex> lock(gWindowMutex);
    if (gWindow) fn(gWindow, rate, 0 /* ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT */);
}

EGLSurface hookedCreateSurface(EGLDisplay dpy, EGLConfig cfg, EGLNativeWindowType win, const EGLint* attribs) {
    EGLSurface s = gOrigCreate(dpy, cfg, win, attribs);
    if (s != EGL_NO_SURFACE && win) {
        std::lock_guard<std::mutex> lock(gWindowMutex);
        auto* w = reinterpret_cast<ANativeWindow*>(win);
        ANativeWindow_acquire(w);
        if (gWindow) ANativeWindow_release(gWindow);
        gWindow = w;
        gRateDirty.store(true);
    }
    return s;
}

EGLBoolean hookedSwap(EGLDisplay dpy, EGLSurface surf) {
    using namespace std::chrono;
    const auto now = steady_clock::now();
    const float ms = duration<float, std::milli>(now - gLastSwap).count();
    gLastSwap = now;
    if (ms > 0.f && ms < 1000.f) gTimeline.push(ms);    // ignore pauses (background, loading)

    if (gRateDirty.exchange(false)) applyFrameRate();

    if (gHud.load(std::memory_order_relaxed) && !gOverlay.broken()) {
        EGLint w = 0, h = 0;
        eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
        eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
        if (w > 0 && h > 0) {
            HudStyle style;
            style.scale = gScale.load(std::memory_order_relaxed);
            style.corner = gCorner.load(std::memory_order_relaxed);
            style.opacity = gOpacity.load(std::memory_order_relaxed);
            style.showGraph = gGraph.load(std::memory_order_relaxed);
            style.showLow = gLow.load(std::memory_order_relaxed);
            buildHud(gTimeline, style, w, h, gVerts);
            gOverlay.draw(gVerts, w, h);
        }
    }
    return gOrigSwap(dpy, surf);
}

}  // namespace

class ExozMod {
public:
    static ExozMod& instance() {
        static ExozMod mod;
        return mod;
    }

    ExozMod() : mSelf(*ll::mod::NativeMod::current()) {}

    bool load() { return true; }

    bool enable() {
        auto& log = mSelf.getLogger();

        void* egl = dlopen("libEGL.so", RTLD_NOW);
        void* swap = egl ? dlsym(egl, "eglSwapBuffers") : nullptr;
        void* create = egl ? dlsym(egl, "eglCreateWindowSurface") : nullptr;
        if (!swap) {
            log.error("eglSwapBuffers not found: Exoz HUD cannot start");
            return false;
        }
        mSwapHook = pl::memory::HookHandle(swap, reinterpret_cast<void*>(&hookedSwap),
                                           reinterpret_cast<void**>(&gOrigSwap), pl::memory::HookPriority::Normal);
        if (!mSwapHook.installed()) {
            log.error("eglSwapBuffers hook failed");
            return false;
        }
        if (create) {
            mCreateHook = pl::memory::HookHandle(create, reinterpret_cast<void*>(&hookedCreateSurface),
                                                 reinterpret_cast<void**>(&gOrigCreate), pl::memory::HookPriority::Normal);
        }

        using pl::modmenu::ConfigType;
        const bool a = pl::modmenu::ModuleBuilder(kHudId, "Exoz HUD")
                           .modId(mSelf.getId())
                           .description("FPS, frame time and 1% low, colour-coded, with a frame-time graph.")
                           .defaultEnabled(true)
                           .onToggle(onToggleHud)
                           .config("graph", "Frame-time graph", ConfigType::Toggle, "true")
                           .config("low", "Show 1% low", ConfigType::Toggle, "true")
                           .config("scale", "Size", ConfigType::SliderInt, "2", "1", "6")
                           .config("corner", "Position", ConfigType::Radio, "1",
                                   "Top Left,Top Right,Bottom Left,Bottom Right")
                           .config("opacity", "Background opacity", ConfigType::SliderInt, "70", "0", "100")
                           .onConfigChanged(onConfigChanged)
                           .registerModule();
        const bool b = pl::modmenu::ModuleBuilder(kRateId, "Frame Rate Hint")
                           .modId(mSelf.getId())
                           .description("Asks Android for a fixed refresh rate for the game. A steady 60 can run "
                                        "cooler than an unstable 120. Needs Android 11+ and a supporting screen.")
                           .defaultEnabled(false)
                           .onToggle(onToggleRate)
                           .config("rate", "Frame rate", ConfigType::Radio, "0", "Auto,30,45,60,90,120")
                           .onConfigChanged(onConfigChanged)
                           .registerModule();
        if (!a || !b) log.warn("mod menu registration failed");
        log.info("Exoz core active");
        return true;
    }

    bool disable() {
        pl::modmenu::unregisterModule(kHudId);
        pl::modmenu::unregisterModule(kRateId);
        mCreateHook.reset();
        mSwapHook.reset();
        return true;
    }

    bool unload() { return true; }

private:
    ll::mod::NativeMod& mSelf;
    pl::memory::HookHandle mSwapHook;
    pl::memory::HookHandle mCreateHook;
};

PL_REGISTER_MOD(ExozMod, ExozMod::instance())
