#pragma once
#include "hud/HudLayout.h"

#include <vector>

// Draws the HUD batch on top of the game's finished frame. Call right before eglSwapBuffers, on the
// render thread, with the game's GLES context current. Every piece of GL state it touches is saved
// and restored, so the game never notices.
class GlOverlay {
public:
    bool draw(const std::vector<Vertex>& verts, int screenW, int screenH);
    bool broken() const { return broken_; }

private:
    bool init();

    void* context_ = nullptr;      // EGLContext the GL objects belong to
    unsigned program_ = 0, vao_ = 0, vbo_ = 0;
    int uScreen_ = -1;
    bool broken_ = false;
};
