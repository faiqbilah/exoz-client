#include "hud/FrameTimeline.h"

#include <algorithm>
#include <cmath>
#include <vector>

void FrameTimeline::push(float ms) {
    ring_[head_] = ms;
    head_ = (head_ + 1) % N;
    if (count_ < N) ++count_;
}

float FrameTimeline::at(size_t i) const {
    if (i >= count_) return 0.f;
    return ring_[(head_ + N - count_ + i) % N];
}

float FrameTimeline::avgMs(size_t window) const {
    const size_t n = std::min(window, count_);
    if (n == 0) return 0.f;
    float sum = 0.f;
    for (size_t k = 0; k < n; ++k) sum += at(count_ - 1 - k);
    return sum / (float)n;
}

float FrameTimeline::fps(size_t window) const {
    const float a = avgMs(window);
    return a > 0.f ? 1000.f / a : 0.f;
}

float FrameTimeline::lowFps() const {
    if (count_ == 0) return 0.f;
    std::vector<float> v;
    v.reserve(count_);
    for (size_t i = 0; i < count_; ++i) v.push_back(at(i));
    std::sort(v.begin(), v.end());
    size_t idx = (size_t)std::ceil(0.99 * (double)count_);
    idx = idx == 0 ? 0 : std::min(idx - 1, count_ - 1);
    return v[idx] > 0.f ? 1000.f / v[idx] : 0.f;
}
