// ===========================================================================
//  render.h  -  shader compiling, off-screen buffers, and the bloom chain
//
//  THE BIG IDEA: WE DO NOT DRAW STRAIGHT TO THE SCREEN
//
//  Instead the scene is drawn into an off-screen image (a "framebuffer
//  object", FBO). That lets us process the picture before showing it, which
//  is how the neon glow is made:
//
//      1. draw the scene into 2 images at once:
//           - the normal picture
//           - a second one holding ONLY the glowing parts
//      2. blur that second image
//      3. add the blur back on top of the first
//
//  Step 3 is what makes a neon sign bleed light onto the wall beside it.
//  Without it the sign is just a brightly coloured rectangle.
// ===========================================================================

#pragma once

#include <glad/glad.h>
#include <iostream>
#include <string>

// ---------------------------------------------------------------------------
//  SHADER COMPILING
//  The error checks matter enormously: a single typo in GLSL otherwise gives
//  a silent black screen with no clue as to why.
// ---------------------------------------------------------------------------
inline GLuint compileShader(GLenum type, const char* src, const char* label) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cout << "SHADER COMPILE FAILED [" << label << "]\n" << log << std::endl;
    }
    return s;
}

inline GLuint makeProgram(const char* vs, const char* fs, const char* label) {
    GLuint v = compileShader(GL_VERTEX_SHADER,   vs, label);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs, label);

    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::cout << "SHADER LINK FAILED [" << label << "]\n" << log << std::endl;
    }

    // The linked program keeps its own copies, so these can go.
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

// ---------------------------------------------------------------------------
//  THE MAIN SCENE BUFFER
//
//  Two colour attachments and one depth buffer:
//
//    colour 0 - the ordinary rendered picture
//    colour 1 - only the pixels bright enough to glow
//    depth    - needed for hidden-surface removal, exactly as on screen
//
//  WHY GL_RGBA16F AND NOT PLAIN RGBA?
//  A normal 8-bit image cannot store a value above 1.0 - anything brighter
//  is clipped to white immediately. Bloom depends on knowing HOW MUCH
//  brighter than white something is, so we need floating-point storage.
//  16-bit half floats are the usual compromise: enough range, half the
//  bandwidth of 32-bit, which matters on integrated graphics.
// ---------------------------------------------------------------------------
struct SceneFBO {
    GLuint fbo = 0;
    GLuint colorTex = 0;     // attachment 0 - the scene
    GLuint brightTex = 0;    // attachment 1 - the glowing parts
    GLuint depthRBO = 0;
    int    width = 0, height = 0;
};

inline void destroySceneFBO(SceneFBO& s) {
    if (s.colorTex)  glDeleteTextures(1, &s.colorTex);
    if (s.brightTex) glDeleteTextures(1, &s.brightTex);
    if (s.depthRBO)  glDeleteRenderbuffers(1, &s.depthRBO);
    if (s.fbo)       glDeleteFramebuffers(1, &s.fbo);
    s = SceneFBO();
}

inline bool createSceneFBO(SceneFBO& s, int w, int h) {
    destroySceneFBO(s);
    s.width = w; s.height = h;

    glGenFramebuffers(1, &s.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);

    // --- attachment 0: the scene picture ---
    glGenTextures(1, &s.colorTex);
    glBindTexture(GL_TEXTURE_2D, s.colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // CLAMP_TO_EDGE stops the blur wrapping light from one screen edge
    // around to the opposite one.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s.colorTex, 0);

    // --- attachment 1: the bright parts ---
    glGenTextures(1, &s.brightTex);
    glBindTexture(GL_TEXTURE_2D, s.brightTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, s.brightTex, 0);

    // Tell OpenGL the fragment shader writes to BOTH attachments. Without
    // this only attachment 0 receives anything and bloom silently does
    // nothing at all.
    GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, bufs);

    // --- depth, as a renderbuffer since we never read it back ---
    glGenRenderbuffers(1, &s.depthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, s.depthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s.depthRBO);

    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!ok) std::cout << "ERROR: scene framebuffer incomplete\n";

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}

// ---------------------------------------------------------------------------
//  THE PING-PONG BLUR BUFFERS
//
//  A separable Gaussian blur needs two passes - horizontal, then vertical -
//  and a shader cannot read and write the same texture at once. So we keep
//  two buffers and bounce between them:
//
//      bright  --H-->  ping  --V-->  pong  --H-->  ping  ...
//
//  Each extra round widens the glow. Two rounds (4 passes) looks good and
//  stays cheap.
//
//  THEY ARE HALF RESOLUTION ON PURPOSE. A blur is smooth by definition, so
//  the lost detail is invisible, but it cuts the work to a quarter. This is
//  the single biggest reason this runs comfortably on integrated graphics.
// ---------------------------------------------------------------------------
struct BloomFBO {
    GLuint fbo[2] = { 0, 0 };
    GLuint tex[2] = { 0, 0 };
    int    width = 0, height = 0;
};

inline void destroyBloomFBO(BloomFBO& b) {
    for (int i = 0; i < 2; ++i) {
        if (b.tex[i]) glDeleteTextures(1, &b.tex[i]);
        if (b.fbo[i]) glDeleteFramebuffers(1, &b.fbo[i]);
    }
    b = BloomFBO();
}

