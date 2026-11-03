#pragma once
#include "hud/FrameTimeline.h"

#include <vector>

// One coloured vertex in screen pixels (origin top-left). The overlay draws everything as triangles,
// so the whole HUD (panel, pixel-font text, graph) is a single batch.
struct Vertex {
    float x, y, r, g, b, a;
};

struct HudStyle {
    int scale = 2;          // pixel size of one font dot (1..6)
    int corner = 1;         // 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right
    int opacity = 70;       // panel opacity, percent
    bool showGraph = true;  // frame-time graph
    bool showLow = true;    // "LOW" (99th percentile) line
};

// Fills `out` with the HUD for the current frame. Pure function: no GL, safe to unit-test on a PC.
void buildHud(const FrameTimeline& timeline, const HudStyle& style, int screenW, int screenH,
              std::vector<Vertex>& out);

// Exposed for tests.
int textWidth(const char* text, int scale);
int litPixels(char c);
