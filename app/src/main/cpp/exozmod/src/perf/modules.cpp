#include "perf/modules.h"
#include "perf/frame_stats.h"

#include <algorithm>
#include <cmath>

// ============================ EntityCulling ================================

EntityCulling::EntityCulling() : Module("EntityCulling") {
    playerDist_      = declare("playerDist",       96.f,   8.f, 256.f);
    mobDist_         = declare("mobDist",          64.f,   8.f, 256.f);
    itemDist_        = declare("itemDist",         32.f,   8.f, 128.f);
    blockEntityDist_ = declare("blockEntityDist",  48.f,   8.f, 128.f);
    particleDist_    = declare("particleDist",     32.f,   8.f, 128.f);
    pad_             = declare("frustumPad",        2.f,   0.f,   8.f);
    occlusion_       = declare("occlusion",         0.f,   0.f,   1.f);
    occlInterval_    = declare("occlusionInterval", 6.f,   1.f,  30.f);
}

void EntityCulling::onFrame(float) {
    ++frame_;
    culledShown_.store(culled_, std::memory_order_relaxed);
    culled_ = 0;
    if (frame_ % 600 == 0) {   // drop occlusion entries for entities we haven't seen in ~5 s
        for (auto it = occl_.begin(); it != occl_.end();)
            it = (frame_ - it->second.lastCheck > 300) ? occl_.erase(it) : std::next(it);
    }
}

// Gribb-Hartmann plane extraction from a column-major view-projection matrix.
void EntityCulling::setFrustum(const float m[16]) {
    auto row = [&](int i, int j) { return m[j * 4 + i]; };
    for (int p = 0; p < 6; ++p) {
        int axis = p / 2;                              // 0=x 1=y 2=z
        float sign = (p % 2 == 0) ? 1.f : -1.f;        // left/bottom/near: +, right/top/far: -
        float* pl = planes_[p];
        for (int j = 0; j < 4; ++j) pl[j] = row(3, j) + sign * row(axis, j);
        float len = std::sqrt(pl[0] * pl[0] + pl[1] * pl[1] + pl[2] * pl[2]);
        if (len > 1e-6f) for (int j = 0; j < 4; ++j) pl[j] /= len;
    }
    haveFrustum_ = true;
}

// aabb = {minX, minY, minZ, maxX, maxY, maxZ}; `pad` grows it to avoid pop-in at screen edges.
bool EntityCulling::inFrustum(const float b[6], float pad) const {
    for (int p = 0; p < 6; ++p) {
        const float* pl = planes_[p];
        float x = pl[0] >= 0.f ? b[3] + pad : b[0] - pad;   // corner furthest along the plane normal
        float y = pl[1] >= 0.f ? b[4] + pad : b[1] - pad;
        float z = pl[2] >= 0.f ? b[5] + pad : b[2] - pad;
        if (pl[0] * x + pl[1] * y + pl[2] * z + pl[3] < 0.f) return false;
    }
    return true;
}

bool EntityCulling::shouldCull(ActorClass cls, uint64_t id, const float b[6], const float cam[3]) {
    if (!enabled()) return false;

    const float cx = (b[0] + b[3]) * 0.5f, cy = (b[1] + b[4]) * 0.5f, cz = (b[2] + b[5]) * 0.5f;
    const float dx = cx - cam[0], dy = cy - cam[1], dz = cz - cam[2];
    const float d2 = dx * dx + dy * dy + dz * dz;

    float lim = 64.f;
    switch (cls) {
        case ActorClass::Player:      lim = playerDist_->get();      break;
        case ActorClass::Mob:         lim = mobDist_->get();         break;
        case ActorClass::Item:        lim = itemDist_->get();        break;
        case ActorClass::BlockEntity: lim = blockEntityDist_->get(); break;
        case ActorClass::Particle:    lim = particleDist_->get();    break;
    }
    if (d2 > lim * lim) { ++culled_; return true; }

    if (haveFrustum_ && !inFrustum(b, pad_->get())) { ++culled_; return true; }

    // Occlusion: only for things worth a raycast, only beyond 6 blocks, throttled per entity.
    // Needs 2 consecutive "blocked" results before hiding (hysteresis) but shows again at once.
    if (probe_ && occlusion_->get() >= 0.5f && d2 > 36.f &&
        cls != ActorClass::Item && cls != ActorClass::Particle) {
        Occl& o = occl_[id];
        if (o.lastCheck == 0 || frame_ - o.lastCheck >= (uint32_t)occlInterval_->get()) {
            o.lastCheck = frame_ ? frame_ : 1;
            const float center[3] = {cx, cy, cz};
            const float head[3] = {cx, b[4], cz};
            const bool blocked = probe_(cam, center) && probe_(cam, head);   // either visible => visible
            if (blocked) { if (o.blockedStreak < 255) ++o.blockedStreak; }
            else o.blockedStreak = 0;
            o.hidden = o.blockedStreak >= 2;
        }
        if (o.hidden) { ++culled_; return true; }
    }
    return false;
}

