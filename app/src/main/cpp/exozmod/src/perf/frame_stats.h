#pragma once
#include <atomic>

// Rolling average of frame time over the last 60 frames.
// push() is called from the render thread (eglSwapBuffers hook); readers can be any thread.
class FrameStats {
public:
    static FrameStats& get() { static FrameStats s; return s; }

    void push(float ms) {
        sum_ += ms - ring_[idx_];
        ring_[idx_] = ms;
        idx_ = (idx_ + 1) % N;
        if (count_ < N) ++count_;
        avg_.store(sum_ / count_, std::memory_order_relaxed);
    }
    float avgMs() const { return avg_.load(std::memory_order_relaxed); }
    float fps() const { float a = avgMs(); return a > 0.f ? 1000.f / a : 0.f; }

private:
    static constexpr int N = 60;
    float ring_[N] = {};
    float sum_ = 0.f;
    int idx_ = 0, count_ = 0;
    std::atomic<float> avg_{16.7f};
};
