// ===========================================================================
//  textures.h  -  textures generated in code, with no image files at all
//
//  WHY NOT LOAD .PNG FILES?
//  Three reasons, and they are worth being able to say out loud:
//
//    1. Everything here is a GRID OF GLOWING RECTANGLES - lit windows, neon
//       panels, billboard bars. That is precisely what a loop draws
//       perfectly, and what a photograph does badly once it is blurred by
//       the bloom pass.
//    2. The proposal promises a city generated from a SEED. Textures built
//       from that same seed keep that promise; shipped image files would not.
//    3. No external files means nothing to copy next to the .exe and no
//       file-not-found crash during a demo.
//
//  HOW A TEXTURE IS MADE HERE
//  We fill a plain array of bytes - 4 per pixel (red, green, blue, alpha) -
//  then hand that array to OpenGL with glTexImage2D. From then on the GPU
//  owns it and the shader reads it with texture(tex0, uv).
// ===========================================================================

#pragma once

#include <glad/glad.h>
#include <vector>
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
//  A tiny reproducible random generator, the same idea as the city's.
//  Textures must look random but come out identical every run, otherwise the
//  windows would rearrange themselves each time the program starts.
// ---------------------------------------------------------------------------
struct TexRandom {
    unsigned int state;
    TexRandom(unsigned int seed) : state(seed ? seed : 1u) {}
    unsigned int next() {
        state = state * 1664525u + 1013904223u;
        return state;
    }
    float f() { return (float)(next() >> 8) / (float)(1 << 24); }
};

// ---------------------------------------------------------------------------
//  Hand a finished byte array to OpenGL and set the sampling rules.
//
//  GL_REPEAT      - UVs beyond 1.0 wrap around, so one window texture can be
//                   tiled many times up a tall building.
//  GL_LINEAR      - blend between neighbouring texels instead of showing hard
//                   blocks when the texture is stretched.
//  MIPMAPS        - smaller pre-shrunk copies, picked automatically when a
//                   surface is far away. Without them a distant window grid
//                   turns into a crawling mess of sparkles as the camera
//                   moves. This matters a lot in a city full of fine grids.
// ---------------------------------------------------------------------------
inline GLuint uploadTexture(const std::vector<unsigned char>& pixels, int w, int h) {
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // ANISOTROPIC FILTERING. Mipmaps pick ONE blur level per pixel, which is
    // right for a wall seen face-on but far too blurry for the road seen at a
    // low angle: it is squashed in one direction only. Anisotropic filtering
    // takes several samples along the squashed direction instead, so the
    // road and the window grids stay sharp into the distance. It is an
    // extension (on every desktop GPU), so it is asked for by number:
    //   0x84FF = MAX_TEXTURE_MAX_ANISOTROPY, 0x84FE = TEXTURE_MAX_ANISOTROPY
    GLfloat maxAniso = 0.0f;
    glGetFloatv(0x84FF, &maxAniso);
    if (maxAniso > 1.0f) glTexParameterf(GL_TEXTURE_2D, 0x84FE, fminf(8.0f, maxAniso));
    glGetError();                     // clear the error if it is not supported

    glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}

// Smooth VALUE NOISE on the CPU, the same idea as vnoise() in the shader:
// random numbers on a grid, blended smoothly in between. 'period' makes it
// wrap, so a texture built from it tiles with no visible seam.
inline float texHash(int x, int y, unsigned seed) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFFFF) / (float)0xFFFFFF;
}
inline float texNoise(float x, float y, int period, unsigned seed) {
    int   x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - x0, fy = y - y0;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    auto H = [&](int a, int b) { return texHash(((a % period) + period) % period,
                                                ((b % period) + period) % period, seed); };
    float a = H(x0, y0) + (H(x0 + 1, y0) - H(x0, y0)) * fx;
    float b = H(x0, y0 + 1) + (H(x0 + 1, y0 + 1) - H(x0, y0 + 1)) * fx;
    return a + (b - a) * fy;
}

