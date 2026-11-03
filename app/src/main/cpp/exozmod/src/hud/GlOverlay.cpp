#include "hud/GlOverlay.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>

namespace {

const char* kVertexShader =
    "#version 300 es\n"
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec4 aColor;\n"
    "uniform vec2 uScreen;\n"
    "out vec4 vColor;\n"
    "void main() {\n"
    "  vec2 n = vec2(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0);\n"
    "  gl_Position = vec4(n, 0.0, 1.0);\n"
    "  vColor = aColor;\n"
    "}\n";

const char* kFragmentShader =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec4 vColor;\n"
    "out vec4 oColor;\n"
    "void main() { oColor = vColor; }\n";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(s); return 0; }
    return s;
}

// Everything draw()/init() may change.
struct SavedState {
    GLint program, vao, arrayBuffer, drawFbo, readFbo;
    GLint viewport[4];
    GLint blendSrcRgb, blendDstRgb, blendSrcAlpha, blendDstAlpha, blendEqRgb, blendEqAlpha;
    GLboolean colorMask[4];
    GLboolean blend, depthTest, cullFace, scissorTest, stencilTest, alphaToCoverage, rasterizerDiscard;

    void save() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEqRgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEqAlpha);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        blend = glIsEnabled(GL_BLEND);
        depthTest = glIsEnabled(GL_DEPTH_TEST);
        cullFace = glIsEnabled(GL_CULL_FACE);
        scissorTest = glIsEnabled(GL_SCISSOR_TEST);
        stencilTest = glIsEnabled(GL_STENCIL_TEST);
        alphaToCoverage = glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);
        rasterizerDiscard = glIsEnabled(GL_RASTERIZER_DISCARD);
    }

    static void set(GLenum cap, GLboolean on) { on ? glEnable(cap) : glDisable(cap); }

    void restore() const {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)drawFbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)readFbo);
        glBindBuffer(GL_ARRAY_BUFFER, (GLuint)arrayBuffer);
        glBindVertexArray((GLuint)vao);
        glUseProgram((GLuint)program);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glBlendFuncSeparate((GLenum)blendSrcRgb, (GLenum)blendDstRgb, (GLenum)blendSrcAlpha, (GLenum)blendDstAlpha);
        glBlendEquationSeparate((GLenum)blendEqRgb, (GLenum)blendEqAlpha);
        glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        set(GL_BLEND, blend);
        set(GL_DEPTH_TEST, depthTest);
        set(GL_CULL_FACE, cullFace);
        set(GL_SCISSOR_TEST, scissorTest);
        set(GL_STENCIL_TEST, stencilTest);
        set(GL_SAMPLE_ALPHA_TO_COVERAGE, alphaToCoverage);
        set(GL_RASTERIZER_DISCARD, rasterizerDiscard);
    }
};

}  // namespace

bool GlOverlay::init() {
    GLuint vs = compile(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(program_); program_ = 0; return false; }
    uScreen_ = glGetUniformLocation(program_, "uScreen");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)(2 * sizeof(float)));
    return true;
}

bool GlOverlay::draw(const std::vector<Vertex>& verts, int w, int h) {
    if (broken_ || verts.empty() || w <= 0 || h <= 0) return false;

    EGLContext ctx = eglGetCurrentContext();
    if (ctx == EGL_NO_CONTEXT) return false;

    SavedState saved;
    saved.save();

    if (ctx != context_) {                  // first frame, or the game recreated its GL context
        program_ = vao_ = vbo_ = 0;         // old names died with the old context
        if (!init()) {
            broken_ = true;                 // don't retry every frame; the HUD just stays off
            saved.restore();
            return false;
        }
        context_ = ctx;
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);         // the frame being presented
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable(GL_RASTERIZER_DISCARD);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);

    glUseProgram(program_);
    glUniform2f(uScreen_, (float)w, (float)h);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(Vertex)), verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());

    saved.restore();
    return true;
}
