#pragma once
#include "perf/module.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// CaveCulling: don't draw terrain the player can't reach through open air (hidden caves,
// everything behind solid rock). This is the "advanced cave culling" idea: every 16x16x16
// sub-chunk gets a connectivity mask (which of its 6 faces connect to which, through
// non-opaque blocks); each frame-ish a flood fill walks outward from the camera's sub-chunk
// through connected faces, always moving away from the camera. Whatever it reaches is drawn.
//
// Faces: 0:+X 1:-X 2:+Y 3:-Y 4:+Z 5:-Z   (the opposite of face f is f ^ 1)
//
// Fail-safe by design: a sub-chunk we have no data for is treated as fully open, so
// unanalysed terrain is never hidden. When a block changes in a sub-chunk, call removeSection()
// first (data becomes "unknown" = visible) and setSection() again once it is rebuilt.
//
// Threading: setSection/removeSection/clear may be called from any thread (chunk workers).
// setCamera/isVisible/onFrame belong to the render thread.
// ---------------------------------------------------------------------------
class CaveCulling : public Module {
public:
    // Byte e of a Conn = bitmask of faces reachable from entry face e (bit f = face f).
    using Conn = uint64_t;
    static constexpr Conn kOpen = 0x00003F3F3F3F3F3FULL;   // everything connected
    static constexpr Conn kSolid = 0;                      // nothing connected

    CaveCulling();

    // opaque[i] != 0 means a full opaque block; index = x + 16*z + 256*y.
    static Conn computeConnectivity(const uint8_t opaque[4096]);

    void setSection(int sx, int sy, int sz, Conn c);
    void removeSection(int sx, int sy, int sz);
    void clear();
    void setWorldHeight(int minSectionY, int maxSectionY);   // overworld default: -4..19

    void setCamera(float x, float y, float z);               // world coords in blocks, once per frame
    bool isVisible(int sx, int sy, int sz);                  // true = draw it
    int hiddenLastFrame() const { return hiddenShown_.load(std::memory_order_relaxed); }
    void onFrame(float frameMs) override;
    void onDisable() override { dirty_.store(true); }

private:
    void rebuild();
    static uint64_t key(int x, int y, int z) {
        return ((uint64_t)(x & 0x1FFFFF) << 42) | ((uint64_t)(z & 0x1FFFFF) << 21) | (uint64_t)(y & 0x1FFFFF);
    }

    Setting *radius_, *near_;
    std::mutex mutex_;
    std::unordered_map<uint64_t, Conn> sections_;
    std::atomic<bool> dirty_{true};
    std::atomic<int> minSy_{-4}, maxSy_{19};

    // Render-thread state: the grid computed by the last rebuild().
    std::vector<uint8_t> visible_, incoming_, pending_;
    int camSx_ = 0, camSy_ = 0, camSz_ = 0;
    int winR_ = 0, winW_ = 0, winH_ = 0, gridMinSy_ = 0;
    int sinceRebuild_ = 1000;
    int hidden_ = 0;
    std::atomic<int> hiddenShown_{0};
};