// ---------------------------------------------------------------------------
//  WINDOW GRID  -  the texture that makes a plain box read as a skyscraper
//
//  The reference images are full of towers whose faces are grids of small
//  lit and unlit squares. That is exactly this texture.
//
//  The layout: the image is divided into COLS x ROWS cells. Each cell is a
//  window with a dark frame around it. A cell is either
//     LIT   - a bright, slightly random colour, or
//     DARK  - nearly black, an empty office.
//
//  Roughly 35% are lit. The reference images are mostly DARK towers with the
//  lit windows standing out; lighting half or more turns every tower into a
//  wall of identical beige squares. All-lit looks fake; far fewer looks
//  abandoned.
// ---------------------------------------------------------------------------
// STYLE gives each texture its own character, so towers differ in their
// windows too, not only in their walls:
//   litChance  - how many offices are occupied (sparse .. busy)
//   coolChance - how many lights are cool white/cyan instead of warm
//   warm       - the colour of the warm lights (incandescent, sodium, pink...)
struct WindowStyle {
    float litChance  = 0.35f;
    float coolChance = 0.25f;
    float warmR = 1.0f, warmG = 0.85f, warmB = 0.55f;
};

inline GLuint makeWindowTexture(unsigned int seed, int cols = 8, int rows = 8,
                                const WindowStyle& style = WindowStyle()) {
    const int CELL = 32;                    // pixels per window cell
    const int W = cols * CELL, H = rows * CELL;
    std::vector<unsigned char> px(W * H * 4);
    TexRandom rng(seed);

    // Decide each window's state ONCE, before touching pixels. Doing it
    // per-pixel would give every pixel its own random value and produce
    // static noise instead of solid windows.
    std::vector<float> cellR(cols * rows), cellG(cols * rows), cellB(cols * rows);
    // INTERIOR DETAIL, also decided once per window: how far the blind is
    // pulled down, whether someone (or a plant, a cabinet) stands in front
    // of the light, and where.
    std::vector<float> blind(cols * rows), figX(cols * rows), figH(cols * rows);
    std::vector<int>   fig(cols * rows);
    for (int i = 0; i < cols * rows; ++i) {
        blind[i] = (rng.f() < 0.45f) ? 0.15f + rng.f() * 0.25f : 0.0f;
        fig[i]   = (rng.f() < 0.30f) ? 1 + (int)(rng.f() * 2.0f) : 0;   // 1 person, 2 cabinet
        figX[i]  = 0.15f + rng.f() * 0.55f;
        figH[i]  = 0.35f + rng.f() * 0.25f;
    }
    for (int i = 0; i < cols * rows; ++i) {
        bool lit = rng.f() < style.litChance;
        if (lit) {
            // Warm office light, with some cool cyan ones mixed in - that
            // colour variety is very visible in the reference photos.
            if (rng.f() < style.coolChance) {
                cellR[i] = 0.45f; cellG[i] = 0.92f; cellB[i] = 1.00f;   // cyan
            } else {
                float w = 0.55f + rng.f() * 0.40f;   // varied brightness
                cellR[i] = w * style.warmR; cellG[i] = w * style.warmG; cellB[i] = w * style.warmB;
            }
        } else {
            cellR[i] = cellG[i] = cellB[i] = 0.02f;
        }
    }

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int cx = x / CELL, cy = y / CELL;
            int ix = x % CELL, iy = y % CELL;
            int ci = cy * cols + cx;

            // A border around each cell is the wall between windows. It is
            // wider at the sides than top and bottom, so the windows come out
            // taller than they are wide, like real office glazing.
            // (Same proportions as the old 16-pixel cell: glass from 1/4 to
            // 3/4 across and 3/16 to 7/8 up - the shader relies on them.)
            bool frame = (ix < 8 || ix >= CELL - 8 || iy < 6 || iy >= CELL - 4);

            float r, g, b;
            if (frame) {
                // dark concrete between windows, with a lighter sill under
                // each window and a thin shadow line above it
                float c = 0.015f;
                if (iy == 5 || iy == 4) c = 0.05f;
                if (iy == CELL - 4 && ix >= 8 && ix < CELL - 8) c = 0.006f;
                r = g = b = c;
            } else {
                // position inside the glass, 0..1 each way (v = 1 at the top)
                float u = (ix - 8 + 0.5f) / (float)(CELL - 16);
                float v = (iy - 6 + 0.5f) / (float)(CELL - 10);
                r = cellR[ci]; g = cellG[ci]; b = cellB[ci];
                bool lit = r > 0.1f;
                if (lit) {
                    // ceiling lights: brighter toward the top of the window
                    float k = 0.80f + 0.35f * v;
                    // a blind pulled part-way down: horizontal slats
                    if (blind[ci] > 0.0f && v > 1.0f - blind[ci])
                        k *= ((iy % 2) == 0) ? 0.55f : 0.85f;
                    // a silhouette standing in front of the light. Kept away
                    // from the very centre of the window (u 0.5, v 0.53) that
                    // the shader samples to ask "is this window lit?".
                    if (fig[ci] == 1) {                       // a person: head + body
                        float dx = u - figX[ci];
                        bool body = fabsf(dx) < 0.09f && v < figH[ci];
                        bool head = (dx * dx + (v - figH[ci] - 0.07f) * (v - figH[ci] - 0.07f) * 0.5f) < 0.004f;
                        if ((body || head) && fabsf(u - 0.5f) > 0.06f) k *= 0.18f;
                    } else if (fig[ci] == 2 && v < 0.30f) {   // a low cabinet
                        k *= 0.35f;
                    }
                    r *= k; g *= k; b *= k;
                } else {
                    // dark glass: a faint cool reflection, lighter at the top
                    float k = 0.6f + 0.8f * v;
                    r = 0.012f * k; g = 0.016f * k; b = 0.03f * k;
                    // a diagonal glint across the pane
                    float gl = fabsf(u + v - 1.1f);
                    if (gl < 0.05f) { r += 0.02f; g += 0.025f; b += 0.04f; }
                }
            }

            int o = (y * W + x) * 4;
            r = fminf(r, 1.0f); g = fminf(g, 1.0f); b = fminf(b, 1.0f);   // a byte holds 0..255: 1.15 * 255 would WRAP round to a wrong colour
            px[o+0] = (unsigned char)(r * 255.0f);
            px[o+1] = (unsigned char)(g * 255.0f);
            px[o+2] = (unsigned char)(b * 255.0f);
            px[o+3] = 255;
        }
    }
    return uploadTexture(px, W, H);
}

