#include "perf/cave_culling.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>

namespace {
const int DX[6] = {1, -1, 0, 0, 0, 0};
const int DY[6] = {0, 0, 1, -1, 0, 0};
const int DZ[6] = {0, 0, 0, 0, 1, -1};
}  // namespace

CaveCulling::CaveCulling() : Module("CaveCulling") {
    radius_ = declare("radius",       16.f, 4.f, 48.f);   // sub-chunks around the camera we analyse
    near_   = declare("nearSections",  1.f, 0.f,  3.f);   // always draw this close to the camera
}

// Flood-fills the non-opaque cells of one sub-chunk. Every connected pocket of air touches
// some set of the 6 faces; all faces touched by the same pocket are connected to each other.
CaveCulling::Conn CaveCulling::computeConnectivity(const uint8_t* opaque) {
    uint8_t reach[6] = {};
    uint8_t seen[4096];
    uint16_t stack[4096];
    std::memset(seen, 0, sizeof seen);

    for (int start = 0; start < 4096; ++start) {
        if (opaque[start] || seen[start]) continue;

        uint8_t faces = 0;
        int sp = 0;
        stack[sp++] = (uint16_t)start;
        seen[start] = 1;
        auto push = [&](int n) {
            if (!opaque[n] && !seen[n]) { seen[n] = 1; stack[sp++] = (uint16_t)n; }
        };
        while (sp) {
            const int i = stack[--sp];
            const int x = i & 15, z = (i >> 4) & 15, y = i >> 8;
            if (x == 15) faces |= 1 << 0;
            if (x == 0)  faces |= 1 << 1;
            if (y == 15) faces |= 1 << 2;
            if (y == 0)  faces |= 1 << 3;
            if (z == 15) faces |= 1 << 4;
            if (z == 0)  faces |= 1 << 5;
            if (x < 15) push(i + 1);
            if (x > 0)  push(i - 1);
            if (z < 15) push(i + 16);
            if (z > 0)  push(i - 16);
            if (y < 15) push(i + 256);
            if (y > 0)  push(i - 256);
        }
        for (int e = 0; e < 6; ++e)
            if (faces & (1 << e)) reach[e] |= faces;
    }

    Conn c = 0;
    for (int e = 0; e < 6; ++e) c |= (Conn)reach[e] << (8 * e);
    return c;
}

void CaveCulling::setSection(int sx, int sy, int sz, Conn c) {
    std::lock_guard<std::mutex> lk(mutex_);
    sections_[key(sx, sy, sz)] = c;
    dirty_.store(true, std::memory_order_relaxed);
}

void CaveCulling::removeSection(int sx, int sy, int sz) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (sections_.erase(key(sx, sy, sz))) dirty_.store(true, std::memory_order_relaxed);
}

void CaveCulling::clear() {
    std::lock_guard<std::mutex> lk(mutex_);
    sections_.clear();
    dirty_.store(true, std::memory_order_relaxed);
}

void CaveCulling::setWorldHeight(int minSy, int maxSy) {
    minSy_.store(minSy);
    maxSy_.store(std::max(minSy, maxSy));
    dirty_.store(true);
}

void CaveCulling::onFrame(float) {
    hiddenShown_.store(hidden_, std::memory_order_relaxed);
    hidden_ = 0;
}

void CaveCulling::setCamera(float x, float y, float z) {
    if (!enabled()) return;
    const int sx = (int)std::floor(x / 16.f);
    const int sy = (int)std::floor(y / 16.f);
    const int sz = (int)std::floor(z / 16.f);

    ++sinceRebuild_;
    const bool moved = sx != camSx_ || sy != camSy_ || sz != camSz_ || (int)radius_->get() != winR_;
    // At most one rebuild every 2 frames. camS* only change inside the rebuild branch, so a
    // throttled move is simply retried next frame.
    if ((moved || dirty_.load(std::memory_order_relaxed)) && sinceRebuild_ >= 2) {
        camSx_ = sx; camSy_ = sy; camSz_ = sz;
        dirty_.store(false, std::memory_order_relaxed);
        sinceRebuild_ = 0;
        rebuild();
    }
}

void CaveCulling::rebuild() {
    const int R = (int)radius_->get();
    const int minSy = minSy_.load(), maxSy = maxSy_.load();
    const int W = 2 * R + 1, H = maxSy - minSy + 1;
    const size_t total = (size_t)W * W * H;

    winR_ = R; winW_ = W; winH_ = H; gridMinSy_ = minSy;
    visible_.assign(total, 0);
    incoming_.assign(total, 0);
    pending_.assign(total, 0);

    const int cy = std::clamp(camSy_ - minSy, 0, H - 1);   // camera above/below the build range: clamp
    const int start = (cy * W + R) * W + R;
    visible_[start] = 1;
    incoming_[start] = 0x3F;
    pending_[start] = 1;

    std::deque<int> q;
    q.push_back(start);

    std::lock_guard<std::mutex> lk(mutex_);
    while (!q.empty()) {
        const int cur = q.front();
        q.pop_front();
        pending_[cur] = 0;

        const int x = cur % W, z = (cur / W) % W, y = cur / (W * W);

        // Faces we can leave through, given every face we've been entered from so far.
        uint8_t reach;
        if (cur == start) {
            reach = 0x3F;                                   // the camera is inside: any face
        } else {
            auto it = sections_.find(key(camSx_ + (x - R), minSy + y, camSz_ + (z - R)));
            if (it == sections_.end()) {
                reach = 0x3F;                               // no data: assume open (never hide unknown terrain)
            } else {
                reach = 0;
                for (int e = 0; e < 6; ++e)
                    if (incoming_[cur] & (1 << e)) reach |= (uint8_t)((it->second >> (8 * e)) & 0x3F);
            }
        }

        for (int f = 0; f < 6; ++f) {
            if (!(reach & (1 << f))) continue;
            // Only move away from the camera on every axis (no walking back toward it).
            if (DX[f] > 0 && x < R) continue;
            if (DX[f] < 0 && x > R) continue;
            if (DY[f] > 0 && y < cy) continue;
            if (DY[f] < 0 && y > cy) continue;
            if (DZ[f] > 0 && z < R) continue;
            if (DZ[f] < 0 && z > R) continue;

            const int nx = x + DX[f], ny = y + DY[f], nz = z + DZ[f];
            if (nx < 0 || nx >= W || ny < 0 || ny >= H || nz < 0 || nz >= W) continue;

            const int ni = (ny * W + nz) * W + nx;
            visible_[ni] = 1;                               // reached = its surface can be seen
            const uint8_t bit = (uint8_t)(1 << (f ^ 1));    // we enter through the face opposite our travel direction
            if (!(incoming_[ni] & bit)) {
                incoming_[ni] |= bit;                       // new entry face: it may open new exits, so revisit
                if (!pending_[ni]) { pending_[ni] = 1; q.push_back(ni); }
            }
        }
    }
}

bool CaveCulling::isVisible(int sx, int sy, int sz) {
    if (!enabled() || visible_.empty()) return true;

    const int x = sx - camSx_ + winR_;
    const int z = sz - camSz_ + winR_;
    const int y = sy - gridMinSy_;
    if (x < 0 || x >= winW_ || z < 0 || z >= winW_ || y < 0 || y >= winH_) return true;   // outside the analysed window

    const int nr = (int)near_->get();
    if (std::abs(sx - camSx_) <= nr && std::abs(sy - camSy_) <= nr && std::abs(sz - camSz_) <= nr) return true;

    if (visible_[((size_t)y * winW_ + z) * winW_ + x]) return true;
    ++hidden_;
    return false;
}
