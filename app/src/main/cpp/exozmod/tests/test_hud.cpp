#include "hud/FrameTimeline.h"
#include "hud/HudLayout.h"

#include <cassert>
#include <cmath>
#include <cstdio>

static void inBounds(const std::vector<Vertex>& v, int w, int h) {
    assert(!v.empty() && v.size() % 6 == 0);
    for (const Vertex& p : v) {
        assert(p.x >= 0.f && p.x <= (float)w);
        assert(p.y >= 0.f && p.y <= (float)h);
        assert(p.a >= 0.f && p.a <= 1.f);
    }
}

int main() {
    // timeline math
    FrameTimeline t;
    for (int i = 0; i < 120; ++i) t.push(16.6667f);
    assert(std::fabs(t.fps() - 60.f) < 0.1f);
    assert(std::fabs(t.lowFps() - 60.f) < 0.1f);
    for (int i = 0; i < 3; ++i) t.push(50.f);                 // a few spikes
    assert(t.lowFps() < 30.f);                                 // the 99th percentile sees them
    assert(t.fps(60) > 50.f);                                  // the average barely moves
    assert(t.count() == 120 && t.at(119) == 50.f && t.at(0) > 16.f);

    // layout: every corner stays on screen, at several scales and resolutions
    struct Res { int w, h; } res[] = {{2400, 1080}, {1280, 720}, {800, 480}};
    std::vector<Vertex> v;
    for (Res r : res) for (int corner = 0; corner < 4; ++corner) for (int s = 1; s <= 6; ++s) {
        HudStyle st; st.corner = corner; st.scale = s;
        buildHud(t, st, r.w, r.h, v);
        inBounds(v, r.w, r.h);
    }

    // corner placement
    HudStyle tl; tl.corner = 0;
    buildHud(t, tl, 2400, 1080, v);
    assert(v[0].x < 100 && v[0].y < 100);                      // panel starts near top-left
    HudStyle br; br.corner = 3;
    buildHud(t, br, 2400, 1080, v);
    assert(v[0].x > 2000 && v[0].y > 700);                     // panel sits bottom-right

    // options change the geometry
    HudStyle a, b; b.showGraph = false; b.showLow = false;
    std::vector<Vertex> va, vb;
    buildHud(t, a, 2400, 1080, va);
    buildHud(t, b, 2400, 1080, vb);
    assert(va.size() > vb.size());

    // empty timeline draws nothing; font coverage
    FrameTimeline empty;
    buildHud(empty, a, 2400, 1080, v);
    assert(v.empty());
    for (char c : {'0','1','2','3','4','5','6','7','8','9','F','P','S','M','L','O','W','.',':','-'}) assert(litPixels(c) > 0);
    assert(litPixels('Z') == 0 && litPixels(' ') == 0);
    assert(textWidth("FPS", 2) == 3 * 12 - 2);

    // colour coding: red text at 20 fps
    FrameTimeline slow; for (int i = 0; i < 60; ++i) slow.push(50.f);
    buildHud(slow, a, 2400, 1080, v);
    bool sawRed = false;
    for (const Vertex& p : v) if (p.r > 0.9f && p.g < 0.4f) sawRed = true;
    assert(sawRed);

    std::printf("hud tests passed (%zu vertices at default style)\n", va.size());
}