// ---------------------------------------------------------------------------
//  BILLBOARD  -  an animated screen, stored as a strip of frames
//
//  The proposal asks for billboards that CYCLE THROUGH SCROLLING FRAMES. A
//  texture cannot hold a video, so this one holds several pictures stacked
//  on top of each other - a "sprite sheet" or "frame strip":
//
//        v = 1.00  +-----------+
//                  |  frame 3  |   ticker: rows of scrolling "text"
//        v = 0.75  +-----------+
//                  |  frame 2  |   logo: a ring and a bar
//        v = 0.50  +-----------+
//                  |  frame 1  |   glyphs: big blocky "characters"
//        v = 0.25  +-----------+
//                  |  frame 0  |   bars: an advert of coloured stripes
//        v = 0.00  +-----------+
//
//  The renderer shows ONE quarter at a time: it scales v by 1/4 and adds
//  frame/4, so changing 'frame' over time flips between the pictures. Adding
//  a growing amount to u slides the picture sideways - the scrolling. Both
//  are just numbers fed to the vertex shader (uvWorldScale and uvOffset).
//
//  Every billboard texture uses one dominant hue so each reads as a single
//  brand rather than a random rainbow.
// ---------------------------------------------------------------------------
static const int BILLBOARD_FRAMES = 4;