// =========================== DynamicResolution =============================

DynamicResolution::DynamicResolution() : Module("DynamicResolution") {
    dynamic_     = declare("dynamic",     1.f,  0.f,   1.f);
    targetFps_   = declare("targetFps",  60.f, 20.f, 120.f);
    minScale_    = declare("minScale",    0.6f, 0.4f,  1.f);
    maxScale_    = declare("maxScale",    1.f,  0.5f,  1.f);
    staticScale_ = declare("staticScale", 1.f,  0.4f,  1.f);
}

void DynamicResolution::onFrame(float) {
    ++frame_;
    if (dynamic_->get() < 0.5f) {
        scale_.store(std::clamp(staticScale_->get(), 0.4f, 1.f), std::memory_order_relaxed);
        return;
    }
    const float lo = minScale_->get();
    const float hi = std::max(lo, maxScale_->get());
    float s = std::clamp(scale_.load(std::memory_order_relaxed), lo, hi);

    const float avg = FrameStats::get().avgMs();
    const float targetMs = 1000.f / targetFps_->get();
    const uint32_t since = frame_ - lastChange_;

    if (avg > targetMs * 1.08f && since >= 15 && s > lo) {
        if (lastProbe_ != 0 && frame_ - lastProbe_ < 60) probeBlockedUntil_ = frame_ + 1800;  // the step up didn't hold
        s -= 0.05f;
        lastChange_ = frame_;
    } else if (avg <= targetMs * 1.03f && since >= 180 && s < hi && frame_ >= probeBlockedUntil_) {
        s += 0.05f;                       // under vsync we can't see headroom, so probe upward
        lastChange_ = lastProbe_ = frame_;
    }
    scale_.store(std::clamp(s, lo, hi), std::memory_order_relaxed);
}

// ============================ RebuildThrottle ==============================

RebuildThrottle::RebuildThrottle() : Module("RebuildThrottle") {
    maxPerFrame_ = declare("maxPerFrame", 4.f, 1.f, 16.f);
    nearBypass_  = declare("nearBypass", 12.f, 0.f, 32.f);
}

void RebuildThrottle::onFrame(float frameMs) {
    int b = (int)maxPerFrame_->get();
    if (frameMs > 33.f) b = std::max(1, b / 2);          // already struggling: build less
    budget_.store(b, std::memory_order_relaxed);
    used_.store(0, std::memory_order_relaxed);
}

bool RebuildThrottle::allow(float distSq) {
    if (!enabled()) return true;
    const float nb = nearBypass_->get();
    if (distSq <= nb * nb) return true;
    return used_.fetch_add(1, std::memory_order_relaxed) < budget_.load(std::memory_order_relaxed);
}

// ============================ ParticleLimiter ==============================

ParticleLimiter::ParticleLimiter() : Module("ParticleLimiter") {
    maxPerFrame_   = declare("maxPerFrame",   40.f, 0.f, 200.f);
    cullOffscreen_ = declare("cullOffscreen",  1.f, 0.f,   1.f);
}

bool ParticleLimiter::allow(bool onScreen) {
    if (!enabled()) return true;
    if (!onScreen && cullOffscreen_->get() >= 0.5f) return false;
    return used_.fetch_add(1, std::memory_order_relaxed) < (int)maxPerFrame_->get();
}
