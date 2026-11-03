#include "hud/HudLayout.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

struct Color { float r, g, b, a; };
constexpr Color kGreen{0.35f, 0.90f, 0.45f, 1.f};
constexpr Color kYellow{1.00f, 0.80f, 0.25f, 1.f};
constexpr Color kRed{1.00f, 0.35f, 0.32f, 1.f};
constexpr Color kText{0.92f, 0.94f, 0.96f, 1.f};
constexpr Color kDim{0.62f, 0.68f, 0.74f, 1.f};

// 5x7 pixel font, one byte per row, 5 low bits used (bit 4 = leftmost dot).
struct Glyph { char c; uint8_t rows[7]; };
constexpr Glyph kFont[] = {
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
};

const Glyph* findGlyph(char c) {
    c = (char)std::toupper((unsigned char)c);
    for (const Glyph& g : kFont) if (g.c == c) return &g;
    return nullptr;
}

void addRect(std::vector<Vertex>& out, float x, float y, float w, float h, Color c) {
    const float x1 = x + w, y1 = y + h;
    out.push_back({x,  y,  c.r, c.g, c.b, c.a});
    out.push_back({x1, y,  c.r, c.g, c.b, c.a});
    out.push_back({x,  y1, c.r, c.g, c.b, c.a});
    out.push_back({x1, y,  c.r, c.g, c.b, c.a});
    out.push_back({x1, y1, c.r, c.g, c.b, c.a});
    out.push_back({x,  y1, c.r, c.g, c.b, c.a});
}

void addText(std::vector<Vertex>& out, float x, float y, int s, const char* text, Color c) {
    for (const char* p = text; *p; ++p, x += 6.f * s) {
        const Glyph* g = findGlyph(*p);
        if (!g) continue;
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (g->rows[row] & (1 << (4 - col)))
                    addRect(out, x + col * s, y + row * s, (float)s, (float)s, c);
    }
}

Color fpsColor(float fps) { return fps >= 55.f ? kGreen : (fps >= 30.f ? kYellow : kRed); }
Color frameColor(float ms) { return ms <= 18.f ? kGreen : (ms <= 34.f ? kYellow : kRed); }

}  // namespace

int textWidth(const char* text, int scale) {
    const int n = (int)std::strlen(text);
    return n == 0 ? 0 : n * 6 * scale - scale;   // last glyph has no trailing gap
}

int litPixels(char c) {
    const Glyph* g = findGlyph(c);
    if (!g) return 0;
    int n = 0;
    for (uint8_t row : g->rows) for (int b = 0; b < 5; ++b) n += (row >> b) & 1;
    return n;
}

void buildHud(const FrameTimeline& t, const HudStyle& style, int screenW, int screenH, std::vector<Vertex>& out) {
    out.clear();
    if (screenW <= 0 || screenH <= 0 || t.count() == 0) return;

    const int s = std::clamp(style.scale, 1, 6);
    const int pad = 3 * s, gap = 2 * s, glyphH = 7 * s;

    char l1[24], l2[24], l3[24];
    const float fps = t.fps();
    std::snprintf(l1, sizeof l1, "FPS %d", (int)std::lround(fps));
    std::snprintf(l2, sizeof l2, "MS %.1f", t.avgMs());
    std::snprintf(l3, sizeof l3, "LOW %d", (int)std::lround(t.lowFps()));

    struct Line { const char* text; Color color; };
    Line lines[3];
    int nLines = 0;
    lines[nLines++] = {l1, fpsColor(fps)};
    lines[nLines++] = {l2, kText};
    if (style.showLow) lines[nLines++] = {l3, kDim};

    int contentW = 0;
    for (int i = 0; i < nLines; ++i) contentW = std::max(contentW, textWidth(lines[i].text, s));

    const int bars = (int)std::min<size_t>(t.count(), 60);
    const int graphW = style.showGraph ? 60 * s : 0;
    const int graphH = style.showGraph ? 12 * s : 0;
    contentW = std::max(contentW, graphW);

    const int w = contentW + 2 * pad;
    int h = 2 * pad + nLines * glyphH + (nLines - 1) * gap;
    if (style.showGraph) h += gap + graphH;

    const int margin = 4 * s + 8;
    const bool right = style.corner == 1 || style.corner == 3;
    const bool bottom = style.corner == 2 || style.corner == 3;
    const float x0 = right ? (float)(screenW - margin - w) : (float)margin;
    const float y0 = bottom ? (float)(screenH - margin - h) : (float)margin;

    const float opacity = (float)std::clamp(style.opacity, 0, 100) / 100.f;
    addRect(out, x0, y0, (float)w, (float)h, {0.04f, 0.05f, 0.07f, 0.80f * opacity});
    addRect(out, x0, y0, (float)w, (float)s, {0.35f, 0.90f, 0.45f, 0.9f});   // accent strip on top

    float y = y0 + pad;
    for (int i = 0; i < nLines; ++i) {
        addText(out, x0 + pad, y, s, lines[i].text, lines[i].color);
        y += glyphH + gap;
    }

    if (style.showGraph) {
        const float gx = x0 + pad;
        const float top = y;                       // y already includes the gap after the last text line
        addRect(out, gx, top, (float)graphW, (float)graphH, {1.f, 1.f, 1.f, 0.06f});
        // 60 fps reference line (16.7 ms) on a 0..33.3 ms scale
        addRect(out, gx, top + graphH * (1.f - 16.7f / 33.3f), (float)graphW, std::max(1.f, (float)s * 0.5f),
                {1.f, 1.f, 1.f, 0.25f});
        for (int i = 0; i < bars; ++i) {
            const float ms = t.at(t.count() - bars + i);
            const float frac = std::clamp(ms / 33.3f, 0.05f, 1.f);
            const float bh = graphH * frac;
            addRect(out, gx + i * (float)s, top + graphH - bh, (float)s, bh, frameColor(ms));
        }
    }
}