inline GLuint makeBillboardTexture(unsigned int seed) {
    const int W = 128, FH = 128, H = FH * BILLBOARD_FRAMES;
    std::vector<unsigned char> px(W * H * 4);
    TexRandom rng(seed);

    float baseR, baseG, baseB;
    int pick = rng.next() % 4;
    if      (pick == 0) { baseR = 1.0f;  baseG = 0.15f; baseB = 0.55f; }   // magenta
    else if (pick == 1) { baseR = 0.15f; baseG = 0.95f; baseB = 1.0f;  }   // cyan
    else if (pick == 2) { baseR = 1.0f;  baseG = 0.65f; baseB = 0.10f; }   // amber
    else                { baseR = 0.65f; baseG = 0.30f; baseB = 1.0f;  }   // violet

    // Write one pixel. 'bright' 0 = the dark screen backing, 1 = full colour.
    auto put = [&](int x, int y, float bright, float white = 0.0f) {
        int o = (y * W + x) * 4;
        float r = baseR * bright + white, g = baseG * bright + white, b = baseB * bright + white;
        px[o+0] = (unsigned char)(fminf(fmaxf(r, 0.024f), 1.0f) * 255.0f);
        px[o+1] = (unsigned char)(fminf(fmaxf(g, 0.016f), 1.0f) * 255.0f);
        px[o+2] = (unsigned char)(fminf(fmaxf(b, 0.040f), 1.0f) * 255.0f);
        px[o+3] = 255;
    };

    // Random numbers for each frame are drawn up front, in a fixed order, so
    // the texture is identical on every run.
    const int BAR = 10;
    float barLen[FH / BAR + 1], barOn[FH / BAR + 1];
    for (int i = 0; i <= FH / BAR; ++i) {
        barLen[i] = 0.25f + rng.f() * 0.70f;
        barOn[i]  = (rng.f() < 0.80f) ? 1.0f : 0.0f;
    }
    bool glyph[4][3][3];                     // 4 characters, each a 3x3 block pattern
    for (int g = 0; g < 4; ++g)
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b)
                glyph[g][a][b] = rng.f() < 0.55f;
    float tick[16];
    for (int i = 0; i < 16; ++i) tick[i] = rng.f();

    for (int f = 0; f < BILLBOARD_FRAMES; ++f) {
        for (int ly = 0; ly < FH; ++ly) {
            int y = f * FH + ly;                  // row in the whole strip
            float v = (float)ly / (float)FH;      // 0..1 within this frame
            for (int x = 0; x < W; ++x) {
                float u = (float)x / (float)W;
                float bright = 0.0f, white = 0.0f;

                if (f == 0) {
                    // BARS - horizontal stripes of varying length
                    int bar = ly / BAR, iy = ly % BAR;
                    bool on = iy >= 2 && u <= barLen[bar] && barOn[bar] > 0.5f;
                    if (on) bright = 0.75f + 0.25f * sinf(u * 18.0f + bar);
                }
                else if (f == 1) {
                    // GLYPHS - four blocky characters in a row, like a sign in
                    // a script you cannot read from a distance
                    int g  = (int)(u * 4.0f);                    // which character
                    float cu = u * 4.0f - g;                      // 0..1 inside it
                    float cv = (v - 0.25f) / 0.5f;                // middle half
                    if (cu > 0.12f && cu < 0.88f && cv > 0.0f && cv < 1.0f) {
                        int a = (int)((cu - 0.12f) / 0.76f * 3.0f);
                        int b = (int)(cv * 3.0f);
                        if (glyph[g][b][a]) bright = 1.0f;
                    }
                }
                else if (f == 2) {
                    // LOGO - a ring with a bar through it, and a white rim
                    float dx = u - 0.5f, dy = v - 0.5f;
                    float r  = sqrtf(dx * dx + dy * dy);
                    if (r > 0.26f && r < 0.36f) bright = 1.0f;
                    if (fabsf(dy) < 0.05f && fabsf(dx) < 0.30f) { bright = 1.0f; white = 0.35f; }
                }
                else {
                    // TICKER - rows of short "words", like scrolling text
                    int row = ly / 16, iy = ly % 16;
                    if (iy >= 5 && iy < 11) {
                        float word = u * 8.0f + row * 0.37f;
                        float frac = word - floorf(word);
                        int   wi   = ((int)floorf(word) + row * 5) & 15;
                        if (frac < 0.25f + tick[wi] * 0.6f) bright = 0.85f;
                    }
                }
                put(x, y, bright, white);
            }
        }
    }
    return uploadTexture(px, W, H);
}

// ---------------------------------------------------------------------------
//  ASPHALT  -  subtle speckle for the road surface
//
//  Almost invisible on its own, but without it the road is a perfectly flat
//  colour and the specular highlight from a street lamp slides across it
//  like plastic. The noise breaks that up so the wet-road sheen looks like
//  a surface rather than a gradient.
// ---------------------------------------------------------------------------
inline GLuint makeAsphaltTexture(unsigned int seed) {
    // 256 x 256 with three layers on top of the speckle:
    //   PATCHES   - large soft areas of older and newer tarmac (noise, 4 cells)
    //   AGGREGATE - medium noise: the stones showing through the surface
    //   CRACKS    - thin dark lines where a second noise crosses its middle
    //               value (a "contour line" of the noise looks like a crack)
    const int W = 256, H = 256;
    std::vector<unsigned char> px(W * H * 4);
    TexRandom rng(seed);

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float patch = texNoise(x / 64.0f, y / 64.0f, 4, seed);
            float aggr  = texNoise(x / 6.0f,  y / 6.0f, 43, seed + 7u);
            float crk   = texNoise(x / 40.0f, y / 40.0f, 6, seed + 13u)
                        + 0.25f * texNoise(x / 9.0f, y / 9.0f, 29, seed + 21u);
            float crack = fabsf(crk - 0.62f) < 0.012f ? 0.45f : 1.0f;
            float n = (0.78f + rng.f() * 0.22f) * (0.82f + 0.30f * patch) * (0.90f + 0.18f * aggr) * crack;
            int o = (y * W + x) * 4;
            unsigned char c = (unsigned char)(fminf(n, 1.0f) * 255.0f);
            px[o+0] = c; px[o+1] = c; px[o+2] = (unsigned char)fminf(c * 1.05f, 255.0f);
            px[o+3] = 255;
        }
    }
    return uploadTexture(px, W, H);
}