inline bool createBloomFBO(BloomFBO& b, int w, int h) {
    destroyBloomFBO(b);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    b.width = w; b.height = h;

    for (int i = 0; i < 2; ++i) {
        glGenFramebuffers(1, &b.fbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, b.fbo[i]);

        glGenTextures(1, &b.tex[i]);
        glBindTexture(GL_TEXTURE_2D, b.tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, b.tex[i], 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cout << "ERROR: bloom framebuffer " << i << " incomplete\n";
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

// ---------------------------------------------------------------------------
//  FULLSCREEN QUAD
//
//  Two triangles covering the whole screen, used by every post-processing
//  pass. The positions are already in clip space (-1..+1), so no matrices
//  are involved - the vertex shader just passes them through.
// ---------------------------------------------------------------------------
struct FullscreenQuad {
    GLuint vao = 0, vbo = 0;
};

inline FullscreenQuad createFullscreenQuad() {
    // x, y, u, v
    float verts[] = {
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,

        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
    };

    FullscreenQuad q;
    glGenVertexArrays(1, &q.vao);
    glGenBuffers(1, &q.vbo);

    glBindVertexArray(q.vao);
    glBindBuffer(GL_ARRAY_BUFFER, q.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    return q;
}

inline void drawFullscreenQuad(const FullscreenQuad& q) {
    glBindVertexArray(q.vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
//  RUN THE BLUR
//
//  Copies the bright image into the ping-pong pair and blurs it, alternating
//  horizontal and vertical. Returns the texture holding the final blur.
// ---------------------------------------------------------------------------
inline GLuint runBloomBlur(BloomFBO& bloom, GLuint blurProgram,
                           const FullscreenQuad& quad, GLuint brightTex,
                           int passes = 4, float spread = 1.0f) {
    glViewport(0, 0, bloom.width, bloom.height);
    glUseProgram(blurProgram);

    GLint locHoriz = glGetUniformLocation(blurProgram, "horizontal");
    GLint locImage = glGetUniformLocation(blurProgram, "image");
    glUniform1i(locImage, 0);
    glUniform1f(glGetUniformLocation(blurProgram, "spread"), spread);
    glUniform1f(glGetUniformLocation(blurProgram, "threshold"), 0.0f);
    glUniform1i(glGetUniformLocation(blurProgram, "useMask"), 0);
    glActiveTexture(GL_TEXTURE0);

    bool horizontal = true;
    bool firstPass  = true;

    for (int i = 0; i < passes; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloom.fbo[horizontal ? 1 : 0]);
        glUniform1i(locHoriz, horizontal ? 1 : 0);

        // The first pass reads the scene's bright attachment; every later
        // pass reads whichever buffer the previous one wrote.
        glBindTexture(GL_TEXTURE_2D, firstPass ? brightTex : bloom.tex[horizontal ? 0 : 1]);

        drawFullscreenQuad(quad);

        horizontal = !horizontal;
        firstPass  = false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // The last write went to the buffer opposite the current 'horizontal'.
    return bloom.tex[horizontal ? 1 : 0];
}

// ---------------------------------------------------------------------------
//  LENS STREAKS  -  "anamorphic" flares
//
//  Cinema lenses squeeze the picture sideways, which smears every bright
//  point into a long horizontal line - the look of almost every neon-noir
//  film. Made here from the same blur shader, but:
//    - HORIZONTAL passes only, so the light spreads sideways and not up
//    - a MASK on the first pass: only pixels whose bright-image ALPHA is 1
//      may streak. The scene shader writes 1 there only for lamp heads and
//      headlights (uniform 'streak'), so neon tubes and signs - just as
//      bright - do not smear into lines across the screen. A threshold
//      then keeps only their hot centres.
//    - each pass spaces its samples 3x further apart than the last, so three
//      passes reach hundreds of pixels for the price of 27 samples
//  The buffers are a quarter of the width (a streak has no fine detail
//  sideways) but half the height, so the lines stay thin.
// ---------------------------------------------------------------------------
inline GLuint runStreakBlur(BloomFBO& s, GLuint blurProgram, const FullscreenQuad& quad,
                            GLuint brightTex, float threshold, int passes = 3) {
    glViewport(0, 0, s.width, s.height);
    glUseProgram(blurProgram);
    glUniform1i(glGetUniformLocation(blurProgram, "image"), 0);
    glUniform1i(glGetUniformLocation(blurProgram, "horizontal"), 1);
    GLint locSpread = glGetUniformLocation(blurProgram, "spread");
    GLint locThresh = glGetUniformLocation(blurProgram, "threshold");
    GLint locMask   = glGetUniformLocation(blurProgram, "useMask");
    glActiveTexture(GL_TEXTURE0);

    GLuint src = brightTex;
    int    dst = 0;
    float  spread = 4.0f;          // first pass reads the full-size image
    for (int i = 0; i < passes; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, s.fbo[dst]);
        glUniform1f(locThresh, i == 0 ? threshold : 0.0f);
        glUniform1i(locMask,   i == 0 ? 1 : 0);
        glUniform1f(locSpread, spread);
        glBindTexture(GL_TEXTURE_2D, src);
        drawFullscreenQuad(quad);
        src = s.tex[dst];
        dst ^= 1;
        spread = (i == 0) ? 3.0f : spread * 3.0f;
    }
    glUniform1i(locMask, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return src;
}
