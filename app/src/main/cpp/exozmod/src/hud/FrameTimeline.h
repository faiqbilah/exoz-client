#pragma once
#include <array>
#include <cstddef>

// Rolling history of frame times (ms). Render thread only.
class FrameTimeline {
public:
    static constexpr size_t N = 120;

    void push(float ms);
    size_t count() const { return count_; }
    float at(size_t i) const;                 // 0 = oldest stored frame, count()-1 = newest
    float avgMs(size_t window = 60) const;    // mean of the last `window` frames
    float fps(size_t window = 60) const;
    float lowFps() const;                     // fps at the 99th-percentile frame time of the stored history

private:
    std::array<float, N> ring_{};
    size_t head_ = 0;
    size_t count_ = 0;
};