// ---------------------------------------------------------------------------
//  PLAIN WHITE  -  the "no texture" texture
//
//  Multiplying by white changes nothing, so any object without a real
//  texture can bind this one and the shader needs no special case.
// ---------------------------------------------------------------------------
inline GLuint makeWhiteTexture() {
    std::vector<unsigned char> px(4 * 4 * 4, 255);
    return uploadTexture(px, 4, 4);
}

// ---------------------------------------------------------------------------
//  PAVING TILES  -  for the pavements and the waterfront promenade
//
//  A 4 x 4 grid of square slabs with thin dark joints between them. Each slab
//  gets its own slightly different shade, decided once per tile (not per
//  pixel), plus a faint speckle - which is what makes real paving read as
//  separate stones rather than one painted surface.
// ---------------------------------------------------------------------------
inline GLuint makeTileTexture(unsigned int seed) {
    const int W = 256, H = 256, TILES = 4, T = W / TILES;
    std::vector<unsigned char> px(W * H * 4);
    TexRandom rng(seed);

    float shade[TILES * TILES];
    for (int i = 0; i < TILES * TILES; ++i) shade[i] = 0.78f + rng.f() * 0.22f;

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int  tx = x / T, ty = y / T;
            int  jx = x % T, jy = y % T;
            bool joint = jx < 3 || jy < 3;                  // 3-pixel grout lines
            // a slight bevel: the slab edges next to a joint are darker
            int  edge  = std::min(std::min(jx - 3, T - 1 - jx), std::min(jy - 3, T - 1 - jy));
            float bevel = edge < 3 ? 0.80f + 0.07f * edge : 1.0f;
            float wear  = 0.88f + 0.20f * texNoise(x / 12.0f, y / 12.0f, 21, seed + 5u);
            float v = joint ? 0.30f : shade[ty * TILES + tx] * bevel * wear * (0.92f + rng.f() * 0.08f);
            int o = (y * W + x) * 4;
            unsigned char c = (unsigned char)(fminf(v, 1.0f) * 255.0f);
            px[o+0] = c; px[o+1] = c; px[o+2] = (unsigned char)fminf(c * 1.04f, 255.0f); px[o+3] = 255;
        }
    }
    return uploadTexture(px, W, H);
}

// ---------------------------------------------------------------------------
//  STONE BLOCKS  -  for the sea wall, the tunnel mouths and the far islands
//
//  Rectangular blocks in a RUNNING BOND: every other row is shifted by half
//  a block, the way real walls are laid so no joint runs straight up. The
//  shift is just an offset added to x on odd rows before working out which
//  block a pixel belongs to.
// ---------------------------------------------------------------------------
inline GLuint makeStoneTexture(unsigned int seed) {
    const int W = 128, H = 128, BW = 32, BH = 16;            // block = 32 x 16 pixels
    std::vector<unsigned char> px(W * H * 4);
    TexRandom rng(seed);

    const int COLS = W / BW, ROWS = H / BH;
    float shade[COLS * ROWS];
    for (int i = 0; i < COLS * ROWS; ++i) shade[i] = 0.70f + rng.f() * 0.30f;

    for (int y = 0; y < H; ++y) {
        int row   = y / BH;
        int shift = (row % 2) ? BW / 2 : 0;                  // the running bond
        for (int x = 0; x < W; ++x) {
            int xs  = (x + shift) % W;
            int col = xs / BW;
            bool joint = (xs % BW) < 2 || (y % BH) < 2;
            float v = joint ? 0.30f : shade[row * COLS + col] * (0.90f + rng.f() * 0.10f);
            int o = (y * W + x) * 4;
            unsigned char c = (unsigned char)(fminf(v, 1.0f) * 255.0f);
            px[o+0] = c; px[o+1] = c; px[o+2] = (unsigned char)fminf(c * 1.06f, 255.0f); px[o+3] = 255;
        }
    }
    return uploadTexture(px, W, H);
}
