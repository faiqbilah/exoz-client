#pragma once
#include "perf/module.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>

// ---------------------------------------------------------------------------
// EntityCulling: distance + frustum + (optional) occlusion culling.
// The render hooks call shouldCull() right before an actor / block entity / particle is drawn.
// Everything runs on the render thread, so there is no locking.
//
// Coordinate rule: aabb, cam and the view-projection matrix must all be in the SAME space
// (world space, or camera-relative if that's what the game's matrix uses).
// Don't call it for the local player.
// ---------------------------------------------------------------------------
enum class ActorClass { Player, Mob, Item, BlockEntity, Particle };

class EntityCulling : public Module {
public:
    // Returns true if the segment from..to is blocked by a solid block. Provided by a world-raycast hook.
    using OcclusionProbe = bool (*)(const float from[3], const float to[3]);

    EntityCulling();
    void setFrustum(const float viewProj[16]);          // column-major, call once per frame
    void setOcclusionProbe(OcclusionProbe p) { probe_ = p; }
    bool shouldCull(ActorClass cls, uint64_t id, const float aabb[6], const float cam[3]);
    int culledLastFrame() const { return culledShown_.load(std::memory_order_relaxed); }
    void onFrame(float frameMs) override;

private:
    struct Occl { uint32_t lastCheck = 0; uint8_t blockedStreak = 0; bool hidden = false; };
    bool inFrustum(const float aabb[6], float pad) const;

    Setting *playerDist_, *mobDist_, *itemDist_, *blockEntityDist_, *particleDist_;
    Setting *pad_, *occlusion_, *occlInterval_;
    float planes_[6][4] = {};
    bool haveFrustum_ = false;
    OcclusionProbe probe_ = nullptr;
    std::unordered_map<uint64_t, Occl> occl_;
    uint32_t frame_ = 0;
    int culled_ = 0;
    std::atomic<int> culledShown_{0};
};

// ---------------------------------------------------------------------------
// DynamicResolution: picks the 3D-world render scale (see scaled_target.h for the GL side).
// Fixed scale, or adaptive: steps down fast when over the frame budget, probes upward slowly,
// and backs off for 30 s if a probe didn't hold (avoids oscillation under vsync).
// ---------------------------------------------------------------------------
class DynamicResolution : public Module {
public:
    DynamicResolution();
    float scale() const { return enabled() ? scale_.load(std::memory_order_relaxed) : 1.f; }
    void onEnable() override { scale_.store(1.f); }
    void onFrame(float frameMs) override;

private:
    Setting *dynamic_, *targetFps_, *minScale_, *maxScale_, *staticScale_;
    std::atomic<float> scale_{1.f};
    uint32_t frame_ = 0, lastChange_ = 0, lastProbe_ = 0, probeBlockedUntil_ = 0;
};

// ---------------------------------------------------------------------------
// RebuildThrottle: caps chunk-mesh rebuilds per frame. Nearby chunks bypass the cap so placing /
// breaking blocks still updates instantly. The hook must treat `false` as "leave it dirty, retry
// next frame". Safe to call from chunk-builder worker threads.
// ---------------------------------------------------------------------------
class RebuildThrottle : public Module {
public:
    RebuildThrottle();
    bool allow(float distSq);
    void onFrame(float frameMs) override;

private:
    Setting *maxPerFrame_, *nearBypass_;
    std::atomic<int> budget_{4}, used_{0};
};

// ---------------------------------------------------------------------------
// ParticleLimiter: per-frame particle spawn/draw cap, optionally dropping off-screen particles.
// ---------------------------------------------------------------------------
class ParticleLimiter : public Module {
public:
    ParticleLimiter();
    bool allow(bool onScreen);
    void onFrame(float) override { used_.store(0, std::memory_order_relaxed); }

private:
    Setting *maxPerFrame_, *cullOffscreen_;
    std::atomic<int> used_{0};
};
